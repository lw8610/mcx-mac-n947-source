/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#if defined(CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE) || defined(CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE)
#include <fsl_common.h>
#endif
#if defined(CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE)
#include "emulator/mcx_opcode_profile.h"
#endif
#if defined(CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE)
#include "emulator/mcx_cycle_profile.h"
#endif
#if defined(CONFIG_MCX_MAC_USB_THREAD_PROFILE)
#include <string.h>
#endif

#include "emulator/m68k_bus.h"
#include "emulator/umac_memory.h"
#include "emulator/umac_timing.h"
#if defined(CONFIG_MCX_MAC_USB_IMAGE_TRANSFER)
#include "usb/umac_usb_transfer.h"
#endif
#if defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_PROBE)
#include "emulator/umac_external_flash_probe.h"
#endif
#if defined(CONFIG_MCX_MAC_UMAC_BOOT)
#include "emulator/umac_boot.h"
#include "emulator/umac_disk_overlay.h"
#include "emulator/umac_serial_input.h"
#include "emulator/umac_video.h"
#if defined(CONFIG_MCX_MAC_USB_MOUSE)
#include "emulator/umac_usb_mouse.h"
#endif
#include <m68k.h>
#include <umac.h>
#endif
#if defined(CONFIG_MCX_MAC_UMAC_SMOKE)
#include "emulator/umac_smoke.h"
#endif
#include "video/vga.h"

#ifndef MCX_MAC_GUEST_LOOP_MS
#define MCX_MAC_GUEST_LOOP_MS 5U
#endif

#if defined(CONFIG_MCX_MAC_UMAC_INSTR_COUNT)
/* Diagnostic only: the Musashi instruction hook increments this on CPU0. */
volatile unsigned int mcx_mac_guest_instructions;
#endif

#if defined(CONFIG_MCX_MAC_USB_THREAD_PROFILE)
struct usb_thread_snapshot {
	k_tid_t threads[32];
	size_t count;
};

static void collect_usb_thread(const struct k_thread *thread, void *user_data)
{
	struct usb_thread_snapshot *snapshot = user_data;

	if (snapshot->count < ARRAY_SIZE(snapshot->threads)) {
		snapshot->threads[snapshot->count++] = (k_tid_t)thread;
	}
}

static void report_usb_thread_cycles(uint32_t interval_ms)
{
	static const char *const names[] = {
		"uhc_mcux_ehci", "usbh", "usbh_bus"
	};
	static uint64_t previous[ARRAY_SIZE(names)];
	static bool seen[ARRAY_SIZE(names)];
	struct usb_thread_snapshot snapshot = { 0 };
	uint32_t thread_ms[ARRAY_SIZE(names)] = { 0 };
	uint32_t found = 0U;
	uint64_t total_cycles = 0U;
	uint32_t cycles_per_ms = sys_clock_hw_cycles_per_sec() / 1000U;

	/* Collect under the thread-list lock; query stats after it is released. */
	k_thread_foreach(collect_usb_thread, &snapshot);
	for (size_t i = 0U; i < snapshot.count; i++) {
		const char *name = k_thread_name_get(snapshot.threads[i]);
		k_thread_runtime_stats_t stats;

		if (name == NULL || k_thread_runtime_stats_get(snapshot.threads[i], &stats) != 0) {
			continue;
		}
		for (size_t j = 0U; j < ARRAY_SIZE(names); j++) {
			if (strcmp(name, names[j]) != 0) {
				continue;
			}
			uint64_t delta = seen[j] ? stats.execution_cycles - previous[j] : 0U;

			previous[j] = stats.execution_cycles;
			seen[j] = true;
			found |= BIT(j);
			thread_ms[j] = (uint32_t)(delta / cycles_per_ms);
			total_cycles += delta;
			break;
		}
	}
	printk("DIAG USB CPU interval_ms=%u found=%x ehci_ms=%u host_ms=%u "
	       "bus_ms=%u total_ms=%u pct_x10=%u%s\n",
	       interval_ms, found, thread_ms[0], thread_ms[1], thread_ms[2],
	       (uint32_t)(total_cycles / cycles_per_ms),
	       interval_ms == 0U ? 0U :
	       (uint32_t)(total_cycles * 1000000U /
			  ((uint64_t)interval_ms * sys_clock_hw_cycles_per_sec())),
	       (found == BIT_MASK(ARRAY_SIZE(names))) ? "" : " (incomplete)");
}
#endif

