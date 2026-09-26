/* SPDX-License-Identifier: MIT */
#ifndef UMAC_FAT_IMAGE_H
#define UMAC_FAT_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#define UMAC_FAT_IMAGE_SECTOR_BYTES 512U

/* Mount an existing FAT volume and open a raw HFS or MFS umac0.img.
 * MFS images are read-only pending a fixed-disk compatibility test.
 * This function never formats storage or creates/truncates a file.
 */
int umac_fat_image_prepare(void);
uint32_t umac_fat_image_sector_count(void);
bool umac_fat_image_is_read_only(void);
int umac_fat_image_read(uint32_t sector, uint8_t *buffer, uint32_t count);
int umac_fat_image_write(uint32_t sector, const uint8_t *buffer,
			 uint32_t count);

#endif
