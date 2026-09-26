/* SPDX-License-Identifier: MIT */
#ifndef MCX_MAC_M68KCONF_H_
#define MCX_MAC_M68KCONF_H_

#if defined(CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE)
void mcx_opcode_profile_tick(void (*handler)(void));
#endif
#if defined(CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE)
#include <stdbool.h>
#include <stdint.h>
bool mcx_cycle_profile_start(uint32_t *started_cycles);
void mcx_cycle_profile_end(void (*handler)(void), uint32_t started_cycles);
#endif

/* Keep the vendor's Musashi configuration unchanged. Project experiments
 * override only the two settings below after loading it. */
#include <m68kconf.h>

#if defined(CONFIG_MCX_MAC_UMAC_INSTR_COUNT)
extern volatile unsigned int mcx_mac_guest_instructions;
#undef M68K_INSTRUCTION_HOOK
#define M68K_INSTRUCTION_HOOK OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) (++mcx_mac_guest_instructions)
#endif

#if defined(CONFIG_MCX_MAC_UMAC_HOT_OPS_RAM)
#undef M68K_FAST_FUNC
/* Zephyr copies .ramfunc from flash before use and grants it execute access.
 * The current RAMX bank is data-only under the MPU, so do not put code there
 * without a separately validated executable region. */
#define M68K_FAST_FUNC(x) __attribute__((noinline, section(".ramfunc"))) x
#elif defined(CONFIG_MCX_MAC_UMAC_HOT_OPS_RAMX)
#undef M68K_FAST_FUNC
/* Zephyr's code-relocation step copies this section to the isolated RAMX
 * executable bank before the MPU is initialized. */
#define M68K_FAST_FUNC(x) __attribute__((noinline, section(".text.mcx_hot"))) x
#endif

#endif /* MCX_MAC_M68KCONF_H_ */
