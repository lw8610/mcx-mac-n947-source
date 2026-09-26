/* SPDX-License-Identifier: MIT */
#include "mcx_cycle_profile.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* Diagnostic-only table in main RAM. The current 208 KiB guest build has
 * enough static headroom for 1024 entries, whereas its RAM-X does not. */
#define PROFILE_SLOTS 1024U
#define PROFILE_TOP 20U

struct handler_sample {
	uintptr_t handler;
	uint32_t count;
	uint64_t cycles;
};

static struct handler_sample samples[PROFILE_SLOTS];
static uint32_t sample_countdown = 1U;
static uint32_t random_state = 0x63a7d451U;
static uint32_t total_samples;
static uint32_t occupied;
static uint32_t overflow;
static uint64_t total_cycles;

void mcx_cycle_profile_init(void)
{
	memset(samples, 0, sizeof(samples));
	sample_countdown = 1U;
	random_state = 0x63a7d451U;
	total_samples = 0U;
	occupied = 0U;
	overflow = 0U;
	total_cycles = 0U;
}

bool mcx_cycle_profile_start(uint32_t *started_cycles)
{
	if (--sample_countdown != 0U) {
		return false;
	}
	/* Variable spacing reduces lockstep bias from tight guest loops. */
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	sample_countdown = 32U + (random_state & 63U);
	*started_cycles = k_cycle_get_32();
	return true;
}

void mcx_cycle_profile_end(void (*handler)(void), uint32_t started_cycles)
{
	/* Capture before the hash-table bookkeeping. Interrupts that preempted
	 * the handler can still be charged to it; repeat measurements to detect
	 * outliers rather than presenting these as exact per-opcode costs. */
	uint32_t elapsed = k_cycle_get_32() - started_cycles;
	uintptr_t address = (uintptr_t)handler;
	uint32_t slot = (uint32_t)(((address >> 2) * 2654435761U) >> 22);

	for (uint32_t probe = 0U; probe < PROFILE_SLOTS; probe++) {
		struct handler_sample *entry = &samples[(slot + probe) & (PROFILE_SLOTS - 1U)];

		if (entry->handler == address) {
			entry->count++;
			entry->cycles += elapsed;
			total_samples++;
			total_cycles += elapsed;
			return;
		}
		if (entry->handler == 0U) {
			entry->handler = address;
			entry->count = 1U;
			entry->cycles = elapsed;
			occupied++;
			total_samples++;
			total_cycles += elapsed;
			return;
		}
	}
	overflow++;
}

void mcx_cycle_profile_report(void)
{
	const struct handler_sample *top[PROFILE_TOP] = { 0 };
	uint64_t top_cycles = 0U;

	for (uint32_t i = 0U; i < PROFILE_SLOTS; i++) {
		const struct handler_sample *entry = &samples[i];

		if (entry->handler == 0U ||
		    (top[PROFILE_TOP - 1U] != NULL &&
		     entry->cycles <= top[PROFILE_TOP - 1U]->cycles)) {
			continue;
		}
		for (uint32_t rank = 0U; rank < PROFILE_TOP; rank++) {
			if (top[rank] == NULL || entry->cycles > top[rank]->cycles) {
				for (uint32_t move = PROFILE_TOP - 1U; move > rank; move--) {
					top[move] = top[move - 1U];
				}
				top[rank] = entry;
				break;
			}
		}
	}
	for (uint32_t i = 0U; i < PROFILE_TOP && top[i] != NULL; i++) {
		top_cycles += top[i]->cycles;
	}
	printk("DIAG CYCLE PROFILE samples=%u unique=%u overflow=%u "
	       "sampled_cycles=%llu top20_pct=%u\n",
	       total_samples, occupied, overflow,
	       (unsigned long long)total_cycles,
	       total_cycles == 0U ? 0U :
	       (uint32_t)(top_cycles * 100U / total_cycles));
	for (uint32_t i = 0U; i < PROFILE_TOP && top[i] != NULL; i++) {
		printk("DIAG CYCLE rank=%u handler=%08x count=%u "
		       "cycles=%llu mean=%u\n",
		       i + 1U, (unsigned int)top[i]->handler, top[i]->count,
		       (unsigned long long)top[i]->cycles,
		       (unsigned int)(top[i]->cycles / top[i]->count));
	}
	/* Separate successive 30-second workloads instead of mixing an idle
	 * desktop with a later application run. The normal build omits this code. */
	mcx_cycle_profile_init();
}
