/* SPDX-License-Identifier: MIT */
#include "mcx_opcode_profile.h"

#include <stdint.h>
#include <string.h>
#include <zephyr/sys/printk.h>

/* 1,968 distinct Musashi handlers fit below 50% occupancy. Keep the 32 KiB
 * table in the spare SRAMX bank, after the volatile disk's 32 KiB overlay. */
#define PROFILE_SLOTS 4096U
#define PROFILE_TOP 20U

struct handler_sample {
	uintptr_t handler;
	uint32_t count;
};

static struct handler_sample samples[PROFILE_SLOTS]
	__attribute__((section("SRAMX"), aligned(4)));
static uint32_t sample_countdown = 1U;
static uint32_t random_state = 0x63a7d451U;
static uint32_t total_samples;
static uint32_t occupied;
static uint32_t overflow;

void mcx_opcode_profile_init(void)
{
	memset(samples, 0, sizeof(samples));
	sample_countdown = 1U;
	random_state = 0x63a7d451U;
	total_samples = 0U;
	occupied = 0U;
	overflow = 0U;
}

void mcx_opcode_profile_tick(void (*handler)(void))
{
	uintptr_t address;
	uint32_t slot;

	if (--sample_countdown != 0U) {
		return;
	}
	/* Variable spacing avoids aliasing with tight periodic guest loops. */
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	sample_countdown = 32U + (random_state & 63U);

	address = (uintptr_t)handler;
	slot = (uint32_t)(((address >> 2) * 2654435761U) >> 20);
	for (uint32_t probe = 0U; probe < PROFILE_SLOTS; probe++) {
		struct handler_sample *entry = &samples[(slot + probe) & (PROFILE_SLOTS - 1U)];

		if (entry->handler == address) {
			entry->count++;
			total_samples++;
			return;
		}
		if (entry->handler == 0U) {
			entry->handler = address;
			entry->count = 1U;
			occupied++;
			total_samples++;
			return;
		}
	}
	overflow++;
}

void mcx_opcode_profile_report(void)
{
	const struct handler_sample *top[PROFILE_TOP] = { 0 };
	uint32_t top_total = 0U;

	for (uint32_t i = 0U; i < PROFILE_SLOTS; i++) {
		const struct handler_sample *entry = &samples[i];

		if (entry->handler == 0U ||
		    (top[PROFILE_TOP - 1U] != NULL &&
		     entry->count <= top[PROFILE_TOP - 1U]->count)) {
			continue;
		}
		for (uint32_t rank = 0U; rank < PROFILE_TOP; rank++) {
			if (top[rank] == NULL || entry->count > top[rank]->count) {
				for (uint32_t move = PROFILE_TOP - 1U; move > rank; move--) {
					top[move] = top[move - 1U];
				}
				top[rank] = entry;
				break;
			}
		}
	}
	for (uint32_t i = 0U; i < PROFILE_TOP && top[i] != NULL; i++) {
		top_total += top[i]->count;
	}
	printk("DIAG OPCODE samples=%u unique=%u overflow=%u top20_pct=%u\n",
	       total_samples, occupied, overflow,
	       total_samples == 0U ? 0U :
	       (uint32_t)((uint64_t)top_total * 100U / total_samples));
	for (uint32_t i = 0U; i < PROFILE_TOP && top[i] != NULL; i++) {
		printk("DIAG OPCODE rank=%u handler=%08x count=%u\n",
		       i + 1U, (unsigned int)top[i]->handler, top[i]->count);
	}
}
