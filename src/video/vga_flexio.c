/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/misc/nxp_flexio/nxp_flexio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>

#include <fsl_clock.h>
#include <fsl_ctimer.h>
#include <fsl_flexio.h>

#include "vga.h"

int vga_smartdma_start_continuous(void);

#define FLEXIO_NODE DT_NODELABEL(flexio0)
#define VGA_PINCTRL_NODE DT_NODELABEL(mcx_mac_vga_pins)

/* EXP-18 keeps the hardware-verified EXP-15 clock tree. PLL0 is 150 MHz;
 * a divide-by-3 FlexIO root gives an exact 25 MHz pixel beat. */
#define VGA_FLEXIO_SOURCE_HZ       150000000U
#define VGA_FLEXIO_ROOT_DIVIDER            3U
#define VGA_TIMER_PRESCALER                16U

/* 50 MHz / 16 / 100 = 31.25 kHz; / 525 = 59.5238 Hz. */
#define VGA_HSYNC_TIMER_TICKS              100U
#define VGA_HSYNC_LOW_TICKS                 12U
#define VGA_HSYNC_HIGH_TICKS \
	(VGA_HSYNC_TIMER_TICKS - VGA_HSYNC_LOW_TICKS)

/* EXP-25 uses timer 1 as a second line-locked PWM. Both timer periods total
 * exactly 100 divided FlexIO clocks, so VIDEO cannot drift relative to HSYNC.
 * The 36-tick low interval covers blanking; the 64-tick high interval is a
 * 20.48 us diagnostic window. */
#define VGA_VIDEO_LOW_TICKS                  36U
#define VGA_VIDEO_HIGH_TICKS                 64U

#define VGA_HSYNC_FLEXIO_PIN                 0U
#define VGA_VSYNC_FLEXIO_PIN                 1U
#define VGA_VIDEO_FLEXIO_PIN                16U
#define VGA_PIXEL_DELAY_US                   1U
#define VGA_ACTIVE_FIRST_LINE \
	(VGA_V_SYNC_LINES + VGA_V_BACK_PORCH_LINES)
#define VGA_MAC_TOP_MARGIN \
	((VGA_V_ACTIVE_LINES - VGA_MAC_HEIGHT) / 2U)
#define VGA_MAC_FIRST_LINE \
	(VGA_ACTIVE_FIRST_LINE + VGA_MAC_TOP_MARGIN)
#define VGA_FLEXIO_SHIFTER_COUNT             8U

/* CTIMER0 shares PLL0 with FlexIO. It generates only the two VSYNC edges per
 * frame, so VSYNC no longer depends on the 31.25 kHz FlexIO line ISR. */
#define VGA_VSYNC_TIMER_BASE             CTIMER0
#define VGA_VSYNC_TIMER_IRQ              CTIMER0_IRQn
#define VGA_VSYNC_TIMER_CLOCK_HZ       150000000U
#define VGA_LINE_CYCLES                    4800U
#define VGA_VSYNC_LOW_CYCLES \
	(VGA_LINE_CYCLES * VGA_V_SYNC_LINES)
#define VGA_FRAME_CYCLES \
	(VGA_LINE_CYCLES * VGA_V_TOTAL_LINES)

PINCTRL_DT_DEFINE(VGA_PINCTRL_NODE);

static const struct device *const flexio_dev = DEVICE_DT_GET(FLEXIO_NODE);
static FLEXIO_Type *const flexio_base = (FLEXIO_Type *)DT_REG_ADDR(FLEXIO_NODE);
static const struct pinctrl_dev_config *const flexio_pincfg =
	PINCTRL_DT_DEV_CONFIG_GET(VGA_PINCTRL_NODE);

static uint8_t shifter_index[VGA_FLEXIO_SHIFTER_COUNT];
static uint8_t timer_index[2];
static volatile uint16_t current_line;
static atomic_t sync_frames;
static atomic_t recovered_lines;
static uint32_t last_line_cycle;
static uint32_t line_cycle_remainder;
static bool initialized;
static bool running;

static uint32_t vga_line_cycle_count(void)
{
	return sys_clock_hw_cycles_per_sec() / 31250U;
}

static void vga_vsync_timer_isr(const void *arg)
{
	uint32_t flags = CTIMER_GetStatusFlags(VGA_VSYNC_TIMER_BASE);

	ARG_UNUSED(arg);
	CTIMER_ClearStatusFlags(VGA_VSYNC_TIMER_BASE, flags);

	if ((flags & kCTIMER_Match0Flag) != 0U) {
		/* End the two-line active-low VSYNC pulse. */
		FLEXIO_PinWrite(flexio_base, VGA_VSYNC_FLEXIO_PIN, 1U);
	}
	if ((flags & kCTIMER_Match1Flag) != 0U) {
		/* MR1 resets CTIMER0, beginning the next frame at an exact PLL0
		 * interval. Realign the software scanline bookkeeping here too. */
		FLEXIO_PinWrite(flexio_base, VGA_VSYNC_FLEXIO_PIN, 0U);
		current_line = 0U;
		vga_smartdma_vsync_sample();
		atomic_inc(&sync_frames);
	}
}

static void advance_vga_line(void)
{
	current_line++;
	if (current_line >= VGA_V_TOTAL_LINES) {
		current_line = 0U;
	}
}

static int vga_flexio_line_isr(void *user_data)
{
	uint32_t timer_mask = BIT(timer_index[0]);
	uint32_t now;
	uint32_t elapsed_cycles;
	uint32_t elapsed_lines;
	uint32_t line_cycles = vga_line_cycle_count();

	ARG_UNUSED(user_data);
	/* Ignore any FlexIO source other than the completed HSYNC line timer. */
	if ((FLEXIO_GetTimerStatusFlags(flexio_base) & timer_mask) == 0U) {
		return 0;
	}

	FLEXIO_ClearTimerStatusFlags(flexio_base, timer_mask);

	/* The timer request is temporarily masked while SmartDMA owns the shared
	 * FlexIO request line. Recover any line events lost during that interval
	 * from the 150 MHz system cycle counter. Both clocks derive from PLL0.
	 */
	now = k_cycle_get_32();
	elapsed_cycles = now - last_line_cycle;
	last_line_cycle = now;
	line_cycle_remainder += elapsed_cycles;
	elapsed_lines = line_cycle_remainder / line_cycles;
	if (elapsed_lines == 0U) {
		/* An IRQ represents at least one completed line. */
		elapsed_lines = 1U;
	} else {
		line_cycle_remainder -= elapsed_lines * line_cycles;
	}
	if (elapsed_lines > 1U) {
		atomic_add(&recovered_lines, elapsed_lines - 1U);
	}
	for (uint32_t i = 0U; i < elapsed_lines; i++) {
		advance_vga_line();
	}

	if (IS_ENABLED(CONFIG_MCX_MAC_SOLID_VIDEO_DIAG)) {
		if ((current_line >= VGA_MAC_FIRST_LINE) &&
		    (current_line < (VGA_MAC_FIRST_LINE + VGA_MAC_HEIGHT))) {
			/* Stay black through sync/back porch, then hold a solid level
			 * for about 500 pixels. No DMA or shifter participates. */
			k_busy_wait(4U);
			FLEXIO_PinWrite(flexio_base, VGA_VIDEO_FLEXIO_PIN, 1U);
			k_busy_wait(20U);
			FLEXIO_PinWrite(flexio_base, VGA_VIDEO_FLEXIO_PIN, 0U);
		} else {
			FLEXIO_PinWrite(flexio_base, VGA_VIDEO_FLEXIO_PIN, 0U);
		}
		return 0;
	}

	if (vga_smartdma_scanout_enabled() &&
	    (current_line >= VGA_MAC_FIRST_LINE) &&
	    (current_line < (VGA_MAC_FIRST_LINE + VGA_MAC_HEIGHT))) {
		/* Start the transfer immediately after HSYNC. The previous delay
		 * left too little of the 32 us line for the measured worst-case
		 * transfer. Horizontal centering will be refined after scanout
		 * completion timing is stable.
		 */
		k_busy_wait(VGA_PIXEL_DELAY_US);
		(void)vga_smartdma_start_line(current_line - VGA_MAC_FIRST_LINE);
	}

	return 0;
}

