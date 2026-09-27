/* SPDX-License-Identifier: MIT */
#include "umac_usb_mouse.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/usb/usbh.h>
#include <soc.h>
#include <fsl_clock.h>
#include <umac.h>

#include "usbh_ch9.h"
#include "usbh_class.h"
#include "usbh_device.h"
#include "umac_usb_hid_keys.h"

#define BOARD_XTAL0_CLK_HZ 24000000U
#define HID_REPORT_MAX 64U
#define KEY_QUEUE_CAPACITY 64U
#define KEY_EVENT_SPACING_MS 20U

#define HID_CLASS 0x03U
#define HID_BOOT_SUBCLASS 0x01U
#define HID_BOOT_KEYBOARD 0x01U
#define HID_BOOT_MOUSE 0x02U

USBH_CONTROLLER_DEFINE(umac_usbh, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

struct hid_port {
	struct usb_device *udev;
	struct uhc_transfer *xfer;
	uint8_t iface;
	uint8_t endpoint;
	uint8_t report_id;
	uint16_t packet_size;
	bool stopping;
	uint32_t probes;
	uint32_t reports;
	uint32_t errors;
	int last_probe_result;
};

struct key_event {
	uint8_t mac_code;
	bool down;
};

static struct {
	struct hid_port port;
	struct k_spinlock lock;
	int dx;
	int dy;
	uint8_t buttons;
	uint8_t last_report[8];
	uint8_t last_report_len;
	bool dirty;
	uint32_t delivered;
	uint32_t raw_x;
	uint32_t raw_y;
} mouse;

static struct {
	struct hid_port port;
	struct k_spinlock lock;
	uint8_t previous[8];
	struct key_event queue[KEY_QUEUE_CAPACITY];
	unsigned int head;
	unsigned int tail;
	uint32_t next_event_ms;
	uint32_t queued;
	uint32_t delivered;
	uint32_t dropped;
} keyboard;

static int host_init_result;
static uint32_t next_bind_ms;

static const struct usb_if_descriptor *hid_iface_descriptor(
	struct usb_device *udev, uint8_t iface)
{
	if (iface > UHC_INTERFACES_MAX || udev->ifaces[iface].dhp == NULL) {
		return NULL;
	}
	return (const struct usb_if_descriptor *)udev->ifaces[iface].dhp;
}

static const struct usb_ep_descriptor *hid_interrupt_in_endpoint(
	struct usb_device *udev, uint8_t iface)
{
	const struct usb_if_descriptor *if_desc = hid_iface_descriptor(udev, iface);
	const struct usb_cfg_descriptor *cfg = udev->cfg_desc;
	const uint8_t *cursor;
	const uint8_t *end;

	if (if_desc == NULL || cfg == NULL) {
		return NULL;
	}
	cursor = (const uint8_t *)if_desc + if_desc->bLength;
	end = (const uint8_t *)cfg + sys_le16_to_cpu(cfg->wTotalLength);
	while (cursor + sizeof(struct usb_desc_header) <= end) {
		const struct usb_desc_header *header =
			(const struct usb_desc_header *)cursor;

		if (header->bLength < sizeof(*header) || cursor + header->bLength > end) {
			break;
		}
		if (header->bDescriptorType == USB_DESC_INTERFACE) {
			break;
		}
		if (header->bDescriptorType == USB_DESC_ENDPOINT &&
		    header->bLength >= sizeof(struct usb_ep_descriptor)) {
			const struct usb_ep_descriptor *ep =
				(const struct usb_ep_descriptor *)cursor;

			if ((ep->bEndpointAddress & 0x80U) != 0U &&
			    (ep->bmAttributes & 0x03U) == 0x03U) {
				return ep;
			}
		}
		cursor += header->bLength;
	}
	return NULL;
}

static uint16_t hid_report_descriptor_length(struct usb_device *udev,
					     uint8_t iface)
{
	const struct usb_if_descriptor *if_desc = hid_iface_descriptor(udev, iface);
	const struct usb_cfg_descriptor *cfg = udev->cfg_desc;
	const uint8_t *cursor;
	const uint8_t *end;

	if (if_desc == NULL || cfg == NULL) {
		return 0;
	}
	cursor = (const uint8_t *)if_desc + if_desc->bLength;
	end = (const uint8_t *)cfg + sys_le16_to_cpu(cfg->wTotalLength);
	while (cursor + 2U <= end) {
		uint8_t length = cursor[0];
		uint8_t type = cursor[1];

		if (length < 2U || cursor + length > end || type == USB_DESC_INTERFACE) {
			break;
		}
		if (type == USB_DESC_HID && length >= 9U) {
			uint8_t descriptor_count = cursor[5];

			for (uint8_t i = 0; i < descriptor_count; i++) {
				unsigned int offset = 6U + i * 3U;

				if (offset + 3U <= length &&
				    cursor[offset] == USB_DESC_HID_REPORT) {
					return sys_get_le16(&cursor[offset + 1U]);
				}
			}
		}
		cursor += length;
	}
	return 0;
}

static uint8_t hid_report_id(struct usb_device *udev, uint8_t iface)
{
	uint16_t length = hid_report_descriptor_length(udev, iface);
	struct net_buf *buf;
	uint8_t report_id = 0;
	int ret;

	if (length == 0U || length > 512U) {
		return 0;
	}
	buf = usbh_xfer_buf_alloc(udev, length);
	if (buf == NULL) {
		return 0;
	}
	ret = usbh_req_setup(udev,
			     (USB_REQTYPE_DIR_TO_HOST << 7) |
			     USB_REQTYPE_RECIPIENT_INTERFACE,
			     USB_SREQ_GET_DESCRIPTOR,
			     USB_DESC_HID_REPORT << 8, iface, length, buf);
	if (ret == 0) {
		for (size_t offset = 0; offset < buf->len;) {
			uint8_t prefix = buf->data[offset];
			unsigned int size;
			unsigned int type;
			unsigned int tag;

			if (prefix == 0xfeU) {
				if (offset + 3U > buf->len) {
					break;
				}
				offset += 3U + buf->data[offset + 1U];
				continue;
			}
			size = prefix & 0x03U;
			size = size == 3U ? 4U : size;
			type = (prefix >> 2) & 0x03U;
			tag = prefix >> 4;
			if (offset + 1U + size > buf->len) {
				break;
			}
			/* Global item, Report ID. */
			if (type == HID_ITEM_TYPE_GLOBAL && tag == 0x08U && size >= 1U) {
				report_id = buf->data[offset + 1U];
				break;
			}
			offset += 1U + size;
		}
	}
	usbh_xfer_buf_free(udev, buf);
	return report_id;
}

static bool report_contains(const uint8_t report[8], uint8_t usage)
{
	for (unsigned int i = 2; i < 8; i++) {
		if (report[i] == usage) {
			return true;
		}
	}
	return false;
}

static void key_enqueue_locked(uint8_t mac_code, bool down)
{
	unsigned int next;

	if (mac_code == UMAC_VK_None) {
		return;
	}
	next = (keyboard.head + 1U) % KEY_QUEUE_CAPACITY;
	if (next == keyboard.tail) {
		keyboard.dropped++;
		return;
	}
	keyboard.queue[keyboard.head] =
		(struct key_event){ .mac_code = mac_code, .down = down };
	keyboard.head = next;
	keyboard.queued++;
}

static void keyboard_process_report(const uint8_t report[8])
{
	static const uint8_t modifier_masks[] = {
		BIT(0) | BIT(4), BIT(1) | BIT(5),
		BIT(2) | BIT(6), BIT(3) | BIT(7),
	};
	static const uint8_t modifier_keys[] = {
		UMAC_VK_Control, UMAC_VK_Shift, UMAC_VK_Option, UMAC_VK_Command,
	};
	k_spinlock_key_t lock = k_spin_lock(&keyboard.lock);

	/* Usages 1..3 signal rollover/error rather than real keys. */
	for (unsigned int i = 2; i < 8; i++) {
		if (report[i] >= 1U && report[i] <= 3U) {
			k_spin_unlock(&keyboard.lock, lock);
			return;
		}
	}
	for (unsigned int i = 0; i < ARRAY_SIZE(modifier_masks); i++) {
		bool was_down = (keyboard.previous[0] & modifier_masks[i]) != 0U;
		bool is_down = (report[0] & modifier_masks[i]) != 0U;

		if (was_down != is_down) {
			key_enqueue_locked(modifier_keys[i], is_down);
		}
	}
	for (unsigned int i = 2; i < 8; i++) {
		uint8_t usage = keyboard.previous[i];

		if (usage > 3U && !report_contains(report, usage)) {
			key_enqueue_locked(umac_usb_hid_usage_to_mac(usage), false);
		}
	}
	for (unsigned int i = 2; i < 8; i++) {
		uint8_t usage = report[i];

		if (usage > 3U && !report_contains(keyboard.previous, usage)) {
			key_enqueue_locked(umac_usb_hid_usage_to_mac(usage), true);
		}
	}
	memcpy(keyboard.previous, report, sizeof(keyboard.previous));
	k_spin_unlock(&keyboard.lock, lock);
}

static int port_rearm(struct hid_port *port,
		      int (*complete)(struct usb_device *, struct uhc_transfer *))
{
	struct net_buf *buf;
	int ret;

	buf = usbh_xfer_buf_alloc(port->udev, port->packet_size);
	if (buf == NULL) {
		return -ENOMEM;
	}
	port->xfer->buf = buf;
	port->xfer->priv = port;
	port->xfer->cb = complete;
	ret = usbh_xfer_enqueue(port->udev, port->xfer);
	if (ret != 0) {
		usbh_xfer_buf_free(port->udev, buf);
	}
	return ret;
}

static int mouse_complete(struct usb_device *udev, struct uhc_transfer *xfer)
{
	struct net_buf *buf = xfer->buf;
	int ret = 0;

	if (xfer->err == 0 && buf != NULL && buf->len >= 3U && !mouse.port.stopping) {
		const uint8_t *report = buf->data;
		unsigned int offset = mouse.port.report_id != 0U && buf->len >= 4U &&
			report[0] == mouse.port.report_id ? 1U : 0U;
		k_spinlock_key_t key = k_spin_lock(&mouse.lock);

		mouse.dx = CLAMP(mouse.dx + (int8_t)report[offset + 1U], -256, 256);
		mouse.dy = CLAMP(mouse.dy + (int8_t)report[offset + 2U], -256, 256);
		mouse.buttons = report[offset];
		mouse.last_report_len = MIN(buf->len, sizeof(mouse.last_report));
		memcpy(mouse.last_report, report, mouse.last_report_len);
		mouse.dirty = true;
		mouse.port.reports++;
		k_spin_unlock(&mouse.lock, key);
	} else if (xfer->err != -ECONNRESET && xfer->err != 0) {
		mouse.port.errors++;
		printk("DIAG USB HID mouse transfer error=%d\n", xfer->err);
	}
	if (buf != NULL) {
		usbh_xfer_buf_free(udev, buf);
	}
	if (!mouse.port.stopping && xfer->err != -ECONNRESET) {
		ret = port_rearm(&mouse.port, mouse_complete);
		if (ret == 0) {
			return 0;
		}
		mouse.port.errors++;
		printk("DIAG USB HID mouse rearm error=%d\n", ret);
	}
	usbh_xfer_free(udev, xfer);
	mouse.port.xfer = NULL;
	return 0;
}

static int keyboard_complete(struct usb_device *udev, struct uhc_transfer *xfer)
{
	struct net_buf *buf = xfer->buf;
	int ret = 0;

	if (xfer->err == 0 && buf != NULL && buf->len >= 8U &&
	    !keyboard.port.stopping) {
		const uint8_t *report = buf->data;
		unsigned int offset = keyboard.port.report_id != 0U && buf->len >= 9U &&
			report[0] == keyboard.port.report_id ? 1U : 0U;

		if (buf->len >= offset + 8U) {
			keyboard_process_report(report + offset);
		}
		keyboard.port.reports++;
	} else if (xfer->err != -ECONNRESET && xfer->err != 0) {
		keyboard.port.errors++;
		printk("DIAG USB HID keyboard transfer error=%d\n", xfer->err);
	}
	if (buf != NULL) {
		usbh_xfer_buf_free(udev, buf);
	}
	if (!keyboard.port.stopping && xfer->err != -ECONNRESET) {
		ret = port_rearm(&keyboard.port, keyboard_complete);
		if (ret == 0) {
			return 0;
		}
		keyboard.port.errors++;
		printk("DIAG USB HID keyboard rearm error=%d\n", ret);
	}
	usbh_xfer_free(udev, xfer);
	keyboard.port.xfer = NULL;
	return 0;
}

static int hid_port_probe(struct hid_port *port, struct usb_device *udev,
			  uint8_t iface, uint8_t protocol,
			  int (*complete)(struct usb_device *, struct uhc_transfer *))
{
	const struct usb_if_descriptor *if_desc = hid_iface_descriptor(udev, iface);
	const struct usb_ep_descriptor *ep;
	uint16_t packet_size;
	int ret;

	port->probes++;
	printk("DIAG USB HID probe iface=%u protocol=%u VID:PID=%04x:%04x\n",
	       iface, protocol, udev->dev_desc.idVendor, udev->dev_desc.idProduct);
	if (if_desc == NULL || if_desc->bInterfaceClass != HID_CLASS ||
	    if_desc->bInterfaceSubClass != HID_BOOT_SUBCLASS ||
	    if_desc->bInterfaceProtocol != protocol) {
		port->last_probe_result = -ENOTSUP;
		return -ENOTSUP;
	}
	ep = hid_interrupt_in_endpoint(udev, iface);
	if (ep == NULL) {
		printk("DIAG USB HID iface=%u has no interrupt IN endpoint\n", iface);
		port->last_probe_result = -ENOTSUP;
		return -ENOTSUP;
	}
	packet_size = sys_le16_to_cpu(ep->wMaxPacketSize) & 0x07ffU;
	if (packet_size < (protocol == HID_BOOT_KEYBOARD ? 8U : 3U)) {
		port->last_probe_result = -ENOTSUP;
		return -ENOTSUP;
	}
	packet_size = MIN(packet_size, (uint16_t)HID_REPORT_MAX);
	ret = usbh_req_setup(udev, 0x21, USB_HID_SET_PROTOCOL,
			     HID_PROTOCOL_BOOT, iface, 0, NULL);
	if (ret != 0) {
		/* Some compliant wireless receivers already emit Boot reports but
		 * stall SET_PROTOCOL. Accept them and validate reports by length. */
		printk("DIAG USB HID iface=%u SET_PROTOCOL Boot=%d; continuing\n",
		       iface, ret);
	}
	port->xfer = usbh_xfer_alloc(udev, ep->bEndpointAddress, complete, port);
	if (port->xfer == NULL) {
		port->last_probe_result = -ENOMEM;
		return -ENOMEM;
	}
	port->udev = udev;
	port->iface = iface;
	port->endpoint = ep->bEndpointAddress;
	port->report_id = hid_report_id(udev, iface);
	port->packet_size = packet_size;
	port->stopping = false;
	ret = port_rearm(port, complete);
	if (ret != 0) {
		usbh_xfer_free(udev, port->xfer);
		port->xfer = NULL;
		port->udev = NULL;
		port->last_probe_result = ret;
		return ret;
	}
	port->last_probe_result = 0;
	printk("DIAG USB HID %s connected VID:PID=%04x:%04x iface=%u EP=%02x MPS=%u report_id=%u\n",
	       protocol == HID_BOOT_KEYBOARD ? "keyboard" : "mouse",
	       udev->dev_desc.idVendor, udev->dev_desc.idProduct, iface,
	       port->endpoint, port->packet_size, port->report_id);
	return 0;
}

static int class_init(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);
	return 0;
}

static int mouse_probe(struct usbh_class_data *const c_data,
		       struct usb_device *udev, uint8_t iface)
{
	ARG_UNUSED(c_data);
	return hid_port_probe(&mouse.port, udev, iface, HID_BOOT_MOUSE,
			      mouse_complete);
}

static int keyboard_probe(struct usbh_class_data *const c_data,
			  struct usb_device *udev, uint8_t iface)
{
	ARG_UNUSED(c_data);
	return hid_port_probe(&keyboard.port, udev, iface, HID_BOOT_KEYBOARD,
			      keyboard_complete);
}

static int hid_port_remove(struct hid_port *port)
{
	int ret = 0;

	port->stopping = true;
	if (port->xfer != NULL && port->udev != NULL) {
		ret = usbh_xfer_dequeue(port->udev, port->xfer);
	}
	port->udev = NULL;
	return ret;
}

static int mouse_removed(struct usbh_class_data *const c_data)
{
	k_spinlock_key_t key;
	int ret;

	ARG_UNUSED(c_data);
	ret = hid_port_remove(&mouse.port);
	key = k_spin_lock(&mouse.lock);
	mouse.dx = 0;
	mouse.dy = 0;
	mouse.buttons = 0;
	mouse.dirty = true;
	k_spin_unlock(&mouse.lock, key);
	printk("DIAG USB HID mouse disconnected\n");
	return ret;
}

