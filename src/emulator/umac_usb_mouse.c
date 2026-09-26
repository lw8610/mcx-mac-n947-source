/* SPDX-License-Identifier: MIT */
#include "umac_usb_mouse.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usbh.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/usb/class/hid.h>
#include <soc.h>
#include <fsl_clock.h>
#include <umac.h>

#include "usbh_ch9.h"
#include "usbh_class.h"
#include "usbh_device.h"

#define BOARD_XTAL0_CLK_HZ 24000000U
#define MOUSE_INTERFACE 1U
#define MOUSE_ENDPOINT 0x82U
#define MOUSE_REPORT_BYTES 8U

USBH_CONTROLLER_DEFINE(umac_usbh, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

static struct {
	struct usb_device *udev;
	struct uhc_transfer *xfer;
	struct k_spinlock lock;
	int dx;
	int dy;
	uint8_t buttons;
	bool dirty;
	bool stopping;
	uint32_t reports;
	uint32_t errors;
	uint32_t delivered;
	uint32_t class_probes;
	int init_result;
	int last_probe_result;
	int32_t scale_remainder_x;
	int32_t scale_remainder_y;
	uint32_t raw_x;
	uint32_t raw_y;
	uint32_t scaled_x;
	uint32_t scaled_y;
} mouse;

/* Apply one linear 1:16 gain to both axes. A speed-dependent divisor made
 * faster physical movement produce less pointer travel for the same distance.
 * Signed remainders retain small, deliberate movements across USB reports.
 */
static int mouse_rate_map(int delta, int32_t *remainder)
{
	int32_t value = *remainder + delta * 16;
	int output = value / 256;

	*remainder = value - output * 256;
	return output;
}

static int mouse_class_init(struct usbh_class_data *const c_data)
{
	ARG_UNUSED(c_data);
	return 0;
}

static int mouse_complete(struct usb_device *udev, struct uhc_transfer *xfer)
{
	struct net_buf *buf = xfer->buf;
	int ret;

	if (xfer->err == 0 && buf != NULL && buf->len >= 3U && !mouse.stopping) {
		const uint8_t *report = buf->data;
		k_spinlock_key_t key = k_spin_lock(&mouse.lock);

		mouse.dx = CLAMP(mouse.dx + (int8_t)report[1], -256, 256);
		mouse.dy = CLAMP(mouse.dy + (int8_t)report[2], -256, 256);
		mouse.buttons = report[0];
		mouse.dirty = true;
		mouse.reports++;
		k_spin_unlock(&mouse.lock, key);
	} else if (xfer->err != -ECONNRESET && xfer->err != 0) {
		mouse.errors++;
		printk("DIAG USB mouse transfer error=%d\n", xfer->err);
	}

	if (buf != NULL) {
		usbh_xfer_buf_free(udev, buf);
	}
	if (xfer->err == 0 && !mouse.stopping) {
		buf = usbh_xfer_buf_alloc(udev, MOUSE_REPORT_BYTES);
		if (buf != NULL) {
			xfer->buf = buf;
			ret = usbh_xfer_enqueue(udev, xfer);
			if (ret == 0) {
				return 0;
			}
			usbh_xfer_buf_free(udev, buf);
		} else {
			ret = -ENOMEM;
		}
		mouse.errors++;
		printk("DIAG USB mouse rearm error=%d\n", ret);
	}
	usbh_xfer_free(udev, xfer);
	mouse.xfer = NULL;
	return 0;
}

static int mouse_class_probe(struct usbh_class_data *const c_data,
			     struct usb_device *udev, uint8_t iface)
{
	const struct usb_ep_descriptor *ep;
	struct uhc_transfer *xfer;
	struct net_buf *buf;
	int ret;

	ARG_UNUSED(c_data);
	mouse.class_probes++;
	printk("DIAG USB mouse probe iface=%u VID:PID=%04x:%04x\n",
	       iface, udev->dev_desc.idVendor, udev->dev_desc.idProduct);
	if (iface != MOUSE_INTERFACE ||
	    udev->dev_desc.idVendor != 0x046dU ||
	    udev->dev_desc.idProduct != 0xc52bU) {
		mouse.last_probe_result = -ENOTSUP;
		return -ENOTSUP;
	}
	ep = udev->ep_in[MOUSE_ENDPOINT & 0x0fU].desc;
	if (ep == NULL || ep->bEndpointAddress != MOUSE_ENDPOINT ||
	    (ep->bmAttributes & 0x03U) != 0x03U) {
		printk("DIAG USB mouse endpoint mismatch\n");
		mouse.last_probe_result = -ENOTSUP;
		return -ENOTSUP;
	}
	ret = usbh_req_setup(udev, 0x21, USB_HID_SET_PROTOCOL,
			     HID_PROTOCOL_BOOT, iface, 0, NULL);
	if (ret != 0) {
		printk("DIAG USB mouse Boot protocol error=%d\n", ret);
		mouse.last_probe_result = ret;
		return -ENOTSUP;
	}
	xfer = usbh_xfer_alloc(udev, MOUSE_ENDPOINT, mouse_complete, NULL);
	if (xfer == NULL) {
		mouse.last_probe_result = -ENOMEM;
		return -ENOTSUP;
	}
	buf = usbh_xfer_buf_alloc(udev, MOUSE_REPORT_BYTES);
	if (buf == NULL) {
		usbh_xfer_free(udev, xfer);
		mouse.last_probe_result = -ENOMEM;
		return -ENOTSUP;
	}
	xfer->buf = buf;
	mouse.stopping = false;
	mouse.udev = udev;
	mouse.xfer = xfer;
	ret = usbh_xfer_enqueue(udev, xfer);
	if (ret != 0) {
		mouse.xfer = NULL;
		mouse.udev = NULL;
		usbh_xfer_buf_free(udev, buf);
		usbh_xfer_free(udev, xfer);
		printk("DIAG USB mouse arm error=%d\n", ret);
		mouse.last_probe_result = ret;
		return -ENOTSUP;
	}
	mouse.last_probe_result = 0;
	printk("DIAG USB mouse connected VID:PID=046d:c52b EP=82\n");
	return 0;
}

static int mouse_class_removed(struct usbh_class_data *const c_data)
{
	int ret = 0;
	k_spinlock_key_t key;

	ARG_UNUSED(c_data);
	mouse.stopping = true;
	if (mouse.xfer != NULL) {
		ret = usbh_xfer_dequeue(mouse.udev, mouse.xfer);
		if (ret != 0) {
			printk("DIAG USB mouse cancel error=%d\n", ret);
		}
	}
	mouse.udev = NULL;
	key = k_spin_lock(&mouse.lock);
	mouse.dx = 0;
	mouse.dy = 0;
	mouse.buttons = 0;
	mouse.dirty = true;
	k_spin_unlock(&mouse.lock, key);
	printk("DIAG USB mouse disconnected\n");
	return ret;
}

static struct usbh_class_api mouse_api = {
	.init = mouse_class_init,
	.probe = mouse_class_probe,
	.removed = mouse_class_removed,
};

/* Match all at the host-class dispatcher so the probe can log each function.
 * The probe itself accepts only the known mouse interface and receiver. */
USBH_DEFINE_CLASS(umac_boot_mouse, &mouse_api, NULL, NULL);

int umac_usb_mouse_bind_interface_one(void)
{
	struct usb_device *root = usbh_device_get_root(&umac_usbh);
	const struct usb_cfg_descriptor *config =
		root != NULL ? root->cfg_desc : NULL;
	const struct usb_if_descriptor *iface;
	int ret;

	if (root == NULL || root->state != USB_STATE_CONFIGURED ||
	    config == NULL || config->bNumInterfaces <= MOUSE_INTERFACE) {
		printk("DIAG USB manual bind deferred: root/config unavailable\n");
		return -ENODEV;
	}
	iface = (const struct usb_if_descriptor *)root->ifaces[MOUSE_INTERFACE].dhp;
	if (iface == NULL || iface->bInterfaceNumber != MOUSE_INTERFACE ||
	    iface->bInterfaceClass != 0x03U ||
	    iface->bInterfaceSubClass != 0x01U ||
	    iface->bInterfaceProtocol != 0x02U) {
		printk("DIAG USB manual bind: interface 1 is not Boot mouse\n");
		return -ENOTSUP;
	}
	if (umac_boot_mouse.state != USBH_CLASS_STATE_IDLE) {
		return -EALREADY;
	}
	/* Zephyr 4.4.2's usbh_desc_get_next_function() skips the next
	 * interface after each unassociated interface. The automatic walk sees
	 * interfaces 0 and 2 but not the receiver's mouse interface 1. */
	ret = mouse_class_probe(umac_boot_mouse.c_data, root, MOUSE_INTERFACE);
	if (ret == 0) {
		umac_boot_mouse.c_data->udev = root;
		umac_boot_mouse.c_data->iface = MOUSE_INTERFACE;
		umac_boot_mouse.state = USBH_CLASS_STATE_BOUND;
	}
	printk("DIAG USB manual bind iface=1 result=%d\n", ret);
	return ret;
}

/* The board's USB clocks are currently initialized only for USB device-mode
 * Kconfig selections. This host-only build must enable the same clock tree.
 */
static int mouse_usb_clocks(void)
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
	int ret = mouse_usb_clocks();

	if (ret == 0) {
		/* The standalone probe initializes the host from the shell, long
		 * after enabling the PHY PLL. Preserve a short settling interval in
		 * the integrated boot path before first bus reset/descriptor I/O. */
		k_sleep(K_MSEC(500));
		ret = usbh_init(&umac_usbh);
	}
	if (ret == 0) {
		k_sleep(K_MSEC(500));
		ret = usbh_enable(&umac_usbh);
	}
	mouse.init_result = ret;
	printk("DIAG USB mouse host init=%d\n", ret);
	return ret;
}

void umac_usb_mouse_poll(void)
{
	k_spinlock_key_t key = k_spin_lock(&mouse.lock);
	int dx = mouse.dx;
	int dy = mouse.dy;
	int button = (mouse.buttons & 1U) != 0U;
	bool dirty = mouse.dirty;

	mouse.dx = 0;
	mouse.dy = 0;
	mouse.dirty = false;
	k_spin_unlock(&mouse.lock, key);
	if (dirty) {
		int mapped_x = dx;
		int mapped_y = dy;

		mouse.raw_x += dx < 0 ? (uint32_t)-dx : (uint32_t)dx;
		mouse.raw_y += dy < 0 ? (uint32_t)-dy : (uint32_t)dy;
#if defined(MCX_MAC_USB_MOUSE_RATE_MAP)
		mapped_x = mouse_rate_map(dx, &mouse.scale_remainder_x);
		mapped_y = mouse_rate_map(dy, &mouse.scale_remainder_y);
#endif
		mouse.scaled_x += mapped_x < 0 ? (uint32_t)-mapped_x : (uint32_t)mapped_x;
		mouse.scaled_y += mapped_y < 0 ? (uint32_t)-mapped_y : (uint32_t)mapped_y;
		/* USB screen coordinates increase downwards; uMac Y increases up. */
		umac_mouse(mapped_x, -mapped_y, button);
		mouse.delivered++;
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
	printk("DIAG USB mouse init=%d probes=%u last_probe=%d connected=%u armed=%u reports=%u delivered=%u errors=%u\n",
	       mouse.init_result, mouse.class_probes, mouse.last_probe_result,
	       mouse.udev != NULL, mouse.xfer != NULL, mouse.reports,
	       mouse.delivered, mouse.errors);
	printk("DIAG USB mouse map raw=%u,%u scaled=%u,%u remainder=%d,%d\n",
	       mouse.raw_x, mouse.raw_y, mouse.scaled_x, mouse.scaled_y,
	       mouse.scale_remainder_x, mouse.scale_remainder_y);
}
