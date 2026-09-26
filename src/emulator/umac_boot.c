#include "umac_boot.h"

#include <stddef.h>
#include <stdint.h>

#include "emulator/umac_memory.h"
#include "emulator/umac_disk_overlay.h"

#include <umac.h>

extern const uint8_t __umac_private_rom_start[];
extern const uint8_t __umac_private_rom_end[];
extern const uint8_t __umac_private_disk_start[];
extern const uint8_t __umac_private_disk_end[];

bool umac_boot_init(void)
{
	disc_descr_t discs[DISC_NUM_DRIVES] = { 0 };
	size_t rom_size = (uintptr_t)__umac_private_rom_end -
			  (uintptr_t)__umac_private_rom_start;
	size_t disk_size = (uintptr_t)__umac_private_disk_end -
			   (uintptr_t)__umac_private_disk_start;

	if (rom_size != 128U * 1024U ||
	    (disk_size != 400U * 1024U && disk_size != 800U * 1024U) ||
	    __umac_private_rom_start[0] != 0x4dU ||
	    __umac_private_rom_start[1] != 0x1fU ||
	    __umac_private_rom_start[2] != 0x81U ||
	    __umac_private_rom_start[3] != 0x72U ||
	    __umac_private_disk_start[0] != 'L' ||
	    __umac_private_disk_start[1] != 'K' ||
	    umac_guest_ram_size() != UMAC_GUEST_RAM_BYTES) {
		return false;
	}

	umac_disk_overlay_init(__umac_private_disk_start, (unsigned int)disk_size);
	discs[0].base = NULL;
	discs[0].size = (unsigned int)disk_size;
	discs[0].read_only = 0;
	discs[0].op_read = umac_disk_overlay_read;
	discs[0].op_write = umac_disk_overlay_write;
	return umac_init(umac_guest_ram_get(),
			 (void *)__umac_private_rom_start, discs) == 0;
}
