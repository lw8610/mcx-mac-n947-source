/* SPDX-License-Identifier: MIT */
#include "rtc.h"
#include <string.h>
enum rtc_state { RTC_COMMAND, RTC_WRITE, RTC_EXTENDED_COMMAND, RTC_EXTENDED_WRITE };
static uint32_t rtc_seconds;
static uint8_t rtc_pram[UMAC_RTC_PRAM_BYTES];
static uint8_t command, shift_byte, bit_count, data_out;
static uint8_t read_mode, write_protect, old_enable = 1U, old_clock = 1U;
static uint8_t extended_address;
static enum rtc_state state;
static uint8_t register_read(unsigned int index)
{
	if (index < 8U) return (uint8_t)(rtc_seconds >> ((index & 3U) * 8U));
	return index < UMAC_RTC_PRAM_BYTES ? rtc_pram[index] : 0U;
}
static void register_write(unsigned int index, uint8_t value)
{
	if (write_protect && index != 13U) return;
	if (index < 8U) {
		unsigned int shift = (index & 3U) * 8U;
		rtc_seconds = (rtc_seconds & ~(0xffU << shift)) | ((uint32_t)value << shift);
	} else if (index == 13U && command == 0x35U) {
		write_protect = (value >> 7) & 1U;
	} else if (index < UMAC_RTC_PRAM_BYTES) rtc_pram[index] = value;
}
static void execute_byte(uint8_t value)
{
	if (state == RTC_EXTENDED_COMMAND) {
		extended_address = (uint8_t)(((command & 7U) << 5) | ((value & 0x7cU) >> 2));
		if (command & 0x80U) {
			read_mode = 1U; shift_byte = rtc_pram[extended_address]; bit_count = 8U; state = RTC_COMMAND;
		} else { state = RTC_EXTENDED_WRITE; shift_byte = bit_count = 0U; }
		return;
	}
	if (state == RTC_EXTENDED_WRITE) {
		if (!write_protect) rtc_pram[extended_address] = value;
		state = RTC_COMMAND; shift_byte = bit_count = 0U; return;
	}
	if (state == RTC_WRITE) {
		register_write((command >> 2) & 0x1fU, value);
		state = RTC_COMMAND; shift_byte = bit_count = 0U; return;
	}
	command = value;
	if ((command & 0x78U) == 0x38U) {
		state = RTC_EXTENDED_COMMAND; shift_byte = bit_count = 0U; return;
	}
	if (command & 0x80U) {
		read_mode = 1U; shift_byte = register_read((command >> 2) & 0x1fU); bit_count = 8U;
	} else { state = RTC_WRITE; shift_byte = bit_count = 0U; }
}
static void shift_falling_edge(uint8_t input)
{
	if (read_mode) {
		if (bit_count != 0U) { bit_count--; data_out = (shift_byte >> bit_count) & 1U; }
	} else {
		shift_byte = (uint8_t)((shift_byte << 1) | (input & 1U));
		if (++bit_count == 8U) execute_byte(shift_byte);
	}
}
void rtc_reset_serial(void)
{
	command = shift_byte = bit_count = data_out = read_mode = 0U; state = RTC_COMMAND;
}
void rtc_init(uint32_t seconds_1904, const uint8_t *pram, unsigned int size)
{
	rtc_seconds = seconds_1904; memset(rtc_pram, 0, sizeof(rtc_pram));
	if (pram != NULL) { if (size > sizeof(rtc_pram)) size = sizeof(rtc_pram); memcpy(rtc_pram, pram, size); }
	else { rtc_pram[16] = 0xa8U; rtc_pram[8] = 7U; }
	write_protect = 0U; old_enable = old_clock = 1U; rtc_reset_serial();
}
void rtc_lines_changed(uint8_t port_b, uint8_t ddr_b)
{
	uint8_t enable = (ddr_b & 4U) ? ((port_b >> 2) & 1U) : 1U;
	uint8_t clock = (ddr_b & 2U) ? ((port_b >> 1) & 1U) : 1U;
	uint8_t input = (ddr_b & 1U) ? (port_b & 1U) : 0U;
	if (enable != old_enable) rtc_reset_serial();
	if (!enable && old_clock && !clock) shift_falling_edge(input);
	old_enable = enable; old_clock = clock;
}
uint8_t rtc_data_read(void) { return data_out; }
void rtc_advance_seconds(uint32_t seconds) { rtc_seconds += seconds; }
uint32_t rtc_get_seconds(void) { return rtc_seconds; }
void rtc_get_pram(uint8_t output[UMAC_RTC_PRAM_BYTES])
{
	if (output != NULL) memcpy(output, rtc_pram, sizeof(rtc_pram));
}
