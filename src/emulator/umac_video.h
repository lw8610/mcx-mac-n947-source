#ifndef MCX_MAC_EMULATOR_UMAC_VIDEO_H_
#define MCX_MAC_EMULATOR_UMAC_VIDEO_H_

#include <stddef.h>
#include <stdint.h>

/* Copy uMac's 512x342 1bpp screen from guest RAM to the proven VGA path.
 * Call after umac_vsync_event(), using umac_get_fb_offset() for fb_offset.
 */
int umac_video_present(const uint8_t *guest_ram, size_t guest_ram_size,
		       size_t fb_offset);

#if defined(CONFIG_MCX_MAC_UMAC_VIDEO_DIRTY_TRACK)
struct umac_video_dirty_stats {
	uint32_t compared;
	uint32_t skipped;
	uint32_t changed;
	uint32_t guest_writes;
	uint32_t periodic_checks;
	uint32_t untracked_changes;
};

void umac_video_mark_guest_write(size_t offset, size_t length);
void umac_video_get_dirty_stats(struct umac_video_dirty_stats *stats);
#endif

#endif /* MCX_MAC_EMULATOR_UMAC_VIDEO_H_ */
