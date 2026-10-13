/* SPDX-License-Identifier: MIT */
#include "audio/umac_audio_convert.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

/*
 * 31-tap Hamming-windowed low-pass filter at the Macintosh source rate.
 * The -6 dB point is 9 kHz, leaving useful classic-Mac audio bandwidth while
 * attenuating the 11.127 kHz Nyquist edge by about 53 dB. Coefficients sum to
 * exactly 32768 in Q15. The same taps are intended for the scalar reference,
 * CMSIS-DSP and PowerQuad backends.
 */
static const int16_t fir_coefficients[UMAC_AUDIO_FIR_TAPS] = {
	22, -57, 96, -116, 70, 88, -355, 638,
	-765, 528, 222, -1489, 3086, -4669, 5834, 26502,
	5834, -4669, 3086, -1489, 222, 528, -765, 638,
	-355, 88, 70, -116, 96, -57, 22,
};

static int16_t saturate_q15(int64_t value)
{
	if (value > INT16_MAX) {
		return INT16_MAX;
	}
	if (value < INT16_MIN) {
		return INT16_MIN;
	}
	return (int16_t)value;
}

int16_t umac_audio_u8_to_q15(uint8_t sample)
{
	return (int16_t)(((int32_t)sample - 128) * 256);
}

static int16_t filter_sample(struct umac_audio_converter *converter,
			     int16_t sample)
{
	int64_t accumulator = 0;
	uint32_t index;

	converter->history[converter->history_pos] = sample;
	index = converter->history_pos;
	for (uint32_t tap = 0U; tap < UMAC_AUDIO_FIR_TAPS; ++tap) {
		accumulator += (int32_t)converter->history[index] *
			       fir_coefficients[tap];
		index = (index == 0U) ? UMAC_AUDIO_FIR_TAPS - 1U : index - 1U;
	}
	converter->history_pos++;
	if (converter->history_pos == UMAC_AUDIO_FIR_TAPS) {
		converter->history_pos = 0U;
	}

	/* Match the Q15 FIR convention: a 2.30 accumulator returns to 1.15. */
	return saturate_q15(accumulator >> 15);
}

static int16_t interpolate(int16_t first, int16_t second,
			   uint32_t numerator, uint32_t denominator)
{
	int64_t delta = (int32_t)second - first;
	int64_t value = (int64_t)first + delta * numerator / denominator;

	return saturate_q15(value);
}

int umac_audio_converter_init(struct umac_audio_converter *converter,
			      uint32_t output_rate_hz)
{
	if (converter == NULL) {
		return -EINVAL;
	}

	memset(converter, 0, sizeof(*converter));
	switch (output_rate_hz) {
	case 48000U:
		/* (244800 / 11) / 48000 = 51 / 110. */
		converter->position_step_num = 51U;
		converter->position_step_den = 110U;
		break;
	case 44100U:
		/* (244800 / 11) / 44100 = 272 / 539. */
		converter->position_step_num = 272U;
		converter->position_step_den = 539U;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int emit_until(struct umac_audio_converter *converter,
		      uint64_t current_position, int16_t current,
		      int16_t *output, size_t output_capacity,
		      size_t *written)
{
	while (converter->next_output_position <= current_position) {
		uint64_t base = converter->next_output_position /
				converter->position_step_den;
		uint32_t fraction = (uint32_t)(converter->next_output_position %
					       converter->position_step_den);
		int16_t value;

		if (*written == output_capacity) {
			return -ENOSPC;
		}
		if (base == converter->input_samples && fraction == 0U) {
			value = current;
		} else {
			value = interpolate(converter->previous_filtered, current,
					    fraction,
					    converter->position_step_den);
		}
		output[(*written)++] = value;
		converter->next_output_position += converter->position_step_num;
	}
	return 0;
}

int umac_audio_converter_process(struct umac_audio_converter *converter,
				 const uint8_t *input, size_t input_count,
				 int16_t *output, size_t output_capacity,
				 size_t *output_count)
{
	size_t written = 0U;

	if (converter == NULL || output_count == NULL ||
	    (input_count != 0U && (input == NULL || output == NULL)) ||
	    converter->position_step_den == 0U || converter->finished) {
		return -EINVAL;
	}
	if (input_count > SIZE_MAX / UMAC_AUDIO_MAX_OUTPUT_PER_INPUT ||
	    output_capacity < input_count * UMAC_AUDIO_MAX_OUTPUT_PER_INPUT) {
		return -ENOSPC;
	}

	for (size_t i = 0U; i < input_count; ++i) {
		int16_t filtered = filter_sample(converter,
						 umac_audio_u8_to_q15(input[i]));
		uint64_t current_position;
		int ret;

		if (!converter->have_previous) {
			converter->previous_filtered = filtered;
			converter->have_previous = true;
			converter->input_samples = 0U;
			if (written == output_capacity) {
				return -ENOSPC;
			}
			output[written++] = filtered;
			converter->next_output_position =
				converter->position_step_num;
			continue;
		}

		converter->input_samples++;
		current_position = converter->input_samples *
				   converter->position_step_den;
		ret = emit_until(converter, current_position, filtered,
				 output, output_capacity, &written);
		if (ret != 0) {
			return ret;
		}
		converter->previous_filtered = filtered;
	}
	*output_count = written;
	return 0;
}

int umac_audio_converter_finish(struct umac_audio_converter *converter,
				int16_t *output, size_t output_capacity,
				size_t *output_count)
{
	uint64_t end_position;
	size_t written = 0U;

	if (converter == NULL || output_count == NULL ||
	    converter->position_step_den == 0U || converter->finished ||
	    (converter->have_previous && output == NULL)) {
		return -EINVAL;
	}
	converter->finished = true;
	if (!converter->have_previous) {
		*output_count = 0U;
		return 0;
	}

	end_position = (converter->input_samples + 1U) *
		       converter->position_step_den;
	while (converter->next_output_position < end_position) {
		if (written == output_capacity) {
			return -ENOSPC;
		}
		output[written++] = converter->previous_filtered;
		converter->next_output_position += converter->position_step_num;
	}
	*output_count = written;
	return 0;
}
