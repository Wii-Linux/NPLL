/*
 * NPLL - Block/SD I/O statistics
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "iostats"
#include <string.h>
#include <npll/iostats.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/types.h>

#if IOSTATS

struct ioStats ioStats;

void IOStats_Reset(void) {
	memset(&ioStats, 0, sizeof(ioStats));
}

void IOStats_MarkStart(void) {
	IOStats_Reset();
	ioStats.wallStartTB = mftb();
	ioStats.wallStarted = true;
}

/* bytes per millisecond == kB/s, and dividing by ms instead of scaling
 * bytes by 1000 keeps both operands inside u32. */
static u32 kBps(u32 bytes, u32 usecs) {
	u32 ms = usecs / 1000u;
	return ms ? (bytes / ms) : 0;
}

static u32 pct(u32 part, u32 whole) {
	return whole ? ((part * 100u) / whole) : 0;
}

void IOStats_Dump(const char *tag) {
	u32 wall = ioStats.wallStarted ? T_ElapsedUsecs(ioStats.wallStartTB) : 0;
	u32 above = (wall > ioStats.devUsecs) ? (wall - ioStats.devUsecs) : 0;

	log_printf("%s: wall %u us\r\n", tag, wall);
	log_printf(" dev %u calls %u B %u us %u%% %u kB/s\r\n",
		ioStats.devCount, ioStats.devBytes, ioStats.devUsecs,
		pct(ioStats.devUsecs, wall), kBps(ioStats.devBytes, ioStats.devUsecs));
	log_printf(" cmd %u/%u us data %u dma %u pio %u blk %u/%u us %u kB/s\r\n",
		ioStats.cmdCount, ioStats.cmdUsecs, ioStats.dataCmdCount,
		ioStats.dmaCmdCount, ioStats.pioCmdCount, ioStats.dataBlocks,
		ioStats.dataCmdUsecs,
		kBps(ioStats.dataBlocks * 512u, ioStats.dataCmdUsecs));
	/* Within wait, cc is the card's response latency and the remainder is
	 * the data phase. resid is cmdUsecs minus every span below it: if the
	 * regions cover the function it should be ~0, and anything else means
	 * the breakdown is still hiding something. */
	log_printf(" setup %u wait %u cc %u/%u = %u avg (us)\r\n",
		ioStats.setupUsecs, ioStats.waitUsecs, ioStats.ccUsecs,
		ioStats.ccCount,
		ioStats.ccCount ? (ioStats.ccUsecs / ioStats.ccCount) : 0);
	log_printf(" pre %u mid %u post %u resid %d (us)\r\n",
		ioStats.preUsecs, ioStats.midUsecs, ioStats.postUsecs,
		(int)ioStats.cmdUsecs - (int)ioStats.preUsecs
			- (int)ioStats.setupUsecs - (int)ioStats.midUsecs
			- (int)ioStats.waitUsecs - (int)ioStats.postUsecs);
	/* bounce/chunk should both read zero; nonzero means a buffer reached
	 * the block layer unaligned and paid a memcpy for it. */
	log_printf(" bnc %u/%u B chk %u/%u B above %u us %u%%\r\n",
		ioStats.bounceCount, ioStats.bounceBytes,
		ioStats.chunkedCount, ioStats.chunkedBytes,
		above, pct(above, wall));
}

#endif /* IOSTATS */
