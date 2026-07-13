#
# NPLL - Entry point
# Copyright (C) 2025 Techflash
#

.section .init
.global _start
.global __argv
.extern init

# these shouldn't need to be marked as global but some toolchains break w/o it
.global setupBATs
.global relocate
.global _copyLoop
.global _copyDone
.global _flushDcacheLoop
.global _flushDcacheDone
.global _invalidateIcacheLoop
.global _invalidateIcacheDone
#define __ASSEMBLY__
#include <npll/cpu.h>
# NOTE: Reminder that "bit [n]" follows the official PowerPC naming scheme throughout this code  - reversed compared to how one may think about them.  "bit 0 is set" = 0x80000000.  "bit 31 is set" = 0x00000001.

# We're here!  Let's get a few things knocked out right off the bat (heh, BATs)
# 1. Make sure we're in real-mode (IR and DR off)
#
# 2. Map [I/D]BAT[0/1], 256M @ 0x00000000 (uncached and cached), which gives us:
#    - MEM1
#    - Flipper registers (0x0c000000)
#    - Hollywood and Latte registers (0x0d000000)
#    - Hollywood and Latte registers mirror (0x0d800000)
#
# 3. Switch to virtual memory (MSR[IR] = 1, MSR[DR] = 1) with our new known map
#
# 4. Zero BSS
#
# 5. Set up a stack
#
# 6. Relocate the rest of the code into high MEM1 (we start in low MEM1)
#
# 6. Zero out the (non-SP) registers
#
# 7. Call into the C init function, where we can do things like hardware detection
_start:
	b _realStart  # this is safe, apparently PPC branch instructions are relative by default
	.ascii "_arg" # HBC-style argv marker
__argv:
	.long 0 # magic
	.long 0 # cmdline
	.long 0 # cmdline length
	.long 0 # argc
	.long 0 # argv
	.long 0 # argv end
_realStart:
	# Load r0 with address of routine to do BATs setup
	lis r0, setupBATs@h
	ori r0, r0, setupBATs@l

	# Clear bit 0, since we want the physical address.  This binary is linked assuming virtual addresses in .text, so r0 = 0x8xxxxxxx right now, which is no good.  Clear bit 0 to get a real MEM1-physical address.
	lis r3, 0x7fff
	ori r3, r3, 0xffff
	and r0, r0, r3

	# store it into SRR0 (return address)
	mtsrr0 r0

	# get MSR value
	mfmsr r0

	# clear IR (26) and DR (27) bits - in between 28 and 25
	rlwinm r0, r0, 0, 28, 25

	# move it into SRR1 (MSR value)
	mtsrr1 r0

	# let's do this thing, in real-mode now
	rfi

setupBATs:
	# Hello from real-mode!

	# First, lets clear anything that may be in the segment registers (SRs)
	# We want the T (0) bit to be set, so that they basically don't matter.
	lis r0, 0x8000
	mtsr 0, r0
	mtsr 1, r0
	mtsr 2, r0
	mtsr 3, r0
	mtsr 4, r0
	mtsr 5, r0
	mtsr 6, r0
	mtsr 7, r0
	mtsr 8, r0
	mtsr 9, r0
	mtsr 10, r0
	mtsr 11, r0
	mtsr 12, r0
	mtsr 13, r0
	mtsr 14, r0
	mtsr 15, r0

	# Now let's set up the BATs, we want:
	#  - [I/D]BAT0: 256MB @ 0x00000000, cached, R/W,
	#               virtual @ 0x80000000
	#
	#  - [I/D]BAT1: 256MB @ 0x00000000, UNcached, R/W,
	#               virtual @ 0xc0000000
	#
	# This is more or less the "standard" GameCube/Wii memory layout.
	# We're not mapping MEM2 on the Wii/Wii U yet, because we don't yet know if we're
	# even on a Wii/Wii U.

	# [I/D]BAT0L: BPRN=0x00000000, WIMG=0000 (cached), PP=RW
	li r3, 2

	# [I/D]BAT0U: BEPI=0x80000000, BL=256MB, VS=1, VP=1
	lis r4, 0x8000
	ori r4, r4, 0x1fff

	# Set them
	mtspr IBAT0L, r3
	mtspr DBAT0L, r3
	mtspr IBAT0U, r4
	mtspr DBAT0U, r4


	# [I/D]BAT1L: BPRN=0x00000000, WIMG=1111 (uncached; write-through, caching inhibited, memory coherent, guarded (it's customary to do MMIO here, and largely not actually care about memory access)), PP=RW
	li r3, 122

	# [I/D]BAT1U: BEPI=0xc0000000, BL=256MB, VS=1, VP=1
	lis r4, 0xc000
	ori r4, r4, 0x1fff

	# Set them
	mtspr IBAT1L, r3
	mtspr DBAT1L, r3
	mtspr IBAT1U, r4
	mtspr DBAT1U, r4

	# enable and invalidate the caches
	mfspr r0, HID0
	ori r0, r0, HID0_ICE | HID0_ICFI | HID0_DCE | HID0_DCFI
	mtspr HID0, r0

	# flush the changes
	sync
	isync

	# prepare to flip IR and DR on
	mfmsr r0
	ori r0, r0, MSR_IR | MSR_DR
	mtsrr1 r0

	# Alright, we've made the hardware sane, lets get back to the bootstrap
	lis r0, relocate@h
	ori r0, r0, relocate@l
	mtsrr0 r0

	rfi # no need to flip address bits - it's already correct

relocate:
	# Select a load bias.  GC, Wii, and vWii retain the linked high-MEM1
	# address.  Native Wii U uses the final 1 MiB of the lower 256 MiB MEM2
	# mapping (cached EA 0x9ff00000, physical 0x1ff00000).
	#
	# r31 remains the load bias until the runtime stack is established.
	li r31, 0

	# A native Wii U has a production Hollywood version, Latte's CAFE chip
	# signature, and a non-zero PIMCOMPAT register.  vWii fails the last test.
	lis r8, 0xcd80
	lwz r9, 0x0214(r8)       # HW_VERSION
	cmpwi r9, 0x11
	beq 1f
	cmpwi r9, 0x21
	bne 2f
1:
	lwz r9, 0x05a0(r8)       # LT_CHIPREVID
	srwi r9, r9, 16
	cmplwi r9, 0xcafe
	bne 2f
	lwz r9, 0x05b0(r8)       # LT_PIMCOMPAT
	cmpwi r9, 0
	beq 2f

	# Expose lower MEM2 at its normal cached alias before copying there.
	lis r9, 0x1000
	ori r9, r9, 2
	lis r10, 0x9000
	ori r10, r10, 0x1fff
	mtspr IBAT2L, r9
	mtspr DBAT2L, r9
	mtspr IBAT2U, r10
	mtspr DBAT2U, r10
	sync
	isync

	lis r31, 0x9ff0
	lis r9, __reloc_dest_start@h
	ori r9, r9, __reloc_dest_start@l
	subf r31, r9, r31
2:

	# load and convert addresses
	lis r3, __reloc_source_start@h
	ori r3, r3, __reloc_source_start@l
	oris r3, r3, 0x8000       # physical load address -> cached MEM1

	lis r4, __reloc_source_end@h
	ori r4, r4, __reloc_source_end@l
	oris r4, r4, 0x8000

	lis r5, __reloc_dest_start@h
	ori r5, r5, __reloc_dest_start@l
	add r5, r5, r31

	lis r6, __reloc_dest_end@h
	ori r6, r6, __reloc_dest_end@l
	add r6, r6, r31

