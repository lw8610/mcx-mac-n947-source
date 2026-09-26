/* SPDX-License-Identifier: MIT */
#include "m68k_musashi_bus.h"

/* Musashi declares these callbacks in m68k.h. Keep this adapter independent of
 * the third-party core so it can be tested before that core is vendored.
 */
static struct m68k_bus *active_bus;

void m68k_musashi_bus_attach(struct m68k_bus *bus)
{
	active_bus = bus;
}

unsigned int m68k_read_memory_8(unsigned int address)
{
	return active_bus != NULL ? m68k_bus_read8(active_bus, address) : 0xffU;
}

unsigned int m68k_read_memory_16(unsigned int address)
{
	return active_bus != NULL ? m68k_bus_read16(active_bus, address) : 0xffffU;
}

unsigned int m68k_read_memory_32(unsigned int address)
{
	return active_bus != NULL ? m68k_bus_read32(active_bus, address) : 0xffffffffU;
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
	if (active_bus != NULL) {
		m68k_bus_write8(active_bus, address, (uint8_t)value);
	}
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
	if (active_bus != NULL) {
		m68k_bus_write16(active_bus, address, (uint16_t)value);
	}
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
	if (active_bus != NULL) {
		m68k_bus_write32(active_bus, address, value);
	}
}

unsigned int m68k_read_immediate_16(unsigned int address)
{
	return m68k_read_memory_16(address);
}

unsigned int m68k_read_immediate_32(unsigned int address)
{
	return m68k_read_memory_32(address);
}

unsigned int m68k_read_pcrelative_8(unsigned int address)
{
	return m68k_read_memory_8(address);
}

unsigned int m68k_read_pcrelative_16(unsigned int address)
{
	return m68k_read_memory_16(address);
}

unsigned int m68k_read_pcrelative_32(unsigned int address)
{
	return m68k_read_memory_32(address);
}

unsigned int m68k_read_disassembler_8(unsigned int address)
{
	return m68k_read_memory_8(address);
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
	return m68k_read_memory_16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
	return m68k_read_memory_32(address);
}

/* Optional 68000 predecrement long write: bus order differs from final value. */
void m68k_write_memory_32_pd(unsigned int address, unsigned int value)
{
	m68k_write_memory_16(address + 2U, value >> 16);
	m68k_write_memory_16(address, value);
}
