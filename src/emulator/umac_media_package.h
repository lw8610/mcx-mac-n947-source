/* SPDX-License-Identifier: MIT */
#ifndef MCX_MAC_UMAC_MEDIA_PACKAGE_H_
#define MCX_MAC_UMAC_MEDIA_PACKAGE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UMAC_MEDIA_HEADER_BYTES 64U
#define UMAC_MEDIA_ROM_OFFSET 0x2000U
#define UMAC_MEDIA_DISK_OFFSET 0x22000U
#define UMAC_MEDIA_SLOT_BYTES 0xf6000U

struct umac_media_package {
	const uint8_t *rom;
	const uint8_t *disk;
	uint32_t disk_size;
};

uint32_t umac_media_crc32(const uint8_t *data, size_t size);
bool umac_media_package_open(const uint8_t *slot, size_t slot_size,
			     struct umac_media_package *out);

#endif
