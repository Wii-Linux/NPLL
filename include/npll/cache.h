/*
 * NPLL - Cache management
 *
 * Copyright (C) 2025 Techflash
 *
 * Based on code in BootMii ppcskel:
 * Copyright (C) 2008, 2009	Hector Martin "marcan" <marcan@marcansoft.com>
 * Copyright (C) 2009			Haxx Enterprises <bushing@gmail.com>
 * Copyright (c) 2009		Sven Peter <svenpeter@gmail.com>
 *
 * Copyright (C) 2009		bLAStY <blasty@bootmii.org>
 * Copyright (C) 2009		John Kelley <wiidev@kelley.ca>
 *
 * Original license disclaimer:
 * This code is licensed to you under the terms of the GNU GPL, version 2;
 * see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 *
 * Some routines and initialization constants originally came from the
 * "GAMECUBE LOW LEVEL INFO" document and sourcecode released by Titanik
 * of Crazy Nation and the GC Linux project.
*/

#ifndef _CACHE_H
#define _CACHE_H

#include <npll/types.h>
static inline void dcache_flush(const void *p, u32 len) {
	u32 a, b;

	a = (u32)p & ~0x1f;
	b = ((u32)p + len + 0x1f) & ~0x1f;

	for ( ; a < b; a += 32)
		asm("dcbst 0,%0" : : "b"(a));

	asm("sync ; isync");
}

#define sync() asm volatile("sync" ::: "memory")
#define barrier() asm volatile ("" ::: "memory")

#endif /* _CACHE_H */
