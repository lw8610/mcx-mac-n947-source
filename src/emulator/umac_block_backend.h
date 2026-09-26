/* SPDX-License-Identifier: MIT */
#ifndef MCX_MAC_UMAC_BLOCK_BACKEND_H
#define MCX_MAC_UMAC_BLOCK_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include <disc.h>

/* Independent block-storage backend for a future replacement Mac disk driver.
 * Offsets and lengths are physical bytes, aligned to 512-byte sectors.
 */
void umac_block_backend_init(const disc_descr_t drives[DISC_NUM_DRIVES]);
int umac_block_backend_transfer(unsigned int drive, bool write,
				uint32_t offset, uint8_t *buffer,
				uint32_t length);

#endif
