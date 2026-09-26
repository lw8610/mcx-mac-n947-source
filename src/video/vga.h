#ifndef MCX_MAC_VIDEO_VGA_H_
#define MCX_MAC_VIDEO_VGA_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* EXP-29 exposes the stable dual-frame scanout as a normal framebuffer. */
#define VGA_H_ACTIVE_PIXELS       640U
#define VGA_H_FRONT_PORCH_PIXELS   16U
#define VGA_H_SYNC_PIXELS          96U
#define VGA_H_BACK_PORCH_PIXELS    48U
#define VGA_H_TOTAL_PIXELS        800U

#define VGA_V_ACTIVE_LINES        480U
#define VGA_V_FRONT_PORCH_LINES    10U
#define VGA_V_SYNC_LINES            2U
#define VGA_V_BACK_PORCH_LINES     33U
#define VGA_V_TOTAL_LINES         525U

/* Classic Macintosh 512 x 342 monochrome framebuffer. */
#define VGA_MAC_WIDTH             512U
#define VGA_MAC_HEIGHT            342U
#define VGA_MAC_STRIDE_BYTES      (VGA_MAC_WIDTH / 8U)
#define VGA_MAC_FRAMEBUFFER_BYTES (VGA_MAC_STRIDE_BYTES * VGA_MAC_HEIGHT)

struct vga_timing_report {
	uint32_t hsync_millihz;
	uint32_t vsync_millihz;
	uint16_t lines_per_frame;
};

struct vga_smartdma_stats {
	uint32_t started_lines;
	uint32_t completed_lines;
	uint32_t dropped_lines;
	uint32_t start_errors;
	uint32_t timeouts;
	uint32_t sync_frames;
	uint32_t recovered_lines;
	uint32_t raster_commits;
	uint32_t transfer_last_cycles;
	uint32_t transfer_max_cycles;
};

int vga_init(void);
int vga_start(void);
void vga_stop(void);
void vga_get_timing_report(struct vga_timing_report *report);
uint32_t vga_flexio_get_frame_count(void);
uint32_t vga_flexio_get_recovered_line_count(void);

uint8_t *vga_framebuffer_get(void);
size_t vga_framebuffer_size(void);
void vga_framebuffer_clear(bool white);
void vga_framebuffer_test_pattern(void);
void vga_framebuffer_test_pattern_phase(uint32_t phase);
void vga_framebuffer_present(void);
bool vga_smartdma_is_ready(void);
int vga_smartdma_start_line(uint16_t display_line);
void vga_smartdma_stop(void);
void vga_smartdma_get_stats(struct vga_smartdma_stats *stats);
void vga_smartdma_vsync_sample(void);
void vga_smartdma_phase_report(void);
void vga_raster_freeze(bool enabled);
bool vga_raster_is_frozen(void);
uint32_t vga_flexio_take_shift_errors(void);
void vga_smartdma_linked_diag(void);
void vga_smartdma_service(void);
bool vga_smartdma_scanout_enabled(void);
void vga_diag_capture(const char *reason);
void vga_diag_print(void);

#endif /* MCX_MAC_VIDEO_VGA_H_ */
