/*
 * NPLL - ELF handling - Assembly
 *
 * Copyright (C) 2025 Techflash
 */

.global ELF_DoEntry

#define __ASSEMBLY__
#include <npll/cpu.h>

# TODO: Need to adapt this for Linux-specific entry once we get there
ELF_DoEntry:
	# Parameters:
	# r3 = physical entry pointer
	# [FUTURE] r4 = physical DTB pointer
	#
	# Scratch:
	# r5 = MSR
	# r6 = HID0
	# r7 = mask
	# r8 = phys function pointer

	# read MSR
	mfmsr r5

	# read HID0
	mfspr r6, HID0

	# set up HID0
	lis r7, 0xffff
	ori r7, r7, (0xffff & ~(HID0_ICE | HID0_DCE))
	and r6, r6, r7
	mtspr HID0, r6

	# set up MSR
	lis r7, 0xffff
	ori r7, r7, (0xffff & ~(MSR_EE | MSR_IR | MSR_DR))
	and r5, r5, r7

	# calculate the address of _elfEntryStep2
	lis r7, 0x7fff # mask
	ori r7, r7, 0xffff
	lis r8, _elfEntryStep2@h # virt address
	ori r8, r8, _elfEntryStep2@l
	and r8, r8, r7 # mask MSB to get phys

	# set up SRR0/SRR1 so we can return with our new MSR and entry
	mtsrr0 r8
	mtsrr1 r5

	# hop over to _elfEntryStep2
	rfi

_elfEntryStep2:
	# We now have caches off, address translation off, and are at the physical address

	# TODO: Any final setup we need here?

	# Jump into the entry
	mtlr r3
	blr

.section .note.GNU-stack
_note:
