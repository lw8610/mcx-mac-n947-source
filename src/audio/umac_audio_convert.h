/* SPDX-License-Identifier: MIT */
#ifndef MCX_MAC_AUDIO_UMAC_AUDIO_CONVERT_H_
#define MCX_MAC_AUDIO_UMAC_AUDIO_CONVERT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UMAC_AUDIO_SOURCE_RATE_NUM 244800U
#define UMAC_AUDIO_SOURCE_RATE_DEN 11U
#define UMAC_AUDIO_FIR_TAPS 31U
#define UMAC_AUDIO_MAX_OUTPUT_PER_INPUT 3U

struct umac_audio_converter {
	int16_t history[UMAC_AUDIO_FIR_TAPS];
	uint32_t history_pos;
	uint64_t input_samples;
	uint64_t next_output_position;
	uint32_t position_step_num;
	uint32_t position_step_den;
	int16_t previous_filtered;
	bool have_previous;
	bool finished;
};

/* Convert the Macintosh unsigned 8-bit midpoint representation to Q15 PCM. */
int16_t umac_audio_u8_to_q15(uint8_t sample);

/* Initialize a streaming converter for 44.1 or 48 kHz mono signed 16-bit PCM. */
int umac_audio_converter_init(struct umac_audio_converter *converter,
			      uint32_t output_rate_hz);

/*
 * Filter and convert a complete input block. The caller provides room for at
 * least input_count * UMAC_AUDIO_MAX_OUTPUT_PER_INPUT samples. Keeping the
 * state between calls preserves FIR history and the exact fractional phase.
 */
int umac_audio_converter_process(struct umac_audio_converter *converter,
				 const uint8_t *input, size_t input_count,
				 int16_t *output, size_t output_capacity,
				 size_t *output_count);

/*
 * Complete a finite stream by extending the last filtered value to the exact
 * input duration. Live audio does not call finish between blocks.
 */
int umac_audio_converter_finish(struct umac_audio_converter *converter,
				int16_t *output, size_t output_capacity,
				size_t *output_count);

#endif /* MCX_MAC_AUDIO_UMAC_AUDIO_CONVERT_H_ */
