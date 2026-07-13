/*
 * NPLL - PowerPC Exception handling, Assembly stub
 *
 * Copyright (C) 2025 Techflash
 *
 * Based on code in BootMii ppcskel:
 * Copyright (C) 2008		Segher Boessenkool <segher@kernel.crashing.org>
 *
 * Original license disclaimer:
 * This code is licensed to you under the terms of the GNU GPL, version 2;
 * see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 */

.globl exception_2200_start, exception_2200_end
.globl exception_2200_handler_hi, exception_2200_handler_lo

exception_2200_start:
	# store all interesting regs
	mfcr 0 ;  stw 0,0x2080(0)
	mfxer 0 ; stw 0,0x2084(0)
	mflr 0 ;  stw 0,0x2088(0)
	mfctr 0 ; stw 0,0x208c(0)
	mfsrr0 0 ;  stw 0,0x2090(0)
	mfsrr1 0 ;  stw 0,0x2094(0)
	mfdar 0 ;   stw 0,0x2098(0)
	mfdsisr 0 ; stw 0,0x209c(0)

	# switch on FP, DR, IR
	mfmsr 0 ; ori 0,0,0x2030 ; mtsrr1 0

	# go to C handler
exception_2200_handler_hi:
	.long 0x3c000000              # lis r0, patched E_Handler@h
exception_2200_handler_lo:
	.long 0x60000000              # ori r0,r0, patched E_Handler@l
	mtsrr0 0
	rfi
exception_2200_end:


.section .note.GNU-stack
_note:
