/* SPDX-License-Identifier: MIT */
/* Experimental exclusive USB device mode. Never mount the guest HFS image
 * here; the PC/Mac owns the FAT block device until safe eject and reset.
 * The VID/PID below is for a private build probe ONLY, not distribution.
 */
#include "umac_usb_transfer.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/usb/usbd.h>

#include <ff.h>

#define TRANSFER_BUTTON DT_ALIAS(sw0)
#define FLASH_NODE DT_NODELABEL(ext_flash_ctrl)
#define TRANSFER_PARTITION DT_NODELABEL(storage_partition)
#define TRANSFER_START 0x00010000U
#define TRANSFER_BYTES 0x007e0000U
#define BLANK_SCAN_BYTES 4096U

_Static_assert(DT_REG_ADDR(TRANSFER_PARTITION) == TRANSFER_START,
	       "unexpected USB transfer partition offset");
_Static_assert(DT_REG_SIZE(TRANSFER_PARTITION) == TRANSFER_BYTES,
	       "unexpected USB transfer partition size");

static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(TRANSFER_BUTTON, gpios);
static FATFS transfer_fat_fs;
static struct fs_mount_t transfer_mount = {
	.type = FS_FATFS,
	.mnt_point = "/NAND:",
	.fs_data = &transfer_fat_fs,
	.storage_dev = (void *)PARTITION_ID(storage_partition),
	.flags = FS_MOUNT_FLAG_NO_FORMAT | FS_MOUNT_FLAG_USE_DISK_ACCESS,
};
static uint8_t blank_scan[BLANK_SCAN_BYTES];

static bool transfer_area_is_blank(void)
{
	const struct device *flash = DEVICE_DT_GET(FLASH_NODE);
	uint64_t device_bytes;
	uint8_t jedec[3];

	if (!device_is_ready(flash) ||
	    flash_get_size(flash, &device_bytes) != 0 ||
	    device_bytes != 0x00800000U ||
	    flash_read_jedec_id(flash, jedec) != 0 ||
	    jedec[0] != 0xef || jedec[1] != 0x40 || jedec[2] != 0x17) {
		return false;
	}
	for (uint32_t offset = TRANSFER_START;
	     offset < TRANSFER_START + TRANSFER_BYTES;
	     offset += BLANK_SCAN_BYTES) {
		if (flash_read(flash, offset, blank_scan,
			       sizeof(blank_scan)) != 0) {
			return false;
		}
		for (size_t i = 0; i < sizeof(blank_scan); i++) {
			if (blank_scan[i] != 0xffU) {
				return false;
			}
		}
	}
	return true;
}

USBD_DEVICE_DEFINE(umac_transfer_usbd,
	DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x2fe3, 0x0008);
USBD_DEFINE_MSC_LUN(umac_image, "NAND", "Zephyr", "MCX Image", "0.01");
USBD_DESC_LANG_DEFINE(umac_lang);
USBD_DESC_MANUFACTURER_DEFINE(umac_manufacturer, "MCX-N prototype");
USBD_DESC_PRODUCT_DEFINE(umac_product, "Mac image transfer test");
USBD_DESC_CONFIG_DEFINE(umac_fs_config_desc, "Full speed");
USBD_DESC_CONFIG_DEFINE(umac_hs_config_desc, "High speed");
USBD_CONFIGURATION_DEFINE(umac_fs_config, 0, 100, &umac_fs_config_desc);
USBD_CONFIGURATION_DEFINE(umac_hs_config, 0, 100, &umac_hs_config_desc);

bool umac_usb_transfer_requested(void)
{
	if (!device_is_ready(button.port) ||
	    gpio_pin_configure_dt(&button, GPIO_INPUT) != 0) {
		return false;
	}
	return gpio_pin_get_dt(&button) > 0;
}

int umac_usb_transfer_run(void)
{
	static const char *const blocklist[] = { NULL };
	int ret;

	printk("USB image transfer mode: Mac and USB mouse disabled\n");
	/* Existing FAT is safe to export. Only a fully erased, exactly matching
	 * W25Q64 may be initialized here; the current raw-HFS layout is refused.
	 */
	ret = fs_mount(&transfer_mount);
	if (ret != 0) {
		if (!transfer_area_is_blank()) {
			printk("USB transfer refused: nonblank non-FAT Flash (%d)\n", ret);
			return ret;
		}
		printk("USB transfer: formatting verified-blank Flash as FAT\n");
		ret = fs_mkfs(FS_FATFS, (uintptr_t)"NAND:", NULL, 0);
		if (ret != 0) {
			return ret;
		}
		ret = fs_mount(&transfer_mount);
		if (ret != 0) {
			return ret;
		}
	}
	ret = fs_unmount(&transfer_mount);
	if (ret != 0) {
		printk("USB transfer refused: FAT unmount failed (%d)\n", ret);
		return ret;
	}
	ret = usbd_add_descriptor(&umac_transfer_usbd, &umac_lang);
	if (ret == 0) {
		ret = usbd_add_descriptor(&umac_transfer_usbd, &umac_manufacturer);
	}
	if (ret == 0) {
		ret = usbd_add_descriptor(&umac_transfer_usbd, &umac_product);
	}
	if (ret != 0) {
		return ret;
	}
	if (USBD_SUPPORTS_HIGH_SPEED &&
	    usbd_caps_speed(&umac_transfer_usbd) == USBD_SPEED_HS) {
		ret = usbd_add_configuration(&umac_transfer_usbd, USBD_SPEED_HS,
					     &umac_hs_config);
		if (ret == 0) {
			ret = usbd_register_all_classes(&umac_transfer_usbd,
				USBD_SPEED_HS, 1, blocklist);
		}
		if (ret != 0) {
			return ret;
		}
	}
	ret = usbd_add_configuration(&umac_transfer_usbd, USBD_SPEED_FS,
				     &umac_fs_config);
	if (ret == 0) {
		ret = usbd_register_all_classes(&umac_transfer_usbd,
			USBD_SPEED_FS, 1, blocklist);
	}
	if (ret == 0) {
		ret = usbd_init(&umac_transfer_usbd);
	}
	if (ret == 0) {
		ret = usbd_enable(&umac_transfer_usbd);
	}
	if (ret != 0) {
		return ret;
	}
	printk("USB image transfer active; safely eject on PC/Mac, then RESET\n");
	for (;;) {
		k_sleep(K_SECONDS(1));
	}
	return -EIO;
}
