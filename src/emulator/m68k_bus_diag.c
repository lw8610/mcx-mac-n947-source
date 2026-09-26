/* SPDX-License-Identifier: MIT */
#include "m68k_bus.h"
#include "m68k_musashi_bus.h"

#include <string.h>

#include <zephyr/sys/printk.h>

bool m68k_bus_self_test(void)
{
	static uint8_t test_ram[256];
	static const uint8_t test_rom[16] = {
		0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	struct m68k_bus bus;
	bool ok = true;

	memset(test_ram, 0, sizeof(test_ram));
	m68k_bus_init(&bus, test_ram, sizeof(test_ram), test_rom,
		      sizeof(test_rom), 0x00f00000U);

	/* Reset reads ROM at address zero while writes still reach RAM below it. */
	ok = ok && m68k_bus_read32(&bus, 0U) == 0x12345678U;
	m68k_bus_write16(&bus, 0U, 0xa55aU);
	m68k_bus_set_reset_overlay(&bus, false);
	ok = ok && m68k_bus_read16(&bus, 0U) == 0xa55aU;

	/* 68000 memory is big-endian and has a 24-bit external address bus. */
	m68k_bus_write32(&bus, 0x20U, 0x89abcdefU);
	ok = ok && test_ram[0x20] == 0x89U && test_ram[0x21] == 0xabU &&
		test_ram[0x22] == 0xcdU && test_ram[0x23] == 0xefU;
	ok = ok && m68k_bus_read32(&bus, 0x01000020U) == 0x89abcdefU;

	/* The same ROM remains visible in its permanent high-address window. */
	ok = ok && m68k_bus_read32(&bus, 0x00f00000U) == 0x12345678U;

	/* Unmapped reads return an open-bus value and leave diagnostic evidence. */
	ok = ok && m68k_bus_read16(&bus, 0x00800000U) == 0xffffU;
	ok = ok && bus.fault_count == 2U;

	/* Exercise the CPU-facing callbacks without running an external CPU core. */
	m68k_musashi_bus_attach(&bus);
	ok = ok && m68k_read_memory_32(0x01000020U) == 0x89abcdefU;
	ok = ok && m68k_read_immediate_16(0x00f00000U) == 0x1234U;
	m68k_write_memory_32_pd(0x30U, 0x11223344U);
	ok = ok && m68k_read_memory_32(0x30U) == 0x33441122U;
	m68k_musashi_bus_attach(NULL);
	ok = ok && m68k_read_memory_8(0U) == 0xffU;

	printk("DIAG M68K bus addr_bits=24 endian=big adapter=%s "
	       "unmapped=%u result=%s\n",
	       ok ? "PASS" : "FAIL", bus.fault_count, ok ? "PASS" : "FAIL");
	return ok;
}
