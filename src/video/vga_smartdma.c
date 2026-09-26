/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_mcux_smartdma.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include <fsl_inputmux.h>
#include <fsl_edma.h>
#include <fsl_reset.h>
#include <fsl_smartdma_fw.h>

#include "vga.h"
#include "vga_pixel_order.h"
#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
#include "vga_smartdma_guest_descriptors.h"
#endif

#define SMARTDMA_NODE DT_NODELABEL(smartdma)
#define EDMA_NODE DT_NODELABEL(edma0)
#define VGA_EDMA_CHANNEL 0U
#define VGA_EDMA_REQUEST 61U
#define SMARTDMA_RAMX_FIRMWARE_RESERVE 0x4000U

#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
extern const uint32_t vga_smartdma_guest_ring_start[];
extern const uint32_t vga_smartdma_guest_ring_end[];

struct vga_smartdma_guest_ring_param {
	const uint32_t *descriptors;
	const uint32_t *source;
	volatile uint32_t *shiftbuf;
	uint32_t words_per_frame;
	volatile uint32_t *remaining_words;
};
#else
extern const uint32_t vga_smartdma_ring_start[];
extern const uint32_t vga_smartdma_ring_end[];

struct vga_smartdma_ring_param {
	uint32_t *frame_words;
	uint32_t *shiftbuf;
	uint32_t words_per_frame;
	volatile uint32_t *remaining_words;
};
#endif

#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
static struct vga_smartdma_guest_ring_param direct_param;
#else
static struct vga_smartdma_ring_param direct_param;
#endif
static struct dma_config direct_dma_config;
static volatile uint32_t direct_remaining_words;
static bool direct_firmware_ready;
static bool direct_running;
static volatile uint32_t direct_commit_last_cycles;
static volatile uint32_t direct_commit_max_cycles;
static volatile uint32_t direct_commit_start_remaining;
static volatile uint32_t direct_commit_end_remaining;
static volatile uint32_t direct_commit_late_starts;
static volatile uint32_t direct_commit_late_ends;
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(sramx_app))
BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(sramx_app)) == 0x14004000U,
	     "App SRAMX must start after the SmartDMA firmware reservation");
#endif

#if defined(CONFIG_MCX_MAC_UMAC_HOT_OPS_RAMX)
BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(ramx_hot)),
	     "Hot opcode RAMX requires umac_hot_ops_ramx.overlay");
BUILD_ASSERT(DT_REG_SIZE(DT_NODELABEL(sramx_app)) == 0x8000U,
	     "Disk overlay must end before hot opcode RAMX");
BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(ramx_hot)) == 0x1400c000U,
	     "Hot opcode RAMX must follow the disk overlay");
BUILD_ASSERT(DT_REG_SIZE(DT_NODELABEL(ramx_hot)) == 0xc000U,
	     "Hot opcode RAMX must occupy the final 48 KiB");
#endif

#if defined(CONFIG_MCX_MAC_VIDEO_RAM_BANK)
BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(video_ram_bank)),
	     "VIDEO_RAM_BANK requires smartdma_video_bank.overlay");
BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(video_ram_bank)) == 0x30040000U,
	     "Video raster must occupy the final main-RAM bank");
BUILD_ASSERT(DT_REG_SIZE(DT_NODELABEL(video_ram_bank)) == 0x10000U,
	     "Video raster bank must be exactly 64 KiB");
#endif

static const struct device *const smartdma_dev = DEVICE_DT_GET(SMARTDMA_NODE);
static const struct device *const edma_dev = DEVICE_DT_GET(EDMA_NODE);
static DMA_Type *const edma_base = (DMA_Type *)DT_REG_ADDR(EDMA_NODE);

/* 21,888 bytes; small enough for internal SRAM and directly usable by umac. */
#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
/* Descriptor indices address aligned words: 0 is blank, 1..5472 pixels. */
static struct {
	uint32_t blank_word;
	uint8_t pixels[VGA_MAC_FRAMEBUFFER_BYTES];
} mac_framebuffer_source
#if defined(CONFIG_MCX_MAC_FRAMEBUFFER_SRAMX)
	__attribute__((section("SRAMX"), aligned(4)));
#else
	__aligned(4);
#endif
#define mac_framebuffer mac_framebuffer_source.pixels
#elif defined(CONFIG_MCX_MAC_FRAMEBUFFER_SRAMX)
/* CPU-side shadow for the pre-expanded raster modes. */
static uint8_t mac_framebuffer[VGA_MAC_FRAMEBUFFER_BYTES]
	__attribute__((section("SRAMX"), aligned(4)));
#else
static uint8_t mac_framebuffer[VGA_MAC_FRAMEBUFFER_BYTES] __aligned(4);
#endif
/* The memory-to-memory routine needs more stack than the display driver.
 * Reserve 128 bytes, as in the NXP-contributed Zephyr SmartDMA sample.
 */
static uint32_t smartdma_stack[32];
static struct dma_config edma_config;
static struct dma_block_config edma_block;
static atomic_t transfer_busy;
static atomic_t started_lines;
static atomic_t completed_lines;
static atomic_t dropped_lines;
static atomic_t start_errors;
static atomic_t timeouts;
static uint32_t transfer_started_ms;
static uint32_t transfer_started_cycles;
static atomic_t transfer_last_cycles;
static atomic_t transfer_max_cycles;
static atomic_t raster_commits;
static atomic_t framebuffer_dirty;
static atomic_t raster_frozen;
static bool smartdma_ready;
static atomic_t scanout_enabled;
static atomic_t selftest_active;
static atomic_t selftest_done;

#if defined(CONFIG_MCX_MAC_CONTINUOUS_RASTER)
#define VGA_RASTER_STRIDE_BYTES (VGA_H_TOTAL_PIXELS / 8U)
#define VGA_RASTER_FRAME_BYTES \
	(VGA_RASTER_STRIDE_BYTES * VGA_V_TOTAL_LINES)
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
#define VGA_RASTER_FRAME_COUNT 1U
#else
#define VGA_RASTER_FRAME_COUNT 2U
#endif
#define VGA_RASTER_BYTES \
	(VGA_RASTER_FRAME_BYTES * VGA_RASTER_FRAME_COUNT)
#define VGA_RASTER_H_OFFSET_BYTES \
	((VGA_H_SYNC_PIXELS + VGA_H_BACK_PORCH_PIXELS + \
	  ((VGA_H_ACTIVE_PIXELS - VGA_MAC_WIDTH) / 2U)) / 8U)
#define VGA_RASTER_V_OFFSET_LINES \
	(VGA_V_SYNC_LINES + VGA_V_BACK_PORCH_LINES + \
	 ((VGA_V_ACTIVE_LINES - VGA_MAC_HEIGHT) / 2U))

#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
/* The full-frame raster is intentionally absent in direct-guest mode. */
#elif defined(CONFIG_MCX_MAC_VIDEO_RAM_BANK)
static uint8_t continuous_raster[VGA_RASTER_BYTES]
	__attribute__((section("VIDEO_RAM"), aligned(32)));
