/*
 * NPLL - Timing
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _TIMER_H
#define _TIMER_H

#include <stdbool.h>
#include <npll/types.h>

/* get the timebase */
static inline u64 mftb(void) {
	u32 hi, hi2, lo;
	hi = hi2 = lo = 0;

	do {
		asm volatile("mftbu %0" : "=r"(hi));
		asm volatile("mftb  %0" : "=r"(lo));
		asm volatile("mftbu %0" : "=r"(hi2));
	/* avoid rollover */
	} while (hi != hi2);

	return (u64)(((u64)hi << 32) | lo);
}

extern void udelay(u32 usec);
extern bool T_HasElapsed(u64 startTB, u32 usecSince);

#endif /* _TIMER_H */

