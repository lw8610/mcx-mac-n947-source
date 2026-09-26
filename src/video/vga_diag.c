/* SPDX-License-Identifier: MIT */
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <fsl_common.h>
#include <fsl_flexio.h>

#include "vga.h"

#define SDMA_NODE DT_NODELABEL(smartdma)
#define FLEX_NODE DT_NODELABEL(flexio0)

/* Only read status/control registers, never SHIFTBUF (which can acknowledge
 * a shifter flag). Save evidence before the timeout handler resets anything.
 */
static struct {
	const char *reason;
	uint32_t boot, ctrl, pc, sp, param, reply, trap;
	uint32_t irq_enabled, irq_pending, irq_active;
	uint32_t flex_ctrl, shiftstat, shifterr, shiftsien, timstat, timien;
	uint32_t shiftctl[8], shiftcfg[8], timctl[2], timcfg[2], timcmp[2];
	uint32_t sec_valid, sec_addr[32], sec_info[32];
} snapshot;

void vga_diag_capture(const char *reason)
{
	SMARTDMA_Type *sdma = (SMARTDMA_Type *)DT_REG_ADDR(SDMA_NODE);
	FLEXIO_Type *flex = (FLEXIO_Type *)DT_REG_ADDR(FLEX_NODE);
	IRQn_Type irq = (IRQn_Type)DT_IRQN(SDMA_NODE);

	snapshot.reason = reason;
	snapshot.boot = sdma->BOOTADR;
	snapshot.ctrl = sdma->CTRL;
	snapshot.pc = sdma->PC;
	snapshot.sp = sdma->SP;
	snapshot.param = sdma->ARM2EZH;
	snapshot.reply = sdma->EZH2ARM;
	snapshot.trap = sdma->PENDTRAP;
	snapshot.irq_enabled = NVIC_GetEnableIRQ(irq);
	snapshot.irq_pending = NVIC_GetPendingIRQ(irq);
	snapshot.irq_active = NVIC_GetActive(irq);
	snapshot.flex_ctrl = flex->CTRL;
	snapshot.shiftstat = flex->SHIFTSTAT;
	snapshot.shifterr = flex->SHIFTERR;
	snapshot.shiftsien = flex->SHIFTSIEN;
	snapshot.timstat = flex->TIMSTAT;
	snapshot.timien = flex->TIMIEN;
	for (size_t i = 0; i < 8; i++) {
		snapshot.shiftctl[i] = flex->SHIFTCTL[i];
		snapshot.shiftcfg[i] = flex->SHIFTCFG[i];
	}
	for (size_t i = 0; i < 2; i++) {
		snapshot.timctl[i] = flex->TIMCTL[i];
		snapshot.timcfg[i] = flex->TIMCFG[i];
		snapshot.timcmp[i] = flex->TIMCMP[i];
	}
	snapshot.sec_valid = AHBSC->SEC_VIO_INFO_VALID;
	for (size_t i = 0; i < 32; i++) {
		if ((snapshot.sec_valid & BIT(i)) != 0U) {
			snapshot.sec_addr[i] = AHBSC->SEC_VIO_ADDR[i];
			snapshot.sec_info[i] = AHBSC->SEC_VIO_MISC_INFO[i];
		}
	}
}

void vga_diag_print(void)
{
	printk("DIAG snapshot: %s\n", snapshot.reason);
	printk("DIAG SDMA boot=%08x ctrl=%08x pc=%08x sp=%08x\n",
	       snapshot.boot, snapshot.ctrl, snapshot.pc, snapshot.sp);
	printk("DIAG SDMA param=%08x reply=%08x trap=%08x IRQ en/pend/active=%u/%u/%u\n",
	       snapshot.param, snapshot.reply, snapshot.trap,
	       snapshot.irq_enabled, snapshot.irq_pending, snapshot.irq_active);
	printk("DIAG FLEX ctrl=%08x stat=%08x err=%08x req=%08x tstat=%08x tien=%08x\n",
	       snapshot.flex_ctrl, snapshot.shiftstat, snapshot.shifterr,
	       snapshot.shiftsien, snapshot.timstat, snapshot.timien);
	for (size_t i = 0; i < 8; i++) {
		printk("DIAG SHIFT%u ctl=%08x cfg=%08x\n", (unsigned int)i,
		       snapshot.shiftctl[i], snapshot.shiftcfg[i]);
	}
	for (size_t i = 0; i < 2; i++) {
		printk("DIAG TIMER%u ctl=%08x cfg=%08x cmp=%08x\n", (unsigned int)i,
		       snapshot.timctl[i], snapshot.timcfg[i], snapshot.timcmp[i]);
	}
	printk("DIAG AHB security violations=%08x\n", snapshot.sec_valid);
	for (size_t i = 0; i < 32; i++) {
		if ((snapshot.sec_valid & BIT(i)) != 0U) {
			printk("DIAG AHB%u addr=%08x info=%08x\n", (unsigned int)i,
			       snapshot.sec_addr[i], snapshot.sec_info[i]);
		}
	}
}
