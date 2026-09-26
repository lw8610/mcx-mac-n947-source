/* SPDX-License-Identifier: MIT */
#ifndef MCX_MAC_VIDEO_VGA_SMARTDMA_GUEST_REF_H_
#define MCX_MAC_VIDEO_VGA_SMARTDMA_GUEST_REF_H_

#include <stddef.h>
#include <stdint.h>

#include "vga_smartdma_stream_ref.h"

/* Model the words a direct guest-framebuffer SmartDMA reader must send to
 * SHIFTBUFBBS. This is a host-test oracle, not a live video path. The Mac
 * framebuffer is 4-byte aligned, but its centered VGA position starts at
 * byte 26 of a 100-byte scan line. Recombine aligned guest words instead
 * of relying on potentially unsupported unaligned SmartDMA loads.
 */
static inline uint32_t vga_smartdma_guest_load_le32(const uint8_t *source)
{
	return (uint32_t)source[0] |
	       ((uint32_t)source[1] << 8) |
	       ((uint32_t)source[2] << 16) |
	       ((uint32_t)source[3] << 24);
}

static inline uint32_t vga_smartdma_guest_reference_word(
	const uint8_t *guest_framebuffer, size_t word_index)
{
	word_index %= VGA_SMARTDMA_STREAM_WORDS_PER_FRAME;
	const size_t line = word_index / VGA_SMARTDMA_STREAM_WORDS_PER_LINE;
	const size_t column = word_index % VGA_SMARTDMA_STREAM_WORDS_PER_LINE;
	const uint8_t *row;

	if (line < VGA_SMARTDMA_STREAM_TOP ||
	    line >= VGA_SMARTDMA_STREAM_TOP + VGA_MAC_HEIGHT ||
	    column < 6U || column > 22U) {
		return UINT32_MAX;
	}

	row = guest_framebuffer +
	      (line - VGA_SMARTDMA_STREAM_TOP) * VGA_MAC_STRIDE_BYTES;
	if (column == 6U) {
		return 0x0000ffffU |
		       (vga_smartdma_guest_load_le32(row) << 16);
	}
	if (column == 22U) {
		return (vga_smartdma_guest_load_le32(row + 60U) >> 16) |
		       0xffff0000U;
	}

	const size_t source_word = column - 7U;
	return (vga_smartdma_guest_load_le32(row + source_word * 4U) >> 16) |
	       (vga_smartdma_guest_load_le32(row + (source_word + 1U) * 4U)
		<< 16);
}

#endif /* MCX_MAC_VIDEO_VGA_SMARTDMA_GUEST_REF_H_ */
