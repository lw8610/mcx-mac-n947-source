/* SPDX-License-Identifier: MIT */
#ifndef MCX_MAC_AUDIO_UMAC_AUDIO_SOURCE_H_
#define MCX_MAC_AUDIO_UMAC_AUDIO_SOURCE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UMAC_AUDIO_SOURCE_FRAME_SAMPLES 370U
#define UMAC_AUDIO_SOURCE_FRAME_BYTES (UMAC_AUDIO_SOURCE_FRAME_SAMPLES * 2U)
#define UMAC_AUDIO_SOURCE_MAIN_FROM_TOP 0x300U
#define UMAC_AUDIO_SOURCE_ALT_FROM_TOP 0x5f00U

size_t umac_audio_source_buffer_offset(size_t ram_size, bool main_buffer);

/* Copy a frame's even-addressed sound bytes; odd bytes control floppy speed. */
int umac_audio_source_extract_frame(const uint8_t *ram, size_t ram_size,
				    size_t buffer_offset, bool enabled,
				    uint8_t volume,
				    uint8_t output[UMAC_AUDIO_SOURCE_FRAME_SAMPLES]);

#endif
