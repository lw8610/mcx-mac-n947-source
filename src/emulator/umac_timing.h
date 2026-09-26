#ifndef MCX_MAC_EMULATOR_UMAC_TIMING_H_
#define MCX_MAC_EMULATOR_UMAC_TIMING_H_

#include <stdbool.h>
#include <stdint.h>

struct umac_timing {
	uint32_t last_frame;
	uint32_t last_second_ms;
	bool started;
};

struct umac_timing_events {
	bool vsync;
	bool second;
};

/* Poll against the hardware VSYNC counter and monotonic Zephyr milliseconds.
 * A delayed poll coalesces missed frames instead of issuing a burst of stale
 * events to the emulator.
 */
struct umac_timing_events umac_timing_poll(struct umac_timing *timing,
					   uint32_t frame, uint32_t uptime_ms);

#endif /* MCX_MAC_EMULATOR_UMAC_TIMING_H_ */
