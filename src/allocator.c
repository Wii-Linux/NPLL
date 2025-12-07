/*
 * NPLL - Memory allocation
 *
 * Copyright (C) 2025 Techflash
 *
 *
 * This is a very simple memory allocator.  It is not designed to be very
 * robust, nor all that fast. It is designed to optimize for the case of
 * allocate-once-free-never, large, allocations, as this is the most
 * common case for memory allocations in NPLL.
 *
 * It supports freeing blocks of memory, but only if they're the most
 * recent allocation, otherwise the free fails, leaking memory. Being the
 * most recent is actually more likely than you may think, since NPLL
 * runs completely single-threaded, without any multitasking or even
 * interrupts enabled.
 *
 * In contrast to a standard "bump" allocator, this allocator allocates
 * _downwards_ from the top.  The first pool is placed immediately before
 * the relocated NPLL binary in MEM1.  As memory is allocated it then grows
 * closer to the start of MEM1 from there.  It's designed this way since we
 * are expecting to place the kernel into low MEM1, so we want low MEM1 to be
 * free from everything possible, with the only thing being there being the
 * PowerPC Exception handlers, since they are architecturally required to be
 * in low memory.
 *
 * It uses the __assume(), __unlikely(), and __likely() macros to attempt to
 * hint to the compiler about various conditions, for optimization.
 */

/* MEM1 and MEM2 */
#define MAX_POOLS 2

/* markers to detect corruption */
#define POOL_HDR_MAGIC "\x7fMEMPOOL"
#define POOL_HDR_MAGIC_SIZE 8

#define BLOCK_HDR_MAGIC "\x7fMBL"
#define BLOCK_HDR_MAGIC_SIZE 4

#include <npll/panic.h>
#include <npll/types.h>
#include <npll/allocator.h>
#include <npll/utils.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

extern u32 __reloc_dest_start;

/* align `x` up to the next multiple of `a` */
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

/*
 * internal data structure describing a pool
 *
 * valid magic && !top && !bottom && !cur_bottom: no pool here
 */
struct pool {
	/* to verify we haven't borked our metadata */
	u8 magic[POOL_HDR_MAGIC_SIZE];

	/* top of memory (where we start allocating from) */
	void *top;

	/* bottom of memory (lowest address that may be used, the end of the pool) */
	void *bottom;

	/* current bottom of memory (highest unused address within pool) */
	void *cur_bottom;

	/* printable name for debug logs */
	char *name;
};

/* data structure describing a memory block - data follows immediately after */
struct block {
	/* to verify we haven't borked our metadata */
	u8 magic[BLOCK_HDR_MAGIC_SIZE];

	/* size of this allocation */
	u32 size;
};

/* our pools */
static struct pool pools[MAX_POOLS];


/* actually allocate from a pool */
static void *_poolAlloc(struct pool *pool, size_t size) {
	struct block *block;
	void *mem;
	u32 bottom;

	if (__unlikely(!pool))
		return NULL;

	if (__unlikely(memcmp(pool->magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE)))
		panic("allocator: _poolAlloc: corrupted pool metadata");

	/* trying to allocate from nonexistant pool */
	if (__unlikely(!pool->top && !pool->bottom && !pool->cur_bottom))
		return NULL;
	
	bottom = (u32)pool->cur_bottom;

	printf("allocator: allocating %u bytes from pool %s\r\n", size, pool->name);

	/* write out a 'struct block' at it's start */
	mem = (void *)(bottom - size);
	block = (struct block *)(((u32)mem) - sizeof(struct block));
	printf("allocator: block @ 0x%08x, data @ 0x%08x\r\n", (u32)block, (u32)mem);

	memcpy(&block->magic[0], BLOCK_HDR_MAGIC, BLOCK_HDR_MAGIC_SIZE);
	block->size = size;

	/* decrease cur_bottom */
	bottom -= size - sizeof(struct block);
	pool->cur_bottom = (void *)bottom;

	printf("allocator: new pool bottom: 0x%08x, returning 0x%08x\r\n", bottom, mem);

	return mem;
}

/* public function to allocate from a specific (or any) pool */
void *M_PoolAlloc(enum pool_idx pool, size_t size) {
	u32 mem1_free, mem2_free;

	switch (pool) {
	case POOL_MEM1:
	case POOL_MEM2:
		return _poolAlloc(&pools[pool], size); /* will be NULL if POOL_MEM2 on GCN */
	case POOL_ANY: {
		if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE)
			return _poolAlloc(&pools[POOL_MEM1], size); /* nowhere else for it to go... */

		/* pick whichever pool has more free */

		/* sanity check the data before we use it */
		if (memcmp(pools[POOL_MEM1].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE))
			panic("allocator: M_PoolAlloc: corrupted MEM1 pool metadata");
		if (memcmp(pools[POOL_MEM2].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE))
			panic("allocator: M_PoolAlloc: corrupted MEM2 pool metadata");

		mem1_free = (u32)pools[POOL_MEM1].cur_bottom - (u32)pools[POOL_MEM1].bottom;
		mem2_free = (u32)pools[POOL_MEM2].cur_bottom - (u32)pools[POOL_MEM2].bottom;

		/*
		 * MEM2 is either MUCH larger or a good bit larger than MEM1,
		 * depending on the platform - the chances that we allocate enough
		 * MEM2 to dip below MEM1 levels of available space is unlikely,
		 * but if so we should balanace it out so we can try to retain the
		 * ability to make large allocations.
		 */
		if (__unlikely(mem1_free > mem2_free))
			return _poolAlloc(&pools[POOL_MEM1], size);
		else
			return _poolAlloc(&pools[POOL_MEM2], size);
	}
	default:
		panic("M_PoolAlloc with invalid pool");
	}
	__builtin_unreachable();
}

/* C stdlib free() implementation */
void free(void *ptr) {
	u32 bottom, mem;
	struct block *block;
	int i;

	for (i = 0; i < MAX_POOLS; i++) {
		if (__unlikely(memcmp(pools[i].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE)))
			panic("allocator: free: corrupted pool metadata");

		if (__unlikely(!pools[i].top && !pools[i].bottom && !pools[i].cur_bottom))
			continue;

		mem = ((u32)ptr) - sizeof(struct block);
		bottom = (u32)pools[i].cur_bottom;

		/* it's the lowest allocation, we can free it */
		if (mem == bottom) {
			block = (struct block *)mem;
			if (__unlikely(memcmp(block->magic, BLOCK_HDR_MAGIC, BLOCK_HDR_MAGIC_SIZE)))
				panic("allocator: free: corrupted block metadata");

			bottom += sizeof(struct block) + block->size;
			pools[i].cur_bottom = (void *)bottom;
			puts("successfully freed block!");
			break;
		}
	}
}

/* C stdlib malloc() implementation */
void *malloc(size_t size) {
	/* 32B alignment */
	size = ALIGN_UP(size, 32 - sizeof(struct block));
	return M_PoolAlloc(POOL_ANY, size);
}

void M_Init(void) {
	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: {
		/* MEM1 below our binary */
		pools[0].top = &__reloc_dest_start;
		pools[0].bottom = (void *)0x80004000;
		pools[0].cur_bottom = pools[0].top;
		pools[0].name = "MEM1";
		memcpy(pools[0].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE);
		break;
	}
	case CONSOLE_TYPE_WII: {
		/* MEM1 below our binary */
		pools[0].top = &__reloc_dest_start;
		pools[0].bottom = (void *)0x80004000;
		pools[0].cur_bottom = pools[0].top;
		pools[0].name = "MEM1";
		memcpy(pools[0].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE);

		/* MEM2 */
		pools[1].top = (void *)(MEM2_CACHED_BASE + MEM2_SIZE_WII);
		pools[1].bottom = (void *)MEM2_CACHED_BASE;
		pools[1].cur_bottom = pools[1].top;
		pools[1].name = "MEM2";
		memcpy(pools[1].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE);
	}
	case CONSOLE_TYPE_WII_U: {
	}
	}
	printf("Memory pool 0: \"%s\", 0x%08x down to 0x%08x\r\n", pools[0].name, pools[0].top, pools[0].bottom);
	if (!memcmp(pools[1].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE))
		printf("Memory pool 1: \"%s\", 0x%08x down to 0x%08x\r\n", pools[1].name, pools[1].top, pools[1].bottom);

	return;
}