#else
static uint8_t continuous_raster[VGA_RASTER_BYTES] __aligned(32) __nocache;
#endif
#if !defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
static edma_tcd_t continuous_tcd __aligned(32) __nocache;
static bool continuous_tcd_ready;
#endif
static uint32_t continuous_last_sync_frame;
/* Captured at each CTIMER VSYNC edge. A stable picture requires the DMA
 * source position to return to the same point in its 525-line frame. */
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
#define VGA_RASTER_ITERATIONS_PER_FRAME (VGA_RASTER_FRAME_BYTES / 4U)
#else
#define VGA_RASTER_ITERATIONS_PER_FRAME (VGA_RASTER_FRAME_BYTES / 8U)
#endif
static volatile uint32_t phase_samples;
static volatile uint32_t phase_last_citer;
static uint32_t phase_first;
static volatile int32_t phase_drift_min;
static volatile int32_t phase_drift_max;
static volatile uint32_t phase_largest_step;
static volatile uint32_t phase_steps_over_two;
static uint32_t phase_previous;
#endif

#if defined(CONFIG_MCX_MAC_EDMA_LINKED)
/* Each display row uses four scatter/gather descriptors on channel 0.
 * Unblank releases the forced-low VIDEO pin, margin emits 64 black pixels,
 * line writes 16 packed words in 64-pixel FIFO refills, and gate forces VIDEO low then
 * clears SHIFTSDEN. The next unblank TCD waits for the next HSYNC. */
static edma_tcd_t linked_unblank_tcd[VGA_MAC_HEIGHT] __aligned(32) __nocache;
static edma_tcd_t linked_margin_tcd[VGA_MAC_HEIGHT] __aligned(32) __nocache;
static edma_tcd_t linked_line_tcd[VGA_MAC_HEIGHT] __aligned(32) __nocache;
static edma_tcd_t linked_gate_tcd[VGA_MAC_HEIGHT] __aligned(32) __nocache;
static uint32_t linked_unblank_value __aligned(4) __nocache;
static uint32_t linked_margin_words[2] __aligned(4) __nocache;
static uint32_t linked_gate_values[2] __aligned(4) __nocache;
static bool linked_tcd_ready;
static bool linked_frame_started;
#endif

/* Black RGB565 input and a guarded, oversized RGB888 destination. 64 input
 * bytes should yield 96 output bytes; extra capacity also bounds diagnosis
 * if an SDK firmware variant interprets buffersize as a pixel count.
 */
static uint32_t test_input[32];
static uint8_t test_output[256] __aligned(4);
static smartdma_rgb565_rgb888_param_t test_param;

void vga_flexio_video_abort(void);
void vga_flexio_edma_request_enable(bool enable);
void vga_flexio_pixel_request_enable(bool enable);
void vga_flexio_video_override_enable(bool enable);
uint32_t vga_flexio_shiftbuf_address(void);
uint32_t vga_flexio_shiftbuf_bitbyte_swapped_address(void);
uint32_t vga_flexio_shift_dma_enable_address(void);
uint32_t vga_flexio_pin_override_address(void);
uint32_t vga_flexio_video_override_word(bool enabled);

static void smartdma_callback(const struct device *dev, void *user_data,
			      uint32_t channel, int status)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	ARG_UNUSED(channel);
	ARG_UNUSED(status);

	atomic_set(&selftest_done, 1);
}

static void vga_edma_callback(const struct device *dev, void *user_data,
			      uint32_t channel, int status)
{
	uint32_t elapsed_cycles;
	atomic_val_t previous_max;

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	ARG_UNUSED(channel);

	vga_flexio_edma_request_enable(false);

	if (atomic_get(&transfer_busy) == 0) {
		return;
	}
	elapsed_cycles = k_cycle_get_32() - transfer_started_cycles;
	atomic_set(&transfer_last_cycles, (atomic_val_t)elapsed_cycles);
	previous_max = atomic_get(&transfer_max_cycles);
	if (elapsed_cycles > (uint32_t)previous_max) {
		atomic_set(&transfer_max_cycles, (atomic_val_t)elapsed_cycles);
	}
	if (status < 0) {
		atomic_inc(&start_errors);
	} else {
		atomic_inc(&completed_lines);
	}
	atomic_clear(&transfer_busy);
}

static bool firmware_matches(void)
{
	const volatile uint8_t *ram =
		(const volatile uint8_t *)DT_PROP(SMARTDMA_NODE, program_mem);

	for (uint32_t i = 0; i < s_smartdmaDisplayFirmwareSize; i++) {
		if (ram[i] != s_smartdmaDisplayFirmware[i]) {
			return false;
		}
	}
	return true;
}

static bool run_memory_selftest(void)
{
	struct dma_config config = {
		.dma_slot = kSMARTDMA_RGB565To888,
		.channel_direction = MEMORY_TO_MEMORY,
		.block_count = 1U,
		.dma_callback = smartdma_callback,
		.head_block = (struct dma_block_config *)&test_param,
	};
	int ret;
	unsigned int zero_prefix = 0U;
	bool irq_done;
	bool firmware_ok;
	bool output_ok = true;

	memset(test_output, 0xa5, sizeof(test_output));
	test_param.inBuf = test_input;
	test_param.outBuf = (uint32_t *)test_output;
	test_param.buffersize = 64U;
	test_param.smartdma_stack = smartdma_stack;
	atomic_set(&selftest_active, 1);
	atomic_clear(&selftest_done);
	ret = dma_config(smartdma_dev, 0U, &config);
	firmware_ok = firmware_matches();
	printk("DIAG MEM config=%d fw_after_reset=%s param=%08x in=%08x out=%08x\n",
	       ret, firmware_ok ? "OK" : "BAD",
	       (uint32_t)&test_param, (uint32_t)test_input, (uint32_t)test_output);
	if (ret != 0 || !firmware_ok) {
		atomic_clear(&selftest_active);
		return false;
	}
	__DSB();
	ret = dma_start(smartdma_dev, 0U);
	if (ret == 0) {
		/* No FlexIO or sync IRQs are enabled during this bounded test. */
		for (int i = 0; i < 20 && atomic_get(&selftest_done) == 0; i++) {
			k_sleep(K_MSEC(1));
		}
	}

	unsigned int key = irq_lock();

	irq_done = atomic_get(&selftest_done) != 0;
	vga_diag_capture("memory self-test, before reset");
	if (ret == 0 && !irq_done) {
		(void)dma_stop(smartdma_dev, 0U);
		NVIC_ClearPendingIRQ((IRQn_Type)DT_IRQN(SMARTDMA_NODE));
	}
	atomic_clear(&selftest_active);
	irq_unlock(key);
	__DSB();
	while (zero_prefix < sizeof(test_output) &&
	       ((volatile uint8_t *)test_output)[zero_prefix] == 0U) {
		zero_prefix++;
	}
	/* Require the documented byte-count result and an intact tail guard. */
	for (size_t i = 96; i < sizeof(test_output); i++) {
		if (((volatile uint8_t *)test_output)[i] != 0xa5) {
			output_ok = false;
		}
	}
	output_ok = output_ok && zero_prefix == 96U;
	printk("DIAG MEM start=%d irq=%u zero_bytes=%u expected=96 result=%s\n",
	       ret, irq_done, zero_prefix,
	       ret == 0 && irq_done && output_ok ? "PASS" : "FAIL");
	vga_diag_print();
	return ret == 0 && irq_done && output_ok;
}

uint8_t *vga_framebuffer_get(void)
{
	return mac_framebuffer;
}

size_t vga_framebuffer_size(void)
{
	return sizeof(mac_framebuffer);
}

void vga_framebuffer_clear(bool white)
{
	/* Classic Mac 1bpp stores black as 1. The serializer inverts the pin,
	 * therefore an all-zero framebuffer is the visible white level. */
	memset(mac_framebuffer, white ? 0x00 : 0xff,
	       sizeof(mac_framebuffer));
	vga_framebuffer_present();
}

void vga_framebuffer_test_pattern_phase(uint32_t phase)
{
	uint32_t x;
	uint32_t y;
	uint32_t shift = (phase & 3U) * 8U;

	/* Shift the 32-pixel checkerboard in byte-sized steps so updates remain
	 * naturally aligned with the packed 1bpp framebuffer. */
	for (y = 0U; y < VGA_MAC_HEIGHT; y++) {
		for (x = 0U; x < VGA_MAC_WIDTH; x += 8U) {
			bool white =
				(((((x + shift) / 32U) + (y / 32U)) & 1U) != 0U);

			/* Classic Mac 1bpp uses one bits for black. The FlexIO
			 * video pin is inverted so black is VGA level 0 V. */
			mac_framebuffer[(y * VGA_MAC_STRIDE_BYTES) + (x / 8U)] =
				white ? 0x00 : 0xff;
		}
		/* Finish every active line at black before horizontal blanking. */
		mac_framebuffer[(y * VGA_MAC_STRIDE_BYTES) +
				(VGA_MAC_STRIDE_BYTES - 1U)] = 0xff;
	}
	vga_framebuffer_present();
}

void vga_framebuffer_test_pattern(void)
{
	vga_framebuffer_test_pattern_phase(0U);
}

void vga_framebuffer_present(void)
{
	/* Writers call this after completing a 512x342 packed-1bpp update.
	 * The scanout service consumes the flag only at the next VSYNC. */
	atomic_set(&framebuffer_dirty, 1);
}

void vga_raster_freeze(bool enabled)
{
#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
	ARG_UNUSED(enabled);
	/* There is no intermediate raster to freeze in this mode. */
	printk("DIAG raster freeze unavailable with direct framebuffer scanout\n");
#else
	atomic_set(&raster_frozen, enabled ? 1 : 0);
	printk("DIAG raster freeze=%u (guest continues)\n", enabled ? 1U : 0U);
#endif
}

bool vga_raster_is_frozen(void)
{
	return atomic_get(&raster_frozen) != 0;
}

bool vga_smartdma_is_ready(void)
{
	return smartdma_ready;
}

bool vga_smartdma_scanout_enabled(void)
{
	return atomic_get(&scanout_enabled) != 0;
}

#if defined(CONFIG_MCX_MAC_CONTINUOUS_RASTER)
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
static bool direct_install_firmware(void)
{
#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
	const uint32_t *const code_start = vga_smartdma_guest_ring_start;
	const uintptr_t code_bytes = (uintptr_t)vga_smartdma_guest_ring_end -
		(uintptr_t)vga_smartdma_guest_ring_start;
#else
	const uint32_t *const code_start = vga_smartdma_ring_start;
	const uintptr_t code_bytes = (uintptr_t)vga_smartdma_ring_end -
		(uintptr_t)vga_smartdma_ring_start;
#endif
	uint32_t firmware[128] = {0};
	const uint32_t firmware_address =
		DT_PROP(SMARTDMA_NODE, program_mem);

	if (code_bytes == 0U || (code_bytes & 3U) != 0U ||
	    code_bytes + sizeof(uint32_t) > sizeof(firmware) ||
	    code_bytes + sizeof(uint32_t) > SMARTDMA_RAMX_FIRMWARE_RESERVE) {
		printk("DIAG SmartDMA direct firmware size invalid: %u\n",
		       (unsigned int)code_bytes);
		return false;
	}
	/* These bytes are copied before SmartDMA starts. Reject a linker layout
	 * that places their input section in RAM without a startup copy. */
	const uintptr_t flash_end = (uintptr_t)CONFIG_FLASH_BASE_ADDRESS +
		CONFIG_FLASH_SIZE * 1024U;

	if ((uintptr_t)code_start < CONFIG_FLASH_BASE_ADDRESS ||
	    (uintptr_t)code_start > flash_end ||
	    code_bytes > flash_end - (uintptr_t)code_start) {
		printk("DIAG SmartDMA direct firmware source outside Flash: %p\n",
		       code_start);
		return false;
	}
	firmware[0] = firmware_address + sizeof(uint32_t);
	memcpy(&firmware[1], code_start, code_bytes);
	dma_smartdma_install_fw(smartdma_dev, (uint8_t *)firmware,
				 code_bytes + sizeof(uint32_t));
	direct_firmware_ready =
		memcmp((void *)firmware_address, firmware,
		       code_bytes + sizeof(uint32_t)) == 0;
	printk("DIAG SmartDMA direct firmware bytes=%u readback=%s\n",
	       (unsigned int)(code_bytes + sizeof(uint32_t)),
	       direct_firmware_ready ? "PASS" : "FAIL");
	return direct_firmware_ready;
}
#endif

static void continuous_commit_framebuffer(void)
{
#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
	/* SmartDMA reads the guest pixels as it scans. No expanded copy exists. */
	__DSB();
	atomic_inc(&raster_commits);
#else
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
	uint32_t start_cycles = k_cycle_get_32();
	uint32_t start_frame = vga_flexio_get_frame_count();
	uint32_t start_remaining = direct_remaining_words;
#endif
	for (uint32_t frame = 0U; frame < VGA_RASTER_FRAME_COUNT; frame++) {
		uint32_t frame_offset = frame * VGA_RASTER_FRAME_BYTES;

		for (uint32_t y = 0U; y < VGA_MAC_HEIGHT; y++) {
			uint32_t dst = frame_offset +
				(VGA_RASTER_V_OFFSET_LINES + y) *
				VGA_RASTER_STRIDE_BYTES +
				VGA_RASTER_H_OFFSET_BYTES;
			uint32_t src = y * VGA_MAC_STRIDE_BYTES;

			for (uint32_t x = 0U; x < VGA_MAC_STRIDE_BYTES; x += 4U) {
				uint32_t mac_word;

				memcpy(&mac_word, &mac_framebuffer[src + x], 4U);
#if defined(CONFIG_MCX_MAC_SMARTDMA_RAW_PIXEL_ALIAS)
				memcpy(&continuous_raster[dst + x], &mac_word, 4U);
#else
				uint32_t flexio_word = vga_flexio_pixel_word(mac_word);

				memcpy(&continuous_raster[dst + x], &flexio_word, 4U);
#endif
			}
		}
	}
	__DSB();
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
	if (direct_running) {
		/* The first Mac row begins after 104 complete VGA lines. A
		 * commit finishing later can race the visible scanout. Capture
		 * both the CPU copy time and the SmartDMA source phase. */
		const uint32_t first_mac_row_remaining =
			VGA_RASTER_ITERATIONS_PER_FRAME -
			VGA_RASTER_V_OFFSET_LINES *
			(VGA_RASTER_STRIDE_BYTES / 4U);
		uint32_t cycles = k_cycle_get_32() - start_cycles;
		uint32_t end_remaining = direct_remaining_words;

		direct_commit_last_cycles = cycles;
		if (cycles > direct_commit_max_cycles) {
			direct_commit_max_cycles = cycles;
		}
		direct_commit_start_remaining = start_remaining;
		direct_commit_end_remaining = end_remaining;
		if (start_remaining < first_mac_row_remaining) {
			direct_commit_late_starts++;
		}
		if (end_remaining < first_mac_row_remaining ||
		    vga_flexio_get_frame_count() != start_frame) {
			direct_commit_late_ends++;
		}
	}
#endif
	atomic_inc(&raster_commits);
#endif
}

