/*
 * NPLL - Block/SD I/O statistics
 *
 * Debug instrumentation for splitting load time between the SD bus, the
 * block layer, and everything above it. Compile out with IOSTATS 0.
 *
 * Counters are u32 on purpose: the image has to fit HIVIRT, and 64-bit
 * math would pull __udivdi3 and printf's long-long path in with it.
 * u32 holds ~4GB of bytes and ~71 minutes of microseconds, so nothing
 * here is close to overflowing.
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _IOSTATS_H
#define _IOSTATS_H

#include <stdbool.h>
#include <npll/timer.h>
#include <npll/types.h>

/* Debug instrumentation; costs ~1-2KB of image. Flip to 1 to get a
 * breakdown of load time across the SD bus, the block layer, and
 * everything above it, dumped just before the kernel is entered. */
#define IOSTATS 0

#if IOSTATS

struct ioStats {
	/* sdhc_send_cmd(): every command issued to the controller */
	u32 cmdCount;
	u32 cmdUsecs;      /* wall time inside sdhc_send_cmd, all commands */
	u32 dataCmdCount;  /* subset of the above carrying a data phase */
	u32 dataCmdUsecs;  /* wall time inside sdhc_send_cmd, data commands */
	u32 dataBlocks;    /* blocks requested across all data commands */
	u32 dmaCmdCount;   /* data commands that ran via SDMA */
	u32 pioCmdCount;   /* data commands that ran via PIO */

	/* Breakdown of a data command's cost. setup + wait ~= dataCmdUsecs;
	 * cc is launch -> Command Complete (the card's response latency), so
	 * wait - cc is the data phase itself. */
	u32 setupUsecs;    /* time inside sdhc_next_cmd (register programming) */
	u32 waitUsecs;     /* time in the completion poll loop */
	u32 ccUsecs;       /* launch -> CC, summed over data commands */
	u32 ccCount;       /* data commands that reported CC */
	/* The rest of sdhc_send_cmd, so pre+setup+mid+wait+post reconciles
	 * against cmdUsecs. mid spans the IRQ_Restore between programming the
	 * command and entering the poll loop, where pending IRQs can fire. */
	u32 preUsecs;      /* function entry -> sdhc_next_cmd */
	u32 midUsecs;      /* sdhc_next_cmd -> poll loop */
	u32 postUsecs;     /* poll loop -> return */

	/* sdmmcRead(): the block device entry point */
	u32 devCount;
	u32 devBytes;
	u32 devUsecs;      /* includes cache maintenance + the cmd time above */

	/* Bounce paths in block.c. These are meant to be insurance only:
	 * a nonzero count means something upstream handed us a buffer that
	 * missed the alignment or block-size requirements. */
	u32 bounceCount;
	u32 bounceBytes;
	u32 bounceUsecs;
	u32 chunkedCount;  /* the block-unaligned RMW path */
	u32 chunkedBytes;
	u32 chunkedUsecs;

	/* Timebase at IOStats_MarkStart(), for the wall-clock denominator. */
	u64 wallStartTB;
	bool wallStarted;
};

extern struct ioStats ioStats;

#define IOSTATS_ADD(field, n) (ioStats.field += (u32)(n))
/* Declares [var] and latches the timebase. Compiles to nothing when
 * disabled, so the variable never exists and mftb() is never called;
 * every read of it lives inside IOSTATS_ADD, which also vanishes. */
#define IOSTATS_TB(var) u64 var = mftb()

void IOStats_Reset(void);
/* Zero the counters and start the wall clock. */
void IOStats_MarkStart(void);
/* Must be called before H_PrepareForExecEntry() tears down logging. */
void IOStats_Dump(const char *tag);

#else /* !IOSTATS */

#define IOSTATS_ADD(field, n) ((void)0)
#define IOSTATS_TB(var) ((void)0)
static inline void IOStats_Reset(void) {}
static inline void IOStats_MarkStart(void) {}
static inline void IOStats_Dump(const char *tag) { (void)tag; }

#endif /* IOSTATS */

#endif /* _IOSTATS_H */
