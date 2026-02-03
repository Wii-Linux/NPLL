/*
 * NPLL - Latte Hardware - Simple framebuffer
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <string.h>
#include <npll/cache.h>
#include <npll/video.h>
#include <npll/drivers.h>
#include <npll/latte/r600.h>

static REGISTER_DRIVER(fbDrv);

static void fbFlush(void);

static struct videoInfo fbVidInfo = {
	.fb = (u32 *)0x97500000, /* TV */
	.width = 1280,
	.height = 720,
#if 0
	.fb = (u32 *)0x978c0000, /* DRC/Gamepad */
	.width = 896, /* weird width/height, comes from linux-loader, check this? */
	.height = 504,
#endif
	.flush = fbFlush,
	.driver = &fbDrv
};


static void fbFlush(void) {
	dcache_flush(fbVidInfo.fb, fbVidInfo.width * fbVidInfo.height * sizeof(u32));
}

static void fbInit(void) {
	/* clean up the format so it presents itself as XRGB like we expect */
	DGRPH_SWAP_CNTL = DGRPH_CROSSBAR_RGBA(R, G, B, A) | DGRPH_ENDIAN_SWAP_32;
	DGRPH_CONTROL = DGRPH_DEPTH_32BPP | DGRPH_FORMAT_32BPP_ARGB8888 | DGRPH_ARRAY_LINEAR_ALIGNED;

	/* clear to black */
	memset(fbVidInfo.fb, 0, (fbVidInfo.width * fbVidInfo.height * sizeof(u32)));

	/* register w/ video subsys */
	V_Register(&fbVidInfo);

	/* we're all good */
	fbDrv.state = DRIVER_STATE_READY;
}

static void fbCleanup(void) {
}

static REGISTER_DRIVER(fbDrv) = {
	.name = "Latte Framebuffer",
	.mask = DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_GFX,
	.init = fbInit,
	.cleanup = fbCleanup
};
