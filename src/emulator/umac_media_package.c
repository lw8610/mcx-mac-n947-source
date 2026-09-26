/* SPDX-License-Identifier: MIT */
#include "umac_media_package.h"

#include <string.h>

#define ROM_BYTES 0x20000U
#define DISK_400K_BYTES 409600U
#define DISK_800K_BYTES 819200U

static uint32_t read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t umac_media_crc32(const uint8_t *data, size_t size)
{
	uint32_t crc = 0xffffffffU;

	if (data == NULL) {
		return 0U;
	}
	for (size_t i = 0; i < size; i++) {
		crc ^= data[i];
		for (unsigned int bit = 0; bit < 8U; bit++) {
			crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
		}
	}
	return ~crc;
}

bool umac_media_package_open(const uint8_t *slot, size_t slot_size,
			     struct umac_media_package *out)
{
	static const uint8_t magic[8] = { 'M', 'C', 'X', 'M', 'A', 'C', '0', '1' };
	uint32_t rom_offset;
	uint32_t rom_size;
	uint32_t disk_offset;
	uint32_t disk_size;

	if (out == NULL) {
		return false;
	}
	memset(out, 0, sizeof(*out));
	if (slot == NULL || slot_size < UMAC_MEDIA_HEADER_BYTES ||
	    slot_size > UMAC_MEDIA_SLOT_BYTES || memcmp(slot, magic, 8U) != 0 ||
	    read_le32(slot + 8U) != 1U ||
	    read_le32(slot + 12U) != UMAC_MEDIA_HEADER_BYTES ||
	    umac_media_crc32(slot, 40U) != read_le32(slot + 40U)) {
		return false;
	}
	rom_offset = read_le32(slot + 16U);
	rom_size = read_le32(slot + 20U);
	disk_offset = read_le32(slot + 28U);
	disk_size = read_le32(slot + 32U);
	if (rom_offset != UMAC_MEDIA_ROM_OFFSET || rom_size != ROM_BYTES ||
	    disk_offset != UMAC_MEDIA_DISK_OFFSET ||
	    (disk_size != DISK_400K_BYTES && disk_size != DISK_800K_BYTES) ||
	    rom_offset > slot_size || rom_size > slot_size - rom_offset ||
	    disk_offset > slot_size || disk_size > slot_size - disk_offset) {
		return false;
	}
	if (slot[rom_offset] != 0x4dU || slot[rom_offset + 1U] != 0x1fU ||
	    slot[rom_offset + 2U] != 0x81U || slot[rom_offset + 3U] != 0x72U ||
	    slot[disk_offset] != 'L' || slot[disk_offset + 1U] != 'K' ||
	    umac_media_crc32(slot + rom_offset, rom_size) !=
		read_le32(slot + 24U) ||
	    umac_media_crc32(slot + disk_offset, disk_size) !=
		read_le32(slot + 36U)) {
		return false;
	}
	out->rom = slot + rom_offset;
	out->disk = slot + disk_offset;
	out->disk_size = disk_size;
	return true;
}
