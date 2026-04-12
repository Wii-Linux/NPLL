/*
 * NPLL - Reversed Little Endian access helpers
 * derived from Linux drivers/mmc/host/sdhci-pltfm.h:
 * Copyright 2010 MontaVista Software, LLC.
 */

#ifndef _REVLE_H
#define _REVLE_H
#include <npll/cache.h>

static inline void _revLEwritel(volatile void *a, u32 v) {
	sync(); barrier();
	*(vu32*)a = v;
	sync(); barrier();
}

static inline void _revLEwritew(volatile void *a, u16 v) {
	uintptr_t addr;
	u32 tmp, shift;
	volatile void *base;

	addr = (uintptr_t)a;
	base = (volatile void *)(addr & ~0x3u);
	shift = (u32)((addr & 0x2u) * 8u);

	sync(); barrier();
	tmp = *(vu32*)base;
	tmp &= ~(0xffffu << shift);
	tmp |= (v << shift);
	*(vu32*)base = tmp;
	sync(); barrier();

	return ;
}

static inline void _revLEwriteb(volatile void *a, u8 v) {
	uintptr_t addr;
	u32 tmp, shift;
	volatile void *base;

	addr = (uintptr_t)a;
	base = (volatile void *)(addr & ~0x3u);
	shift = (u32)((addr & 0x3u) * 8u);

	sync(); barrier();
	tmp = *(vu32*)base;
	tmp &= ~(0xffu << shift);
	tmp |= (v << shift);
	*(vu32*)base = tmp;
	sync(); barrier();

	return ;
}

static inline u32 _revLEreadl(volatile void *a) {
	u32 ret;

	sync(); barrier();
	ret = *(vu32*)(a);
	sync(); barrier();

	return ret;
}

static inline u16 _revLEreadw(volatile void *a) {
	uintptr_t addr;
	u32 tmp, shift;

	addr = (uintptr_t)a;
	shift = (u32)((addr & 0x2u) * 8u);
	addr &= ~0x3u;
	sync(); barrier();
	tmp = *(vu32*)addr;
	sync(); barrier();

	return (u16)(tmp >> shift);
}

static inline u8 _revLEreadb(volatile void *a) {
	uintptr_t addr;
	u32 tmp, shift;

	addr = (uintptr_t)a;
	shift = (u32)((addr & 0x3u) * 8u);
	addr &= ~0x3u;
	sync(); barrier();
	tmp = *(vu32*)addr;
	sync(); barrier();

	return (u8)(tmp >> shift);
}

#define revLEwritel(a, v) _revLEwritel((volatile void *)(a), v)
#define revLEwritew(a, v) _revLEwritew((volatile void *)(a), v)
#define revLEwriteb(a, v) _revLEwriteb((volatile void *)(a), v)
#define revLEreadl(a) _revLEreadl((volatile void *)(a))
#define revLEreadw(a) _revLEreadw((volatile void *)(a))
#define revLEreadb(a) _revLEreadb((volatile void *)(a))

#endif /* _REVLE_H */