static void continuous_prepare_raster(void)
{
#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
	mac_framebuffer_source.blank_word = UINT32_MAX;
#else
	memset(continuous_raster, 0xff, sizeof(continuous_raster));
#endif
	atomic_clear(&raster_commits);
	continuous_commit_framebuffer();
	atomic_clear(&framebuffer_dirty);
	continuous_last_sync_frame = 0U;
	phase_samples = 0U;
	phase_last_citer = 0U;
	phase_first = 0U;
	phase_drift_min = 0;
	phase_drift_max = 0;
	phase_largest_step = 0U;
	phase_steps_over_two = 0U;
	phase_previous = 0U;
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
	direct_remaining_words = VGA_RASTER_ITERATIONS_PER_FRAME;
	direct_commit_last_cycles = 0U;
	direct_commit_max_cycles = 0U;
	direct_commit_start_remaining = 0U;
	direct_commit_end_remaining = 0U;
	direct_commit_late_starts = 0U;
	direct_commit_late_ends = 0U;
#endif
}

#if !defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
static void continuous_prepare_tcd(void)
{
	edma_transfer_config_t config;
	uint16_t csr;
	bool valid;

	memset(&config, 0, sizeof(config));
	EDMA_PrepareTransferConfig(
		&config, continuous_raster, 4U, 4,
		(void *)vga_flexio_shiftbuf_address(), 4U, 4,
		8U, sizeof(continuous_raster));
	config.enabledInterruptMask = 0U;
	EDMA_TcdSetTransferConfigExt(edma_base, &continuous_tcd, &config,
				     &continuous_tcd);
	EDMA_TcdSetModuloExt(edma_base, &continuous_tcd,
			     kEDMA_ModuloDisable, kEDMA_Modulo8bytes);
	EDMA_TcdEnableAutoStopRequestExt(edma_base, &continuous_tcd, false);
	EDMA_SetChannelMux(edma_base, VGA_EDMA_CHANNEL, VGA_EDMA_REQUEST);
	__DSB();
	csr = EDMA_TCD_CSR(&continuous_tcd, EDMA_TCD_TYPE(edma_base));
	valid =
		EDMA_TCD_SADDR(&continuous_tcd, EDMA_TCD_TYPE(edma_base)) ==
			(uint32_t)continuous_raster &&
		EDMA_TCD_DADDR(&continuous_tcd, EDMA_TCD_TYPE(edma_base)) ==
			vga_flexio_shiftbuf_address() &&
		EDMA_TCD_SOFF(&continuous_tcd, EDMA_TCD_TYPE(edma_base)) == 4U &&
		EDMA_TCD_DOFF(&continuous_tcd, EDMA_TCD_TYPE(edma_base)) == 4U &&
		EDMA_TCD_NBYTES(&continuous_tcd, EDMA_TCD_TYPE(edma_base)) == 8U &&
		EDMA_TCD_CITER(&continuous_tcd, EDMA_TCD_TYPE(edma_base)) ==
			(sizeof(continuous_raster) / 8U) &&
		EDMA_TCD_DLAST_SGA(&continuous_tcd, EDMA_TCD_TYPE(edma_base)) ==
			(uint32_t)&continuous_tcd &&
		(csr & DMA_CSR_ESG_MASK) != 0U &&
		(csr & (DMA_CSR_DREQ_MASK | DMA_CSR_INTMAJOR_MASK |
			DMA_CSR_INTHALF_MASK)) == 0U;
	continuous_tcd_ready = valid;
	printk("DIAG EDMA continuous bytes=%u words=%u fifo_words=2 frames=2 "
	       "stride=%u offset=%u,%u validate=%s\n",
	       (uint32_t)sizeof(continuous_raster),
	       (uint32_t)sizeof(continuous_raster) / 4U,
	       VGA_RASTER_STRIDE_BYTES, VGA_RASTER_H_OFFSET_BYTES,
	       VGA_RASTER_V_OFFSET_LINES, valid ? "PASS" : "FAIL");
}
#endif
#endif

#if defined(CONFIG_MCX_MAC_CONTINUOUS_RASTER)
static int32_t phase_difference(uint32_t current, uint32_t reference)
{
	int32_t difference = (int32_t)current - (int32_t)reference;
	const int32_t period = (int32_t)VGA_RASTER_ITERATIONS_PER_FRAME;

	if (difference > period / 2) {
		difference -= period;
	} else if (difference < -(period / 2)) {
		difference += period;
	}
	return difference;
}
#endif

void vga_smartdma_vsync_sample(void)
{
#if defined(CONFIG_MCX_MAC_CONTINUOUS_RASTER)
	uint32_t remaining;
	uint32_t phase;
	int32_t drift;
	int32_t step;

	if (!vga_smartdma_scanout_enabled()) {
		return;
	}
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
	if (!direct_running) {
		return;
	}
	remaining = direct_remaining_words;
	if (remaining == 0U || remaining > VGA_RASTER_ITERATIONS_PER_FRAME) {
		return;
	}
#else
	if (!continuous_tcd_ready) {
		return;
	}
	remaining = EDMA_GetRemainingMajorLoopCount(edma_base, VGA_EDMA_CHANNEL);
#endif
	phase = remaining % VGA_RASTER_ITERATIONS_PER_FRAME;
	phase_last_citer = remaining;
	if (phase_samples == 0U) {
		phase_first = phase;
		phase_previous = phase;
	} else {
		step = phase_difference(phase, phase_previous);
		if ((uint32_t)(step < 0 ? -step : step) > phase_largest_step) {
			phase_largest_step = (uint32_t)(step < 0 ? -step : step);
		}
		if (step > 2 || step < -2) {
			phase_steps_over_two++;
		}
		phase_previous = phase;
	}
	drift = phase_difference(phase, phase_first);
	if (drift < phase_drift_min) {
		phase_drift_min = drift;
	}
	if (drift > phase_drift_max) {
		phase_drift_max = drift;
	}
	phase_samples++;
#endif
}

void vga_smartdma_phase_report(void)
{
#if defined(CONFIG_MCX_MAC_CONTINUOUS_RASTER)
	/* The VSYNC ISR only writes word-sized fields; this read-only diagnostic
	 * does not stall the raster or change its TCD. */
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
	printk("DIAG SMARTDMA raster phase samples=%u remaining=%u modulo=%u "
	       "drift_min/max=%d/%d largest_step=%u steps_gt2=%u "
	       "trap=%08x shift_error=%08x\n",
	       phase_samples, phase_last_citer,
	       phase_last_citer % VGA_RASTER_ITERATIONS_PER_FRAME,
	       phase_drift_min, phase_drift_max,
	       phase_largest_step, phase_steps_over_two,
	       SMARTDMA0->PENDTRAP, FLEXIO0->SHIFTERR);
	printk("DIAG SMARTDMA commit cycles last/max=%u/%u "
	       "remaining start/end=%u/%u late start/end=%u/%u\n",
	       direct_commit_last_cycles, direct_commit_max_cycles,
	       direct_commit_start_remaining, direct_commit_end_remaining,
	       direct_commit_late_starts, direct_commit_late_ends);
#else
	printk("DIAG RASTER phase samples=%u citer=%u modulo=%u "
	       "drift_min/max=%d/%d largest_step=%u steps_gt2=%u dma_error=%08x\n",
	       phase_samples, phase_last_citer,
	       phase_last_citer % VGA_RASTER_ITERATIONS_PER_FRAME,
	       phase_drift_min, phase_drift_max,
	       phase_largest_step, phase_steps_over_two,
	       EDMA_GetErrorStatusFlags(edma_base));
#endif
#endif
}

