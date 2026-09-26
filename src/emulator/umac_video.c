/* SPDX-License-Identifier: MIT */
#include "umac_video.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "video/vga.h"

#if defined(CONFIG_MCX_MAC_UMAC_VIDEO_DIRTY_TRACK)
static const uint8_t *tracked_guest_ram;
static size_t tracked_guest_ram_size;
static size_t tracked_fb_offset;
static bool guest_frame_dirty = true;
static uint8_t clean_frames;
static struct umac_video_dirty_stats dirty_stats;

void umac_video_mark_guest_write(size_t offset, size_t length)
{
	if (length == 0U) {
		return;
	}
	/* Before the first present, a full comparison is mandatory anyway. */
	if (tracked_guest_ram == NULL) {
		guest_frame_dirty = true;
		return;
	}
	if (offset > SIZE_MAX - length ||
	    (offset < tracked_fb_offset + VGA_MAC_FRAMEBUFFER_BYTES &&
	     offset + length > tracked_fb_offset)) {
		guest_frame_dirty = true;
		dirty_stats.guest_writes++;
	}
}

void umac_video_get_dirty_stats(struct umac_video_dirty_stats *stats)
{
	if (stats != NULL) {
		*stats = dirty_stats;
	}
}
#endif

#if defined(CONFIG_MCX_MAC_UMAC_WORD_VIDEO)
/* The picolibc memcmp/memcpy in this target build walk one byte at a time.
 * The Mac framebuffer is word-aligned and word-sized in normal operation.
 * memcpy of a fixed four bytes compiles to aligned word accesses without
 * violating C's effective-type rules; retain a bytewise library fallback for
 * callers with unaligned buffers.
 */
static bool video_equal(const uint8_t *a, const uint8_t *b, size_t size)
{
	if ((((uintptr_t)a | (uintptr_t)b | size) & 3U) != 0U) {
		return memcmp(a, b, size) == 0;
	}

	for (size_t offset = 0U; offset < size; offset += sizeof(uint32_t)) {
		uint32_t a_word;
		uint32_t b_word;

		memcpy(&a_word, a + offset, sizeof(a_word));
		memcpy(&b_word, b + offset, sizeof(b_word));
		if (a_word != b_word) {
			return false;
		}
	}
	return true;
}

static void video_copy(uint8_t *dst, const uint8_t *src, size_t size)
{
	if ((((uintptr_t)dst | (uintptr_t)src | size) & 3U) != 0U) {
		memcpy(dst, src, size);
		return;
	}

	for (size_t offset = 0U; offset < size; offset += sizeof(uint32_t)) {
		uint32_t word;

		memcpy(&word, src + offset, sizeof(word));
		memcpy(dst + offset, &word, sizeof(word));
	}
}
#endif

int umac_video_present(const uint8_t *guest_ram, size_t guest_ram_size,
		       size_t fb_offset)
{
	uint8_t *video;
#if defined(CONFIG_MCX_MAC_UMAC_VIDEO_DIRTY_TRACK)
	bool periodic_check = false;
#endif

	if (guest_ram == NULL || fb_offset > guest_ram_size ||
	    guest_ram_size - fb_offset < VGA_MAC_FRAMEBUFFER_BYTES ||
	    vga_framebuffer_size() != VGA_MAC_FRAMEBUFFER_BYTES) {
		return -EINVAL;
	}

	video = vga_framebuffer_get();
	if (video == NULL) {
		return -ENODEV;
	}

#if defined(CONFIG_MCX_MAC_UMAC_VIDEO_DIRTY_TRACK)
	if (tracked_guest_ram != guest_ram ||
	    tracked_guest_ram_size != guest_ram_size ||
	    tracked_fb_offset != fb_offset) {
		tracked_guest_ram = guest_ram;
		tracked_guest_ram_size = guest_ram_size;
		tracked_fb_offset = fb_offset;
		guest_frame_dirty = true;
	}
	if (!guest_frame_dirty) {
		/* Catch an uninstrumented direct RAM write without paying for a
		 * full framebuffer comparison on every unchanged VSYNC. */
		if (++clean_frames < 60U) {
			dirty_stats.skipped++;
			return 0;
		}
		periodic_check = true;
		dirty_stats.periodic_checks++;
	}
	clean_frames = 0U;
	dirty_stats.compared++;
#endif

	/* Both formats are packed 1bpp, 64 bytes per line, with 1 = black.
	 * The VGA backend handles the inverted electrical output and blanking.
	 */
	/* The Finder desktop is often unchanged for many VSYNCs. Rewriting both
	 * cyclic DMA rasters on every frame can overlap scanout and needlessly
	 * consume the memory bus. Only queue a vblank commit for changed pixels.
	 */
#if defined(CONFIG_MCX_MAC_UMAC_WORD_VIDEO)
	if (!video_equal(video, guest_ram + fb_offset,
			 VGA_MAC_FRAMEBUFFER_BYTES)) {
		video_copy(video, guest_ram + fb_offset,
			   VGA_MAC_FRAMEBUFFER_BYTES);
#else
	if (memcmp(video, guest_ram + fb_offset,
		   VGA_MAC_FRAMEBUFFER_BYTES) != 0) {
		memcpy(video, guest_ram + fb_offset, VGA_MAC_FRAMEBUFFER_BYTES);
#endif
		vga_framebuffer_present();
#if defined(CONFIG_MCX_MAC_UMAC_VIDEO_DIRTY_TRACK)
		dirty_stats.changed++;
		if (periodic_check) {
			dirty_stats.untracked_changes++;
		}
#endif
	}
#if defined(CONFIG_MCX_MAC_UMAC_VIDEO_DIRTY_TRACK)
	guest_frame_dirty = false;
#endif
	return 0;
}
