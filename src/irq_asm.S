/*
 * NPLL - IRQ handling - Assembly
 *
 * Copyright (C) 2026 Techflash
 */

.global IRQ_Return
.global IRQ_Disable
.global IRQ_Enable

#define __ASSEMBLY__
#include <npll/cpu.h>

IRQ_Return:
	# Load offset of saved state into r1, which will get overwritten at the final step
	lis	r1, 0x8000
	ori	r1, r1, 0x2000

	# start loading stuff into r3 as scratch, which will also get overwritten at the final step
	lwz	r3, 0x80(r1)
	mtcr	r3
	lwz	r3, 0x84(r1)
	mtxer	r3
	lwz	r3, 0x88(r1)
	mtlr	r3
	lwz	r3, 0x8c(r1)
	mtctr	r3
	lwz	r3, 0x90(r1)
	mtsrr0	r3
	lwz	r3, 0x94(r1)
	mtsrr1	r3
	lwz	r3, 0x98(r1)
	mtdar	r3
	lwz	r3, 0x9c(r1)
	mtdsisr	r3

	# restore r2-r31
	lmw	r2, 8(r1)

	# restore r0
	lwz	r0, 0(r1)

	# restore r1
	lwz	r1, 4(r1)

	# return
	rfi

IRQ_Disable:
	mfmsr	r3
	# clear bit 16 aka MSR[EE]
	rlwinm	r3, r3, 0, 17, 15
	mtmsr	r3
	blr

IRQ_Enable:
	mfmsr	r3
	ori	r3, r3, MSR_EE
	mtmsr	r3
	blr

.section .note.GNU-stack
_note:
