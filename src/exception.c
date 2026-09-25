/*
 * NPLL - PowerPC Exception handling
 *
 * Copyright (C) 2025-2026 Techflash
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
#include <npll/cache.h>
#include <npll/cpu.h>
#include <npll/irq.h>
#include <npll/panic.h>
#include <npll/timer.h>
#include <npll/types.h>
#include <npll/utils.h>

extern char exception_entry_start, exception_entry_end;
extern u32 exception_handler_hi, exception_handler_lo;

static void dump_stack_trace(u32 *sp) {
	u32 prev_sp, lr;
	int depth;
	printf("Stack trace:\n");

	for (depth = 0; sp && depth < 32; depth++) {
		prev_sp = sp[0];
		lr = sp[1];

		printf("  #%d  SP=0x%08x  LR=0x%08x\r\n", depth, (uintptr_t)sp, lr);

		// sanity checks
		if (prev_sp <= (uintptr_t)sp || prev_sp == 0 || prev_sp == 0xffffffff)
			break;

		sp = (u32 *)(uintptr_t)prev_sp;
	}
}

struct exceptionFrame {
	u32 gpr[32];
	u32 cr;
	u32 xer;
	u32 lr;
	u32 ctr;
	u32 srr0;
	u32 srr1;
	u32 dar;
	u32 dsisr;
};

#define MAX_EXCEPTION_RECURSION 32

static struct exceptionFrame exceptionFrame __attribute__((aligned(32)));
static struct exceptionFrame exceptionFrames[MAX_EXCEPTION_RECURSION];
static uint exceptionRecursionCount = 0;

void __attribute__((noreturn)) E_Handler(int exception) {
	u32 sp;
	struct exceptionFrame *frame;
	int i;

	frame = &exceptionFrame;

	if (exception == 0x0500) {
		IRQ_Handle();
		__builtin_unreachable();
	}
	else if (exception == 0x0900) {
		if (exceptionRecursionCount >= MAX_EXCEPTION_RECURSION)
			panic("DEC exception recursion overflow");
		memcpy(&exceptionFrames[exceptionRecursionCount++], frame, sizeof(struct exceptionFrame));
		IRQ_Enable();
		T_DECHandler();
		IRQ_Disable();
		memcpy(frame, &exceptionFrames[--exceptionRecursionCount], sizeof(struct exceptionFrame));
		IRQ_Return();
		__builtin_unreachable();
	}

	printf("\r\nException %04x occurred!\r\n", exception);

	sp = frame->gpr[1];

	printf("\r\n R0..R7    R8..R15  R16..R23  R24..R31\r\n");
	for (i = 0; i < 8; i++)
		printf("%08x  %08x  %08x  %08x\r\n", frame->gpr[0 + i], frame->gpr[8 + i], frame->gpr[16 + i], frame->gpr[24 + i]);

	printf("\r\n CR/XER    LR/CTR  SRR0/SRR1 DAR/DSISR\r\n");
	printf("%08x  %08x  %08x  %08x\r\n", frame->cr, frame->lr, frame->srr0, frame->dar);
	printf("%08x  %08x  %08x  %08x\r\n", frame->xer, frame->ctr, frame->srr1, frame->dsisr);

	dump_stack_trace((u32 *)sp);

	panic("Got fatal exception");
}

void E_Init(void) {
	u32 handler, *insn;
	uintptr_t vector;
	TRACE();

	/*
	 * SPRG0/1 save the original r3/r4, SPRG2/3 hold the physical frame and
	 * entry addresses, keep both free from lowmem loader addresses.
	 */
	handler = (u32)(uintptr_t)E_Handler;
	exception_handler_hi = 0x3c000000 | (handler >> 16);
	exception_handler_lo = 0x60000000 | (handler & 0xffff);
	dcache_flush_icache_invalidate(&exception_entry_start, (uintptr_t)&exception_entry_end - (uintptr_t)&exception_entry_start);
	mtspr(SPRG2, (u32)(uintptr_t)virtToPhys(&exceptionFrame));
	mtspr(SPRG3, (u32)(uintptr_t)virtToPhys(&exception_entry_start));

	for (vector = 0x100; vector < 0x1800; vector += 0x20) {
		insn = physToCached(vector);

		insn[0] = 0x7c7043a6;			// mtspr SPRG0,r3
		insn[1] = 0x7c9143a6;			// mtspr SPRG1,r4
		insn[2] = 0x7c8902a6;			// mfctr r4
		insn[3] = 0x7c7342a6;			// mfspr r3,SPRG3
		insn[4] = 0x7c6903a6;			// mtctr r3
		insn[5] = 0x38600000 | (u32)vector;	// li r3,vector
		insn[6] = 0x4e800420;			// bctr
		insn[7] = 0;
	}
	dcache_flush_icache_invalidate(physToCached(0x100), 0x1700);
}
