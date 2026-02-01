/*
 * NPLL - Top-level video handling
 *
 * Copyright (C) 2025-2026 Techflash
 *
 * ANSI Escape code parsing based on code from U-Boot:
 * Copyright (c) 2015 Google, Inc
 * (C) Copyright 2001-2015
 * DENX Software Engineering -- wd@denx.de
 * Compulab Ltd - http://compulab.co.il/
 * Bernecker & Rainer Industrieelektronik GmbH - http://www.br-automation.com
 */

#define MODULE "VIDEO"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <npll/log.h>
#include <npll/output.h>
#include <npll/panic.h>
#include <npll/timer.h>
#include <npll/utils.h>
#include <npll/video.h>

struct videoInfo *V_ActiveDriver = NULL;
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
static bool isInEscape = false;
static int escapeLen = 0;
static char escapeBuf[8];

/* stolen from the VGA color palette */
static u32 colors[16] = {
	0xFF000000, /* black */      0xFF555555, /* gray */
	0xFFAA0000, /* red */        0xFFFF0000, /* bright red */
	0xFF00AA00, /* green */      0xFF55FF55, /* bright green */
	0xFFAA5500, /* brown */      0xFFFFFF55, /* yellow */
	0xFF0000AA, /* blue */       0xFF5555FF, /* light blue */
	0xFFAA00AA, /* magenta */    0xFFFF55FF, /* bright magenta */
	0xFF00AAAA, /* cyan */       0xFF55FFFF, /* bright cyan */
	0xFFAAAAAA, /* light gray */ 0xFFFFFFFF, /* white */
};

static int colorIdx[2];
static u32 color[2];
static void odevWriteChar(char c);

static char *parsenum(const char *s, int *num) {
	char *end;
	*num = strtol(s, &end, 10);
	return end;
}

static void handleEscape(char c) {
	int num, mode, i, tmp, val;
	char *s, *end;

	/* Sanity checking for bogus ESC sequences: */
	if (escapeLen >= sizeof(escapeBuf))
		goto error;
	if (escapeLen == 0) {
		switch (c) {
		case '[':
			break;
		default:
			goto error;
		}
	}

	escapeBuf[escapeLen++] = c;

	/*
	 * Escape sequences are terminated by a letter, so keep
	 * accumulating until we get one:
	 */
	if (!isalpha(c))
		return;

	/*
	 * clear escape mode first, otherwise things will get highly
	 * surprising if you hit any debug prints that come back to
	 * this console.
	 */
	isInEscape = false;

	switch (c) {
	case 'A':
	case 'B':
	case 'C':
	case 'D':
	case 'E':
	case 'F': {
		s = escapeBuf;

		/*
		 * Cursor up/down: [%dA, [%dB, [%dE, [%dF
		 * Cursor left/right: [%dD, [%dC
		 */
		s++;    /* [ */
		s = parsenum(s, &num);
		if (num == 0)			/* No digit in sequence ... */
			num = 1;		/* ... means "move by 1". */

		if (c == 'A' || c == 'F')
			posY -= num;
		if (c == 'C')
			posX += num;
		if (c == 'D')
			posX -= num;
		if (c == 'B' || c == 'E')
			posY += num;
		if (c == 'E' || c == 'F')
			posX = 0;
		if (posX < 0)
			posX = 0;
		if (posY < 0)
			posY += 0;
		break;
	}
	case 'H':
	case 'f': {
		s = escapeBuf;

		/*
		 * Set cursor position: [%d;%df or [%d;%dH
		 */
		s++;    /* [ */
		s = parsenum(s, &posY);
		s++;    /* ; */
		s = parsenum(s, &posX);

		/*
		 * Video origin is [0, 0], terminal origin is [1, 1].
		 */
		if (posX)
			--posX;
		if (posY)
			--posY;

		break;
	}
	case 'J': {
		/*
		 * Clear part/all screen:
		 *   [J or [0J - clear screen from cursor down
		 *   [1J       - clear screen from cursor up
		 *   [2J       - clear entire screen
		 *
		 * TODO we really only handle entire-screen case, others
		 * probably require some additions to video-uclass (and
		 * are not really needed yet by efi_console)
		 */
		parsenum(escapeBuf + 1, &mode);

		if (mode == 2) {
			memset(V_FbPtr, 0, V_FbStride * V_FbHeight);
			V_Flush();
			posX = 0;
			posY = 0;
		} else {
			log_printf("clear mode was %d\r\n", mode);
			assert_msg(false, "invalid clear mode");
		}
		break;
	}
	case 'K': {
		/*
		 * Clear (parts of) current line
		 *   [0K       - clear line to end
		 *   [2K       - clear entire line
		 */
		parsenum(escapeBuf + 1, &mode);

		if (mode == 2) {
			for (i = posX; i < videoOutDev.columns; i++) {
				odevWriteChar(' ');
			}
		}
		break;
	}
	case 'm': {
		s = escapeBuf;
		end = &escapeBuf[escapeLen];

		/*
		 * Set graphics mode: [%d;...;%dm
		 *
		 * Currently only supports the color attributes:
		 *
		 * Foreground Colors:
		 *
		 *   30	Black
		 *   31	Red
		 *   32	Green
		 *   33	Yellow
		 *   34	Blue
		 *   35	Magenta
		 *   36	Cyan
		 *   37	White
		 *
		 * Background Colors:
		 *
		 *   40	Black
		 *   41	Red
		 *   42	Green
		 *   43	Yellow
		 *   44	Blue
		 *   45	Magenta
		 *   46	Cyan
		 *   47	White
		 */

		s++;    /* [ */
		while (s < end) {
			s = parsenum(s, &val);
			s++;

			switch (val) {
			case 0:
				/* all attributes off */
				colorIdx[0] = C_LGRAY;
				colorIdx[1] = C_BLACK;
				color[0] = colors[colorIdx[0]];
				color[1] = colors[colorIdx[1]];
				break;
			case 1:
				/* bold */
				colorIdx[0] |= 1;
				color[0] = colors[colorIdx[0]];
				break;
			case 7:
				/* reverse video */
				tmp = color[0];
				color[0] = color[1];
				color[1] = tmp;
				break;
			case 30 ... 37:
				/* foreground color */
				/* basically a fast way of changing the color whilst keeping bold state */
				colorIdx[0] = ((val - 30) << 1) | (colorIdx[0] & 1);
				color[0] = colors[colorIdx[0]];
				break;
			case 40 ... 47:
				/* background color */
				/* same as above */
				colorIdx[1] = ((val - 40) << 1) | (colorIdx[1] & 1);
				color[1] = colors[colorIdx[1]];
				break;
			default:
				/* ignore unsupported SGR parameter */
				break;
			}
		}

		break;
	}
	default:
		log_printf("unrecognized escape sequence: %*s\n",
		      escapeLen, escapeBuf);
	}

	return;

error:
	/* something went wrong, just revert to normal mode: */
	isInEscape = false;
}

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

	/* handle ANSI escape code */
	if (isInEscape) {
		handleEscape(c);
		return;
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
	case 0x1b: {
		isInEscape = true;
		escapeLen = 0;
		memset(escapeBuf, 0, sizeof(escapeBuf));
		return;
	}
	}

	/* do we need to wrap? */
	if (posX >= videoOutDev.columns) {
		posX = 0;
		posY++;
		maybeScroll();
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
	.ansiEscSupport = true
};

void V_Flush(void) {
	if (__likely(!T_HasElapsed(lastFlush, FRAME_MS_TARGET * 1000)))
		return;

	assert_msg(V_ActiveDriver, "Tried to V_Flush with no driver");

	if (V_ActiveDriver->flush)
		V_ActiveDriver->flush();
}

void V_Register(struct videoInfo *info) {
	assert_msg(info, "Tried to register NULL videoInfo");
	assert_msg(!V_ActiveDriver, "Tried to register video driver but there's already an active one");

	log_printf("Registering driver %s\r\n", info->driver->name);
	snprintf(odevName, MAX_NAME, "%s - Framebuffer console", info->driver->name);
	videoOutDev.columns = info->width / FONT_WIDTH;
	videoOutDev.rows = info->height / FONT_HEIGHT;

	V_FbPtr = info->fb;
	V_FbWidth = info->width;
	V_FbHeight = info->height;
	V_FbStride = info->width * sizeof(u32);
	V_ActiveDriver = info;

	colorIdx[0] = C_LGRAY;
	colorIdx[1] = C_BLACK;
	color[0] = colors[colorIdx[0]];
	color[1] = colors[colorIdx[1]];

	O_AddDevice(&videoOutDev);
}
