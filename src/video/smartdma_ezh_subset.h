/*
 * Copyright 2014, 2019, 2025-2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The EZH encodings below are a small subset of NXP's
 * mcuxsdk/drivers/smartdma/fsl_smartdma_prv.h (26.06). Keeping only the
 * instructions used by this program makes the build independent of the
 * incompatible SmartDMA assembler macros in older Zephyr MCUX SDKs.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef MCX_MAC_SMARTDMA_EZH_SUBSET_H
#define MCX_MAC_SMARTDMA_EZH_SUBSET_H

#define R0 0U
#define R1 1U
#define R2 2U
#define R3 3U
#define R4 4U
#define R5 5U
#define R6 6U
#define R7 7U
#define CFS 10U
#define CFM 11U
#define PC 13U
#define EU 0U
#define ZE 1U

#define EZH_WORD(value) __asm__ volatile(".word %c0" : : "i" (value))
#define E_NOP EZH_WORD(0x12U)
#define E_GOTO_REG(address) EZH_WORD(0x15U + ((address) << 14))
#define E_MOV(dst, src) EZH_WORD(((src) << 14) + ((dst) << 10) + (EU << 5))
#define E_COND_MOV(cond, dst, src) \
	EZH_WORD(((src) << 14) + ((dst) << 10) + ((cond) << 5))
#define E_LOAD_IMM(dst, value) \
	EZH_WORD(((dst) << 10) + (((value) & 0x7ffU) << 20) + (1U << 18) + (EU << 5))
#define E_LDR(dst, src, offset) \
	EZH_WORD(0x1U + ((dst) << 10) + ((src) << 14) + ((offset) << 24) + \
		 (1U << 18) + (EU << 5))
#define E_LDR_POST(dst, src, offset) \
	EZH_WORD(0x1U + (1U << 19) + ((dst) << 10) + ((src) << 14) + \
		 ((offset) << 24) + (1U << 20) + (1U << 18) + (EU << 5))
#define E_STR(address, data, offset) \
	EZH_WORD(0x2U + ((address) << 14) + ((data) << 20) + \
		 ((offset) << 24) + (1U << 18) + (EU << 5))
#define E_PER_READ(dst, address) \
	EZH_WORD(0x4U + ((dst) << 10) + (((address) & 0x000ffffcU) << 12) + \
		 (EU << 5))
#define E_SUB_IMMS(dst, src, value) \
	EZH_WORD(0x8U + ((dst) << 10) + ((src) << 14) + (1U << 9) + \
		 (EU << 5) + ((value) << 20) + (1U << 18))
#define E_ADD(dst, src1, src2) \
	EZH_WORD(0x6U + ((dst) << 10) + ((src1) << 14) + \
		 ((src2) << 20) + (EU << 5))
#define E_LSL(dst, src, bits) \
	EZH_WORD(0x10U + ((dst) << 10) + ((src) << 20) + \
		 (((bits) & 31U) << 24) + (EU << 5))
#define E_LSR(dst, src, bits) \
	EZH_WORD(0x10U + ((dst) << 10) + ((src) << 20) + \
		 (((bits) & 31U) << 24) + (1U << 19) + (EU << 5))
#define E_LSL_OR(dst, base, src, bits) \
	EZH_WORD(0x10U + ((dst) << 10) + ((base) << 14) + \
		 ((src) << 20) + (((bits) & 31U) << 24) + \
		 (2U << 29) + (EU << 5))
#define E_HOLD EZH_WORD(0x1cU + (PC << 10) + (1U << 15) + (EU << 5))

#endif
