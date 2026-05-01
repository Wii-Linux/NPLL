/*
 * NPLL - Top-level video handling
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _VIDEO_H
#define _VIDEO_H

#include <stdbool.h>
#include <npll/drivers.h>
#include <npll/types.h>

struct videoInfo {
	u32 *fb;
	uint width;
	uint height;
	void (*flush)(void);
	struct driver *driver;
};

extern void V_Flush(void);
extern void V_Register(struct videoInfo *info);
extern bool V_LockFB(void);
extern void V_UnlockFB(void);

extern struct videoInfo *V_ActiveDriver;
extern u32 *V_FbPtr;
extern uint V_FbWidth;
extern uint V_FbHeight;
extern uint V_FbStride;

enum {
	C_BLACK,
	C_GRAY,
	C_RED,
	C_BRED,
	C_GREEN,
	C_BGREEN,
	C_BROWN,
	C_YELLOW,
	C_BLUE,
	C_LBLUE,
	C_MAGENTA,
	C_BMAGENTA,
	C_CYAN,
	C_BCYAN,
	C_LGRAY,
	C_WHITE
};

#endif /* _VIDEO_H */
