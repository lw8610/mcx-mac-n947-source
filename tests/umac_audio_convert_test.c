#include "audio/umac_audio_convert.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t convert(const uint8_t *input, size_t count, size_t chunk,
		      uint32_t rate, int16_t *output, size_t capacity)
{
	struct umac_audio_converter converter;
	size_t input_offset = 0U;
	size_t output_offset = 0U;

	assert(umac_audio_converter_init(&converter, rate) == 0);
	while (input_offset < count) {
		size_t current = count - input_offset;
		size_t written;

		if (current > chunk) {
			current = chunk;
		}
		assert(umac_audio_converter_process(
			       &converter, input + input_offset, current,
			       output + output_offset, capacity - output_offset,
			       &written) == 0);
		input_offset += current;
		output_offset += written;
	}
	{
		size_t written;

		assert(umac_audio_converter_finish(
			       &converter, output + output_offset,
			       capacity - output_offset, &written) == 0);
		output_offset += written;
	}
	return output_offset;
}

int main(void)
{
	uint8_t input[4096];
	int16_t *whole = malloc(sizeof(*whole) * 13000U);
	int16_t *chunked = malloc(sizeof(*chunked) * 13000U);
	size_t whole_count;
	size_t chunked_count;

	assert(whole != NULL && chunked != NULL);
	assert(umac_audio_u8_to_q15(0U) == INT16_MIN);
	assert(umac_audio_u8_to_q15(128U) == 0);
	assert(umac_audio_u8_to_q15(255U) == 32512);
	{
		struct umac_audio_converter invalid;

		assert(umac_audio_converter_init(NULL, 48000U) == -EINVAL);
		assert(umac_audio_converter_init(&invalid, 32000U) == -EINVAL);
	}

	memset(input, 128, sizeof(input));
	whole_count = convert(input, sizeof(input), sizeof(input), 48000U,
			      whole, 13000U);
	assert(whole_count == (sizeof(input) * 110U + 50U) / 51U);
	for (size_t i = 0U; i < whole_count; ++i) {
		assert(whole[i] == 0);
	}

	for (size_t i = 0U; i < sizeof(input); ++i) {
		input[i] = (uint8_t)((i * 73U + i / 7U) & 0xffU);
	}
	whole_count = convert(input, sizeof(input), sizeof(input), 48000U,
			      whole, 13000U);
	chunked_count = convert(input, sizeof(input), 37U, 48000U,
				chunked, 13000U);
	assert(chunked_count == whole_count);
	assert(memcmp(whole, chunked, whole_count * sizeof(*whole)) == 0);

	whole_count = convert(input, sizeof(input), 113U, 44100U,
			      whole, 13000U);
	assert(whole_count == (sizeof(input) * 539U + 271U) / 272U);
	chunked_count = convert(input, sizeof(input), 1U, 44100U,
				chunked, 13000U);
	assert(chunked_count == whole_count);
	assert(memcmp(whole, chunked, whole_count * sizeof(*whole)) == 0);

	free(chunked);
	free(whole);
	puts("uMac audio conversion tests: PASS");
	return 0;
}
