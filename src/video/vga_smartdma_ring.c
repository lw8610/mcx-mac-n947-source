/* SPDX-License-Identifier: MIT */
/* Continuous 1bpp FlexIO scanout from a single, odd-word-count frame.
 * Only the 180-byte EZH instruction sequence is copied into SmartDMA RAMX.
 */
#include "smartdma_ezh_subset.h"

__attribute__((naked, used, section(".text.smartdma_vga_ring")))
void vga_smartdma_ring(void)
{
	__asm__ volatile(".global vga_smartdma_ring_start\n"
			 "vga_smartdma_ring_start:");
	E_NOP;
	E_NOP;
	E_NOP;
	E_NOP;
	E_PER_READ(R0, 0x50033040);
	E_NOP;
	E_NOP;
	E_LDR(R1, R0, 0); /* frame base */
	E_NOP;
	E_NOP;
	E_LDR(R2, R0, 1); /* FlexIO SHIFTBUF0 */
	E_NOP;
	E_NOP;
	E_LDR(R7, R0, 2); /* words per frame */
	E_NOP;
	E_NOP;
	E_LDR(R4, R0, 3); /* phase/progress word */
	E_NOP;
	E_NOP;
	E_MOV(R6, R1);
	E_MOV(R0, R7);
	E_LOAD_IMM(CFS, 0);
	E_LOAD_IMM(CFM, 0x401); /* FlexIO shifter-0 status level. */
	E_MOV(R5, PC);
	E_NOP;
	E_HOLD;
	E_LDR_POST(R3, R1, 1);
	E_NOP;
	E_NOP;
	E_STR(R2, R3, 0);
	E_SUB_IMMS(R7, R7, 1);
	E_COND_MOV(ZE, R1, R6);
	E_COND_MOV(ZE, R7, R0);
	E_LDR_POST(R3, R1, 1);
	E_NOP;
	E_NOP;
	E_STR(R2, R3, 1);
	E_SUB_IMMS(R7, R7, 1);
	E_COND_MOV(ZE, R1, R6);
	E_COND_MOV(ZE, R7, R0);
	E_STR(R4, R7, 0);
	E_GOTO_REG(R5);
	__asm__ volatile(".global vga_smartdma_ring_end\n"
			 "vga_smartdma_ring_end:");
	__builtin_unreachable();
}
