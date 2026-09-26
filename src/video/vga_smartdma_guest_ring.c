/* SPDX-License-Identifier: MIT */
/* Continuous two-shifter scanout from compact, Flash-resident descriptors. */
#include "smartdma_ezh_subset.h"

/* Keep these instruction bytes in Flash in every Kconfig variant. An orphan
 * output section was placed in uncopied RAM when CONFIG_FLASH=y, so the
 * firmware installer read zeroes and the scanout engine never advanced. */
__attribute__((naked, used, section(".text.smartdma_guest_ring")))
void vga_smartdma_guest_ring(void)
{
	__asm__ volatile(".global vga_smartdma_guest_ring_start\n"
			 "vga_smartdma_guest_ring_start:");
	E_NOP;
	E_NOP;
	E_NOP;
	E_NOP;
	E_PER_READ(R7, 0x50033040);
	E_NOP;
	E_NOP;
	E_LDR(R1, R7, 0); /* Flash descriptor table. */
	E_NOP;
	E_NOP;
	E_LDR(R2, R7, 1); /* SRAM source: white word, then Mac pixels. */
	E_NOP;
	E_NOP;
	E_LDR(R3, R7, 2); /* FlexIO SHIFTBUFBBS[0]. */
	E_NOP;
	E_NOP;
	E_LDR(R4, R7, 3); /* Words until frame wrap. */
	E_NOP;
	E_NOP;
	E_LOAD_IMM(CFS, 0);
	E_LOAD_IMM(CFM, 0x401);
	E_MOV(R5, PC);
	E_NOP;
	E_HOLD;
	E_LDR_POST(R6, R1, 1);
	E_NOP;
	E_NOP;
	E_LSL(R0, R6, 16);
	E_LSR(R0, R0, 16);
	E_LSR(R6, R6, 16);
	E_LSL(R0, R0, 2);
	E_LSL(R6, R6, 2);
	E_ADD(R0, R2, R0);
	E_ADD(R6, R2, R6);
	E_LDR(R0, R0, 0);
	E_LDR(R6, R6, 0);
	E_NOP;
	E_NOP;
	E_LSR(R0, R0, 16);
	E_LSL_OR(R0, R0, R6, 16);
	E_NOP;
	E_STR(R3, R0, 0);
	E_SUB_IMMS(R4, R4, 1);
	E_LDR(R6, R7, 0);
	E_COND_MOV(ZE, R1, R6);
	E_LDR(R6, R7, 3);
	E_COND_MOV(ZE, R4, R6);
	E_LDR_POST(R6, R1, 1);
	E_NOP;
	E_NOP;
	E_LSL(R0, R6, 16);
	E_LSR(R0, R0, 16);
	E_LSR(R6, R6, 16);
	E_LSL(R0, R0, 2);
	E_LSL(R6, R6, 2);
	E_ADD(R0, R2, R0);
	E_ADD(R6, R2, R6);
	E_LDR(R0, R0, 0);
	E_LDR(R6, R6, 0);
	E_NOP;
	E_NOP;
	E_LSR(R0, R0, 16);
	E_LSL_OR(R0, R0, R6, 16);
	E_NOP;
	E_STR(R3, R0, 1);
	E_SUB_IMMS(R4, R4, 1);
	E_LDR(R6, R7, 0);
	E_COND_MOV(ZE, R1, R6);
	E_LDR(R6, R7, 3);
	E_COND_MOV(ZE, R4, R6);
	E_LDR(R6, R7, 4); /* Progress for phase diagnostics. */
	E_STR(R6, R4, 0);
	E_GOTO_REG(R5);
	__asm__ volatile(".global vga_smartdma_guest_ring_end\n"
			 "vga_smartdma_guest_ring_end:");
	__builtin_unreachable();
}
