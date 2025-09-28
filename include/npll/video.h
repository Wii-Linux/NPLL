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

extern u32 *V_FbPtr;
extern int V_FbWidth;
extern int V_FbHeight;
extern int V_FbStride;

#define C_LGRAY 7
#define C_BLACK 1

#endif /* _VIDEO_H */
