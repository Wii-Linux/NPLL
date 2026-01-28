/*
 * NPLL - Top-level video handling
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _VIDEO_H
#define _VIDEO_H

#include <npll/drivers.h>
#include <npll/types.h>

struct videoInfo {
	u32 *fb;
	int width;
	int height;
	void (*flush)(void);
	struct driver *driver;
};

extern void V_Flush(void);
extern void V_Register(struct videoInfo *info);

extern struct videoInfo *V_ActiveDriver;
extern u32 *V_FbPtr;
extern int V_FbWidth;
extern int V_FbHeight;
extern int V_FbStride;

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
