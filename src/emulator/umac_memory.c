/* SPDX-License-Identifier: MIT */
#include "umac_memory.h"

#include <string.h>

/* This must be contiguous: uMac addresses guest memory through one RAM
 * pointer. The 192/208 KiB variants stay in CPU0's main RAM; a future 512 KiB
 * configuration needs a separately validated external-RAM backend.
 */
static uint8_t guest_ram[UMAC_GUEST_RAM_BYTES] __attribute__((aligned(4)));

uint8_t *umac_guest_ram_get(void)
{
	return guest_ram;
}

size_t umac_guest_ram_size(void)
{
	return sizeof(guest_ram);
}

bool umac_guest_ram_prepare(void)
{
	uint32_t pattern = 0x6d414331U;
	bool valid = true;

	/* Exercise the full allocation before giving it to the CPU emulator.
	 * Distinct pseudo-random bytes catch common stuck-address aliasing.
	 */
	for (size_t i = 0; i < sizeof(guest_ram); ++i) {
		pattern = pattern * 1664525U + 1013904223U;
		guest_ram[i] = (uint8_t)(pattern >> 24);
	}

	pattern = 0x6d414331U;
	for (size_t i = 0; i < sizeof(guest_ram); ++i) {
		pattern = pattern * 1664525U + 1013904223U;
		if (guest_ram[i] != (uint8_t)(pattern >> 24)) {
			valid = false;
			break;
		}
	}

	memset(guest_ram, 0, sizeof(guest_ram));
	return valid;
}
