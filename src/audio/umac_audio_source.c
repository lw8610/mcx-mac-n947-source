/* SPDX-License-Identifier: MIT */
#include "audio/umac_audio_source.h"

#include <errno.h>
#include <string.h>

size_t umac_audio_source_buffer_offset(size_t ram_size, bool main_buffer)
{
	size_t from_top = main_buffer ? UMAC_AUDIO_SOURCE_MAIN_FROM_TOP :
					UMAC_AUDIO_SOURCE_ALT_FROM_TOP;

	return ram_size >= from_top ? ram_size - from_top : SIZE_MAX;
}

int umac_audio_source_extract_frame(const uint8_t *ram, size_t ram_size,
				    size_t buffer_offset, bool enabled,
				    uint8_t volume,
				    uint8_t output[UMAC_AUDIO_SOURCE_FRAME_SAMPLES])
{
	if (ram == NULL || output == NULL || volume > 7U ||
	    buffer_offset > ram_size ||
	    UMAC_AUDIO_SOURCE_FRAME_BYTES > ram_size - buffer_offset)
		return -EINVAL;
	if (!enabled || volume == 0U) {
		memset(output, 128, UMAC_AUDIO_SOURCE_FRAME_SAMPLES);
		return 0;
	}
	for (size_t i = 0U; i < UMAC_AUDIO_SOURCE_FRAME_SAMPLES; ++i) {
		int32_t centered = (int32_t)ram[buffer_offset + i * 2U] - 128;
		output[i] = (uint8_t)(centered * volume / 7 + 128);
	}
	return 0;
}
