/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdbool.h>

#include <zephyr/sys/util.h>

#include "vga.h"

int vga_flexio_init(void);
int vga_flexio_start(void);
void vga_flexio_stop(void);
void vga_flexio_get_timing_report(struct vga_timing_report *report);
int vga_smartdma_prepare(void);
bool vga_smartdma_is_ready(void);

int vga_init(void)
{
	int ret;

	ret = vga_flexio_init();
	if (ret != 0) {
		return ret;
	}

	if (!IS_ENABLED(CONFIG_MCX_MAC_SOLID_VIDEO_DIAG) &&
	    !IS_ENABLED(CONFIG_MCX_MAC_HARDWARE_VIDEO_DIAG)) {
		/* Prepare the Phase 2 framebuffer and verify the SmartDMA device. */
		ret = vga_smartdma_prepare();
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

int vga_start(void)
{
	return vga_flexio_start();
}

void vga_stop(void)
{
	vga_smartdma_stop();
	vga_flexio_stop();
}

void vga_get_timing_report(struct vga_timing_report *report)
{
	vga_flexio_get_timing_report(report);
}
