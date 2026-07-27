/*
 * NPLL - Latte Hardware - Simple framebuffer
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <string.h>
#include <npll/allocator.h>
#include <npll/cache.h>
#include <npll/drivers.h>
#include <npll/latte/r600.h>
#include <npll/types.h>
#include <npll/utils.h>
#include <npll/video.h>

static REGISTER_DRIVER(fbDrv);

static void fbFlush(uint x, uint y, uint width, uint height);
static void fbScroll(uint rows);

/* TV */
#define REAL_FB (u32 *)(MEM2_CACHED_BASE + 0x07500000)
#if 0
/* DRC/Gamepad */
#define REAL_FB (u32 *)(MEM2_CACHED_BASE + 0x078c0000)
#endif

#define FB_SIZE (uint)(fbVidInfo.width * fbVidInfo.height * (uint)sizeof(u32))

static const u32 *realFb = REAL_FB;
static u32 *shadowFb;

static struct videoInfo fbVidInfo = {
	.fb = NULL, /* TV */
	.width = 1280,
	.height = 720,
#if 0
	.fb = NULL, /* DRC/Gamepad */
	.width = 896, /* weird width/height, comes from linux-loader, check this? */
	.height = 504,
#endif
	.flush = fbFlush,
	.scroll = fbScroll,
	.driver = &fbDrv
};


static void fbFlush(uint x, uint y, uint width, uint height) {
	uint row;
	u32 *src = shadowFb + y * fbVidInfo.width + x;
	u32 *dest = (u32 *)realFb + y * fbVidInfo.width + x;
	uint rowSize = width * (uint)sizeof(u32);

	for (row = 0; row < height; row++) {
		memcpy(dest, src, rowSize);
		src += fbVidInfo.width;
		dest += fbVidInfo.width;
	}

	/* Flush one span to avoid paying for a sync on every scanline. */
	dcache_flush((u32 *)realFb + y * fbVidInfo.width + x,
	    ((height - 1) * fbVidInfo.width + width) * sizeof(u32));
}

static void fbScroll(uint rows) {
	uint rowSize = fbVidInfo.width * (uint)sizeof(u32);
	uint size = (fbVidInfo.height - rows) * rowSize;

	memmove((void *)realFb, (const u8 *)realFb + rows * rowSize, size);
	dcache_flush(realFb, size);
}

static void fbInit(void) {
	/* clean up the format so it presents itself as XRGB like we expect */
	DGRPH_SWAP_CNTL = DGRPH_CROSSBAR_RGBA(R, G, B, A) | DGRPH_ENDIAN_SWAP_32;
	DGRPH_CONTROL = DGRPH_DEPTH_32BPP | DGRPH_FORMAT_32BPP_ARGB8888 | DGRPH_ARRAY_LINEAR_ALIGNED;

	/* clear to black */
	memset((void *)realFb, 0, FB_SIZE);
	dcache_flush(realFb, FB_SIZE);
	shadowFb = M_PoolAlloc(POOL_MEM2, FB_SIZE, 32);
	memset(shadowFb, 0, FB_SIZE);

	/* register w/ video subsys */
	fbVidInfo.fb = (void *)shadowFb;
	V_Register(&fbVidInfo);

	/* we're all good */
	fbDrv.state = DRIVER_STATE_READY;
}

static void fbCleanup(void) {
	free(shadowFb);
	fbDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(fbDrv) = {
	.name = "Latte Framebuffer",
	.mask = DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_GFX,
	.init = fbInit,
	.cleanup = fbCleanup
};
