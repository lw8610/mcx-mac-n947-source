/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include "rtc.h"
static uint8_t port = 6U, ddr = 7U;
static void lines(void) { rtc_lines_changed(port, ddr); }
static void enable(void) { port |= 6U; lines(); port &= (uint8_t)~4U; lines(); }
static void disable(void) { port |= 4U; lines(); }
static void send(uint8_t v) { for (unsigned i=0;i<8U;i++){ if(v&0x80U)port|=1U;else port&=(uint8_t)~1U; port|=2U;lines();port&=(uint8_t)~2U;lines();port|=2U;lines();v<<=1;} }
static uint8_t receive(void) { uint8_t v=0;ddr&=(uint8_t)~1U;lines();for(unsigned i=0;i<8U;i++){port&=(uint8_t)~2U;lines();v=(uint8_t)((v<<1)|rtc_data_read());port|=2U;lines();}ddr|=1U;lines();return v; }
static uint8_t read_cmd(uint8_t c){uint8_t v;enable();send(c);v=receive();disable();return v;}
static void write_cmd(uint8_t c,uint8_t v){enable();send(c);send(v);disable();}
static void write_ext(uint8_t a,uint8_t v){enable();send((uint8_t)(0x38U|(a>>5)));send((uint8_t)((a&31U)<<2));send(v);disable();}
static uint8_t read_ext(uint8_t a){uint8_t v;enable();send((uint8_t)(0xb8U|(a>>5)));send((uint8_t)((a&31U)<<2));v=receive();disable();return v;}
static int fail(const char *m){fprintf(stderr,"FAIL: %s\n",m);return 1;}
int main(void){rtc_init(0x12345678U,NULL,0U);if(read_cmd(0x81U)!=0x78U||read_cmd(0x8dU)!=0x12U)return fail("seconds");if(read_cmd(0xa1U)!=7U||read_cmd(0xc1U)!=0xa8U)return fail("defaults");write_cmd(0x21U,3U);if(read_cmd(0xa1U)!=3U)return fail("PRAM");write_cmd(0x35U,0x80U);write_cmd(0x21U,6U);if(read_cmd(0xa1U)!=3U)return fail("protect");write_cmd(0x35U,0U);write_ext(0x5aU,0x69U);if(read_ext(0x5aU)!=0x69U)return fail("extended");rtc_advance_seconds(1U);puts("uMac RTC/PRAM tests: PASS");return 0;}