int main(void)
{
	struct vga_timing_report report;
	struct vga_smartdma_stats stats;
	struct umac_timing timing = { 0 };
	uint32_t umac_vsync_ticks = 0U;
	uint32_t umac_second_ticks = 0U;
	uint32_t next_report_ms;
#if defined(CONFIG_MCX_MAC_UMAC_BOOT)
	uint32_t guest_loops = 0U;
	uint32_t next_guest_ms = 0U;
	bool guest_active = false;
#endif
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
	uint64_t perf_sleep_cycles = 0U;
	uint64_t perf_raster_cycles = 0U;
	uint64_t perf_video_cycles = 0U;
	uint64_t perf_usb_poll_cycles = 0U;
	uint64_t perf_guest_cycles = 0U;
	uint32_t perf_guest_max_cycles = 0U;
	uint32_t perf_guest_calls = 0U;
	uint32_t perf_video_calls = 0U;
	uint32_t perf_last_report_ms;
#endif
#if defined(CONFIG_MCX_MAC_UMAC_INSTR_COUNT)
	uint32_t instr_last_report_ms;
#endif
#if defined(CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE) || defined(CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE)
	uint32_t profile_next_report_ms;
#endif
	int ret;

#if defined(CONFIG_MCX_MAC_USB_IMAGE_TRANSFER)
	/* The two roles share USB1 and must never own it simultaneously. */
	if (umac_usb_transfer_requested()) {
		return umac_usb_transfer_run();
	}
#endif

	printk("mcx_mac: Phase 3 uMac %uK %s\n",
	       (unsigned int)(UMAC_GUEST_RAM_BYTES / 1024U),
	       IS_ENABLED(CONFIG_MCX_MAC_UMAC_MEDIA_FROM_SLOT1) ?
	       "separately installed media (image-1 slot)" :
	       IS_ENABLED(CONFIG_MCX_MAC_USB_MOUSE) ?
	       "private ROM + writable System disk, generic USB HID keyboard/mouse" :
	       IS_ENABLED(CONFIG_MCX_MAC_UMAC_BOOT) ?
	       "private ROM + read-only System disk, UART RX diagnostic EXP-41" :
	       "synthetic CPU EXP-34");

#if defined(CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE)
	mcx_opcode_profile_init();
#elif defined(CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE)
	mcx_cycle_profile_init();
#endif
#if defined(CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE) || defined(CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE)
	printk("DIAG CACHE LPCAC_CTRL=%08x enabled=%u NVM_CTRL=%08x\n",
	       (unsigned int)SYSCON->LPCAC_CTRL,
	       (SYSCON->LPCAC_CTRL & SYSCON_LPCAC_CTRL_DIS_LPCAC_MASK) == 0U,
	       (unsigned int)SYSCON->NVM_CTRL);
#endif

	if (IS_ENABLED(CONFIG_MCX_MAC_M68K_BUS_DIAG) &&
	    !m68k_bus_self_test()) {
		printk("M68K bus self-test failed; CPU integration disabled\n");
	}
	if (!umac_guest_ram_prepare()) {
		printk("DIAG uMac guest RAM=%u result=FAIL\n",
		       (unsigned int)umac_guest_ram_size());
		return -1;
	}
	printk("DIAG uMac guest RAM=%u result=PASS\n",
	       (unsigned int)umac_guest_ram_size());
#if defined(CONFIG_MCX_MAC_EXTERNAL_FLASH_PROBE)
	(void)umac_external_flash_probe();
#endif
#if defined(CONFIG_MCX_MAC_UMAC_SMOKE)
	printk("DIAG uMac synthetic CPU=%s (not Macintosh boot)\n",
	       umac_smoke_test() ? "PASS" : "FAIL");
#endif

#if defined(CONFIG_MCX_MAC_USB_MOUSE)
	ret = umac_usb_mouse_init();
	if (ret != 0) {
		printk("DIAG USB HID unavailable; continuing VGA/uMac\n");
	} else {
		/* Give enumeration an uncontended window before FlexIO/EDMA
		 * scanout starts; this differentiates startup timing from VGA load. */
		printk("DIAG USB pre-video enumeration window start\n");
		k_sleep(K_SECONDS(3));
		umac_usb_mouse_report();
		(void)umac_usb_hid_bind_interfaces();
		printk("DIAG USB pre-video enumeration window end\n");
	}
#endif

	ret = vga_init();
	if (ret != 0) {
		printk("VGA init failed: %d\n", ret);
		return ret;
	}

	ret = vga_start();
	if (ret != 0) {
		printk("VGA start failed: %d\n", ret);
		return ret;
	}
#if defined(CONFIG_MCX_MAC_UMAC_BOOT)
	guest_active = umac_boot_init();
	next_guest_ms = k_uptime_get_32();
	printk("DIAG uMac boot init=%s disk=volatile-writable\n",
	       guest_active ? "PASS" : "FAIL");
	if (guest_active) {
		umac_serial_input_init();
	}
#endif

	vga_get_timing_report(&report);
	printk("Nominal HSYNC: %u.%03u kHz, VSYNC: %u.%03u Hz\n",
	       report.hsync_millihz / 1000000U,
	       (report.hsync_millihz / 1000U) % 1000U,
	       report.vsync_millihz / 1000U,
	       report.vsync_millihz % 1000U);
	printk("Probe PIO0_8 (HSYNC) and PIO0_9 (VSYNC)\n");
	printk("Framebuffer: 512x342 mono, %u bytes; video path: %s\n",
	       (unsigned int)vga_framebuffer_size(),
	       IS_ENABLED(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR) ?
	       "SmartDMA direct guest pixels, Flash schedule (no raster buffer)" :
	       IS_ENABLED(CONFIG_MCX_MAC_SMARTDMA_DIRECT) ?
	       "experimental one-frame SmartDMA raster (no line IRQ)" :
	       IS_ENABLED(CONFIG_MCX_MAC_CONTINUOUS_RASTER) ?
	       "continuous cyclic EDMA raster (no line IRQ)" :
	       IS_ENABLED(CONFIG_MCX_MAC_HARDWARE_VIDEO_DIAG) ?
	       "FlexIO hardware timing diagnostic (CPU and DMA bypassed)" :
	       IS_ENABLED(CONFIG_MCX_MAC_SOLID_VIDEO_DIAG) ?
	       "CPU solid diagnostic (DMA bypassed)" :
	       vga_smartdma_is_ready() ?
	       "SmartDMA self-test + linked EDMA scanout" : "not ready");
	printk("VIDEO: PIO2_8 (J8-13); %s\n",
	       IS_ENABLED(CONFIG_MCX_MAC_ANIMATED_TEST) ?
	       "vblank-updated moving checkerboard" :
	       IS_ENABLED(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR) ?
	       "512x342 guest framebuffer, direct scanout" :
	       IS_ENABLED(CONFIG_MCX_MAC_SMARTDMA_DIRECT) ?
	       "512x342 normal framebuffer, vblank commit, one-frame ring" :
	       IS_ENABLED(CONFIG_MCX_MAC_CONTINUOUS_RASTER) ?
	       "512x342 normal framebuffer, vblank commit, dual-frame FIFO" :
	       IS_ENABLED(CONFIG_MCX_MAC_HARDWARE_VIDEO_DIAG) ?
	       "hardware-timed horizontal window, no line IRQ" :
	       IS_ENABLED(CONFIG_MCX_MAC_SOLID_VIDEO_DIAG) ?
	       "solid rectangle, no framebuffer DMA" :
	       vga_smartdma_scanout_enabled() ?
	       "512x342 centered, static 32-pixel checkerboard" :
	       "disabled after diagnostic failure (sync only)");

	next_report_ms = k_uptime_get_32() + 5000U;
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
	perf_last_report_ms = k_uptime_get_32();
#endif
#if defined(CONFIG_MCX_MAC_UMAC_INSTR_COUNT)
	instr_last_report_ms = k_uptime_get_32();
#endif
#if defined(CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE) || defined(CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE)
	profile_next_report_ms = k_uptime_get_32() + 30000U;
#endif
	for (;;) {
		uint32_t now;
		struct umac_timing_events events;
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
		uint32_t perf_start = k_cycle_get_32();
#endif

#if defined(CONFIG_MCX_MAC_UMAC_YIELD_SCHED)
		if (guest_active) {
			k_yield();
		} else {
			k_sleep(K_MSEC(1));
		}
#elif defined(CONFIG_MCX_MAC_UMAC_FAST_SCHED)
		k_sleep(guest_active ? K_USEC(100) : K_MSEC(1));
#else
		k_sleep(K_MSEC(1));
#endif
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
		perf_sleep_cycles += k_cycle_get_32() - perf_start;
		perf_start = k_cycle_get_32();
#endif
		vga_smartdma_service();
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
		perf_raster_cycles += k_cycle_get_32() - perf_start;
#endif
		now = k_uptime_get_32();
		events = umac_timing_poll(&timing, vga_flexio_get_frame_count(), now);
		umac_vsync_ticks += events.vsync ? 1U : 0U;
		umac_second_ticks += events.second ? 1U : 0U;
#if defined(CONFIG_MCX_MAC_UMAC_BOOT)
		if (guest_active) {
			umac_serial_input_poll();
#if defined(CONFIG_MCX_MAC_USB_MOUSE)
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
			perf_start = k_cycle_get_32();
#endif
			umac_usb_mouse_poll();
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
			perf_usb_poll_cycles += k_cycle_get_32() - perf_start;
#endif
#endif
			if (!umac_serial_guest_paused() && events.vsync) {
				umac_vsync_event();
				if (!umac_serial_test_pattern_enabled()) {
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
					perf_start = k_cycle_get_32();
#endif
					ret = umac_video_present(umac_guest_ram_get(),
						 umac_guest_ram_size(),
						 umac_get_fb_offset());
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
					perf_video_cycles += k_cycle_get_32() - perf_start;
					perf_video_calls++;
#endif
					if (ret != 0) {
						printk("DIAG uMac video present failed: %d\n", ret);
						guest_active = false;
					}
				}
			}
			if (!umac_serial_guest_paused() && events.second) {
				umac_1hz_event();
			}
			if (guest_active && !umac_serial_guest_paused() &&
			    (int32_t)(now - next_guest_ms) >= 0) {
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
				perf_start = k_cycle_get_32();
#endif
				ret = umac_loop();
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
				uint32_t elapsed = k_cycle_get_32() - perf_start;

				perf_guest_cycles += elapsed;
				perf_guest_calls++;
				if (elapsed > perf_guest_max_cycles) {
					perf_guest_max_cycles = elapsed;
				}
#endif
				if (ret != 0) {
					printk("DIAG uMac guest stopped\n");
					guest_active = false;
				} else {
					guest_loops++;
					next_guest_ms += MCX_MAC_GUEST_LOOP_MS;
					if ((int32_t)(now - next_guest_ms) > 20) {
						next_guest_ms = now + MCX_MAC_GUEST_LOOP_MS;
					}
				}
			}
		}
#endif
	if ((int32_t)(now - next_report_ms) < 0) {
		continue;
	}
	next_report_ms += 5000U;
#if defined(CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE) || defined(CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE)
	if ((int32_t)(now - profile_next_report_ms) >= 0) {
		profile_next_report_ms += 30000U;
#if defined(CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE)
		mcx_opcode_profile_report();
#else
		mcx_cycle_profile_report();
#endif
	}
#endif
#if defined(CONFIG_MCX_MAC_UMAC_INSTR_COUNT)
	{
		uint32_t interval_ms = now - instr_last_report_ms;
		uint32_t instructions = mcx_mac_guest_instructions;

		mcx_mac_guest_instructions = 0U;
		instr_last_report_ms = now;
		/* instructions/ms has the same numeric value as kIPS. */
		printk("DIAG M68K interval_ms=%u instructions=%u kips=%u\n",
		       interval_ms, instructions,
		       interval_ms == 0U ? 0U : instructions / interval_ms);
	}
#endif
#if defined(CONFIG_MCX_MAC_USB_THREAD_PROFILE)
	report_usb_thread_cycles(5000U);
#endif
#if defined(CONFIG_MCX_MAC_UMAC_PERF_DIAG)
		const uint32_t cycles_per_ms = sys_clock_hw_cycles_per_sec() / 1000U;
		uint32_t guest_avg_us = perf_guest_calls == 0U ? 0U :
			(uint32_t)((perf_guest_cycles / perf_guest_calls) /
				     (cycles_per_ms / 1000U));

		printk("DIAG PERF interval_ms=%u guest_calls=%u guest_ms=%u "
		       "guest_avg/max_us=%u/%u video_calls=%u video_ms=%u "
		       "raster_ms=%u usb_poll_ms=%u sleep_ms=%u\n",
		       now - perf_last_report_ms, perf_guest_calls,
		       (uint32_t)(perf_guest_cycles / cycles_per_ms),
		       guest_avg_us,
		       perf_guest_max_cycles / (cycles_per_ms / 1000U),
		       perf_video_calls,
		       (uint32_t)(perf_video_cycles / cycles_per_ms),
		       (uint32_t)(perf_raster_cycles / cycles_per_ms),
		       (uint32_t)(perf_usb_poll_cycles / cycles_per_ms),
		       (uint32_t)(perf_sleep_cycles / cycles_per_ms));
		perf_last_report_ms = now;
		perf_sleep_cycles = 0U;
		perf_raster_cycles = 0U;
		perf_video_cycles = 0U;
		perf_usb_poll_cycles = 0U;
		perf_guest_cycles = 0U;
		perf_guest_max_cycles = 0U;
		perf_guest_calls = 0U;
		perf_video_calls = 0U;
#endif
		vga_smartdma_get_stats(&stats);
		vga_smartdma_linked_diag();
		vga_smartdma_phase_report();
		printk("DIAG RASTER freeze=%u shifter_errors=%08x\n",
		       vga_raster_is_frozen() ? 1U : 0U,
		       vga_flexio_take_shift_errors());
		printk("EDMA lines: %u start, %u complete, %u dropped, "
		       "%u errors, %u timeouts; sync frames: %u, recovered: %u, "
		       "raster commits: %u; "
		       "transfer us last/max: %u/%u\n",
		       stats.started_lines, stats.completed_lines,
		       stats.dropped_lines, stats.start_errors, stats.timeouts,
		       stats.sync_frames, stats.recovered_lines,
		       stats.raster_commits,
		       (uint32_t)(((uint64_t)stats.transfer_last_cycles * 1000000U) /
				  sys_clock_hw_cycles_per_sec()),
			(uint32_t)(((uint64_t)stats.transfer_max_cycles * 1000000U) /
				  sys_clock_hw_cycles_per_sec()));
#if defined(CONFIG_MCX_MAC_UMAC_BOOT)
	struct umac_mouse_diagnostics mouse_diag;

#if defined(CONFIG_MCX_MAC_UMAC_VIDEO_DIRTY_TRACK)
		struct umac_video_dirty_stats video_dirty;

		umac_video_get_dirty_stats(&video_dirty);
		printk("DIAG uMac video compared=%u skipped=%u changed=%u guest_writes=%u "
		       "periodic_checks=%u untracked_changes=%u\n",
		       video_dirty.compared, video_dirty.skipped,
		       video_dirty.changed, video_dirty.guest_writes,
		       video_dirty.periodic_checks, video_dirty.untracked_changes);
#endif

		umac_serial_input_report();
		umac_mouse_get_diagnostics(&mouse_diag);
		printk("DIAG uMac mouse input=%u,%u clipped=%u,%u steps=%u,%u "
		       "limit=%u irq_busy=%u pending=%d,%d\n",
		       mouse_diag.input_x, mouse_diag.input_y,
		       mouse_diag.clipped_x, mouse_diag.clipped_y,
		       mouse_diag.steps_x, mouse_diag.steps_y,
		       mouse_diag.limit_events, mouse_diag.irq_busy,
		       mouse_diag.pending_x, mouse_diag.pending_y);
		printk("DIAG uMac disk COW sectors=%u/%u writes=%u errors=%u (volatile)\n",
		       umac_disk_overlay_used(), UMAC_DISK_OVERLAY_SECTORS,
		       umac_disk_overlay_write_count(),
		       umac_disk_overlay_error_count());
#if defined(CONFIG_MCX_MAC_USB_MOUSE)
		umac_usb_mouse_report();
#endif
		printk("DIAG uMac clock vsync=%u second=%u guest=%s loops=%u pc=%06x\n",
		       umac_vsync_ticks, umac_second_ticks,
		       guest_active ? "active" : "inactive", guest_loops,
		       guest_active ? m68k_get_reg(NULL, M68K_REG_PC) : 0U);
#else
		printk("DIAG uMac clock vsync=%u second=%u guest=inactive\n",
		       umac_vsync_ticks, umac_second_ticks);
#endif
	}
}