static int keyboard_removed(struct usbh_class_data *const c_data)
{
	k_spinlock_key_t key;
	int ret;

	ARG_UNUSED(c_data);
	ret = hid_port_remove(&keyboard.port);
	key = k_spin_lock(&keyboard.lock);
	for (unsigned int i = 2; i < 8; i++) {
		if (keyboard.previous[i] > 3U) {
			key_enqueue_locked(
				umac_usb_hid_usage_to_mac(keyboard.previous[i]), false);
		}
	}
	if (keyboard.previous[0] != 0U) {
		key_enqueue_locked(UMAC_VK_Control, false);
		key_enqueue_locked(UMAC_VK_Shift, false);
		key_enqueue_locked(UMAC_VK_Option, false);
		key_enqueue_locked(UMAC_VK_Command, false);
	}
	memset(keyboard.previous, 0, sizeof(keyboard.previous));
	k_spin_unlock(&keyboard.lock, key);
	printk("DIAG USB HID keyboard disconnected\n");
	return ret;
}

static struct usbh_class_api mouse_api = {
	.init = class_init,
	.probe = mouse_probe,
	.removed = mouse_removed,
};

static struct usbh_class_api keyboard_api = {
	.init = class_init,
	.probe = keyboard_probe,
	.removed = keyboard_removed,
};

static const struct usbh_class_filter mouse_filters[] = {
	{ .class = HID_CLASS, .sub = HID_BOOT_SUBCLASS, .proto = HID_BOOT_MOUSE,
	  .flags = USBH_CLASS_MATCH_CODE_TRIPLE },
	{ 0 },
};

static const struct usbh_class_filter keyboard_filters[] = {
	{ .class = HID_CLASS, .sub = HID_BOOT_SUBCLASS,
	  .proto = HID_BOOT_KEYBOARD, .flags = USBH_CLASS_MATCH_CODE_TRIPLE },
	{ 0 },
};

USBH_DEFINE_CLASS(umac_boot_mouse, &mouse_api, NULL, mouse_filters);
USBH_DEFINE_CLASS(umac_boot_keyboard, &keyboard_api, NULL, keyboard_filters);

static int manual_bind(struct usbh_class_node *node, struct usb_device *root,
		       uint8_t protocol)
{
	for (uint8_t iface = 0; iface <= UHC_INTERFACES_MAX; iface++) {
		const struct usb_if_descriptor *if_desc =
			hid_iface_descriptor(root, iface);
		int ret;

		if (if_desc == NULL || if_desc->bInterfaceClass != HID_CLASS ||
		    if_desc->bInterfaceSubClass != HID_BOOT_SUBCLASS ||
		    if_desc->bInterfaceProtocol != protocol) {
			continue;
		}
		if (node->state != USBH_CLASS_STATE_IDLE) {
			return 0;
		}
		ret = node->c_data->api->probe(node->c_data, root, iface);
		if (ret == 0) {
			node->c_data->udev = root;
			node->c_data->iface = iface;
			node->state = USBH_CLASS_STATE_BOUND;
		}
		printk("DIAG USB HID manual bind protocol=%u iface=%u result=%d\n",
		       protocol, iface, ret);
		return ret;
	}
	return -ENODEV;
}

int umac_usb_hid_bind_interfaces(void)
{
	struct usb_device *root = usbh_device_get_root(&umac_usbh);
	const struct usb_cfg_descriptor *config =
		root != NULL ? root->cfg_desc : NULL;
	int keyboard_ret;
	int mouse_ret;

	if (root == NULL || root->state != USB_STATE_CONFIGURED || config == NULL) {
		printk("DIAG USB HID manual bind deferred: root/config unavailable\n");
		return -ENODEV;
	}
	keyboard_ret = manual_bind(&umac_boot_keyboard, root, HID_BOOT_KEYBOARD);
	mouse_ret = manual_bind(&umac_boot_mouse, root, HID_BOOT_MOUSE);
	return keyboard_ret == 0 || mouse_ret == 0 ? 0 : -ENODEV;
}

/* The board's USB clocks are currently initialized only for USB device-mode
 * Kconfig selections. This host-only build must enable the same clock tree. */
static int hid_usb_clocks(void)
{
	SPC0->ACTIVE_VDELAY = 0x0500;
	SPC0->ACTIVE_CFG &= ~SPC_ACTIVE_CFG_CORELDO_VDD_DS_MASK;
	SPC0->ACTIVE_CFG |= SPC_ACTIVE_CFG_DCDC_VDD_LVL(0x3) |
		SPC_ACTIVE_CFG_CORELDO_VDD_LVL(0x3) |
		SPC_ACTIVE_CFG_SYSLDO_VDD_DS_MASK |
		SPC_ACTIVE_CFG_DCDC_VDD_DS(0x2u);
	while (SPC0->SC & SPC_SC_BUSY_MASK) {
	}
	if ((SCG0->LDOCSR & SCG_LDOCSR_LDOEN_MASK) == 0U) {
		SCG0->TRIM_LOCK = 0x5a5a0001U;
		SCG0->LDOCSR |= SCG_LDOCSR_LDOEN_MASK;
		while ((SCG0->LDOCSR & SCG_LDOCSR_VOUT_OK_MASK) == 0U) {
		}
	}
	SYSCON->AHBCLKCTRLSET[2] |= SYSCON_AHBCLKCTRL2_USB_HS_MASK |
		SYSCON_AHBCLKCTRL2_USB_HS_PHY_MASK;
	SCG0->SOSCCFG &= ~(SCG_SOSCCFG_RANGE_MASK | SCG_SOSCCFG_EREFS_MASK);
	SCG0->SOSCCFG = (1U << SCG_SOSCCFG_RANGE_SHIFT) |
		(1U << SCG_SOSCCFG_EREFS_SHIFT);
	SCG0->SOSCCSR |= SCG_SOSCCSR_SOSCEN_MASK;
	while ((SCG0->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK) == 0U) {
	}
	SYSCON->CLOCK_CTRL |= SYSCON_CLOCK_CTRL_CLKIN_ENA_MASK |
		SYSCON_CLOCK_CTRL_CLKIN_ENA_FM_USBH_LPT_MASK;
	CLOCK_EnableClock(kCLOCK_UsbHs);
	CLOCK_EnableClock(kCLOCK_UsbHsPhy);
	if (!CLOCK_EnableUsbhsPhyPllClock(kCLOCK_Usbphy480M,
					 BOARD_XTAL0_CLK_HZ) ||
	    !CLOCK_EnableUsbhsClock()) {
		return -EIO;
	}
	return 0;
}

int umac_usb_mouse_init(void)
{
	int ret = hid_usb_clocks();

	if (ret == 0) {
		k_sleep(K_MSEC(500));
		ret = usbh_init(&umac_usbh);
	}
	if (ret == 0) {
		k_sleep(K_MSEC(500));
		ret = usbh_enable(&umac_usbh);
	}
	host_init_result = ret;
	keyboard.next_event_ms = k_uptime_get_32();
	next_bind_ms = keyboard.next_event_ms;
	printk("DIAG USB HID host init=%d\n", ret);
	return ret;
}

void umac_usb_mouse_poll(void)
{
	k_spinlock_key_t lock = k_spin_lock(&mouse.lock);
	int dx = mouse.dx;
	int dy = mouse.dy;
	int button = (mouse.buttons & 1U) != 0U;
	bool dirty = mouse.dirty;
	uint32_t now;

	mouse.dx = 0;
	mouse.dy = 0;
	mouse.dirty = false;
	k_spin_unlock(&mouse.lock, lock);
	if (dirty) {
		mouse.raw_x += dx < 0 ? (uint32_t)-dx : (uint32_t)dx;
		mouse.raw_y += dy < 0 ? (uint32_t)-dy : (uint32_t)dy;
		/* Boot-mouse deltas are already relative motion units. */
		umac_mouse(dx, -dy, button);
		mouse.delivered++;
	}

	now = k_uptime_get_32();
	if ((mouse.port.udev == NULL || keyboard.port.udev == NULL) &&
	    (int32_t)(now - next_bind_ms) >= 0) {
		(void)umac_usb_hid_bind_interfaces();
		next_bind_ms = now + 1000U;
	}
	if ((int32_t)(now - keyboard.next_event_ms) >= 0) {
		struct key_event event;
		bool available = false;

		lock = k_spin_lock(&keyboard.lock);
		if (keyboard.tail != keyboard.head) {
			event = keyboard.queue[keyboard.tail];
			keyboard.tail = (keyboard.tail + 1U) % KEY_QUEUE_CAPACITY;
			available = true;
		}
		k_spin_unlock(&keyboard.lock, lock);
		if (available) {
			umac_kbd_event((uint8_t)((event.mac_code << 1U) | 1U),
				       event.down ? 1 : 0);
			keyboard.delivered++;
			keyboard.next_event_ms = now + KEY_EVENT_SPACING_MS;
		}
	}
}

