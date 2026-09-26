/* SPDX-License-Identifier: MIT */
#include "umac_block_backend.h"

#include <stddef.h>
#include <string.h>

#define BLOCK_BYTES 512U

static disc_descr_t block_drives[DISC_NUM_DRIVES];

void umac_block_backend_init(const disc_descr_t drives[DISC_NUM_DRIVES])
{
	if (drives == NULL) {
		memset(block_drives, 0, sizeof(block_drives));
		return;
	}
	memcpy(block_drives, drives, sizeof(block_drives));
}

int umac_block_backend_transfer(unsigned int drive, bool write,
				uint32_t offset, uint8_t *buffer,
				uint32_t length)
{
	disc_descr_t *media;

	if (drive >= DISC_NUM_DRIVES || buffer == NULL ||
	    (offset % BLOCK_BYTES) != 0U ||
	    (length % BLOCK_BYTES) != 0U) {
		return -1;
	}
	media = &block_drives[drive];
	if (media->size == 0U || offset > media->size ||
	    length > media->size - offset || (write && media->read_only)) {
		return -1;
	}
	if (length == 0U) {
		return 0;
	}
	if (write) {
		if (media->op_write != NULL) {
			return media->op_write(media->op_ctx, buffer, offset,
					       length);
		}
		if (media->base == NULL) {
			return -1;
		}
		memcpy(media->base + offset, buffer, length);
		return 0;
	}
	if (media->op_read != NULL) {
		return media->op_read(media->op_ctx, buffer, offset, length);
	}
	if (media->base == NULL) {
		return -1;
	}
	memcpy(buffer, media->base + offset, length);
	return 0;
}
