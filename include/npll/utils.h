/*
 * NPLL - Misc utilities
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _UTILS_H
#define _UTILS_H

#include <npll/console.h>
#include <npll/log.h>
#include <npll/types.h>

/* branch predictor helpers */
#define __assume(cond) do { if (!(cond)) __builtin_unreachable(); } while (0)
#define __likely(cond) __builtin_expect((cond), 1)
#define __unlikely(cond) __builtin_expect((cond), 0)

/* stringification helpers */
#define __stringify(str) #str
#define __stringifyResult(str) __stringify(str)

/* memory helpers */
#define CACHED_BASE        0x80000000
#define UNCACHED_BASE      0xc0000000

static inline void *physToCached(void *_addr) {
	u32 addr = (u32)_addr;
	addr |= CACHED_BASE;
	return (void *)addr;
}

static inline void *physToUncached(void *_addr) {
	u32 addr = (u32)_addr;
	addr |= UNCACHED_BASE;
	return (void *)addr;
}

static inline void *virtToPhys(void *_addr) {
	u32 addr = (u32)_addr;
	addr &= ~UNCACHED_BASE; /* works for both cached and uncached */
	return (void *)addr;
}


#define MEM1_PHYS_BASE     0x00000000
#define MEM1_CACHED_BASE   ((u32)physToCached((void *)MEM1_PHYS_BASE))
#define MEM1_UNCACHED_BASE ((u32)physToUncached((void *)MEM1_PHYS_BASE))

#define MEM2_PHYS_BASE     0x10000000
#define MEM2_CACHED_BASE   ((u32)physToCached((void *)MEM2_PHYS_BASE))
#define MEM2_UNCACHED_BASE ((u32)physToUncached((void *)MEM2_PHYS_BASE))

#define MEM1_SIZE_GCN      0x01800000
#define MEM1_SIZE_WII      MEM1_SIZE_GCN
#define MEM1_SIZE_NDEV     0x04000000
#define MEM1_SIZE_WIIU     0x02000000

#define MEM2_SIZE_WII      0x04000000
#define MEM2_SIZE_NDEV     0x08000000
/* lie, but this is all that we map */
#define MEM2_SIZE_WIIU     0x10000000


static inline int addrIsValidCached(void *_addr) {
	u32 addr = (u32)_addr;

	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: {
		return (addr >= MEM1_CACHED_BASE && addr < (MEM1_CACHED_BASE + MEM1_SIZE_GCN));
	}
	case CONSOLE_TYPE_WII: {
		return ((addr >= MEM1_CACHED_BASE && addr < (MEM1_CACHED_BASE + MEM1_SIZE_WII)) ||
			(addr >= MEM2_CACHED_BASE && addr < (MEM2_CACHED_BASE + MEM2_SIZE_WII)));
	}
	case CONSOLE_TYPE_WII_U: {
		return ((addr >= MEM1_CACHED_BASE && addr < (MEM1_CACHED_BASE + MEM1_SIZE_WIIU)) ||
			(addr >= MEM2_CACHED_BASE && addr < (MEM2_CACHED_BASE + MEM2_SIZE_WIIU)));
	}
	}
	__builtin_unreachable();
}

static inline int addrIsValidUncached(void *_addr) {
	u32 addr = (u32)_addr;

	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: {
		return (addr >= MEM1_UNCACHED_BASE && addr < (MEM1_UNCACHED_BASE + MEM1_SIZE_GCN));
	}
	case CONSOLE_TYPE_WII: {
		return ((addr >= MEM1_UNCACHED_BASE && addr < (MEM1_UNCACHED_BASE + MEM1_SIZE_WII)) ||
			(addr >= MEM2_UNCACHED_BASE && addr < (MEM2_UNCACHED_BASE + MEM2_SIZE_WII)));
	}
	case CONSOLE_TYPE_WII_U: {
		return ((addr >= MEM1_UNCACHED_BASE && addr < (MEM1_UNCACHED_BASE + MEM1_SIZE_WIIU)) ||
			(addr >= MEM2_UNCACHED_BASE && addr < (MEM2_UNCACHED_BASE + MEM2_SIZE_WIIU)));
	}
	}
	__builtin_unreachable();
}

/* don't check >= MEM1_PHYS_BASE, since it inherently must be at least 0 */
static inline int addrIsValidPhys(void *_addr) {
	u32 addr = (u32)_addr;

	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: {
		return addr < (MEM1_PHYS_BASE + MEM1_SIZE_GCN);
	}
	case CONSOLE_TYPE_WII: {
		return ((addr < (MEM1_PHYS_BASE + MEM1_SIZE_WII)) ||
			(addr >= MEM2_PHYS_BASE && addr < (MEM2_PHYS_BASE + MEM2_SIZE_WII)));
	}
	case CONSOLE_TYPE_WII_U: {
		return ((addr < (MEM1_PHYS_BASE + MEM1_SIZE_WIIU)) ||
			(addr >= MEM2_PHYS_BASE && addr < (MEM2_PHYS_BASE + MEM2_SIZE_WIIU)));
	}
	}
	__builtin_unreachable();
}

#ifdef DO_TRACE
#define TRACE() log_printf("TRACE: %s entered\r\n", __func__)
#else
#define TRACE() (void)0
#endif

#define ALIGN(x) __attribute__((aligned(x)))
#define BIT(nr) (1 << (nr))

#endif /* _UTILS_H */
