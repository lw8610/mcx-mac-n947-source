/* SPDX-License-Identifier: MIT */
#include "umac_boot.h"

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>

#include "emulator/umac_disk_overlay.h"
#if defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_HDD)
#include "emulator/umac_flash_hdd.h"
#endif
#if defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_FAT_IMAGE)
#include "emulator/umac_fat_image.h"
#endif
#include "emulator/umac_media_package.h"
#include "emulator/umac_memory.h"

#include <umac.h>

/* The FRDM-MCXN947's image-1 partition is a memory-mapped 0xf6000-byte
 * window. The released firmware only reads it. No write/erase occurs here.
 */
#define MEDIA_SLOT_NODE DT_NODELABEL(slot1_partition)
#define SONY_STUB_OFFSET 0x17d30U
#if defined(CONFIG_MCX_MAC_UMAC_FIXED_DISK_ROM)
#define SONY_STUB_BYTES 218U
#define SONY_STUB_CRC32 0x22f45fb3U
#else
#define SONY_STUB_BYTES 146U
#define SONY_STUB_CRC32 0x9f85bce3U
#endif
_Static_assert(DT_NODE_EXISTS(MEDIA_SLOT_NODE), "image-1 slot unavailable");
_Static_assert(DT_REG_SIZE(MEDIA_SLOT_NODE) == UMAC_MEDIA_SLOT_BYTES,
	       "unexpected image-1 size");

#if defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_HDD) && \
	!defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_HDD_SKIP_MEDIA)
static int hdd_read(void *ctx, uint8_t *buffer, uint32_t offset,
		    uint32_t length)
{
	(void)ctx;
	return umac_flash_hdd_read(offset / UMAC_FLASH_HDD_SECTOR_BYTES,
		buffer, length / UMAC_FLASH_HDD_SECTOR_BYTES);
}

static int hdd_write(void *ctx, uint8_t *buffer, uint32_t offset,
		     uint32_t length)
{
	(void)ctx;
	return umac_flash_hdd_write(offset / UMAC_FLASH_HDD_SECTOR_BYTES,
		buffer, length / UMAC_FLASH_HDD_SECTOR_BYTES);
}
#endif

#if defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_FAT_IMAGE)
static int fat_image_read(void *ctx, uint8_t *buffer, uint32_t offset,
			  uint32_t length)
{
	(void)ctx;
	return umac_fat_image_read(offset / UMAC_FAT_IMAGE_SECTOR_BYTES,
		buffer, length / UMAC_FAT_IMAGE_SECTOR_BYTES);
}

static int fat_image_write(void *ctx, uint8_t *buffer, uint32_t offset,
			   uint32_t length)
{
	(void)ctx;
	return umac_fat_image_write(offset / UMAC_FAT_IMAGE_SECTOR_BYTES,
		buffer, length / UMAC_FAT_IMAGE_SECTOR_BYTES);
}
#endif

bool umac_boot_init(void)
{
	const uint8_t *slot = (const uint8_t *)(uintptr_t)
		(CONFIG_FLASH_BASE_ADDRESS + DT_REG_ADDR(MEDIA_SLOT_NODE));
	struct umac_media_package media;
	disc_descr_t discs[DISC_NUM_DRIVES] = { 0 };

	if (!umac_media_package_open(slot, UMAC_MEDIA_SLOT_BYTES, &media) ||
	    umac_guest_ram_size() != UMAC_GUEST_RAM_BYTES) {
		printk("DIAG uMac media missing or invalid in image-1 slot\n");
		return false;
	}
	if (umac_media_crc32(media.rom + SONY_STUB_OFFSET,
			      SONY_STUB_BYTES) != SONY_STUB_CRC32) {
		printk("DIAG uMac ROM independent floppy stub mismatch\n");
		return false;
	}
	umac_disk_overlay_init(media.disk, media.disk_size);
	discs[0].size = media.disk_size;
	discs[0].op_read = umac_disk_overlay_read;
	discs[0].op_write = umac_disk_overlay_write;
#if defined(CONFIG_MCX_MAC_UMAC_FIXED_DISK_ROM)
#if defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_HDD_SKIP_MEDIA)
	printk("DIAG uMac fixed disk ROM only; external media not accessed\n");
#elif defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_HDD)
	int hdd_ret = umac_flash_hdd_prepare();

#if defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_HDD_FORMAT_BLANK)
	if (hdd_ret != 0) {
		hdd_ret = umac_flash_hdd_format_blank();
	}
#endif
	if (hdd_ret == 0) {
		discs[1].size = UMAC_FLASH_HDD_SECTORS *
			UMAC_FLASH_HDD_SECTOR_BYTES;
		discs[1].op_read = hdd_read;
		discs[1].op_write = hdd_write;
		printk("DIAG uMac external fixed disk ready: %u sectors\n",
		       UMAC_FLASH_HDD_SECTORS);
	} else {
		printk("DIAG uMac external fixed disk unavailable: %d\n",
		       hdd_ret);
	}
#elif defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_FAT_IMAGE)
	int image_ret = umac_fat_image_prepare();

	if (image_ret == 0) {
		discs[1].size = umac_fat_image_sector_count() *
			UMAC_FAT_IMAGE_SECTOR_BYTES;
		discs[1].op_read = fat_image_read;
		discs[1].read_only = umac_fat_image_is_read_only();
		if (!discs[1].read_only) {
			discs[1].op_write = fat_image_write;
		}
		printk("DIAG uMac FAT image ready: %u sectors %s\n",
		       umac_fat_image_sector_count(),
		       discs[1].read_only ? "MFS read-only" : "HFS writable");
	} else {
		printk("DIAG uMac FAT image unavailable: %d\n", image_ret);
	}
#else
	printk("DIAG uMac fixed disk ROM only; disk backend disabled\n");
#endif
#endif
	return umac_init(umac_guest_ram_get(), (void *)media.rom, discs) == 0;
}