_copyLoop:
	# start copying data
	cmpw r3, r4
	bge _copyDone  # are we done?
	lwz r0, 0(r3)  # load a word
	stw r0, 0(r5)  # store it in the dest
	addi r3, r3, 4 # keep moving
	addi r5, r5, 4
	b _copyLoop

_copyDone:
	# Relocate the embedded-PPC GOT and every absolute pointer slot named by
	# GCC's .fixup table.  With a zero bias these loops are harmless.
	lis r3, __got2_start@h
	ori r3, r3, __got2_start@l
	add r3, r3, r31
	lis r4, __got2_end@h
	ori r4, r4, __got2_end@l
	add r4, r4, r31
3:
	cmplw r3, r4
	bge 4f
	lwz r0, 0(r3)
	add r0, r0, r31
	stw r0, 0(r3)
	addi r3, r3, 4
	b 3b
4:
	lis r3, __fixup_start@h
	ori r3, r3, __fixup_start@l
	add r3, r3, r31
	lis r4, __fixup_end@h
	ori r4, r4, __fixup_end@l
	add r4, r4, r31
5:
	cmplw r3, r4
	bge 6f
	lwz r7, 0(r3)            # linked address of the pointer slot
	add r7, r7, r31
	lwz r0, 0(r7)
	add r0, r0, r31
	stw r0, 0(r7)
	addi r3, r3, 4
	b 5b
6:
	# flush D$ for dest
	lis r5, __reloc_dest_start@h     # reload start of dest
	ori r5, r5, __reloc_dest_start@l
	add r5, r5, r31

_flushDcacheLoop:
	cmpw r5, r6
	bge _flushDcacheDone # are we done?
	dcbf 0, r5           # flush D$ for this region
	addi r5, r5, 32      # advance by one D$ block
	b _flushDcacheLoop   # keep going

_flushDcacheDone:
	sync

	# start I$ invalidate
	lis r5, __reloc_dest_start@h     # reload start of dest
	ori r5, r5, __reloc_dest_start@l
	add r5, r5, r31

_invalidateIcacheLoop:
	cmpw r5, r6
	bge _invalidateIcacheDone # are we done?
	icbi 0, r5                # invalidate I$ for this region
	addi r5, r5, 32           # advance by one I$ block
	b _invalidateIcacheLoop   # keep going

_invalidateIcacheDone:
	sync
	isync

	# Clear relocated BSS/SBSS, then install the relocated stack.
	li r0, 0
	lis r3, __bss_start@h
	ori r3, r3, __bss_start@l
	add r3, r3, r31
	lis r4, __sbss_end@h
	ori r4, r4, __sbss_end@l
	add r4, r4, r31
7:
	cmplw r3, r4
	bge 8f
	stw r0, 0(r3)
	addi r3, r3, 4
	b 7b
8:
	lis r1, __stack_top@h
	ori r1, r1, __stack_top@l
	add r1, r1, r31

	# Branch to the relocated continuation.  The bootstrap itself is not
	# copied, so form the target explicitly rather than using a relative bl.
	lis r12, clearRegs@h
	ori r12, r12, clearRegs@l
	add r12, r12, r31
	mtctr r12
	bctr

.section .text
clearRegs:
	li r0, 0
	# don't clear r1, or r2, we just set them above
	li r3, 0
	li r4, 0
	li r5, 0
	li r6, 0
	li r7, 0
	li r8, 0
	li r9, 0
	li r10, 0
	li r11, 0
	li r12, 0
	# don't clear r13, we just set it above
	li r14, 0
	li r15, 0
	li r16, 0
	li r17, 0
	li r18, 0
	li r19, 0
	li r20, 0
	li r21, 0
	li r22, 0
	li r23, 0
	li r24, 0
	li r25, 0
	li r26, 0
	li r27, 0
	li r28, 0
	li r29, 0
	li r30, 0
	li r31, 0

jumpToInit:
	# YOLO, jump into the C code
	bl init
_hang:
	b _hang


.section .note.GNU-stack
_note:
