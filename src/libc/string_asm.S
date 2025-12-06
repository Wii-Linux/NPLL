/*
 * NPLL - libc - stdring
 *
 * Copyright (C) 2025 Techflash
 */

.global memcmp
.global memcpy
.global memmove
.global memset


memset:
	# Params:
	# r3 = dest start
	# r4 = value
	# r5 = len
	# scratch:
	# r6 = dest end

	# compute dest end
	mr r6, r3
	add r6, r6, r5

_memsetLoop:
	stb r4, 0(r3)

	# move forward by 1 byte
	addi r3, r3, 1

	# are we done?
	cmplw r3, r6

	# yes
	bge _memsetDone

	b _memsetLoop

_memsetDone:
	# return
	blr


.section .note.GNU-stack
_note:
