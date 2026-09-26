/* SPDX-License-Identifier: MIT */
/* Prototype fixed-disk block storage, deliberately separate from .Sony.
 * Never autoformat. Preserve the first 64 KiB erase sector and reserve the
 * last 64 KiB for future journal/recovery work.
 */
#include "umac_flash_hdd.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>

#define EXT_FLASH_NODE DT_NODELABEL(ext_flash_ctrl)
#define FLASH_BYTES 0x800000U
#define AREA_START 0x010000U
#define AREA_BYTES 0x7e0000U
#define ERASE_BYTES 4096U
#define DATA_START (AREA_START + ERASE_BYTES)
#define AREA_END (AREA_START + AREA_BYTES)
#define HEADER_BYTES 256U

_Static_assert(AREA_END == FLASH_BYTES - 0x10000U,
	       "HDD journal reservation mismatch");
_Static_assert((UMAC_FLASH_HDD_SECTORS * UMAC_FLASH_HDD_SECTOR_BYTES) ==
	       (AREA_BYTES - ERASE_BYTES), "HDD bounds mismatch");

static const uint8_t hdd_magic[8] = { 'M', 'C', 'X', 'H', 'D', 'D', '0', '2' };
static const uint8_t expected_jedec[3] = { 0xef, 0x40, 0x17 };
static const struct device *flash_dev;
static bool ready;
static uint8_t erase_buffer[ERASE_BYTES] __aligned(4);

static int validate_device(void)
{
	uint64_t size;
	uint8_t jedec[3];
	int ret;

	flash_dev = DEVICE_DT_GET(EXT_FLASH_NODE);
	if (!device_is_ready(flash_dev)) {
		return -ENODEV;
	}
	ret = flash_get_size(flash_dev, &size);
	if (ret != 0) {
		return ret;
	}
	ret = flash_read_jedec_id(flash_dev, jedec);
	if (ret != 0) {
		return ret;
	}
	if (size != FLASH_BYTES ||
	    memcmp(jedec, expected_jedec, sizeof(jedec)) != 0) {
		return -ENODEV;
	}
	return 0;
}

int umac_flash_hdd_prepare(void)
{
	uint8_t header[HEADER_BYTES];
	int ret;

	ready = false;
	ret = validate_device();
	if (ret != 0) {
		return ret;
	}
	ret = flash_read(flash_dev, AREA_START, header, sizeof(header));
	if (ret != 0) {
		return ret;
	}
	if (memcmp(header, hdd_magic, sizeof(hdd_magic)) != 0 ||
	    header[8] != (UMAC_FLASH_HDD_SECTORS & 0xffU) ||
	    header[9] != (UMAC_FLASH_HDD_SECTORS >> 8) ||
	    header[10] != 0U || header[11] != 0U) {
		return -ENODATA;
	}
	ready = true;
	return 0;
}

int umac_flash_hdd_format_blank(void)
{
	uint8_t header[HEADER_BYTES];
	int ret = validate_device();

	if (ret != 0) {
		return ret;
	}
	/* Formatting never erases existing data. Every byte in the reserved
	 * area must already be blank, not merely the first/last sector.
	 */
	for (uint32_t offset = AREA_START; offset < AREA_END;
	     offset += ERASE_BYTES) {
		ret = flash_read(flash_dev, offset, erase_buffer,
				 sizeof(erase_buffer));
		if (ret != 0) {
			return ret;
		}
		for (unsigned int i = 0U; i < sizeof(erase_buffer); i++) {
			if (erase_buffer[i] != 0xffU) {
				return -EEXIST;
			}
		}
	}
	memset(header, 0xff, sizeof(header));
	memcpy(header, hdd_magic, sizeof(hdd_magic));
	header[8] = (uint8_t)UMAC_FLASH_HDD_SECTORS;
	header[9] = (uint8_t)(UMAC_FLASH_HDD_SECTORS >> 8);
	header[10] = 0U;
	header[11] = 0U;
	ret = flash_write(flash_dev, AREA_START, header, sizeof(header));
	return ret == 0 ? umac_flash_hdd_prepare() : ret;
}

static bool valid_io(uint32_t sector, const void *buffer, uint32_t count)
{
	return ready && buffer != NULL && sector < UMAC_FLASH_HDD_SECTORS &&
	       count != 0U && count <= UMAC_FLASH_HDD_SECTORS - sector;
}

int umac_flash_hdd_read(uint32_t sector, uint8_t *buffer, uint32_t count)
{
	if (!valid_io(sector, buffer, count)) {
		return -EINVAL;
	}
	return flash_read(flash_dev,
		DATA_START + sector * UMAC_FLASH_HDD_SECTOR_BYTES,
		buffer, count * UMAC_FLASH_HDD_SECTOR_BYTES);
}

int umac_flash_hdd_write(uint32_t sector, const uint8_t *buffer,
			 uint32_t count)
{
	uint32_t offset;
	uint32_t bytes;
	int ret;

	if (!valid_io(sector, buffer, count)) {
		return -EINVAL;
	}
	offset = DATA_START + sector * UMAC_FLASH_HDD_SECTOR_BYTES;
	bytes = count * UMAC_FLASH_HDD_SECTOR_BYTES;
	while (bytes != 0U) {
		uint32_t erase_start = offset & ~(ERASE_BYTES - 1U);
		uint32_t inside = offset - erase_start;
		uint32_t chunk = ERASE_BYTES - inside;

		if (chunk > bytes) {
			chunk = bytes;
		}
		ret = flash_read(flash_dev, erase_start, erase_buffer,
				 sizeof(erase_buffer));
		if (ret != 0) {
			return ret;
		}
		memcpy(erase_buffer + inside, buffer, chunk);
		ret = flash_erase(flash_dev, erase_start, ERASE_BYTES);
		if (ret != 0) {
			return ret;
		}
		ret = flash_write(flash_dev, erase_start, erase_buffer,
				  sizeof(erase_buffer));
		if (ret != 0) {
			return ret;
		}
		offset += chunk;
		buffer += chunk;
		bytes -= chunk;
	}
	return 0;
}
