/*
 * NPLL - Timing
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdbool.h>
#include <npll/timer.h>
#include <npll/types.h>
#include <npll/console.h>

/* bus clock / 4 */
static u32 ticksPerUsec[] = {
	0, /* platform 0 (invalid) */
	162 / 4, /* platform 1 (GameCube) */
	243 / 4, /* platform 2 (Wii) */
	248 / 4, /* platform 3 (Wii U) */
};


/* spin waiting for [ticks] ticks of the timebase */
static void spinOnTB(u64 ticks) {
	u64 start = mftb();
	while ((mftb() - start) < ticks) {
		/* TODO: would do real work here if it were more advanced */
	}
}

bool T_HasElapsed(u64 startTB, u32 usecSince) {
	u64 tb, ticks;
	tb = mftb();
	ticks = (u64)ticksPerUsec[H_ConsoleType] * usecSince;
	return (tb >= (startTB + ticks));
}

/* delay for [n] microseconds */
void udelay(u32 usec) {
	spinOnTB((u64)ticksPerUsec[H_ConsoleType] * usec);
}
