/* SPDX-License-Identifier: MIT */
#include "m68k_bus.h"

static bool region_contains(uint32_t address, uint32_t base, size_t size)
{
	return address >= base && (size_t)(address - base) < size;
}

void m68k_bus_init(struct m68k_bus *bus, uint8_t *ram, size_t ram_size,
		   const uint8_t *rom, size_t rom_size, uint32_t rom_base)
{
	bus->ram = ram;
	bus->ram_size = ram_size;
	bus->rom = rom;
	bus->rom_size = rom_size;
	bus->rom_base = rom_base & M68K_BUS_ADDRESS_MASK;
	bus->fault_count = 0U;
	bus->reset_overlay = true;
}

void m68k_bus_set_reset_overlay(struct m68k_bus *bus, bool enabled)
{
	bus->reset_overlay = enabled;
}

uint8_t m68k_bus_read8(struct m68k_bus *bus, uint32_t address)
{
	address &= M68K_BUS_ADDRESS_MASK;

	if (bus->reset_overlay && address < bus->rom_size) {
		return bus->rom[address];
	}
	if (address < bus->ram_size) {
		return bus->ram[address];
	}
	if (region_contains(address, bus->rom_base, bus->rom_size)) {
		return bus->rom[address - bus->rom_base];
	}

	bus->fault_count++;
	return 0xffU;
}

uint16_t m68k_bus_read16(struct m68k_bus *bus, uint32_t address)
{
	return ((uint16_t)m68k_bus_read8(bus, address) << 8) |
		m68k_bus_read8(bus, address + 1U);
}

uint32_t m68k_bus_read32(struct m68k_bus *bus, uint32_t address)
{
	return ((uint32_t)m68k_bus_read16(bus, address) << 16) |
		m68k_bus_read16(bus, address + 2U);
}

void m68k_bus_write8(struct m68k_bus *bus, uint32_t address, uint8_t value)
{
	address &= M68K_BUS_ADDRESS_MASK;

	/* Writes reach RAM beneath the reset-time ROM overlay. */
	if (address < bus->ram_size) {
		bus->ram[address] = value;
		return;
	}

	bus->fault_count++;
}

void m68k_bus_write16(struct m68k_bus *bus, uint32_t address, uint16_t value)
{
	m68k_bus_write8(bus, address, (uint8_t)(value >> 8));
	m68k_bus_write8(bus, address + 1U, (uint8_t)value);
}

void m68k_bus_write32(struct m68k_bus *bus, uint32_t address, uint32_t value)
{
	m68k_bus_write16(bus, address, (uint16_t)(value >> 16));
	m68k_bus_write16(bus, address + 2U, (uint16_t)value);
}
