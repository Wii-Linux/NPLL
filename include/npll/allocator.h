/*
 * NPLL - Memory allocation
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#include <npll/utils.h>
#include <stdint.h>

enum pool_idx {
	POOL_MEM1,
	POOL_MEM2,
	POOL_ANY
};

extern void M_Init(void);
extern void *__attribute__((malloc, returns_nonnull, assume_aligned(32))) M_PoolAlloc(enum pool_idx pool, size_t size);

extern void *__attribute__((malloc, returns_nonnull, assume_aligned(32))) malloc(size_t size);
extern void free(void *ptr);

#endif /* _ALLOCATOR_H */
