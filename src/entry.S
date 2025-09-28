#
# NPLL - Entry point
# Copyright (C) 2025 Techflash
#

.section .text
.global _start
.extern init
.extern panic

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
# 6. Zero out the (non-SP/R) registers
#
# 7. Call into the C init function, where we can do things like hardware detection
_start:
	# Load r0 with address of routine to do BATs setup
	lis r0, setupBATs@h
	ori r0, r0, setupBATs@l

	# Clear bit 0, since we want the physical address.  This binary is linked assuming virtual addresses in .text, so r0 = 0x8000xxxx right now, which is no good.  Clear bit 0 to get a real MEM1-physical address.
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


	# [I/D]BAT1L: BPRN=0x00000000, WIMG=0101 (uncached, guarded (it's customary to do MMIO here, and largely not actually care about memory access)), PP=RW
	li r3, 42
	
	# [I/D]BAT1U: BEPI=0xc0000000, BL=256MB, VS=1, VP=1
	lis r4, 0xc000
	ori r4, r4, 0x1fff

	# Set them
	mtspr IBAT1L, r3
	mtspr DBAT1L, r3
	mtspr IBAT1U, r4
	mtspr DBAT1U, r4

	# flush the changes
	sync
	isync

	# prepare to flip IR and DR on
	mfmsr r0
	ori r0, r0, MSR_IR|MSR_DR
	mtsrr1 r0

	# Alright, we've made the hardware sane, lets get back to real code
	lis r0, clearBSS@h
	ori r0, r0, clearBSS@l
	mtsrr0 r0

	rfi # no need to flip address bits - it's already correct

clearBSS:
	# We now have virtual memory flipped *on*,
	# only a few more steps and we can get out of manual asm hell!
	# Next up we need to clear the BSS.
	#
	# Asumptions:
	# - __bss_start and __bss_end are 4-byte aligned
	# - __bss_end is at least 4 bytes after __bss_start
	#
	# Registers:
	# - r0: zero
	# - r3: bss start / current bss pointer
	# - r4: bss end
	li r0, 0
	lis r3, __bss_start@h
	ori r3, r3, __bss_start@l
	lis r4, __bss_end@h
	ori r4, r4, __bss_end@l

_bssLoop:
	# store 0 at r3
	stw r0, 0(r3)

	# move forward 4 bytes
	addi r3, r3, 4

	# are we done?
	cmplw r3, r4

	# yes!
	bge setupStack

	# nope
	b _bssLoop

setupStack:
	# set r1 (sp) to the top of the stack
	lis r1, __stack_top@h
	ori r1, r1, __stack_top@l

setupSDA:
	# set r2 (Small Data Area 2 pointer) to the start of SDA2
	lis r2, __sda2_start@h
	ori r2, r2, __sda2_start@l

	# set r13 (Small Data Area pointer) to the start of SDA
	lis r13, __sda_start@h
	ori r13, r13, __sda_start@l

clearRegs:
	li r0, 0
	# don't clear r1 nor r2, we just set them above
	li r3, 0 # args for init would go heere if it took any, but it doesn't.
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
	# It should have never returned.  Something has gone wrong - panic.

	lis r3, panicStr@h
	ori r3, r3, panicStr@l
	bl panic

_hang:
	b _hang


.section .rodata
panicStr:
	.asciz "init() returned - this should not be possible"

.section .note.GNU-stack
_note:
