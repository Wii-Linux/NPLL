/*
 * NPLL - Top-level video handling
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <string.h>
#include <npll/panic.h>
#include <npll/video.h>
#include <npll/timer.h>
#include <npll/output.h>

static struct videoInfo *activeDriver = NULL;
u32 *V_FbPtr;
int V_FbWidth;
int V_FbHeight;
int V_FbStride;
static u64 lastFlush = 0;

/* 60 FPS */
#define FRAME_MS_TARGET 16
#define MAX_NAME 128
#define FONT_WIDTH 8
#define FONT_HEIGHT 16

extern u8 font[];

static struct outputDevice videoOutDev;

static char odevName[MAX_NAME];
static int posX = 0, posY = 0;

static int nextByteIsColor = 0;

/* mostly stolen from the VGA color palette */
static u32 colors[10] = {
	0xFFFFFFFF, /* white */   0xFF000000, /* black */
	0xFFAA0000, /* red */     0xFF00AA00, /* green */
	0xFF0000AA, /* blue */    0xFF00AAAA, /* cyan */
	0xFFAA00AA, /* purple */  0xFFAAAAAA, /* light gray */
	0xFFAA5500, /* orange */  0xFFFFFF00  /* yellow */
};

static u32 color[2];

static void maybeScroll(void) {
	if (posY < videoOutDev.rows)
		return;
	posY = videoOutDev.rows - 1;
	int fontSz = ((V_FbWidth * 4) * FONT_HEIGHT);
	u8 *srcAddr = (((u8 *)V_FbPtr) + fontSz);
	int size = ((V_FbWidth * 4) * V_FbHeight) - fontSz;
	memmove(V_FbPtr, srcAddr, size);

	u8 *startZeroAddr = (srcAddr + size) - fontSz;
	memset(startZeroAddr, 0, fontSz);
}

static void odevWriteChar(char c) {
	u8 *row, dat;
	int x, y;

	if ((u8)c == 0xff) { /* fg */
		nextByteIsColor = 1;
		return;
	}
	else if ((u8)c == 0xfe) { /* bg */
		nextByteIsColor = 2;
		return;
	}

	if (nextByteIsColor) {
		color[nextByteIsColor - 1] = colors[(u8)c];
		nextByteIsColor = 0;
	}

	/* special chars */
	switch (c) {
	case '\b': {
		if (posX) posX--;
		odevWriteChar(' ');
		return;
	}
	case '\r': {
		posX = 0;
		return;
	}
	case '\n': {
		posY++;
		maybeScroll();
		return;
	}
	case '\t': {
		int spc;
		/* round up to nearest 8th char */
		spc = 8 - ((posX + 8) % 8);
		while (spc) {
			odevWriteChar(' ');
			spc--;
		}
		return;
	}
	}

	row = font + (c * FONT_HEIGHT);

	for (y = 0; y < FONT_HEIGHT; y++) {
		dat = *row;
		for (x = 0; x < FONT_WIDTH; x++) {
			u32 pix;
			int off = (V_FbWidth * FONT_HEIGHT * posY) + /* v offset in buf */
				  (V_FbWidth * y) + /* v offset within char */
				  (FONT_WIDTH * posX) + /* h offset in buf */
				  x; /* h offset within char */

			if (dat & (1 << (8 - x)))
				pix = color[0]; /* fg */
			else
				pix = color[1]; /* bg */
			V_FbPtr[off] = pix;
		}

		row++;
	}

	/* done writing */
	posX++;
	
	/* do we need to wrap? */
	if (posX >= videoOutDev.columns) {
		posX = 0;
		posY++;
		maybeScroll();
	}
}

static void odevWriteStr(const char *str) {
	while (*str) {
		odevWriteChar(*str);
		str++;
	}
}

static struct outputDevice videoOutDev = {
	.name = odevName,
	.writeChar = odevWriteChar,
	.writeStr = odevWriteStr,
	.isGraphical = true
};

void V_Flush(void) {
	if (!T_HasElapsed(lastFlush, FRAME_MS_TARGET * 1000))
		return;

	if (!activeDriver)
		panic("Tried to V_Flush with no driver");

	if (activeDriver->flush)
		activeDriver->flush();
}

void V_Register(struct videoInfo *info) {
	if (!info)
		panic("Tried to register NULL videoInfo");

	if (activeDriver)
		panic("Tried to register video driver but there's already an active one");

	printf("VIDEO: Registering driver %s\r\n", info->driver->name);
	snprintf(odevName, MAX_NAME, "%s - Framebuffer console", info->driver->name);
	videoOutDev.columns = info->width / FONT_WIDTH;
	videoOutDev.rows = info->height / FONT_HEIGHT;

	V_FbPtr = info->fb;
	V_FbWidth = info->width;
	V_FbHeight = info->height;
	V_FbStride = info->width * sizeof(u32);
	activeDriver = info;

	color[0] = colors[C_LGRAY];
	color[1] = colors[C_BLACK];

	O_AddDevice(&videoOutDev);
}