#if defined(CONFIG_MCX_MAC_EDMA_LINKED)
static void linked_prepare_tcds(void)
{
	edma_transfer_config_t unblank_config;
	edma_transfer_config_t margin_config;
	edma_transfer_config_t line_config;
	edma_transfer_config_t gate_config;
	const uint32_t pin_override = vga_flexio_pin_override_address();
	const uint32_t shift_dma_enable =
		vga_flexio_shift_dma_enable_address();
	const int16_t gate_dest_offset =
		(int16_t)(shift_dma_enable - pin_override);

	linked_unblank_value = vga_flexio_video_override_word(false);
	linked_margin_words[0] = 0xffffffffU;
	linked_margin_words[1] = 0xffffffffU;
	linked_gate_values[0] = vga_flexio_video_override_word(true);
	linked_gate_values[1] = 0U;

	for (uint32_t i = 0U; i < VGA_MAC_HEIGHT; i++) {
		EDMA_PrepareTransfer(
			&unblank_config, &linked_unblank_value, 4U,
			(void *)pin_override, 4U, 4U, 4U,
			kEDMA_MemoryToPeripheral);
		unblank_config.enabledInterruptMask = 0U;
		EDMA_TcdSetTransferConfigExt(
			edma_base, &linked_unblank_tcd[i], &unblank_config,
			&linked_margin_tcd[i]);

		EDMA_PrepareTransferConfig(
			&margin_config, linked_margin_words, 4U, 4,
			(void *)vga_flexio_shiftbuf_address(), 4U, 4,
			8U, sizeof(linked_margin_words));
		margin_config.enabledInterruptMask = 0U;
		EDMA_TcdSetTransferConfigExt(
			edma_base, &linked_margin_tcd[i], &margin_config,
			&linked_line_tcd[i]);
		EDMA_TcdSetModuloExt(edma_base, &linked_margin_tcd[i],
				     kEDMA_ModuloDisable, kEDMA_Modulo8bytes);

		memset(&line_config, 0, sizeof(line_config));
		EDMA_PrepareTransferConfig(
			&line_config,
			&mac_framebuffer[i * VGA_MAC_STRIDE_BYTES], 4U, 4,
			(void *)vga_flexio_shiftbuf_address(), 4U, 4,
			8U, VGA_MAC_STRIDE_BYTES);
		/* EDMA_PrepareTransfer() enables INTMAJOR by default. Static
		 * scatter/gather scanout has no per-TCD callback and must not
		 * generate unowned DMA interrupts per frame. */
		line_config.enabledInterruptMask = 0U;
		EDMA_TcdSetTransferConfigExt(
			edma_base, &linked_line_tcd[i], &line_config,
			&linked_gate_tcd[i]);
		EDMA_TcdSetModuloExt(edma_base, &linked_line_tcd[i],
				     kEDMA_ModuloDisable, kEDMA_Modulo8bytes);

		/* One FlexIO request performs two 32-bit writes: first force the
		 * VIDEO pin to its preloaded low GPIO value, then clear SHIFTSDEN.
		 * The destination offset is 0x30 - 0x64 = -52 bytes. */
		EDMA_PrepareTransferConfig(
			&gate_config, linked_gate_values, 4U, 4,
			(void *)pin_override, 4U, gate_dest_offset, 8U, 8U);
		gate_config.enabledInterruptMask = 0U;
		EDMA_TcdSetTransferConfigExt(
			edma_base, &linked_gate_tcd[i], &gate_config,
			(i + 1U < VGA_MAC_HEIGHT) ?
			&linked_unblank_tcd[i + 1U] : NULL);
		if (i + 1U == VGA_MAC_HEIGHT) {
			EDMA_TcdEnableAutoStopRequestExt(
				edma_base, &linked_gate_tcd[i], true);
		}
	}

	EDMA_SetChannelMux(edma_base, VGA_EDMA_CHANNEL, VGA_EDMA_REQUEST);
	__DSB();
	linked_tcd_ready = true;
}

