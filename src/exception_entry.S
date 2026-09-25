/*
 * NPLL - PowerPC Exception handling, Assembly stub
 *
 * Copyright (C) 2025-2026 Techflash
 *
 * Based on code in BootMii ppcskel:
 * Copyright (C) 2008		Segher Boessenkool <segher@kernel.crashing.org>
 *
 * Original license disclaimer:
 * This code is licensed to you under the terms of the GNU GPL, version 2;
 * see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 */

#define __ASSEMBLY__
#include <npll/cpu.h>

.section .text.exception_entry, "ax"
.balign 32
.globl exception_entry_start, exception_entry_end
.globl exception_handler_hi, exception_handler_lo

exception_entry_start:
	# r3 = vector, r4 = original CTR; SPRG0/1 = original r3/r4
	# Restore CTR before reusing r4 as the physical frame pointer
	mtctr 4
	mfspr 4, SPRG2
	stw 0,0x00(4)
	stw 1,0x04(4)
	stw 2,0x08(4)
	stmw 5,0x14(4)
	mfspr 0, SPRG0 ; stw 0,0x0c(4)
	mfspr 0, SPRG1 ; stw 0,0x10(4)

	# store all interesting regs
	mfcr 0 ;   stw 0,0x80(4)
	mfxer 0 ;  stw 0,0x84(4)
	mflr 0 ;   stw 0,0x88(4)
	mfctr 0 ;  stw 0,0x8c(4)
	mfsrr0 0 ; stw 0,0x90(4)
	mfsrr1 0 ; stw 0,0x94(4)
	mfdar 0 ;  stw 0,0x98(4)
	mfdsisr 0 ; stw 0,0x9c(4)

	# switch on FP, DR, IR
	mfmsr 0 ; ori 0,0,0x2030 ; mtsrr1 0

	# go to C handler
exception_handler_hi:
	.long 0x3c000000              # lis r0, patched E_Handler@h
exception_handler_lo:
	.long 0x60000000              # ori r0,r0, patched E_Handler@l
	mtsrr0 0
	rfi

exception_entry_end:

.section .note.GNU-stack
_note:
