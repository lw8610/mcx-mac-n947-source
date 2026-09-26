/* SPDX-License-Identifier: MIT */
#ifndef UMAC_FLASH_HDD_H
#define UMAC_FLASH_HDD_H

#include <stdint.h>

#define UMAC_FLASH_HDD_SECTOR_BYTES 512U
#define UMAC_FLASH_HDD_SECTORS 16120U

/* No operation below is implicit at startup. prepare() only reads Flash.
 * format_blank() is an explicit, guarded one-time operation. The current
 * erase-block RMW write path is a prototype, not power-failure atomic.
 */
int umac_flash_hdd_prepare(void);
int umac_flash_hdd_format_blank(void);
int umac_flash_hdd_read(uint32_t sector, uint8_t *buffer, uint32_t count);
int umac_flash_hdd_write(uint32_t sector, const uint8_t *buffer,
			 uint32_t count);

#endif
