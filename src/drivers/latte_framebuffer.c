/*
 * NPLL - Latte Hardware - Simple framebuffer
 *
 * Copyright (C) 2025 Techflash
 */

#include <string.h>
#include <npll/cache.h>
#include <npll/video.h>
#include <npll/drivers.h>

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
