/* Host-only WAV generator for the portable uMac audio conversion core. */
#include "audio/umac_audio_convert.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SOURCE_SAMPLES 44509U
#define OUTPUT_CAPACITY (SOURCE_SAMPLES * UMAC_AUDIO_MAX_OUTPUT_PER_INPUT)

static void put_u16(FILE *file, uint16_t value)
{
	fputc((int)(value & 0xffU), file);
	fputc((int)(value >> 8), file);
}

static void put_u32(FILE *file, uint32_t value)
{
	put_u16(file, (uint16_t)(value & 0xffffU));
	put_u16(file, (uint16_t)(value >> 16));
}

static int write_wav(const char *path, const int16_t *samples,
		     size_t count, uint32_t rate)
{
	FILE *file = fopen(path, "wb");
	uint32_t data_bytes;

	if (file == NULL || count > UINT32_MAX / 2U) {
		return -1;
	}
	data_bytes = (uint32_t)count * 2U;
	fwrite("RIFF", 1U, 4U, file);
	put_u32(file, 36U + data_bytes);
	fwrite("WAVEfmt ", 1U, 8U, file);
	put_u32(file, 16U);
	put_u16(file, 1U);
	put_u16(file, 1U);
	put_u32(file, rate);
	put_u32(file, rate * 2U);
	put_u16(file, 2U);
	put_u16(file, 16U);
	fwrite("data", 1U, 4U, file);
	put_u32(file, data_bytes);
	for (size_t i = 0U; i < count; ++i) {
		put_u16(file, (uint16_t)samples[i]);
	}
	return fclose(file);
}

int main(int argc, char **argv)
{
	struct umac_audio_converter converter;
	uint8_t *source;
	int16_t *output;
	uint32_t rate = 48000U;
	size_t produced;
	size_t tail;
	const double source_rate = (double)UMAC_AUDIO_SOURCE_RATE_NUM /
				   UMAC_AUDIO_SOURCE_RATE_DEN;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s OUTPUT.wav [48000|44100]\n", argv[0]);
		return 2;
	}
	if (argc == 3) {
		rate = (uint32_t)strtoul(argv[2], NULL, 10);
	}
	source = malloc(SOURCE_SAMPLES);
	output = malloc(sizeof(*output) * OUTPUT_CAPACITY);
	if (source == NULL || output == NULL ||
	    umac_audio_converter_init(&converter, rate) != 0) {
		fprintf(stderr, "initialization failed\n");
		free(output);
		free(source);
		return 1;
	}

	for (size_t i = 0U; i < SOURCE_SAMPLES; ++i) {
		double envelope = i < 512U ? (double)i / 512.0 : 1.0;
		double value = sin(2.0 * 3.14159265358979323846 * 1000.0 *
				   (double)i / source_rate);

		source[i] = (uint8_t)(128.0 + 80.0 * envelope * value);
	}
	if (umac_audio_converter_process(&converter, source, SOURCE_SAMPLES,
					 output, OUTPUT_CAPACITY, &produced) != 0 ||
	    umac_audio_converter_finish(&converter, output + produced,
					OUTPUT_CAPACITY - produced, &tail) != 0 ||
	    write_wav(argv[1], output, produced + tail, rate) != 0) {
		fprintf(stderr, "conversion or WAV write failed\n");
		free(output);
		free(source);
		return 1;
	}

	printf("WAV %s: source=%u at %.6f Hz, output=%zu at %u Hz\n",
	       argv[1], SOURCE_SAMPLES, source_rate, produced + tail, rate);
	free(output);
	free(source);
	return 0;
}
