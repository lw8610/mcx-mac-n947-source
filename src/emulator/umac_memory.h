#ifndef MCX_MAC_EMULATOR_UMAC_MEMORY_H_
#define MCX_MAC_EMULATOR_UMAC_MEMORY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef UMAC_MEMSIZE
#define UMAC_MEMSIZE 128U
#endif

#define UMAC_GUEST_RAM_BYTES (UMAC_MEMSIZE * 1024U)

uint8_t *umac_guest_ram_get(void);
size_t umac_guest_ram_size(void);
bool umac_guest_ram_prepare(void);

#endif /* MCX_MAC_EMULATOR_UMAC_MEMORY_H_ */