static const struct nxp_flexio_child vga_flexio_child = {
	.isr = vga_flexio_line_isr,
	.user_data = NULL,
	.res = {
		.shifter_index = shifter_index,
		.shifter_count = ARRAY_SIZE(shifter_index),
		.timer_index = timer_index,
		.timer_count = ARRAY_SIZE(timer_index),
	},
};

static int validate_flexio_resources(void)
{
	uint8_t i;

	/* NXP's MCXN947 display firmware is fixed to shifters 0 through 7. */
	for (i = 0U; i < ARRAY_SIZE(shifter_index); i++) {
		if (shifter_index[i] != i) {
			return -ENOTSUP;
		}
	}

	return 0;
}

static void configure_vsync_pin(void)
{
	flexio_gpio_config_t pin_config = {
		.pinDirection = kFLEXIO_DigitalOutput,
		.outputLogic = 1U,
		.inputConfig = 0U,
	};

	FLEXIO_SetPinConfig(flexio_base, VGA_VSYNC_FLEXIO_PIN, &pin_config);
	FLEXIO_PinWrite(flexio_base, VGA_VSYNC_FLEXIO_PIN, 1U);
}

static void configure_vsync_timer(void)
{
	ctimer_config_t timer_config;
	ctimer_match_config_t match_config = {
		.enableCounterReset = false,
		.enableCounterStop = false,
		.outControl = kCTIMER_Output_NoAction,
		.outPinInitState = false,
		.enableInterrupt = true,
	};

	CTIMER_GetDefaultConfig(&timer_config);
	CTIMER_Init(VGA_VSYNC_TIMER_BASE, &timer_config);

	match_config.matchValue = VGA_VSYNC_LOW_CYCLES - 1U;
	CTIMER_SetupMatch(VGA_VSYNC_TIMER_BASE, kCTIMER_Match_0,
			  &match_config);

	match_config.matchValue = VGA_FRAME_CYCLES - 1U;
	match_config.enableCounterReset = true;
	CTIMER_SetupMatch(VGA_VSYNC_TIMER_BASE, kCTIMER_Match_1,
			  &match_config);

	IRQ_CONNECT(VGA_VSYNC_TIMER_IRQ, 0, vga_vsync_timer_isr, NULL, 0);
	irq_disable(VGA_VSYNC_TIMER_IRQ);
	CTIMER_ClearStatusFlags(VGA_VSYNC_TIMER_BASE,
				kCTIMER_Match0Flag | kCTIMER_Match1Flag);
}

static void configure_video_pin_black(void)
{
	flexio_gpio_config_t pin_config = {
		.pinDirection = kFLEXIO_DigitalOutput,
		.outputLogic = 0U,
		.inputConfig = 0U,
	};

	/* Keep video black until the Phase 2 SmartDMA serializer is started. */
	FLEXIO_SetPinConfig(flexio_base, VGA_VIDEO_FLEXIO_PIN, &pin_config);
	FLEXIO_PinWrite(flexio_base, VGA_VIDEO_FLEXIO_PIN, 0U);
}

static void configure_hsync_timer(void)
{
	flexio_timer_config_t timer_config;

	memset(&timer_config, 0, sizeof(timer_config));
	timer_config.triggerSource = kFLEXIO_TimerTriggerSourceInternal;
	timer_config.pinConfig = kFLEXIO_PinConfigOutput;
	timer_config.pinSelect = VGA_HSYNC_FLEXIO_PIN;
	/* PWMLow already produces the negative sync pulse; do not invert it again. */
	timer_config.pinPolarity = kFLEXIO_PinActiveHigh;
	timer_config.timerMode = kFLEXIO_TimerModeDual8BitPWMLow;
	timer_config.timerOutput = kFLEXIO_TimerOutputZeroNotAffectedByReset;
	timer_config.timerDecrement =
		kFLEXIO_TimerDecSrcDiv16OnFlexIOClockShiftTimerOutput;
	timer_config.timerReset = kFLEXIO_TimerResetNever;
	timer_config.timerDisable = kFLEXIO_TimerDisableNever;
	timer_config.timerEnable = kFLEXIO_TimerEnabledAlways;
	timer_config.timerStop = kFLEXIO_TimerStopBitDisabled;
	timer_config.timerStart = kFLEXIO_TimerStartBitDisabled;
	timer_config.timerCompare =
		((VGA_HSYNC_HIGH_TICKS - 1U) << 8) |
		(VGA_HSYNC_LOW_TICKS - 1U);

	FLEXIO_SetTimerConfig(flexio_base, timer_index[0], &timer_config);
}

static void configure_hardware_video_timer(void)
{
	flexio_timer_config_t timer_config;

	memset(&timer_config, 0, sizeof(timer_config));
	timer_config.triggerSource = kFLEXIO_TimerTriggerSourceInternal;
	timer_config.pinConfig = kFLEXIO_PinConfigOutput;
	timer_config.pinSelect = VGA_VIDEO_FLEXIO_PIN;
	timer_config.pinPolarity = kFLEXIO_PinActiveHigh;
	timer_config.timerMode = kFLEXIO_TimerModeDual8BitPWMLow;
	timer_config.timerOutput = kFLEXIO_TimerOutputZeroNotAffectedByReset;
	timer_config.timerDecrement =
		kFLEXIO_TimerDecSrcDiv16OnFlexIOClockShiftTimerOutput;
	timer_config.timerReset = kFLEXIO_TimerResetNever;
	timer_config.timerDisable = kFLEXIO_TimerDisableNever;
	timer_config.timerEnable = kFLEXIO_TimerEnabledAlways;
	timer_config.timerStop = kFLEXIO_TimerStopBitDisabled;
	timer_config.timerStart = kFLEXIO_TimerStartBitDisabled;
	timer_config.timerCompare =
		((VGA_VIDEO_HIGH_TICKS - 1U) << 8) |
		(VGA_VIDEO_LOW_TICKS - 1U);

	FLEXIO_SetTimerConfig(flexio_base, timer_index[1], &timer_config);

	/* configure_video_pin_black() asserted the GPIO-style output override
	 * during safe startup. Release it now so timer 1, rather than PINOUTD,
	 * owns the VIDEO pin. EXP-24 omitted this hand-off and stayed black. */
	FLEXIO_ConfigPinOverride(flexio_base, VGA_VIDEO_FLEXIO_PIN, false);
}

static void configure_pixel_serializer(void)
{
	uint8_t i;
	uint32_t timer_control;

	/* Keep the startup override asserted. EXP-13's first unblank TCD releases
	 * it immediately before the first framebuffer word is transferred. */

	/* Chain shifters 0 and 1 as a 64-pixel transmit FIFO. EXP-27 streams two
	 * complete 800x525 frames per DMA cycle, which is exactly divisible by
	 * 64 pixels. eDMA refills both shifters in one request, doubling the
	 * service window from 1.28 us to 2.56 us.
	 * This follows the MCUXpresso multi-beat FlexIO arrangement: shifter 0
	 * drives the pin and consumes shifter 1 through INSRC.
	 */
	for (i = 2U; i < ARRAY_SIZE(shifter_index); i++) {
		flexio_base->SHIFTCFG[shifter_index[i]] = 0U;
		flexio_base->SHIFTCTL[shifter_index[i]] = 0U;
	}
	flexio_base->SHIFTCFG[shifter_index[0]] =
		FLEXIO_SHIFTCFG_PWIDTH(0U) |
		FLEXIO_SHIFTCFG_INSRC(kFLEXIO_ShifterInputFromNextShifterOutput);
	flexio_base->SHIFTCTL[shifter_index[0]] =
		FLEXIO_SHIFTCTL_TIMSEL(timer_index[1]) |
		FLEXIO_SHIFTCTL_TIMPOL(kFLEXIO_ShifterTimerPolarityOnPositive) |
		FLEXIO_SHIFTCTL_PINCFG(kFLEXIO_PinConfigOutput) |
		FLEXIO_SHIFTCTL_PINSEL(VGA_VIDEO_FLEXIO_PIN) |
		/* Classic Mac bitmap 1 means black; invert it to VGA low (0 V).
		 * The idle level is controlled separately by the pin override. */
		FLEXIO_SHIFTCTL_PINPOL(kFLEXIO_PinActiveLow) |
		FLEXIO_SHIFTCTL_SMOD(kFLEXIO_ShifterModeTransmit);
	flexio_base->SHIFTCFG[shifter_index[1]] =
		FLEXIO_SHIFTCFG_PWIDTH(0U) |
		FLEXIO_SHIFTCFG_INSRC(kFLEXIO_ShifterInputFromNextShifterOutput);
	flexio_base->SHIFTCTL[shifter_index[1]] =
		FLEXIO_SHIFTCTL_TIMSEL(timer_index[1]) |
		FLEXIO_SHIFTCTL_TIMPOL(kFLEXIO_ShifterTimerPolarityOnPositive) |
		FLEXIO_SHIFTCTL_PINCFG(kFLEXIO_PinConfigOutputDisabled) |
		FLEXIO_SHIFTCTL_PINSEL(0U) |
		FLEXIO_SHIFTCTL_PINPOL(kFLEXIO_PinActiveLow) |
		FLEXIO_SHIFTCTL_SMOD(kFLEXIO_ShifterModeTransmit);

	/* 64 serial pixel beats per two-word refill, at 25 MHz. */
	flexio_base->TIMCMP[timer_index[1]] = (127U << 8U) | 0U;
	flexio_base->TIMCFG[timer_index[1]] =
		FLEXIO_TIMCFG_TIMOUT(kFLEXIO_TimerOutputOneNotAffectedByReset) |
		FLEXIO_TIMCFG_TIMDEC(kFLEXIO_TimerDecSrcOnFlexIOClockShiftTimerOutput) |
		FLEXIO_TIMCFG_TIMRST(kFLEXIO_TimerResetNever) |
		FLEXIO_TIMCFG_TIMDIS(kFLEXIO_TimerDisableOnTimerCompare) |
		FLEXIO_TIMCFG_TIMENA(kFLEXIO_TimerEnableOnTriggerHigh) |
		FLEXIO_TIMCFG_TSTOP(kFLEXIO_TimerStopBitDisabled) |
		FLEXIO_TIMCFG_TSTART(kFLEXIO_TimerStartBitDisabled);

	timer_control =
		FLEXIO_TIMCTL_TRGSEL(
			FLEXIO_TIMER_TRIGGER_SEL_SHIFTnSTAT(shifter_index[1])) |
		FLEXIO_TIMCTL_TRGPOL(kFLEXIO_TimerTriggerPolarityActiveLow) |
		FLEXIO_TIMCTL_TRGSRC(kFLEXIO_TimerTriggerSourceInternal) |
		FLEXIO_TIMCTL_PINCFG(kFLEXIO_PinConfigOutputDisabled) |
		FLEXIO_TIMCTL_PINSEL(0U) |
		FLEXIO_TIMCTL_PINPOL(kFLEXIO_PinActiveHigh) |
		FLEXIO_TIMCTL_TIMOD(kFLEXIO_TimerModeDual8BitBaudBit);
	flexio_base->TIMCTL[timer_index[1]] = timer_control;
}

void vga_flexio_pixel_request_enable(bool enable)
{
#if defined(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
	const uint32_t request_mask = BIT(shifter_index[0]);
#else
	const uint32_t request_mask = BIT(shifter_index[7]);
#endif
	if (enable) {
		/* INPUTMUX carries the combined FlexIO IRQ level. Mask the line
		 * timer at the peripheral too, so it cannot impersonate a shifter
		 * request or keep that handshake permanently high during video.
		 */
		nxp_flexio_irq_disable(flexio_dev);
		FLEXIO_DisableTimerStatusInterrupts(flexio_base, BIT(timer_index[0]));
		FLEXIO_EnableShifterStatusInterrupts(flexio_base, request_mask);
	} else {
		FLEXIO_DisableShifterStatusInterrupts(flexio_base, request_mask);
		if (running) {
			FLEXIO_EnableTimerStatusInterrupts(flexio_base, BIT(timer_index[0]));
			nxp_flexio_irq_enable(flexio_dev);
		}
	}
}

void vga_flexio_edma_request_enable(bool enable)
{
	/* EDMA has a dedicated FlexIO request, so the line-timer interrupt can
	 * remain enabled throughout pixel transfer. Each request writes one
	 * packed 32-pixel word to SHIFTBUF0.
	 */
	FLEXIO_EnableShifterStatusDMA(flexio_base, BIT(shifter_index[0]), enable);
}

void vga_flexio_video_override_enable(bool enable)
{
	FLEXIO_ConfigPinOverride(flexio_base, VGA_VIDEO_FLEXIO_PIN, enable);
}

uint32_t vga_flexio_shiftbuf_address(void)
{
	return (uint32_t)&flexio_base->SHIFTBUF[shifter_index[0]];
}

uint32_t vga_flexio_shiftbuf_bitbyte_swapped_address(void)
{
	return (uint32_t)&flexio_base->SHIFTBUFBBS[shifter_index[0]];
}

uint32_t vga_flexio_shift_dma_enable_address(void)
{
	return (uint32_t)&flexio_base->SHIFTSDEN;
}

uint32_t vga_flexio_pin_override_address(void)
{
	return (uint32_t)&flexio_base->PINOUTE;
}

uint32_t vga_flexio_video_override_word(bool enabled)
{
	uint32_t value = flexio_base->PINOUTE;

	if (enabled) {
		return value | BIT(VGA_VIDEO_FLEXIO_PIN);
	}
	return value & ~BIT(VGA_VIDEO_FLEXIO_PIN);
}

void vga_flexio_video_abort(void)
{
	FLEXIO_DisableShifterStatusInterrupts(flexio_base, BIT(shifter_index[7]));
	flexio_base->TIMCTL[timer_index[1]] = 0U;
	flexio_base->TIMCFG[timer_index[1]] = 0U;
	for (size_t i = 0; i < ARRAY_SIZE(shifter_index); i++) {
		flexio_base->SHIFTCTL[shifter_index[i]] = 0U;
		flexio_base->SHIFTCFG[shifter_index[i]] = 0U;
	}
	configure_video_pin_black();
	if (running) {
		/* The disabled CPU IRQ may have missed lines. Start a fresh frame. */
		current_line = 0U;
		FLEXIO_ClearTimerStatusFlags(flexio_base, BIT(timer_index[0]));
		FLEXIO_EnableTimerStatusInterrupts(flexio_base, BIT(timer_index[0]));
		nxp_flexio_irq_enable(flexio_dev);
	}
}

int vga_flexio_init(void)
{
	int ret;

	if (initialized) {
		return 0;
	}

	if (!device_is_ready(flexio_dev)) {
		return -ENODEV;
	}

	ret = pinctrl_apply_state(flexio_pincfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0) {
		return ret;
	}

	ret = nxp_flexio_child_attach(flexio_dev, &vga_flexio_child);
	if (ret != 0) {
		return ret;
	}
	ret = validate_flexio_resources();
	if (ret != 0) {
		return ret;
	}
	printk("DIAG FLEX base=%08x timers=%u,%u shifters=0..7\n",
	       (uint32_t)flexio_base, timer_index[0], timer_index[1]);

	nxp_flexio_lock(flexio_dev);
	FLEXIO_Enable(flexio_base, false);
	CLOCK_SetClkDiv(kCLOCK_DivFlexioClk, VGA_FLEXIO_ROOT_DIVIDER);
	printk("DIAG clocks FLEXIO=%u Hz CTIMER0=%u Hz cycle_counter=%u Hz "
	       "line_cycles=%u\n",
	       CLOCK_GetFlexioClkFreq(), VGA_VSYNC_TIMER_CLOCK_HZ,
	       sys_clock_hw_cycles_per_sec(), vga_line_cycle_count());
	configure_vsync_pin();
	configure_vsync_timer();
	configure_video_pin_black();
	configure_hsync_timer();
	if (IS_ENABLED(CONFIG_MCX_MAC_HARDWARE_VIDEO_DIAG)) {
		configure_hardware_video_timer();
	} else if (!IS_ENABLED(CONFIG_MCX_MAC_SOLID_VIDEO_DIAG)) {
		configure_pixel_serializer();
	}
	FLEXIO_ClearTimerStatusFlags(flexio_base, BIT(timer_index[0]));
	nxp_flexio_unlock(flexio_dev);

	initialized = true;
	return 0;
}

