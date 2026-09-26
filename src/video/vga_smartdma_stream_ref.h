/* SPDX-License-Identifier: MIT */
#ifndef MCX_MAC_VIDEO_VGA_SMARTDMA_STREAM_REF_H_
#define MCX_MAC_VIDEO_VGA_SMARTDMA_STREAM_REF_H_

#include <stddef.h>
#include <stdint.h>

#include "vga.h"
#include "vga_pixel_order.h"

/* Reference behavior for a future SmartDMA program. A 32-bit FlexIO write
 * represents 32 serial pixels. The complete 525-row frame is modeled,
 * including the final 13 rows that the earlier 512-row model omitted. A
 * frame has an odd number of words;
 * the current two-shifter FlexIO consumes pairs, so a pair can cross a
 * frame boundary. This function is CPU-only, not the stable scanout path.
 */
#define VGA_SMARTDMA_STREAM_LINES VGA_V_TOTAL_LINES
#define VGA_SMARTDMA_STREAM_WORDS_PER_LINE (VGA_H_TOTAL_PIXELS / 32U)
#define VGA_SMARTDMA_STREAM_WORDS_PER_FRAME \
	(VGA_SMARTDMA_STREAM_LINES * VGA_SMARTDMA_STREAM_WORDS_PER_LINE)
#define VGA_SMARTDMA_STREAM_BYTES \
	(VGA_SMARTDMA_STREAM_LINES * VGA_H_TOTAL_PIXELS / 8U)
#define VGA_SMARTDMA_STREAM_PAIR_PERIOD \
	VGA_SMARTDMA_STREAM_WORDS_PER_FRAME
#define VGA_SMARTDMA_STREAM_TOP \
	(VGA_V_SYNC_LINES + VGA_V_BACK_PORCH_LINES + \
	 (VGA_V_ACTIVE_LINES - VGA_MAC_HEIGHT) / 2U)
#define VGA_SMARTDMA_STREAM_LEFT_BYTES \
	((VGA_H_SYNC_PIXELS + VGA_H_BACK_PORCH_PIXELS + \
	  (VGA_H_ACTIVE_PIXELS - VGA_MAC_WIDTH) / 2U) / 8U)

static inline uint32_t vga_smartdma_stream_reference_word(
	const uint8_t *mac_framebuffer, size_t word_index)
{
	word_index %= VGA_SMARTDMA_STREAM_WORDS_PER_FRAME;
	size_t line = word_index / VGA_SMARTDMA_STREAM_WORDS_PER_LINE;
	size_t line_byte = (word_index % VGA_SMARTDMA_STREAM_WORDS_PER_LINE) * 4U;
	uint32_t flexio_word = 0U;

	for (size_t byte = 0U; byte < 4U; byte++) {
		size_t x = line_byte + byte;
		uint8_t mac_byte = 0xffU;

		if (line >= VGA_SMARTDMA_STREAM_TOP &&
		    line < VGA_SMARTDMA_STREAM_TOP + VGA_MAC_HEIGHT &&
		    x >= VGA_SMARTDMA_STREAM_LEFT_BYTES &&
		    x < VGA_SMARTDMA_STREAM_LEFT_BYTES + VGA_MAC_STRIDE_BYTES) {
			mac_byte = mac_framebuffer[
				(line - VGA_SMARTDMA_STREAM_TOP) *
				VGA_MAC_STRIDE_BYTES +
				(x - VGA_SMARTDMA_STREAM_LEFT_BYTES)];
		}
		flexio_word |= (uint32_t)vga_flexio_pixel_byte(mac_byte) <<
			       (byte * 8U);
	}
	return flexio_word;
}

/* A pair index spans two frames before the 64-pixel FIFO phase repeats.
 * pair_index == WORDS_PER_FRAME is the same pair as pair_index == 0.
 */
static inline void vga_smartdma_stream_reference_pair(
	const uint8_t *mac_framebuffer, size_t pair_index,
	uint32_t *first, uint32_t *second)
{
	size_t word_index = (pair_index % VGA_SMARTDMA_STREAM_PAIR_PERIOD) * 2U;

	*first = vga_smartdma_stream_reference_word(mac_framebuffer, word_index);
	*second = vga_smartdma_stream_reference_word(mac_framebuffer,
						      word_index + 1U);
}

#endif /* MCX_MAC_VIDEO_VGA_SMARTDMA_STREAM_REF_H_ */