static bool linked_validate_tcds(void)
{
	bool ok = true;
	const uint32_t shiftbuf = vga_flexio_shiftbuf_address();
	const uint32_t pin_override = vga_flexio_pin_override_address();
	const int16_t gate_dest_offset = (int16_t)(
		vga_flexio_shift_dma_enable_address() - pin_override);

	for (uint32_t i = 0U; i < VGA_MAC_HEIGHT; i++) {
		const uint32_t expected_unblank_next =
			(uint32_t)&linked_margin_tcd[i];
		const uint32_t expected_margin_next =
			(uint32_t)&linked_line_tcd[i];
		const uint32_t expected_line_next =
			(uint32_t)&linked_gate_tcd[i];
		const uint32_t expected_gate_next =
			(i + 1U < VGA_MAC_HEIGHT) ?
			(uint32_t)&linked_unblank_tcd[i + 1U] : 0U;
		const uint32_t expected_source =
			(uint32_t)&mac_framebuffer[i * VGA_MAC_STRIDE_BYTES];
		const uint16_t unblank_csr =
			EDMA_TCD_CSR(&linked_unblank_tcd[i],
				     EDMA_TCD_TYPE(edma_base));
		const uint16_t margin_csr =
			EDMA_TCD_CSR(&linked_margin_tcd[i],
				     EDMA_TCD_TYPE(edma_base));
		const uint16_t line_csr =
			EDMA_TCD_CSR(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base));
		const uint16_t gate_csr =
			EDMA_TCD_CSR(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base));

		if (EDMA_TCD_SADDR(&linked_unblank_tcd[i], EDMA_TCD_TYPE(edma_base)) !=
			(uint32_t)&linked_unblank_value ||
		    EDMA_TCD_DADDR(&linked_unblank_tcd[i], EDMA_TCD_TYPE(edma_base)) !=
			pin_override ||
		    EDMA_TCD_NBYTES(&linked_unblank_tcd[i], EDMA_TCD_TYPE(edma_base)) != 4U ||
		    EDMA_TCD_CITER(&linked_unblank_tcd[i], EDMA_TCD_TYPE(edma_base)) != 1U ||
		    EDMA_TCD_DLAST_SGA(&linked_unblank_tcd[i], EDMA_TCD_TYPE(edma_base)) !=
			expected_unblank_next ||
		    (unblank_csr & DMA_CSR_ESG_MASK) == 0U ||
		    (unblank_csr & (DMA_CSR_INTMAJOR_MASK | DMA_CSR_INTHALF_MASK |
				     DMA_CSR_MAJORELINK_MASK)) != 0U ||
		    EDMA_TCD_SADDR(&linked_margin_tcd[i], EDMA_TCD_TYPE(edma_base)) !=
			(uint32_t)linked_margin_words ||
		    EDMA_TCD_DADDR(&linked_margin_tcd[i], EDMA_TCD_TYPE(edma_base)) !=
			shiftbuf ||
		    EDMA_TCD_SOFF(&linked_margin_tcd[i], EDMA_TCD_TYPE(edma_base)) != 4U ||
		    EDMA_TCD_DOFF(&linked_margin_tcd[i], EDMA_TCD_TYPE(edma_base)) != 4U ||
		    EDMA_TCD_NBYTES(&linked_margin_tcd[i], EDMA_TCD_TYPE(edma_base)) != 8U ||
		    EDMA_TCD_CITER(&linked_margin_tcd[i], EDMA_TCD_TYPE(edma_base)) != 1U ||
		    EDMA_TCD_DLAST_SGA(&linked_margin_tcd[i], EDMA_TCD_TYPE(edma_base)) !=
			expected_margin_next ||
		    (margin_csr & DMA_CSR_ESG_MASK) == 0U ||
		    (margin_csr & (DMA_CSR_INTMAJOR_MASK | DMA_CSR_INTHALF_MASK |
				    DMA_CSR_MAJORELINK_MASK)) != 0U ||
		    EDMA_TCD_SADDR(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)) != expected_source ||
		    EDMA_TCD_DADDR(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)) != shiftbuf ||
		    EDMA_TCD_SOFF(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)) != 4U ||
		    EDMA_TCD_DOFF(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)) != 4U ||
		    EDMA_TCD_NBYTES(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)) != 8U ||
		    EDMA_TCD_CITER(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)) != 8U ||
		    EDMA_TCD_DLAST_SGA(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)) != expected_line_next ||
		    (line_csr & DMA_CSR_ESG_MASK) == 0U ||
		    (line_csr & (DMA_CSR_INTMAJOR_MASK | DMA_CSR_INTHALF_MASK |
				 DMA_CSR_MAJORELINK_MASK)) != 0U ||
		    EDMA_TCD_SADDR(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)) !=
			(uint32_t)linked_gate_values ||
		    EDMA_TCD_DADDR(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)) !=
			pin_override ||
		    EDMA_TCD_SOFF(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)) != 4U ||
		    EDMA_TCD_DOFF(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)) !=
			(uint16_t)gate_dest_offset ||
		    EDMA_TCD_NBYTES(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)) != 8U ||
		    EDMA_TCD_CITER(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)) != 1U ||
		    EDMA_TCD_DLAST_SGA(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)) !=
			expected_gate_next ||
		    (gate_csr & (DMA_CSR_INTMAJOR_MASK | DMA_CSR_INTHALF_MASK |
				 DMA_CSR_MAJORELINK_MASK)) != 0U ||
		    (i + 1U < VGA_MAC_HEIGHT &&
		     (gate_csr & DMA_CSR_ESG_MASK) == 0U)) {
			printk("DIAG TCD fail row=%u csr unblank/margin/line/gate="
			       "%04x/%04x/%04x/%04x\n",
			       i, unblank_csr, margin_csr, line_csr, gate_csr);
			printk("DIAG TCD fail row=%u line src=%08x/%08x dst=%08x/%08x "
			       "soff=%u doff=%u nbytes=%u citer=%u next=%08x/%08x csr=%04x\n",
			       i,
			       EDMA_TCD_SADDR(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       expected_source,
			       EDMA_TCD_DADDR(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       shiftbuf,
			       EDMA_TCD_SOFF(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       EDMA_TCD_DOFF(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       EDMA_TCD_NBYTES(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       EDMA_TCD_CITER(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       EDMA_TCD_DLAST_SGA(&linked_line_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       expected_line_next, line_csr);
			printk("DIAG TCD fail row=%u gate src=%08x/%08x dst=%08x/%08x "
			       "soff=%u doff=%u nbytes=%u citer=%u next=%08x/%08x csr=%04x\n",
			       i,
			       EDMA_TCD_SADDR(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       (uint32_t)linked_gate_values,
			       EDMA_TCD_DADDR(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       pin_override,
			       EDMA_TCD_SOFF(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       EDMA_TCD_DOFF(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       EDMA_TCD_NBYTES(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       EDMA_TCD_CITER(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       EDMA_TCD_DLAST_SGA(&linked_gate_tcd[i], EDMA_TCD_TYPE(edma_base)),
			       expected_gate_next, gate_csr);
			ok = false;
			break;
		}
	}

	if ((EDMA_TCD_CSR(&linked_unblank_tcd[VGA_MAC_HEIGHT - 1U],
			    EDMA_TCD_TYPE(edma_base)) & DMA_CSR_DREQ_MASK) != 0U ||
	    (EDMA_TCD_CSR(&linked_margin_tcd[VGA_MAC_HEIGHT - 1U],
			    EDMA_TCD_TYPE(edma_base)) & DMA_CSR_DREQ_MASK) != 0U ||
	    (EDMA_TCD_CSR(&linked_line_tcd[VGA_MAC_HEIGHT - 1U],
			    EDMA_TCD_TYPE(edma_base)) & DMA_CSR_DREQ_MASK) != 0U ||
	    (EDMA_TCD_CSR(&linked_gate_tcd[VGA_MAC_HEIGHT - 1U],
			    EDMA_TCD_TYPE(edma_base)) & DMA_CSR_DREQ_MASK) == 0U) {
		printk("DIAG TCD final DREQ unblank=%04x margin=%04x line=%04x "
		       "gate=%04x\n",
		       EDMA_TCD_CSR(&linked_unblank_tcd[VGA_MAC_HEIGHT - 1U],
				    EDMA_TCD_TYPE(edma_base)),
		       EDMA_TCD_CSR(&linked_margin_tcd[VGA_MAC_HEIGHT - 1U],
				    EDMA_TCD_TYPE(edma_base)),
		       EDMA_TCD_CSR(&linked_line_tcd[VGA_MAC_HEIGHT - 1U],
				    EDMA_TCD_TYPE(edma_base)),
		       EDMA_TCD_CSR(&linked_gate_tcd[VGA_MAC_HEIGHT - 1U],
				    EDMA_TCD_TYPE(edma_base)));
		ok = false;
	}

	printk("DIAG EDMA linked validate=%s lines=%u words=%u fifo_words=2 tcds_per_line=4\n",
	       ok ? "PASS" : "FAIL", VGA_MAC_HEIGHT, 16U);
	return ok;
}

static void linked_start_frame(void)
{
	EDMA_DisableChannelRequest(edma_base, VGA_EDMA_CHANNEL);
	EDMA_ClearChannelStatusFlags(edma_base, VGA_EDMA_CHANNEL,
				    kEDMA_DoneFlag | kEDMA_ErrorFlag | kEDMA_InterruptFlag);
	EDMA_InstallTCD(edma_base, VGA_EDMA_CHANNEL, &linked_unblank_tcd[0]);
	__DSB();
	EDMA_EnableChannelRequest(edma_base, VGA_EDMA_CHANNEL);
	linked_frame_started = true;
}
#endif

/*
 * Phase 2 boundary. Zephyr initializes SmartDMA from the board devicetree.
 *
 * SmartDMA is verified here for future framebuffer conversion. Real-time
 * monochrome scanout uses eDMA to copy packed 1bpp words into SHIFTBUF0.
 */
int vga_smartdma_prepare(void)
{
	if (!device_is_ready(smartdma_dev) || !device_is_ready(edma_dev)) {
		return -ENODEV;
	}
	smartdma_ready = true;
	printk("DIAG chip DIEID=%08x rev=%u SDMA=%08x fw_addr=%08x fw_bytes=%u\n",
	       SYSCON->DIEID, Chip_GetVersion(), (uint32_t)DT_REG_ADDR(SMARTDMA_NODE),
	       (uint32_t)DT_PROP(SMARTDMA_NODE, program_mem), s_smartdmaDisplayFirmwareSize);
	printk("DIAG security master=%08x antipol=%08x misc=%08x RAMA=%08x\n",
	       AHBSC->MASTER_SEC_LEVEL, AHBSC->MASTER_SEC_ANTI_POL_REG,
	       AHBSC->MISC_CTRL_REG, AHBSC->RAMA_MEM_RULE);
	if (Chip_GetVersion() == 0U) {
		printk("DIAG A0 silicon: NXP example requires A1; video disabled\n");
		return 0;
	}

	vga_framebuffer_test_pattern();

	/* Required by the MCXN947 SDK before programming INPUTMUX routes. */
	RESET_ClearPeripheralReset(kMUX_RST_SHIFT_RSTn);
	INPUTMUX_Init(INPUTMUX0);
	INPUTMUX_AttachSignal(INPUTMUX0, 0U, kINPUTMUX_FlexioToSmartDma);
	printk("DIAG INPUTMUX route0=%08x (expected 0)\n",
	       INPUTMUX0->SMARTDMAARCHB_INMUX[0]);
	INPUTMUX_Deinit(INPUTMUX0);

	if (s_smartdmaDisplayFirmwareSize > SMARTDMA_RAMX_FIRMWARE_RESERVE) {
		printk("DIAG SmartDMA firmware exceeds reserved RAMX; video disabled\n");
		return 0;
	}
	dma_smartdma_install_fw(smartdma_dev,
				(uint8_t *)s_smartdmaDisplayFirmware,
				s_smartdmaDisplayFirmwareSize);
	if (!firmware_matches()) {
		printk("DIAG firmware readback FAILED; video disabled\n");
		return 0;
	}
	if (!run_memory_selftest()) {
		printk("DIAG memory test failed; sync only, no video retries\n");
		return 0;
	}

	memset(&edma_config, 0, sizeof(edma_config));
	memset(&edma_block, 0, sizeof(edma_block));
	edma_config.dma_slot = VGA_EDMA_REQUEST;
	edma_config.channel_direction = MEMORY_TO_PERIPHERAL;
	edma_config.source_data_size = 4U;
	edma_config.dest_data_size = 4U;
	edma_config.source_burst_length = 4U;
	edma_config.block_count = 1U;
	edma_config.dma_callback = vga_edma_callback;
	edma_config.head_block = &edma_block;
	edma_block.dest_address = vga_flexio_shiftbuf_address();
	edma_block.block_size = VGA_MAC_STRIDE_BYTES;
	edma_block.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	edma_block.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	printk("DIAG EDMA base=%08x channel=%u request=%u dest=%08x\n",
	       (uint32_t)edma_base, VGA_EDMA_CHANNEL, VGA_EDMA_REQUEST,
	       edma_block.dest_address);

#if defined(CONFIG_MCX_MAC_CONTINUOUS_RASTER)
	continuous_prepare_raster();
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
	if (!direct_install_firmware()) {
		printk("DIAG SmartDMA direct firmware validation failed; video disabled\n");
		atomic_clear(&scanout_enabled);
		return 0;
	}
#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
	direct_param.descriptors = vga_smartdma_guest_descriptors;
	direct_param.source = &mac_framebuffer_source.blank_word;
	direct_param.shiftbuf =
		(uint32_t *)vga_flexio_shiftbuf_bitbyte_swapped_address();
#else
	direct_param.frame_words = (uint32_t *)continuous_raster;
#if defined(CONFIG_MCX_MAC_SMARTDMA_RAW_PIXEL_ALIAS)
	direct_param.shiftbuf =
		(uint32_t *)vga_flexio_shiftbuf_bitbyte_swapped_address();
#else
	direct_param.shiftbuf = (uint32_t *)vga_flexio_shiftbuf_address();
#endif
#endif
	direct_param.words_per_frame = VGA_RASTER_ITERATIONS_PER_FRAME;
	direct_param.remaining_words = &direct_remaining_words;
	memset(&direct_dma_config, 0, sizeof(direct_dma_config));
	direct_dma_config.dma_slot = 0U;
	direct_dma_config.channel_direction = MEMORY_TO_MEMORY;
	direct_dma_config.block_count = 1U;
	direct_dma_config.head_block =
		(struct dma_block_config *)&direct_param;
#if defined(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
	printk("DIAG SmartDMA guest descriptor flash_bytes=%u raster_ram=0 "
	       "words=%u framebuffer=%u source=%08x progress=%08x\n",
	       (unsigned int)sizeof(vga_smartdma_guest_descriptors),
	       VGA_RASTER_ITERATIONS_PER_FRAME,
	       (unsigned int)sizeof(mac_framebuffer),
	       (unsigned int)(uintptr_t)&mac_framebuffer_source.blank_word,
	       (unsigned int)(uintptr_t)&direct_remaining_words);
#else
	printk("DIAG SmartDMA direct raster bytes=%u words=%u "
	       "framebuffer=%u source=%08x progress=%08x\n",
	       (unsigned int)sizeof(continuous_raster),
	       VGA_RASTER_ITERATIONS_PER_FRAME,
	       (unsigned int)sizeof(mac_framebuffer),
	       (unsigned int)(uintptr_t)continuous_raster,
	       (unsigned int)(uintptr_t)&direct_remaining_words);
#endif
#else
	continuous_prepare_tcd();
	if (!continuous_tcd_ready) {
		printk("DIAG continuous TCD validation failed; video disabled\n");
		atomic_clear(&scanout_enabled);
		return 0;
	}
#endif
#endif

#if defined(CONFIG_MCX_MAC_EDMA_LINKED)
	linked_prepare_tcds();
	printk("DIAG EDMA linked rows=%u tcds_per_line=4 channel=%u "
	       "blank_dest=%08x gate_dest=%08x\n",
	       VGA_MAC_HEIGHT, VGA_EDMA_CHANNEL,
	       vga_flexio_pin_override_address(),
	       vga_flexio_shift_dma_enable_address());
	if (!linked_validate_tcds()) {
		printk("DIAG EDMA linked TCD validation failed; video disabled\n");
		atomic_clear(&scanout_enabled);
		return 0;
	}
#endif

	atomic_set(&scanout_enabled, 1);
	return 0;
}

int vga_smartdma_start_line(uint16_t display_line)
{
	int ret;

	if (!vga_smartdma_scanout_enabled()) {
		return -EACCES;
	}

#if defined(CONFIG_MCX_MAC_EDMA_LINKED)
	if (!linked_tcd_ready) {
		return -EACCES;
	}
	if (display_line == 0U || !linked_frame_started) {
		linked_start_frame();
	}
	vga_flexio_edma_request_enable(true);
	atomic_inc(&started_lines);
	if (display_line != 0U) {
		atomic_inc(&completed_lines);
	}
	return 0;
#endif

	if (!atomic_cas(&transfer_busy, 0, 1)) {
		atomic_inc(&dropped_lines);
		return -EBUSY;
	}

	edma_block.source_address = (uint32_t)&mac_framebuffer[
		display_line * VGA_MAC_STRIDE_BYTES];

	ret = dma_config(edma_dev, VGA_EDMA_CHANNEL, &edma_config);
	if (ret != 0) {
		atomic_inc(&start_errors);
		atomic_clear(&transfer_busy);
		return ret;
	}
	/* Present the dedicated FlexIO DMA request before enabling the channel. */
	transfer_started_ms = k_uptime_get_32();
	transfer_started_cycles = k_cycle_get_32();
	atomic_inc(&started_lines);
	vga_flexio_edma_request_enable(true);
	__DSB();
	ret = dma_start(edma_dev, VGA_EDMA_CHANNEL);
	if (ret != 0) {
		atomic_inc(&start_errors);
		atomic_clear(&transfer_busy);
		vga_flexio_edma_request_enable(false);
		return ret;
	}

	return 0;
}

int vga_smartdma_start_continuous(void)
{
#if defined(CONFIG_MCX_MAC_CONTINUOUS_RASTER)
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
	int ret;

	if (!vga_smartdma_scanout_enabled() || !direct_firmware_ready) {
		return -EACCES;
	}
	RESET_ClearPeripheralReset(kMUX_RST_SHIFT_RSTn);
	INPUTMUX_Init(INPUTMUX0);
	INPUTMUX_AttachSignal(INPUTMUX0, 0U, kINPUTMUX_FlexioToSmartDma);
	direct_remaining_words = VGA_RASTER_ITERATIONS_PER_FRAME;
	ret = dma_config(smartdma_dev, 0U, &direct_dma_config);
	if (ret == 0) {
		ret = dma_start(smartdma_dev, 0U);
	}
	if (ret != 0) {
		INPUTMUX_Deinit(INPUTMUX0);
		return ret;
	}
	direct_running = true;
	vga_flexio_video_override_enable(false);
	vga_flexio_pixel_request_enable(true);
	printk("DIAG SmartDMA direct armed; VIDEO starts with FlexIO\n");
	return 0;
#else
	if (!vga_smartdma_scanout_enabled() || !continuous_tcd_ready) {
		return -EACCES;
	}

	EDMA_DisableChannelRequest(edma_base, VGA_EDMA_CHANNEL);
	EDMA_ClearChannelStatusFlags(edma_base, VGA_EDMA_CHANNEL,
				    kEDMA_DoneFlag | kEDMA_ErrorFlag |
				    kEDMA_InterruptFlag);
	EDMA_InstallTCD(edma_base, VGA_EDMA_CHANNEL, &continuous_tcd);
	vga_flexio_video_override_enable(false);
	vga_flexio_edma_request_enable(true);
	__DSB();
	EDMA_EnableChannelRequest(edma_base, VGA_EDMA_CHANNEL);
	printk("DIAG EDMA continuous armed; VIDEO starts with FlexIO\n");
	return 0;
#endif
#else
	return -ENOTSUP;
#endif
}

void vga_smartdma_get_stats(struct vga_smartdma_stats *stats)
{
	if (stats == NULL) {
		return;
	}

	stats->started_lines = (uint32_t)atomic_get(&started_lines);
	stats->completed_lines = (uint32_t)atomic_get(&completed_lines);
	stats->dropped_lines = (uint32_t)atomic_get(&dropped_lines);
	stats->start_errors = (uint32_t)atomic_get(&start_errors);
	stats->timeouts = (uint32_t)atomic_get(&timeouts);
	stats->sync_frames = vga_flexio_get_frame_count();
	stats->recovered_lines = vga_flexio_get_recovered_line_count();
	stats->raster_commits = (uint32_t)atomic_get(&raster_commits);
	stats->transfer_last_cycles =
		(uint32_t)atomic_get(&transfer_last_cycles);
	stats->transfer_max_cycles =
		(uint32_t)atomic_get(&transfer_max_cycles);
}

void vga_smartdma_linked_diag(void)
{
#if defined(CONFIG_MCX_MAC_EDMA_LINKED)
	printk("DIAG LINK ch0 flags=%08x remain=%u error=%08x\n",
	       EDMA_GetChannelStatusFlags(edma_base, VGA_EDMA_CHANNEL),
	       EDMA_GetRemainingMajorLoopCount(edma_base, VGA_EDMA_CHANNEL),
	       EDMA_GetErrorStatusFlags(edma_base));
#endif
}

void vga_smartdma_service(void)
{
	bool failed = false;

#if defined(CONFIG_MCX_MAC_CONTINUOUS_RASTER)
	if (vga_smartdma_scanout_enabled()) {
		uint32_t sync_frame = vga_flexio_get_frame_count();

		if (sync_frame != continuous_last_sync_frame) {
			continuous_last_sync_frame = sync_frame;
			if (!vga_raster_is_frozen() &&
			    atomic_cas(&framebuffer_dirty, 1, 0)) {
				continuous_commit_framebuffer();
			}
		}
	}
#endif

	unsigned int key = irq_lock();

	/* Snapshot and stop once. Do not erase evidence in a repeated reset loop.
	 * The lock prevents a completion ISR racing with stop/PM-lock release.
	 */
	if (vga_smartdma_scanout_enabled() && atomic_get(&transfer_busy) != 0 &&
	    (uint32_t)(k_uptime_get_32() - transfer_started_ms) > 20U) {
		atomic_clear(&scanout_enabled);
		vga_diag_capture("video timeout, before reset");
		(void)dma_stop(edma_dev, VGA_EDMA_CHANNEL);
		vga_flexio_edma_request_enable(false);
		atomic_clear(&transfer_busy);
		atomic_inc(&timeouts);
		vga_flexio_video_abort();
		failed = true;
	}
	irq_unlock(key);
	if (failed) {
		vga_diag_print();
		printk("DIAG video stopped after timeout; sync resumed, no retries\n");
	}
}

void vga_smartdma_stop(void)
{
	if (!smartdma_ready) {
		return;
	}

	unsigned int key = irq_lock();

	atomic_clear(&scanout_enabled);
#if defined(CONFIG_MCX_MAC_EDMA_LINKED)
	EDMA_DisableChannelRequest(edma_base, VGA_EDMA_CHANNEL);
	linked_frame_started = false;
#endif
#if defined(CONFIG_MCX_MAC_CONTINUOUS_RASTER)
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
	if (direct_running) {
		vga_flexio_pixel_request_enable(false);
		(void)dma_stop(smartdma_dev, 0U);
		direct_running = false;
		INPUTMUX_Deinit(INPUTMUX0);
	}
#else
	EDMA_DisableChannelRequest(edma_base, VGA_EDMA_CHANNEL);
	vga_flexio_edma_request_enable(false);
	continuous_tcd_ready = false;
#endif
#endif
	if (atomic_get(&transfer_busy) != 0) {
		(void)dma_stop(edma_dev, VGA_EDMA_CHANNEL);
		vga_flexio_edma_request_enable(false);
		atomic_clear(&transfer_busy);
	}
	vga_flexio_video_abort();
	irq_unlock(key);
}
