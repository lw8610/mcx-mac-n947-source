/* SPDX-License-Identifier: MIT */
#include "umac_disk_overlay.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SECTOR_SIZE 512U

#if defined(__APPLE__)
#define OVERLAY_SRAMX
#else
#define OVERLAY_SRAMX __attribute__((section("SRAMX"), aligned(4)))
#endif

/* The board overlay places this section after the reserved SmartDMA firmware
 * area in SRAMX. The two RAMX address windows alias the same physical bytes;
 * placing this section at the default RAMX base would corrupt the firmware.
 * SRAMX is separate from the 320 KiB main RAM, which is nearly full.
 * This is a volatile overlay: the original private image in flash is never
 * modified, and all changes disappear on reset.
 */
static uint8_t overlay_data[UMAC_DISK_OVERLAY_SECTORS][SECTOR_SIZE]
	OVERLAY_SRAMX;
static unsigned int overlay_sector[UMAC_DISK_OVERLAY_SECTORS];
static const uint8_t *base_image;
static unsigned int base_size;
static unsigned int used_sectors;
static unsigned int write_count;
static unsigned int error_count;

static int find_sector(unsigned int sector)
{
	for (unsigned int i = 0; i < used_sectors; i++) {
		if (overlay_sector[i] == sector) {
			return (int)i;
		}
	}
	return -1;
}

static bool valid_range(unsigned int offset, unsigned int len)
{
	return base_image != NULL && (offset % SECTOR_SIZE) == 0U &&
		(len % SECTOR_SIZE) == 0U && offset <= base_size &&
		len <= base_size - offset;
}

void umac_disk_overlay_init(const uint8_t *image, unsigned int image_size)
{
	base_image = image;
	base_size = image_size;
	used_sectors = 0U;
	write_count = 0U;
	error_count = 0U;
}

int umac_disk_overlay_read(void *ctx, uint8_t *data, unsigned int offset,
			   unsigned int len)
{
	(void)ctx;
	if (data == NULL || !valid_range(offset, len)) {
		error_count++;
		return -1;
	}
	for (unsigned int pos = 0; pos < len; pos += SECTOR_SIZE) {
		unsigned int sector = (offset + pos) / SECTOR_SIZE;
		int slot = find_sector(sector);

		memcpy(data + pos, slot >= 0 ? overlay_data[slot] :
		       base_image + offset + pos, SECTOR_SIZE);
	}
	return 0;
}

int umac_disk_overlay_write(void *ctx, uint8_t *data, unsigned int offset,
			    unsigned int len)
{
	unsigned int new_sectors = 0U;

	(void)ctx;
	if (data == NULL || !valid_range(offset, len)) {
		error_count++;
		return -1;
	}
	/* The driver writes aligned, consecutive sectors. Check capacity before
	 * changing any of them so an oversized request cannot partly succeed.
	 */
	for (unsigned int pos = 0; pos < len; pos += SECTOR_SIZE) {
		if (find_sector((offset + pos) / SECTOR_SIZE) < 0) {
			new_sectors++;
		}
	}
	if (new_sectors > UMAC_DISK_OVERLAY_SECTORS - used_sectors) {
		error_count++;
		return -1;
	}
	for (unsigned int pos = 0; pos < len; pos += SECTOR_SIZE) {
		unsigned int sector = (offset + pos) / SECTOR_SIZE;
		int slot = find_sector(sector);

		if (slot < 0) {
			slot = (int)used_sectors++;
			overlay_sector[slot] = sector;
		}
		memcpy(overlay_data[slot], data + pos, SECTOR_SIZE);
	}
	write_count++;
	return 0;
}

unsigned int umac_disk_overlay_used(void)
{
	return used_sectors;
}

unsigned int umac_disk_overlay_write_count(void)
{
	return write_count;
}

unsigned int umac_disk_overlay_error_count(void)
{
	return error_count;
}
