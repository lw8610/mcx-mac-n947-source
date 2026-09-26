/* SPDX-License-Identifier: MIT */
#include "umac_external_flash_probe.h"

#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/sys/printk.h>

#define EXT_FLASH_NODE DT_NODELABEL(ext_flash_ctrl)
#define PROBE_CHUNK_BYTES 256U
#define ERASE_SECTOR_BYTES 4096U
#define EXPECTED_FLASH_BYTES 8388608U

_Static_assert(DT_NODE_EXISTS(EXT_FLASH_NODE), "external Flash node missing");

/* Only reads. In particular, a nonblank first/last sector is never erased. */
static int sector_is_blank(const struct device *flash, uint64_t offset)
{
	uint8_t chunk[PROBE_CHUNK_BYTES];
	bool blank = true;

	for (unsigned int pos = 0; pos < ERASE_SECTOR_BYTES;
	     pos += sizeof(chunk)) {
		int ret = flash_read(flash, (off_t)(offset + pos), chunk,
				     sizeof(chunk));

		if (ret != 0) {
			return ret;
		}
		for (unsigned int i = 0; i < sizeof(chunk); i++) {
			if (chunk[i] != 0xffU) {
				blank = false;
			}
		}
	}
	return blank ? 1 : 0;
}

#if defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_WRITE_TEST)
static bool test_blank_tail_sector(const struct device *flash, uint64_t offset)
{
	uint8_t pattern[PROBE_CHUNK_BYTES];
	uint8_t readback[PROBE_CHUNK_BYTES];
	int write_ret;
	int read_ret = -1;
	int erase_ret;
	int blank_ret;

	for (unsigned int i = 0; i < sizeof(pattern); i++) {
		pattern[i] = (uint8_t)(i ^ 0x5aU);
	}
	write_ret = flash_write(flash, (off_t)offset, pattern, sizeof(pattern));
	if (write_ret == 0) {
		read_ret = flash_read(flash, (off_t)offset, readback,
				      sizeof(readback));
		if (read_ret == 0 && memcmp(pattern, readback,
					    sizeof(pattern)) != 0) {
			read_ret = -1;
		}
	}
	/* The target was confirmed blank before writing. Always try to restore it. */
	erase_ret = flash_erase(flash, (off_t)offset, ERASE_SECTOR_BYTES);
	blank_ret = erase_ret == 0 ? sector_is_blank(flash, offset) : -1;
	printk("DIAG external Flash tail write=%d verify=%d erase=%d blank=%d\n",
	       write_ret, read_ret, erase_ret, blank_ret);
	return write_ret == 0 && read_ret == 0 && erase_ret == 0 &&
	       blank_ret == 1;
}
#endif

bool umac_external_flash_probe(void)
{
	const struct device *flash = DEVICE_DT_GET(EXT_FLASH_NODE);
	uint64_t size = 0U;
	uint8_t jedec[3] = { 0 };
	int first;
	int last;
	int ret;

	if (!device_is_ready(flash)) {
		printk("DIAG external Flash ready=0 (guest disk unchanged)\n");
		return false;
	}
	ret = flash_get_size(flash, &size);
	if (ret != 0 || size < 2U * ERASE_SECTOR_BYTES) {
		printk("DIAG external Flash size error=%d bytes=%llu\n",
		       ret, (unsigned long long)size);
		return false;
	}
	ret = flash_read_jedec_id(flash, jedec);
	if (ret != 0) {
		printk("DIAG external Flash JEDEC error=%d\n", ret);
		return false;
	}
	first = sector_is_blank(flash, 0U);
	last = sector_is_blank(flash, size - ERASE_SECTOR_BYTES);
	printk("DIAG external Flash JEDEC=%02x%02x%02x bytes=%llu "
	       "first_blank=%d last_blank=%d read_only=1\n",
	       jedec[0], jedec[1], jedec[2], (unsigned long long)size,
	       first, last);
	if (first < 0 || last < 0) {
		return false;
	}
#if defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_WRITE_TEST)
	if (size != EXPECTED_FLASH_BYTES || jedec[0] != 0xefU ||
	    jedec[1] != 0x40U || jedec[2] != 0x17U || last != 1) {
		printk("DIAG external Flash write test skipped: identity/blank guard\n");
		return false;
	}
	return test_blank_tail_sector(flash, size - ERASE_SECTOR_BYTES);
#endif
	return first >= 0 && last >= 0;
}
