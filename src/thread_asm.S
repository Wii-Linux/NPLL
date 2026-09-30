/*
 * NPLL - Threading
 *
 * Copyright (C) 2026 Techflash
 */

.global TH_Switch
TH_Switch:
	stwu 1,-96(1)
	mflr 0
	stw 0,4(1)
	mfcr 0
	stw 0,8(1)
	stmw 13,20(1)
	stw 1,0(3)
	lwz 1,0(4)
	lmw 13,20(1)
	lwz 0,8(1)
	mtcr 0
	lwz 0,4(1)
	mtlr 0
	addi 1,1,96
	blr

.section .note.GNU-stack
_note:
