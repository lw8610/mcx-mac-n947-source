/* SPDX-License-Identifier: MIT */
#ifndef UMAC_RTC_H
#define UMAC_RTC_H
#include <stdint.h>
#define UMAC_RTC_PRAM_BYTES 256U
void rtc_init(uint32_t seconds_1904, const uint8_t *pram, unsigned int size);
void rtc_reset_serial(void);
void rtc_lines_changed(uint8_t port_b, uint8_t ddr_b);
uint8_t rtc_data_read(void);
void rtc_advance_seconds(uint32_t seconds);
uint32_t rtc_get_seconds(void);
void rtc_get_pram(uint8_t output[UMAC_RTC_PRAM_BYTES]);
#endif
