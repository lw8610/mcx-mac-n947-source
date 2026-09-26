/* SPDX-License-Identifier: MIT */
#ifndef MCX_MAC_VIDEO_VGA_SMARTDMA_GUEST_DESCRIPTORS_H_
#define MCX_MAC_VIDEO_VGA_SMARTDMA_GUEST_DESCRIPTORS_H_

#include <stdint.h>

#include "vga_smartdma_stream_ref.h"

/* A descriptor packs two indices into the aligned source word array.
 * Index 0 is a white word, indices 1..5472 are the 21,888 Mac screen bytes.
 */
extern const uint32_t
	vga_smartdma_guest_descriptors[VGA_SMARTDMA_STREAM_WORDS_PER_FRAME];

#endif
