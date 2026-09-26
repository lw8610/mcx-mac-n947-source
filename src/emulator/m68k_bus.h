#ifndef MCX_MAC_EMULATOR_M68K_BUS_H_
#define MCX_MAC_EMULATOR_M68K_BUS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M68K_BUS_ADDRESS_MASK 0x00ffffffU

struct m68k_bus {
	uint8_t *ram;
	size_t ram_size;
	const uint8_t *rom;
	size_t rom_size;
	uint32_t rom_base;
	uint32_t fault_count;
	bool reset_overlay;
};

void m68k_bus_init(struct m68k_bus *bus, uint8_t *ram, size_t ram_size,
		   const uint8_t *rom, size_t rom_size, uint32_t rom_base);
void m68k_bus_set_reset_overlay(struct m68k_bus *bus, bool enabled);
uint8_t m68k_bus_read8(struct m68k_bus *bus, uint32_t address);
uint16_t m68k_bus_read16(struct m68k_bus *bus, uint32_t address);
uint32_t m68k_bus_read32(struct m68k_bus *bus, uint32_t address);
void m68k_bus_write8(struct m68k_bus *bus, uint32_t address, uint8_t value);
void m68k_bus_write16(struct m68k_bus *bus, uint32_t address, uint16_t value);
void m68k_bus_write32(struct m68k_bus *bus, uint32_t address, uint32_t value);
bool m68k_bus_self_test(void);

#endif /* MCX_MAC_EMULATOR_M68K_BUS_H_ */