int vga_flexio_start(void)
{
	if (!initialized) {
		return -EACCES;
	}
	if (running) {
		return 0;
	}

	current_line = 0U;
	atomic_clear(&sync_frames);
	atomic_clear(&recovered_lines);
	line_cycle_remainder = 0U;
	if (!vga_smartdma_scanout_enabled() &&
	    !IS_ENABLED(CONFIG_MCX_MAC_HARDWARE_VIDEO_DIAG)) {
		vga_flexio_video_abort();
	}
	FLEXIO_PinWrite(flexio_base, VGA_VSYNC_FLEXIO_PIN, 0U);
	FLEXIO_ClearTimerStatusFlags(flexio_base, BIT(timer_index[0]));
	if (!IS_ENABLED(CONFIG_MCX_MAC_HARDWARE_VIDEO_DIAG) &&
	    !IS_ENABLED(CONFIG_MCX_MAC_CONTINUOUS_RASTER)) {
		FLEXIO_EnableTimerStatusInterrupts(flexio_base, BIT(timer_index[0]));
	} else {
		FLEXIO_DisableTimerStatusInterrupts(flexio_base,
						    BIT(timer_index[0]));
		nxp_flexio_irq_disable(flexio_dev);
	}
	running = true;
	last_line_cycle = k_cycle_get_32();
	if (IS_ENABLED(CONFIG_MCX_MAC_CONTINUOUS_RASTER)) {
		int ret = vga_smartdma_start_continuous();

		if (ret != 0) {
			running = false;
			return ret;
		}
	}
	FLEXIO_Enable(flexio_base, true);
	CTIMER_Reset(VGA_VSYNC_TIMER_BASE);
	CTIMER_ClearStatusFlags(VGA_VSYNC_TIMER_BASE,
				kCTIMER_Match0Flag | kCTIMER_Match1Flag);
	irq_enable(VGA_VSYNC_TIMER_IRQ);
	CTIMER_StartTimer(VGA_VSYNC_TIMER_BASE);

	return 0;
}

uint32_t vga_flexio_get_frame_count(void)
{
	return (uint32_t)atomic_get(&sync_frames);
}

uint32_t vga_flexio_get_recovered_line_count(void)
{
	return (uint32_t)atomic_get(&recovered_lines);
}

uint32_t vga_flexio_take_shift_errors(void)
{
	uint32_t errors = FLEXIO_GetShifterErrorFlags(flexio_base) &
		(BIT(shifter_index[0]) | BIT(shifter_index[1]));

	if (errors != 0U) {
		FLEXIO_ClearShifterErrorFlags(flexio_base, errors);
	}
	return errors;
}

void vga_flexio_stop(void)
{
	if (!initialized) {
		return;
	}

	FLEXIO_DisableTimerStatusInterrupts(flexio_base, BIT(timer_index[0]));
	CTIMER_StopTimer(VGA_VSYNC_TIMER_BASE);
	irq_disable(VGA_VSYNC_TIMER_IRQ);
	FLEXIO_Enable(flexio_base, false);
	FLEXIO_PinWrite(flexio_base, VGA_VSYNC_FLEXIO_PIN, 1U);
	running = false;
}

void vga_flexio_get_timing_report(struct vga_timing_report *report)
{
	uint32_t timer_clock_hz;

	if (report == NULL) {
		return;
	}

	timer_clock_hz = VGA_FLEXIO_SOURCE_HZ /
		VGA_FLEXIO_ROOT_DIVIDER / VGA_TIMER_PRESCALER;
	report->hsync_millihz =
		(uint32_t)(((uint64_t)timer_clock_hz * 1000U) /
			   VGA_HSYNC_TIMER_TICKS);
	report->vsync_millihz = report->hsync_millihz / VGA_V_TOTAL_LINES;
	report->lines_per_frame = VGA_V_TOTAL_LINES;
}
