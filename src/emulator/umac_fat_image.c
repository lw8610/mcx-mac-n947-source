/* SPDX-License-Identifier: MIT */
/* One raw HFS/MFS file on a host-readable FAT volume serves as Mac drive 8.
 * The USB transfer mode must have exclusive ownership of the same volume.
 */
#include "umac_fat_image.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>

#include <ff.h>

#define STORAGE_PARTITION_ID PARTITION_ID(storage_partition)
#define FLASH_NODE DT_NODELABEL(ext_flash_ctrl)
#define IMAGE_PATH "/NAND:/umac0.img"
#define VOLUME_SIGNATURE_OFFSET (2U * UMAC_FAT_IMAGE_SECTOR_BYTES)
#define MAX_IMAGE_BYTES 0x007e0000U

static FATFS fat_fs;
static struct fs_mount_t fat_mount = {
	.type = FS_FATFS,
	.mnt_point = "/NAND:",
	.fs_data = &fat_fs,
	.storage_dev = (void *)STORAGE_PARTITION_ID,
	.flags = FS_MOUNT_FLAG_NO_FORMAT | FS_MOUNT_FLAG_USE_DISK_ACCESS,
};
static struct fs_file_t image_file;
static uint32_t image_sectors;
static bool ready;
static bool read_only;

int umac_fat_image_prepare(void)
{
	const struct device *flash = DEVICE_DT_GET(FLASH_NODE);
	struct fs_dirent info;
	uint8_t signature[2];
	uint8_t jedec[3];
	uint64_t device_bytes;
	int ret;

	ready = false;
	read_only = false;
	image_sectors = 0U;
	if (!device_is_ready(flash) ||
	    flash_get_size(flash, &device_bytes) != 0 ||
	    device_bytes != 0x00800000U ||
	    flash_read_jedec_id(flash, jedec) != 0 ||
	    jedec[0] != 0xef || jedec[1] != 0x40 || jedec[2] != 0x17) {
		return -ENODEV;
	}
	ret = fs_mount(&fat_mount);
	if (ret != 0) {
		return ret;
	}
	ret = fs_stat(IMAGE_PATH, &info);
	if (ret != 0) {
		goto fail;
	}
	if (info.type != FS_DIR_ENTRY_FILE ||
	    info.size < VOLUME_SIGNATURE_OFFSET + sizeof(signature) ||
	    info.size > MAX_IMAGE_BYTES ||
	    (info.size % UMAC_FAT_IMAGE_SECTOR_BYTES) != 0U) {
		ret = -EINVAL;
		goto fail;
	}
	fs_file_t_init(&image_file);
	ret = fs_open(&image_file, IMAGE_PATH, FS_O_READ);
	if (ret != 0) {
		goto fail;
	}
	ret = fs_seek(&image_file, VOLUME_SIGNATURE_OFFSET, FS_SEEK_SET);
	if (ret == 0) {
		ret = fs_read(&image_file, signature, sizeof(signature));
	}
	if (ret != sizeof(signature)) {
		ret = ret < 0 ? ret : -EINVAL;
		(void)fs_close(&image_file);
		goto fail;
	}
	if (signature[0] == 0xd2U && signature[1] == 0xd7U) {
		read_only = true;
	} else if (signature[0] != 'B' || signature[1] != 'D') {
		(void)fs_close(&image_file);
		ret = -EINVAL;
		goto fail;
	}
	if (!read_only) {
		(void)fs_close(&image_file);
		fs_file_t_init(&image_file);
		ret = fs_open(&image_file, IMAGE_PATH, FS_O_READ | FS_O_WRITE);
		if (ret != 0) {
			goto fail;
		}
	}
	image_sectors = info.size / UMAC_FAT_IMAGE_SECTOR_BYTES;
	ready = true;
	return 0;

fail:
	(void)fs_unmount(&fat_mount);
	return ret;
}

uint32_t umac_fat_image_sector_count(void)
{
	return image_sectors;
}

bool umac_fat_image_is_read_only(void)
{
	return read_only;
}

static bool valid_io(uint32_t sector, const void *buffer, uint32_t count)
{
	return ready && buffer != NULL && count != 0U &&
	       sector < image_sectors && count <= image_sectors - sector;
}

static int transfer(uint32_t sector, uint8_t *buffer, uint32_t count,
		    bool write)
{
	size_t bytes = (size_t)count * UMAC_FAT_IMAGE_SECTOR_BYTES;
	int ret = fs_seek(&image_file,
			  (off_t)sector * UMAC_FAT_IMAGE_SECTOR_BYTES,
			  FS_SEEK_SET);

	if (ret != 0) {
		return ret;
	}
	ret = write ? fs_write(&image_file, buffer, bytes) :
		      fs_read(&image_file, buffer, bytes);
	if (ret < 0) {
		return ret;
	}
	if ((size_t)ret != bytes) {
		return -EIO;
	}
	return write ? fs_sync(&image_file) : 0;
}

int umac_fat_image_read(uint32_t sector, uint8_t *buffer, uint32_t count)
{
	return valid_io(sector, buffer, count) ?
		transfer(sector, buffer, count, false) : -EINVAL;
}

int umac_fat_image_write(uint32_t sector, const uint8_t *buffer,
			 uint32_t count)
{
	if (read_only) {
		return -EROFS;
	}
	return valid_io(sector, buffer, count) ?
		transfer(sector, (uint8_t *)buffer, count, true) : -EINVAL;
}
