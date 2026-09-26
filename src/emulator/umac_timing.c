/* SPDX-License-Identifier: MIT */
#include "umac_timing.h"

struct umac_timing_events umac_timing_poll(struct umac_timing *timing,
					   uint32_t frame, uint32_t uptime_ms)
{
	struct umac_timing_events events = { 0 };
	uint32_t elapsed;

	if (!timing->started) {
		timing->last_frame = frame;
		timing->last_second_ms = uptime_ms;
		timing->started = true;
		return events;
	}

	if (frame != timing->last_frame) {
		timing->last_frame = frame;
		events.vsync = true;
	}

	elapsed = uptime_ms - timing->last_second_ms;
	if (elapsed >= 1000U) {
		timing->last_second_ms += (elapsed / 1000U) * 1000U;
		events.second = true;
	}
	return events;
}
