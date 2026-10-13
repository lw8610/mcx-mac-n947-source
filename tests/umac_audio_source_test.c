/* SPDX-License-Identifier: MIT */
#include "audio/umac_audio_source.h"
#include <stdio.h>
#include <string.h>
#define TEST_RAM_SIZE (128U * 1024U)
static int fail(const char *message) { fprintf(stderr, "FAIL: %s\n", message); return 1; }
int main(void)
{
	static uint8_t ram[TEST_RAM_SIZE];
	uint8_t output[UMAC_AUDIO_SOURCE_FRAME_SAMPLES];
	size_t main_offset = umac_audio_source_buffer_offset(TEST_RAM_SIZE, true);
	size_t alt_offset = umac_audio_source_buffer_offset(TEST_RAM_SIZE, false);
	if (main_offset != 0x1fd00U || alt_offset != 0x1a100U) return fail("128K addresses");
	for (size_t i = 0U; i < UMAC_AUDIO_SOURCE_FRAME_SAMPLES; ++i) {
		ram[main_offset + i * 2U] = (uint8_t)i;
		ram[main_offset + i * 2U + 1U] = 0x5aU;
	}
	if (umac_audio_source_extract_frame(ram, sizeof(ram), main_offset, true, 7U, output)) return fail("extract");
	for (size_t i = 0U; i < sizeof(output); ++i) if (output[i] != (uint8_t)i) return fail("even bytes");
	if (umac_audio_source_extract_frame(ram, sizeof(ram), main_offset, false, 7U, output)) return fail("disabled");
	for (size_t i = 0U; i < sizeof(output); ++i) if (output[i] != 128U) return fail("silence");
	memset(ram + main_offset, 0, UMAC_AUDIO_SOURCE_FRAME_BYTES);
	if (umac_audio_source_extract_frame(ram, sizeof(ram), main_offset, true, 3U, output) || output[0] != 74U) return fail("volume");
	if (!umac_audio_source_extract_frame(ram, sizeof(ram), main_offset, true, 8U, output)) return fail("volume bounds");
	puts("uMac audio source extraction: PASS");
	return 0;
}
