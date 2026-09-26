#ifndef MCX_MAC_EMULATOR_M68K_MUSASHI_BUS_H_
#define MCX_MAC_EMULATOR_M68K_MUSASHI_BUS_H_

#include "m68k_bus.h"

/* Bind one guest address space to Musashi's global memory callbacks. */
void m68k_musashi_bus_attach(struct m68k_bus *bus);

unsigned int m68k_read_memory_8(unsigned int address);
unsigned int m68k_read_memory_16(unsigned int address);
unsigned int m68k_read_memory_32(unsigned int address);
void m68k_write_memory_8(unsigned int address, unsigned int value);
void m68k_write_memory_16(unsigned int address, unsigned int value);
void m68k_write_memory_32(unsigned int address, unsigned int value);
unsigned int m68k_read_immediate_16(unsigned int address);
unsigned int m68k_read_immediate_32(unsigned int address);
unsigned int m68k_read_pcrelative_8(unsigned int address);
unsigned int m68k_read_pcrelative_16(unsigned int address);
unsigned int m68k_read_pcrelative_32(unsigned int address);
unsigned int m68k_read_disassembler_8(unsigned int address);
unsigned int m68k_read_disassembler_16(unsigned int address);
unsigned int m68k_read_disassembler_32(unsigned int address);
void m68k_write_memory_32_pd(unsigned int address, unsigned int value);

#endif /* MCX_MAC_EMULATOR_M68K_MUSASHI_BUS_H_ */
