/*
 * NPLL - Misc utilities
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _UTILS_H
#define _UTILS_H

#ifndef __ASSEMBLY__
#  include <npll/console.h>
#  include <npll/log_internal.h>
#  include <npll/types.h>

/* branch predictor helpers */
#  define __assume(cond) do { if (!(cond)) __builtin_unreachable(); } while (0)
#  define __likely(cond) __builtin_expect((cond), 1)
#  define __unlikely(cond) __builtin_expect((cond), 0)
#endif

/* stringification helpers */
#define __stringify(str) #str
#define __stringifyResult(str) __stringify(str)

/* memory helpers */
#ifdef __ASSEMBLY__
#  define CACHED_BASE        0x80000000
#  define UNCACHED_BASE      0xc0000000
#else
#  define CACHED_BASE        0x80000000u
#  define UNCACHED_BASE      0xc0000000u
#endif

#define _physToCached(addr)   ((addr) | CACHED_BASE)
#define _physToUncached(addr) ((addr) | UNCACHED_BASE)
/* works for both cached and uncached */
#define _virtToPhys(addr)     ((addr) & ~UNCACHED_BASE)

#ifndef __ASSEMBLY__
#  define physToCached(x) ((void *)(uintptr_t)_physToCached((uintptr_t)(x)))
#  define physToUncached(x) ((void *)(uintptr_t)_physToUncached((uintptr_t)(x)))
#  define virtToPhys(x) ((void *)(uintptr_t)_virtToPhys((uintptr_t)(x)))
#else
#  define physToCached(x) _physToCached(x)
#  define physToUncached(x) _physToUncached(x)
#  define virtToPhys(x) _virtToPhys(x)
#endif
#define cachedToUncached(x) physToUncached(x)
#define uncachedToCached(x) physToCached(uncachedToPhys(x))
#define cachedToPhys(x) virtToPhys(x)
#define uncachedToPhys(x) virtToPhys(x)

#ifndef __ASSEMBLY__
#  define MEM1_PHYS_BASE     0x00000000u
#  define MEM2_PHYS_BASE     0x10000000u
#  define MEM1_CACHED_BASE   ((uintptr_t)physToCached(MEM1_PHYS_BASE))
#  define MEM1_UNCACHED_BASE ((uintptr_t)physToUncached(MEM1_PHYS_BASE))
#  define MEM2_CACHED_BASE   ((uintptr_t)physToCached(MEM2_PHYS_BASE))
#  define MEM2_UNCACHED_BASE ((uintptr_t)physToUncached(MEM2_PHYS_BASE))
#else
#  define MEM1_PHYS_BASE     0x00000000
#  define MEM2_PHYS_BASE     0x10000000
#  define MEM1_CACHED_BASE   physToCached(MEM1_PHYS_BASE)
#  define MEM1_UNCACHED_BASE physToUncached(MEM1_PHYS_BASE)
#  define MEM2_CACHED_BASE   physToCached(MEM2_PHYS_BASE)
#  define MEM2_UNCACHED_BASE physToUncached(MEM2_PHYS_BASE)
#endif

#define FLIPPER_MMIO_BASE  (UNCACHED_BASE + 0x0c000000)

#define AHB_UNTRUSTED_BASE (UNCACHED_BASE + 0x0d000000)
#define AHB_TRUSTED_OFFSET 0x00800000
#define AHB_BASE           (AHB_UNTRUSTED_BASE + AHB_TRUSTED_OFFSET)

#define HOLLYWOOD_SRAM_BASE (UNCACHED_BASE + 0x0d400000)

#define MEM1_SIZE_GCN      0x01800000
#define MEM1_SIZE_WII      MEM1_SIZE_GCN
#define MEM1_SIZE_NDEV     MEM1_SIZE_WII
#define MEM1_SIZE_TDEV     0x03000000
#define MEM1_SIZE_WIIU     0x02000000

#define MEM2_SIZE_WII      0x04000000
#define MEM2_SIZE_NDEV     0x08000000
/* lie, but this is all that we map */
#define MEM2_SIZE_WIIU     0x10000000

#ifndef __ASSEMBLY__

static inline bool addrIsValidCached(void *_addr) {
	uintptr_t addr = (uintptr_t)_addr;

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

static inline bool addrIsValidUncached(void *_addr) {
	uintptr_t addr = (uintptr_t)_addr;

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
static inline bool addrIsValidPhys(void *_addr) {
	uintptr_t addr = (uintptr_t)_addr;

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
/* to avoid the lack of MODULE */
#define TRACE() _log_printf("TRACE: %s entered\r\n", __func__)
#else
#define TRACE() (void)0
#endif

#define ALIGN(x) __attribute__((aligned(x)))
#define BIT(nr) (1u << (nr))
#define VISIBLE __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))


static inline bool ptrAligned(const void *ptr, u32 align) {
	if (!align)
		return true;

	return ((uintptr_t)ptr % align) == 0;
}

static inline u64 alignDownU64(u64 val, u32 align) {
	return val - (val % align);
}

static inline u64 alignUpU64(u64 val, u32 align) {
	u64 rem;

	rem = val % align;
	if (!rem)
		return val;

	return val + (align - rem);
}

static inline u32 alignDownU32(u32 val, u32 align) {
	return val - (val % align);
}

static inline u32 alignUpU32(u32 val, u32 align) {
	u32 rem;

	rem = val % align;
	if (!rem)
		return val;

	return val + (align - rem);
}

#endif /* !__ASSEMBLY__ */

#endif /* _UTILS_H */
