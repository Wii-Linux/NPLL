/*
 * NPLL - PowerPC Exception handling
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

#include <stdio.h>
#include <string.h>
#include <npll/panic.h>
#include <npll/types.h>

extern char exception_2200_start, exception_2200_end;

void sync_before_exec(const void *p, u32 len) {
	u32 a, b;

	a = (u32)p & ~0x1f;
	b = ((u32)p + len + 0x1f) & ~0x1f;

	for ( ; a < b; a += 32)
		asm("dcbst 0,%0 ; sync ; icbi 0,%0" : : "b"(a));

	asm("sync ; isync");
}

void E_Handler(int exception)
{
	u32 *x;
	u32 i;

	printf("\nException %04x occurred!\n", exception);

	x = (u32 *)0x80002000;

	printf("\n R0..R7    R8..R15  R16..R23  R24..R31\n");
	for (i = 0; i < 8; i++) {
		printf("%08x  %08x  %08x  %08x\n", x[0], x[8], x[16], x[24]);
		x++;
	}
	x = (u32 *)0x80002080;

	printf("\n CR/XER    LR/CTR  SRR0/SRR1 DAR/DSISR\n");
	for (i = 0; i < 2; i++) {
		printf("%08x  %08x  %08x  %08x\n", x[0], x[2], x[4], x[6]);
		x++;
	}

	panic("Got fatal exception");
}

void E_Init(void)
{
	u32 vector;
	u32 len_2200;

	for (vector = 0x100; vector < 0x2000; vector += 0x10) {
		u32 *insn = (u32 *)(0x80000000 + vector);

		insn[0] = 0xbc002000;			// stmw 0,0x2000(0)
		insn[1] = 0x38600000 | (u32)vector;	// li 3,vector
		insn[2] = 0x48002202;			// ba 0x2200
		insn[3] = 0;
	}
	sync_before_exec((void *)0x80000100, 0x1f00);

	len_2200 = &exception_2200_end - &exception_2200_start;
	memcpy((void *)0x80002200, &exception_2200_start, len_2200);
	sync_before_exec((void *)0x80002200, len_2200);
}

