/*
 * NPLL - CPU Cleanup - Assembly
 *
 * Copyright (C) 2026 Techflash
 */

#define __ASSEMBLY__
#include <npll/cpu.h>

.global CPU_Idle
CPU_Idle:
	mfspr %r4, HID0
	# make sure we're not going into nap/sleep
	lis %r5, (HID0_NAP | HID0_SLEEP)@h
	andc %r5, %r4, %r5
	# enable doze
	oris %r5, %r5, HID0_DOZE@h
	mtspr HID0, %r5
	sync

	# enable interrupts and power management
	# (triggers doze mode)
	mfmsr %r3
	ori %r3, %r3, MSR_EE
	oris %r3, %r3, MSR_POW@h
	mtmsr %r3
	isync

	# disable interrupts
	mfmsr %r3
	rlwinm %r3, %r3, 0, 17, 15
	mtmsr %r3
	mtspr HID0, %r4
	blr

.section .note.GNU-stack
_note:
