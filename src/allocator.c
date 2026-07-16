/*
 * NPLL - Memory allocation
 *
 * Copyright (C) 2025-2026 Techflash
 *
 *
 * This is a very simple memory allocator.  It is not designed to be very
 * robust, nor all that fast. It is designed to optimize for the case of
 * allocate-once-free-never, large, allocations, as this is the most
 * common case for memory allocations in NPLL.
 *
 * It supports freeing blocks of memory, but only if they're the most
 * recent allocation, otherwise the block is marked and the free is
 * postponed until all the more recent allocations are freed.
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


#define MODULE "allocator"

/* MEM1 and MEM2 */
#define MAX_POOLS 2

/* markers to detect corruption */
#define POOL_HDR_MAGIC "\x7fMEMPOOL"
#define POOL_HDR_MAGIC_SIZE 8

#define BLOCK_HDR_MAGIC "\x7fMBL"
#define BLOCK_HDR_MAGIC_SIZE 4

/* Marks a block that can be freed.
 *
 * This mask forces MIN_ALIGN to be >= 2.
 */
#define BLOCK_SIZE_CAN_FREE_MASK 0x1u

/* Marks a block that has been freed.
 *
 * The size of struct block is smaller than MIN_ALIGN,
 * which guarantees block size 0 is impossible during alloc.
 */
#define BLOCK_SIZE_FREE 0u

#include <npll/allocator.h>
#include <npll/console.h>
#include <npll/log.h>
#include <npll/panic.h>
#include <npll/types.h>
#include <npll/utils.h>
#include <string.h>

extern u32 __reloc_dest_start;

/* align `x` up to the next multiple of `a` (power of 2) */
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

/* align `x` down to the previous multiple of `a` (power of 2) */
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

#define IS_POWER_OF_2(x) ((x) && ((x) & ((x) - 1)) == 0)

#define MIN_ALIGN 32

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

static inline bool _blockCanFree(struct block *block) {
	return !!(block->size & BLOCK_SIZE_CAN_FREE_MASK);
}

static inline struct block *_blockFree(struct block *block) {
	u32 size = (block->size & ~BLOCK_SIZE_CAN_FREE_MASK);
	block->size = BLOCK_SIZE_FREE;
	return (struct block *)((size_t)block + sizeof(struct block) + size);
}

#define PTR_IN_POOL 0
#define PTR_IS_USED 1
#define PTR_IS_FREE 2
static inline bool _poolPtr(struct pool *pool, void *ptr, int what) {
	switch (what) {
		case PTR_IN_POOL:
			return (ptr >= pool->bottom && ptr < pool->top);
		case PTR_IS_USED:
			return (ptr >= pool->cur_bottom && ptr < pool->top);
		case PTR_IS_FREE:
			return (ptr >= pool->bottom && ptr < pool->cur_bottom);
		default:
			return false;
	}
}

/* actually allocate from a pool */
static void *__attribute__((malloc, returns_nonnull, assume_aligned(32))) _poolAlloc(struct pool *pool, size_t size, size_t align) {
	struct block *block;
	void *mem;

	if (__unlikely(!pool))
		panic("_poolAlloc: trying to allocate from NULL pool");

	if (__unlikely(memcmp(pool->magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE)))
		panic("_poolAlloc: corrupted pool metadata");

	/* trying to allocate from nonexistant pool */
	if (__unlikely(!pool->top && !pool->bottom && !pool->cur_bottom))
		panic("_poolAlloc: trying to allocate from nonexistent pool");

	/* write out a 'struct block' at it's start */
	mem = (void *)ALIGN_DOWN(((uintptr_t)pool->cur_bottom - size), align);
	block = (struct block *)(((uintptr_t)mem) - sizeof(struct block));
	if (__unlikely((void *)block < pool->bottom))
		panic("_poolAlloc: out of memory");

	memcpy(&block->magic[0], BLOCK_HDR_MAGIC, BLOCK_HDR_MAGIC_SIZE);
	block->size = (u32)((uintptr_t)pool->cur_bottom - (uintptr_t)mem); /* to the next block */
	if (_blockCanFree(block))
		panic("_poolAlloc: bad size");

	pool->cur_bottom = (void *)block;

	//log_printf("alloc sz %u(%u) from %s; new bottom: 0x%08x, data: 0x%08x\r\n", size, block->size, pool->name, (u32)pool->cur_bottom, (u32)mem);

	return mem;
}

static bool _rangesOverlap(uintptr_t start, uintptr_t end, const struct memRange *range) {
	u32 rangeEnd;

	if (!range->size || range->start > 0xffffffffu - range->size)
		return false;
	rangeEnd = range->start + range->size;
	return start < rangeEnd && range->start < end;
}

static void *_poolAllocAvoid(struct pool *pool, size_t size, size_t align,
			    const struct memRange *avoid, size_t avoidCount, bool commit) {
	uintptr_t originalBottom, candidateTop, mem, blockStart, allocEnd;
	size_t i;
	bool retry;
	struct block *block;

	if (__unlikely(!pool || memcmp(pool->magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE)))
		panic("_poolAllocAvoid: invalid pool");
	if (__unlikely(!pool->top && !pool->bottom && !pool->cur_bottom))
		return NULL;

	originalBottom = (uintptr_t)pool->cur_bottom;
	candidateTop = originalBottom;
	for (;;) {
		if (candidateTop < size)
			return NULL;
		mem = ALIGN_DOWN(candidateTop - size, align);
		if (mem < sizeof(*block))
			return NULL;
		blockStart = mem - sizeof(*block);
		allocEnd = mem + size;
		if (blockStart < (uintptr_t)pool->bottom)
			return NULL;

		retry = false;
		for (i = 0; i < avoidCount; i++) {
			if (_rangesOverlap((uintptr_t)virtToPhys(blockStart),
					   (uintptr_t)virtToPhys(allocEnd), &avoid[i])) {
				candidateTop = (uintptr_t)physToCached(avoid[i].start);
				retry = true;
				break;
			}
		}
		if (!retry)
			break;
	}

	if (!commit)
		return (void *)mem;

	block = (struct block *)blockStart;
	memcpy(block->magic, BLOCK_HDR_MAGIC, BLOCK_HDR_MAGIC_SIZE);
	block->size = (u32)(originalBottom - mem);
	if (_blockCanFree(block))
		panic("_poolAllocAvoid: bad size");
	pool->cur_bottom = block;
	return (void *)mem;
}

/* public function to allocate from a specific (or any) pool */
void *__attribute__((malloc, returns_nonnull, assume_aligned(32))) M_PoolAlloc(enum pool_idx pool, size_t size, size_t align) {
	u32 mem1_free, mem2_free;

	if (align == 0)
		align = MIN_ALIGN; /* any alignment */
	else if (!IS_POWER_OF_2(align))
		panic("M_PoolAlloc: align not a power of 2");
	else if (align < MIN_ALIGN)
		align = MIN_ALIGN; /* also aligns to lower powers of 2 */

	switch (pool) {
	case POOL_MEM1:
	case POOL_MEM2:
		return _poolAlloc(&pools[pool], size, align); /* will be NULL if POOL_MEM2 on GCN */
	case POOL_ANY: {
		if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE)
			return _poolAlloc(&pools[POOL_MEM1], size, align); /* nowhere else for it to go... */

		/* pick whichever pool has more free */

		/* sanity check the data before we use it */
		if (memcmp(pools[POOL_MEM1].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE))
			panic("M_PoolAlloc: corrupted MEM1 pool metadata");
		if (memcmp(pools[POOL_MEM2].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE))
			panic("M_PoolAlloc: corrupted MEM2 pool metadata");

		mem1_free = (u32)((uintptr_t)pools[POOL_MEM1].cur_bottom - (uintptr_t)pools[POOL_MEM1].bottom);
		mem2_free = (u32)((uintptr_t)pools[POOL_MEM2].cur_bottom - (uintptr_t)pools[POOL_MEM2].bottom);

		/*
		 * MEM2 is either MUCH larger or a good bit larger than MEM1,
		 * depending on the platform - the chances that we allocate enough
		 * MEM2 to dip below MEM1 levels of available space is unlikely,
		 * but if so we should balanace it out so we can try to retain the
		 * ability to make large allocations.
		 */
		if (__unlikely(mem1_free > mem2_free))
			return _poolAlloc(&pools[POOL_MEM1], size, align);
		else
			return _poolAlloc(&pools[POOL_MEM2], size, align);
	}
	default:
		panic("M_PoolAlloc with invalid pool");
	}
	__builtin_unreachable();
}

void *__attribute__((malloc, assume_aligned(32))) M_PoolAllocAvoid(
	enum pool_idx pool, size_t size, size_t align, const struct memRange *avoid, size_t avoidCount) {
	void *mem1, *mem2;
	u32 mem1Free, mem2Free;

	if (!avoid || !avoidCount)
		return M_PoolAlloc(pool, size, align);
	if (align == 0)
		align = MIN_ALIGN;
	else if (!IS_POWER_OF_2(align))
		panic("M_PoolAllocAvoid: align not a power of 2");
	else if (align < MIN_ALIGN)
		align = MIN_ALIGN;

	if (pool == POOL_MEM1 || pool == POOL_MEM2) {
		mem1 = _poolAllocAvoid(&pools[pool], size, align, avoid, avoidCount, true);
		return mem1;
	}
	if (pool != POOL_ANY)
		panic("M_PoolAllocAvoid: invalid pool");
	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
		mem1 = _poolAllocAvoid(&pools[POOL_MEM1], size, align, avoid, avoidCount, true);
		return mem1;
	}

	mem1 = _poolAllocAvoid(&pools[POOL_MEM1], size, align, avoid, avoidCount, false);
	mem2 = _poolAllocAvoid(&pools[POOL_MEM2], size, align, avoid, avoidCount, false);
	if (!mem1 && !mem2)
		return NULL;
	if (!mem1)
		return _poolAllocAvoid(&pools[POOL_MEM2], size, align, avoid, avoidCount, true);
	if (!mem2)
		return _poolAllocAvoid(&pools[POOL_MEM1], size, align, avoid, avoidCount, true);

	mem1Free = (u32)((uintptr_t)mem1 - (uintptr_t)pools[POOL_MEM1].bottom);
	mem2Free = (u32)((uintptr_t)mem2 - (uintptr_t)pools[POOL_MEM2].bottom);
	if (mem1Free > mem2Free)
		return _poolAllocAvoid(&pools[POOL_MEM1], size, align, avoid, avoidCount, true);
	return _poolAllocAvoid(&pools[POOL_MEM2], size, align, avoid, avoidCount, true);
}

/* C stdlib free() implementation */
void free(void *ptr) {
	struct pool *pool;
	struct block *block;
	int i;

	for (i = 0; i < MAX_POOLS; i++) {
		pool = &pools[i];
		if (__unlikely(memcmp(pool->magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE)))
			panic("free: corrupted pool metadata");

		if (__unlikely(!pool->top && !pool->bottom && !pool->cur_bottom))
			continue;

		if (!_poolPtr(pool, ptr, PTR_IN_POOL))
			continue; /* wrong pool */
		if (!_poolPtr(pool, ptr, PTR_IS_USED))
			panic("free: double free (freed)");

		block = (struct block *)((size_t)ptr - sizeof(struct block));
		if (__unlikely(memcmp(block->magic, BLOCK_HDR_MAGIC, BLOCK_HDR_MAGIC_SIZE)))
			panic("free: corrupted block metadata");
		if (_blockCanFree(block))
			panic("free: double free (pending)");
		block->size |= BLOCK_SIZE_CAN_FREE_MASK;

		/* deallocate lowest free blocks */
		while (block == pool->cur_bottom && _blockCanFree(block)) {
			block = _blockFree(block);
			pool->cur_bottom = (void *)block;
			//log_puts("successfully freed block!");
			if (!_poolPtr(pool, block, PTR_IN_POOL))
				break; /* empty pool */

			if (__unlikely(memcmp(block->magic, BLOCK_HDR_MAGIC, BLOCK_HDR_MAGIC_SIZE)))
				panic("free: corrupted block metadata");
		}
		return; /* done */
	}
	panic("free: no pool, unknown memory");
}

void M_PoolStats(enum pool_idx pool_idx, u32 *total, u32 *used, u32 *free_bytes, u32 *largest_alloc, u32 *largest_free) {
	struct pool *pool;
	struct block *block;
	u32 pool_total, bottom_free, pending_free, max_alloc, blk_size;

	if (__unlikely(pool_idx == POOL_ANY || pool_idx >= MAX_POOLS))
		panic("M_PoolStats: invalid pool index");

	pool = &pools[pool_idx];
	if (__unlikely(memcmp(pool->magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE)))
		panic("M_PoolStats: corrupted pool metadata");
	if (__unlikely(!pool->top && !pool->bottom && !pool->cur_bottom))
		panic("M_PoolStats: nonexistent pool");

	pool_total = (u32)((uintptr_t)pool->top - (uintptr_t)pool->bottom);
	bottom_free = (u32)((uintptr_t)pool->cur_bottom - (uintptr_t)pool->bottom);
	pending_free = 0;
	max_alloc = 0;

	/* walk all blocks from cur_bottom up to top */
	block = (struct block *)pool->cur_bottom;
	while ((void *)block < pool->top) {
		if (__unlikely(memcmp(block->magic, BLOCK_HDR_MAGIC, BLOCK_HDR_MAGIC_SIZE)))
			panic("M_PoolStats: corrupted block metadata");

		blk_size = block->size & ~BLOCK_SIZE_CAN_FREE_MASK;

		if (_blockCanFree(block))
			pending_free += sizeof(struct block) + blk_size;
		else if (blk_size > max_alloc)
			max_alloc = blk_size;

		block = (struct block *)((uintptr_t)block + sizeof(struct block) + blk_size);
	}

	*total = pool_total;
	*free_bytes = bottom_free + pending_free;
	*used = pool_total - *free_bytes;
	*largest_alloc = max_alloc;
	*largest_free = bottom_free;
}

/* C stdlib malloc() implementation */
void *__attribute__((malloc, returns_nonnull, assume_aligned(32))) malloc(size_t size) {
	return M_PoolAlloc(POOL_ANY, size, MIN_ALIGN);
}

void M_Init(void) {
	TRACE();
	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: {
		/* MEM1 below our binary */
		pools[0].top = &__reloc_dest_start;
		pools[0].bottom = (void *)(MEM1_CACHED_BASE + 0x4000);
		pools[0].cur_bottom = pools[0].top;
		pools[0].name = "MEM1";
		memcpy(pools[0].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE);
		break;
	}
	case CONSOLE_TYPE_WII: {
		/* MEM1 below our binary */
		pools[0].top = &__reloc_dest_start;
		pools[0].bottom = (void *)(MEM1_CACHED_BASE + 0x4000);
		pools[0].cur_bottom = pools[0].top;
		pools[0].name = "MEM1";
		memcpy(pools[0].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE);

		/* MEM2 */
		if (H_WiiMEM2Top)
			pools[1].top = physToCached(H_WiiMEM2Top);
		else
			pools[1].top = (void *)(MEM2_CACHED_BASE + MEM2_SIZE_WII);
		pools[1].bottom = (void *)MEM2_CACHED_BASE;
		pools[1].cur_bottom = pools[1].top;
		pools[1].name = "MEM2";
		memcpy(pools[1].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE);
		break;
	}
	case CONSOLE_TYPE_WII_U: {
		/* NPLL itself is in MEM2, so all non-vector MEM1 is available. */
		pools[0].top = (void *)(MEM1_CACHED_BASE + MEM1_SIZE_WIIU);
		pools[0].bottom = (void *)(MEM1_CACHED_BASE + 0x4000);
		pools[0].cur_bottom = pools[0].top;
		pools[0].name = "MEM1";
		memcpy(pools[0].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE);

		/* MEM2 */
		pools[1].top = (void *)NPLL_WIIU_MEMLOG_BASE;
		pools[1].bottom = (void *)MEM2_CACHED_BASE;
		pools[1].cur_bottom = pools[1].top;
		pools[1].name = "MEM2";
		memcpy(pools[1].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE);
		break;
	}
	}
	log_printf("Memory pool 0: \"%s\", 0x%08x down to 0x%08x\r\n", pools[0].name, pools[0].top, pools[0].bottom);
	if (!memcmp(pools[1].magic, POOL_HDR_MAGIC, POOL_HDR_MAGIC_SIZE))
		log_printf("Memory pool 1: \"%s\", 0x%08x down to 0x%08x\r\n", pools[1].name, pools[1].top, pools[1].bottom);

	return;
}
