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

static void tvFlush(uint x, uint y, uint width, uint height);
static void tvScroll(uint rows);
static void drcFlush(uint x, uint y, uint width, uint height);

#define TV_FB  ((u32 *)NPLL_WIIU_TV_FB_BASE)
#define DRC_FB ((u32 *)NPLL_WIIU_DRC_FB_BASE)

#define FB_SIZE(info) ((info).width * (info).height * (uint)sizeof(u32))

static u32 *tvShadowFB;
static u32 *drcShadowFB;
static u32 *drcSavedFB;

static struct videoInfo tvVidInfo = {
	.fb = NULL,
	.width = 1280,
	.height = 720,
	.flush = tvFlush,
	.scroll = tvScroll,
	.driver = &fbDrv
};

static struct videoInfo drcVidInfo = {
	.fb = NULL,
	.width = 896,
	/* linux-loader says it's 504 but that places the bottom line offsecreen for me */
	.height = 480,
	.flush = drcFlush,
	.scroll = NULL,
	.driver = &fbDrv
};

static void flushFB(u32 *realFB, u32 *shadowFB, uint stride,
    uint x, uint y, uint width, uint height) {
	uint row;
	u32 *src = shadowFB + y * stride + x;
	u32 *dest = realFB + y * stride + x;
	uint rowSize = width * (uint)sizeof(u32);

	for (row = 0; row < height; row++) {
		memcpy(dest, src, rowSize);
		src += stride;
		dest += stride;
	}

	/* Flush one span to avoid paying for a sync on every scanline. */
	dcache_flush(realFB + y * stride + x,
	    ((height - 1) * stride + width) * sizeof(u32));
}

static void tvFlush(uint x, uint y, uint width, uint height) {
	flushFB(TV_FB, tvShadowFB, tvVidInfo.width, x, y, width, height);
}

static void drcFlush(uint x, uint y, uint width, uint height) {
	uint row, col;
	u32 pixel, *src, *dest;

	for (row = 0; row < height; row++) {
		src = drcShadowFB + (y + row) * drcVidInfo.width + x;
		dest = DRC_FB + (y + row) * drcVidInfo.width + x;
		for (col = 0; col < width; col++) {
			pixel = src[col];
			/* linux-loader's DRC surface uses G/R/B? */
			pixel = (pixel & 0xff0000ffu) |
			    ((pixel & 0x00ff0000u) >> 8) |
			    ((pixel & 0x0000ff00u) << 8);
			dest[col] = pixel;
		}
	}
	dcache_flush(DRC_FB + y * drcVidInfo.width + x,
	    ((height - 1) * drcVidInfo.width + width) * sizeof(u32));
}

static void tvScroll(uint rows) {
	uint rowSize = tvVidInfo.width * (uint)sizeof(u32);
	uint size = (tvVidInfo.height - rows) * rowSize;

	memmove(TV_FB, (const u8 *)TV_FB + rows * rowSize, size);
	dcache_flush(TV_FB, size);
}

static void fbInit(void) {
	tvShadowFB = M_PoolAlloc(POOL_MEM2, FB_SIZE(tvVidInfo), 32);
	drcShadowFB = M_PoolAlloc(POOL_MEM2, FB_SIZE(drcVidInfo), 32);
	drcSavedFB = M_PoolAlloc(POOL_MEM2, FB_SIZE(drcVidInfo), 32);

	/*
	 * Preserve linux-loader's console before taking over its DRC surface.
	 * It only redraws pixels that its console considers dirty after handoff.
	 */
	dcache_invalidate(DRC_FB, FB_SIZE(drcVidInfo));
	memcpy(drcSavedFB, DRC_FB, FB_SIZE(drcVidInfo));

	/* clean up the format so it presents itself as XRGB like we expect */
	DGRPH_SWAP_CNTL = DGRPH_CROSSBAR_RGBA(R, G, B, A) | DGRPH_ENDIAN_SWAP_32;
	DGRPH_CONTROL = DGRPH_DEPTH_32BPP | DGRPH_FORMAT_32BPP_ARGB8888 | DGRPH_ARRAY_LINEAR_ALIGNED;

	/* clear to black */
	memset(TV_FB, 0, FB_SIZE(tvVidInfo));
	dcache_flush(TV_FB, FB_SIZE(tvVidInfo));
	memset(DRC_FB, 0, FB_SIZE(drcVidInfo));
	dcache_flush(DRC_FB, FB_SIZE(drcVidInfo));
	memset(tvShadowFB, 0, FB_SIZE(tvVidInfo));
	memset(drcShadowFB, 0, FB_SIZE(drcVidInfo));

	tvVidInfo.fb = tvShadowFB;
	drcVidInfo.fb = drcShadowFB;
	V_Register(&tvVidInfo);
	V_Register(&drcVidInfo);

	/* we're all good */
	fbDrv.state = DRIVER_STATE_READY;
}

static void fbCleanup(void) {
	memcpy(DRC_FB, drcSavedFB, FB_SIZE(drcVidInfo));
	dcache_flush(DRC_FB, FB_SIZE(drcVidInfo));
	free(drcSavedFB);
	free(drcShadowFB);
	free(tvShadowFB);
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
