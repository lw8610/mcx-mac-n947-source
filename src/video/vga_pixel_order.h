#ifndef MCX_MAC_VIDEO_VGA_PIXEL_ORDER_H_
#define MCX_MAC_VIDEO_VGA_PIXEL_ORDER_H_

#include <stdint.h>

/* The Mac framebuffer encodes the leftmost pixel in bit 7. FlexIO SHIFTBUF
 * serializes each byte least-significant bit first on this VGA path. Reverse
 * bits within each byte without changing the order of bytes along a line.
 */
static inline uint8_t vga_flexio_pixel_byte(uint8_t mac_byte)
{
	mac_byte = (uint8_t)((mac_byte >> 4) | (mac_byte << 4));
	mac_byte = (uint8_t)(((mac_byte & 0xccU) >> 2) |
			     ((mac_byte & 0x33U) << 2));
	return (uint8_t)(((mac_byte & 0xaaU) >> 1) |
			 ((mac_byte & 0x55U) << 1));
}

static inline uint32_t vga_flexio_pixel_word(uint32_t mac_word)
{
#if defined(__arm__) || defined(__thumb__)
	uint32_t flexio_word;

	/* RBIT reverses all 32 bits; REV restores the original byte order. */
	__asm__ volatile("rbit %0, %1\n\trev %0, %0"
			 : "=&r"(flexio_word) : "r"(mac_word));
	return flexio_word;
#else
	return (uint32_t)vga_flexio_pixel_byte((uint8_t)mac_word) |
	       ((uint32_t)vga_flexio_pixel_byte((uint8_t)(mac_word >> 8)) << 8) |
	       ((uint32_t)vga_flexio_pixel_byte((uint8_t)(mac_word >> 16)) << 16) |
	       ((uint32_t)vga_flexio_pixel_byte((uint8_t)(mac_word >> 24)) << 24);
#endif
}

#endif