void umac_usb_mouse_report(void)
{
	struct usb_device *root = usbh_device_get_root(&umac_usbh);

	printk("DIAG USB root=%u state=%u addr=%u cfg=%u desc=%u\n",
	       root != NULL, root != NULL ? root->state : 0U,
	       root != NULL ? root->addr : 0U,
	       root != NULL ? root->actual_cfg : 0U,
	       root != NULL && root->cfg_desc != NULL);
	printk("DIAG USB HID host=%d mouse probes=%u result=%d connected=%u iface=%u ep=%02x reports=%u delivered=%u errors=%u motion=%u,%u\n",
	       host_init_result, mouse.port.probes, mouse.port.last_probe_result,
	       mouse.port.udev != NULL, mouse.port.iface, mouse.port.endpoint,
	       mouse.port.reports, mouse.delivered, mouse.port.errors,
	       mouse.raw_x, mouse.raw_y);
	printk("DIAG USB HID mouse raw len=%u %02x %02x %02x %02x %02x %02x %02x %02x\n",
	       mouse.last_report_len, mouse.last_report[0], mouse.last_report[1],
	       mouse.last_report[2], mouse.last_report[3], mouse.last_report[4],
	       mouse.last_report[5], mouse.last_report[6], mouse.last_report[7]);
	printk("DIAG USB HID keyboard probes=%u result=%d connected=%u iface=%u ep=%02x reports=%u queued=%u delivered=%u dropped=%u errors=%u\n",
	       keyboard.port.probes, keyboard.port.last_probe_result,
	       keyboard.port.udev != NULL, keyboard.port.iface,
	       keyboard.port.endpoint, keyboard.port.reports, keyboard.queued,
	       keyboard.delivered, keyboard.dropped, keyboard.port.errors);
}
