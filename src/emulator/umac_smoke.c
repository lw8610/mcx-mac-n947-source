#include "umac_smoke.h"

#include <stddef.h>
#include <stdint.h>

#include "emulator/umac_memory.h"

#include <m68k.h>
#include <umac.h>

/* Project-owned synthetic ROM: reset SP=0x0001fff0, PC=0x00400008,
 * followed by a BRA.S * instruction. It proves that uMac's bus and the
 * actual Musashi CPU execute together without bundling an Apple ROM.
 */
static const uint8_t smoke_rom[0x20000] = {
	[0] = 0x00, [1] = 0x01, [2] = 0xff, [3] = 0xf0,
	[4] = 0x00, [5] = 0x40, [6] = 0x00, [7] = 0x08,
	[8] = 0x60, [9] = 0xfe,
};

bool umac_smoke_test(void)
{
	disc_descr_t discs[DISC_NUM_DRIVES] = { 0 };
	uint32_t pc;

	if (umac_guest_ram_size() != 128U * 1024U ||
	    umac_init(umac_guest_ram_get(), (void *)smoke_rom, discs) != 0) {
		return false;
	}

	if (umac_loop() != 0) {
		return false;
	}
	pc = m68k_get_reg(NULL, M68K_REG_PC);
	return pc == 0x00400008U;
}
