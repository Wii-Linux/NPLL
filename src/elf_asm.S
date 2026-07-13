/*
 * NPLL - ELF handling - Assembly
 *
 * Copyright (C) 2025 Techflash
 */

.global ELF_DoEntry

#define __ASSEMBLY__
#include <npll/cpu.h>

ELF_DoEntry:
	# Parameters:
	# r3-r5 = entry r3-r5
	# r6 = physical entry pointer
	# r7 = nonzero to preserve enabled caches
	#
	# Scratch:
	# r7 = MSR after cache handling
	# r8 = HID0
	# r10 = saved Linux handoff flag
	# r11 = mask
	# r12 = phys function pointer

	# r7 is reused for MSR below, so retain the handoff mode.
	mr r10, r7

	# Generic ELF entry historically disables and invalidates L1.  Linux's
	# early 32-bit entry code expects usable caches before its CPU setup runs.
	cmpwi r7, 0
	bne 1f
	mfspr r8, HID0
	lis r11, 0xffff
	ori r11, r11, (0xffff & ~(HID0_ICE | HID0_DCE))
	and r8, r8, r11
	mtspr HID0, r8
	sync
	isync
	ori r8, r8, (HID0_ICFI | HID0_DCFI)
	mtspr HID0, r8
	sync
	isync

1:
	# read MSR
	mfmsr r7

	# set up MSR
	lis r11, 0xffff
	ori r11, r11, (0xffff & ~(MSR_EE | MSR_IR | MSR_DR))
	and r7, r7, r11

	# calculate the address of _elfEntryStep2
	lis r11, 0x7fff # mask
	ori r11, r11, 0xffff
	lis r12, _elfEntryStep2@h # virt address
	ori r12, r12, _elfEntryStep2@l
	and r12, r12, r11 # mask MSB to get phys

	# set up SRR0/SRR1 so we can return with our new MSR and entry
	mtsrr0 r12
	mtsrr1 r7

	# hop over to _elfEntryStep2
	rfi

_elfEntryStep2:
	# Address translation is off and we are executing at the physical address.

	# NPLL's stack uses its 0x8xxxxxxx cached MEM1 alias.  Linux calls C code
	# before installing its own stack/MMU mappings, so give it the underlying
	# physical stack rather than an unmapped effective address.
	cmpwi r10, 0
	beq 1f
	clrlwi r1, r1, 2

	# The client calling convention is already installed by the caller.
1:
	mtlr r6
	blr

.section .note.GNU-stack
_note:
