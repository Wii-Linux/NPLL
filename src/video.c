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
#include <npll/irq.h>
#include <npll/block.h>
#include <npll/endian.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/output.h>
#include <npll/panic.h>
#include <npll/timer.h>
#include <npll/utils.h>
#include <npll/video.h>

struct videoInfo *V_ActiveDriver = NULL;
u32 *V_FbPtr;
uint V_FbWidth, V_FbHeight, V_FbStride;
#define MAX_FRAMEBUFFERS 4
#define MAX_NAME 128
struct videoConsole {
	struct videoInfo *info;
	struct outputDevice outDev;
	char name[MAX_NAME];
	uint posX, posY;
	bool isInEscape;
	uint escapeLen;
	char escapeBuf[8];
	uint stateColorIdx[2];
	u32 stateColor[2];
	volatile bool dirty;
	volatile uint dirtyMinX, dirtyMinY, dirtyMaxX, dirtyMaxY;
};
static struct videoConsole consoles[MAX_FRAMEBUFFERS];
static uint numConsoles;
static struct videoConsole *activeConsole;
static volatile bool fbLocked = false;
/*
 * Each console tracks its own dirty rectangle. This avoids redundant native
 * framebuffer copies while allowing displays with different dimensions to
 * wrap and scroll independently.
 */
/* 15 FPS */
#define FRAME_MS_TARGET 66
#define FONT_WIDTH 8
#define FONT_HEIGHT 16

extern u8 font[];

#define videoOutDev (activeConsole->outDev)
#define posX (activeConsole->posX)
#define posY (activeConsole->posY)
#define isInEscape (activeConsole->isInEscape)
#define escapeLen (activeConsole->escapeLen)
#define escapeBuf (activeConsole->escapeBuf)
#define colorIdx (activeConsole->stateColorIdx)
#define color (activeConsole->stateColor)
#define CUR_FB (activeConsole->info->fb)
#define CUR_WIDTH (activeConsole->info->width)
#define CUR_HEIGHT (activeConsole->info->height)

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

static void odevWriteChar(char c);

static void markDirty(uint x, uint y, uint width, uint height) {
	bool irqs;
	uint maxX = x + width;
	uint maxY = y + height;

	irqs = IRQ_DisableSave();
	if (!activeConsole->dirty) {
		activeConsole->dirtyMinX = x;
		activeConsole->dirtyMinY = y;
		activeConsole->dirtyMaxX = maxX;
		activeConsole->dirtyMaxY = maxY;
		activeConsole->dirty = true;
	} else {
		if (x < activeConsole->dirtyMinX)
			activeConsole->dirtyMinX = x;
		if (y < activeConsole->dirtyMinY)
			activeConsole->dirtyMinY = y;
		if (maxX > activeConsole->dirtyMaxX)
			activeConsole->dirtyMaxX = maxX;
		if (maxY > activeConsole->dirtyMaxY)
			activeConsole->dirtyMaxY = maxY;
	}
	IRQ_Restore(irqs);
}

static char *parsenum(const char *s, uint *num) {
	char *end;
	*num = (uint)strtoul(s, &end, 10);
	return end;
}

static void handleEscape(char c) {
	uint num, mode, i, tmp, val;
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

		if (c == 'A' || c == 'F') {
			if (posY < num)
				posY = 0;
			else
				posY -= num;
		}
		if (c == 'C')
			posX += num;
		if (c == 'D') {
			if (posX < num)
				posX = 0;
			else
				posX -= num;
		}
		if (c == 'B' || c == 'E')
			posY += num;
		if (c == 'E' || c == 'F')
			posX = 0;
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
			memset(CUR_FB, 0, CUR_WIDTH * CUR_HEIGHT * sizeof(u32));
			markDirty(0, 0, CUR_WIDTH, CUR_HEIGHT);
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
	uint fontSz, size;
	u8 *srcAddr, *startZeroAddr;

	if (posY < videoOutDev.rows)
		return;

	posY = videoOutDev.rows - 1;
	fontSz = CUR_WIDTH * sizeof(u32) * FONT_HEIGHT;
	srcAddr = (u8 *)CUR_FB + fontSz;
	size = CUR_WIDTH * sizeof(u32) * (CUR_HEIGHT - FONT_HEIGHT);

	/*
	 * Bring the driver's native framebuffer up to date before shifting it.
	 * Otherwise a line written since the previous periodic flush would be
	 * missing from the scrolled native framebuffer.
	 */
	if (activeConsole->info->scroll)
		V_Flush();

	memmove(CUR_FB, srcAddr, size);
	startZeroAddr = (u8 *)CUR_FB + size;
	memset(startZeroAddr, 0, fontSz);

	if (activeConsole->info->scroll) {
		activeConsole->info->scroll(FONT_HEIGHT);
		markDirty(0, videoOutDev.rows * FONT_HEIGHT - FONT_HEIGHT,
		    CUR_WIDTH, FONT_HEIGHT);
	} else {
		markDirty(0, 0, CUR_WIDTH, CUR_HEIGHT);
	}
}

static void odevWriteChar(char c) {
	u8 *row, dat;
	u32 *dst;
	uint x, y, spc;

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

	row = font + ((u8)c * FONT_HEIGHT);
	dst = CUR_FB + (posY * FONT_HEIGHT * CUR_WIDTH) +
	    (posX * FONT_WIDTH);

	for (y = 0; y < FONT_HEIGHT; y++) {
		dat = *row;
		for (x = 0; x < FONT_WIDTH; x++) {
			if (dat & (1 << (7 - x)))
				dst[x] = color[0]; /* fg */
			else
				dst[x] = color[1]; /* bg */
		}

		row++;
		dst += CUR_WIDTH;
	}
	markDirty(posX * FONT_WIDTH, posY * FONT_HEIGHT,
	    FONT_WIDTH, FONT_HEIGHT);

	/* done writing */
	posX++;

}

static void videoWriteChar(const struct outputDevice *dev, const char c) {
	activeConsole = dev->priv;
	odevWriteChar(c);
}

static void videoWriteStr(const struct outputDevice *dev, const char *str) {
	activeConsole = dev->priv;
	while (*str) {
		odevWriteChar(*str);
		str++;
	}
}

#ifdef VID_BENCH
static u64 flushTB;
static u64 tbStart[300] = {0};
static u64 tbEnd[300] = {0};
static uint numFlushes = 0;
extern u32 ticksPerUsec;
#endif
void V_Flush(void) {
	struct videoConsole *console;
	struct videoInfo *info;
	uint x, y, width, height, i;
	#ifdef VID_BENCH
	u64 total;
	int benchIdx;
	#endif
	assert_msg(V_ActiveDriver, "Tried to V_Flush with no driver");

	if (!V_LockFB())
		return;

	#ifdef VID_BENCH
	tbStart[numFlushes] = mftb();
	#endif
	for (i = 0; i < numConsoles; i++) {
		console = &consoles[i];
		info = console->info;
		if (!console->dirty || !info->flush)
			continue;

		/* clear first so a write during the flush remains pending */
		x = console->dirtyMinX;
		y = console->dirtyMinY;
		width = console->dirtyMaxX - x;
		height = console->dirtyMaxY - y;
		console->dirty = false;
		info->flush(x, y, width, height);
	}
	#ifdef VID_BENCH
	tbEnd[numFlushes] = mftb();
	numFlushes++;

	if (T_HasElapsed(flushTB, 5000 * 1000)) {
		total = 0;
		for (benchIdx = 0; benchIdx < numFlushes; benchIdx++)
			total += tbEnd[benchIdx] - tbStart[benchIdx];
		total /= ticksPerUsec;
		total /= numFlushes;
		log_printf("avg V_Flush us: %u\r\n", total);
		flushTB = mftb();
		numFlushes = 0;
	}
	#endif

	V_UnlockFB();
}

static void flushWrapper(void *arg) {
	(void)arg;

	V_Flush();
}

void V_Register(struct videoInfo *info) {
	struct videoConsole *console;

	assert_msg(info, "Tried to register NULL videoInfo");
	assert_msg(info->fb, "Tried to register videoInfo with no framebuffer");
	assert_msg(numConsoles < MAX_FRAMEBUFFERS, "Too many framebuffer drivers");

	log_printf("Registering driver %s: %dx%d @ 0x%08x\r\n", info->driver->name, info->width, info->height, info->fb);
	console = &consoles[numConsoles++];
	memset(console, 0, sizeof(*console));
	console->info = info;
	snprintf(console->name, MAX_NAME, "%s %dx%d - Framebuffer console", info->driver->name, info->width, info->height);
	console->outDev.name = console->name;
	console->outDev.writeChar = videoWriteChar;
	console->outDev.writeStr = videoWriteStr;
	console->outDev.ansiEscSupport = true;
	console->outDev.columns = info->width / FONT_WIDTH;
	console->outDev.rows = info->height / FONT_HEIGHT;
	console->outDev.driver = info->driver;
	console->outDev.priv = console;
	console->dirtyMaxX = info->width;
	console->dirtyMaxY = info->height;
	console->dirty = true;
	console->stateColorIdx[0] = C_LGRAY;
	console->stateColorIdx[1] = C_BLACK;
	console->stateColor[0] = colors[C_LGRAY];
	console->stateColor[1] = colors[C_BLACK];
	activeConsole = console;

	if (!V_ActiveDriver) {
		V_FbPtr = info->fb;
		V_FbWidth = info->width;
		V_FbHeight = info->height;
		V_FbStride = info->width * sizeof(u32);
		V_ActiveDriver = info;

	#ifdef VID_BENCH
	flushTB = mftb();
	#endif
		T_QueueRepeatingEvent(FRAME_MS_TARGET * 1000, flushWrapper, NULL);
	}
	O_AddDevice(&console->outDev);
}

bool V_LockFB(void) {
	bool irqs;
	bool locked = false;

	irqs = IRQ_DisableSave();
	if (!fbLocked) {
		fbLocked = true;
		locked = true;
	}
	IRQ_Restore(irqs);

	return locked;
}

void V_UnlockFB(void) {
	bool irqs;

	irqs = IRQ_DisableSave();
	fbLocked = false;
	IRQ_Restore(irqs);
}
struct bmpHdr {
	char magic[2];
	u32 size;
	u16 rsrvd[2];
	u32 dataOff;
} __attribute__((packed));
enum bihCompMethod : u32 {
	a = 0
};
struct dibBitmapInfoHdr {
	u32 hdrSize;
	i32 bmpWidth;
	i32 bmpHeight;
	u16 numColorPlanes;
	u16 bpp;
	enum bihCompMethod compMethod;
	u32 rawSize;
	i32 horizPPM;
	i32 vertPPM;
	u32 numPaletteColors;
	u32 numImportantColors;
} __attribute__((packed));

struct bmp {
	struct bmpHdr bmpHdr;
	struct dibBitmapInfoHdr dibHdr;
} __attribute__((packed));

static bool screenshotDevice(const struct blockDevice *bdev) {
	return !(bdev->flags & BLOCK_FLAG_READ_ONLY) &&
	       (!strcmp(bdev->name, "sdhci0") ||
	        !strcmp(bdev->name, "sdgecko-a") ||
	        !strcmp(bdev->name, "sdgecko-b") ||
	        !strcmp(bdev->name, "sdgecko-sp1") ||
	        !strcmp(bdev->name, "sdgecko-sp2"));
}

int V_SaveScreenshot(void) {
	struct filesystem *oldFS = FS_Mounted, *targetFS = NULL;
	struct partition *oldPart = FS_MountedPartition, *targetPart = NULL;
	u8 *file, *row;
	struct bmp *bmp;
	u32 rowSize, imageSize, fileSize, pixel;
	uint i, j, x, y;
	int fd = -1, ret = -1;

	if (!V_ActiveDriver)
		return -1;
	if (oldPart && screenshotDevice(oldPart->bdev) && oldFS) {
		targetFS = oldFS;
		targetPart = oldPart;
	}
	else {
		for (i = 0; i < B_NumDevices && !targetPart; i++) {
			if (!screenshotDevice(B_Devices[i]))
				continue;
			for (j = 0; j < B_Devices[i]->numPartitions; j++) {
				targetFS = FS_Probe(B_Devices[i]->partitions[j]);
				if (targetFS) {
					targetPart = B_Devices[i]->partitions[j];
					break;
				}
			}
		}
	}
	if (!targetPart) {
		log_puts("screenshot: no writable FAT SD/SDGecko found");
		return -1;
	}
	if (targetPart != oldPart && FS_Mount(targetFS, targetPart))
		return -1;

	rowSize = (V_FbWidth * 3u + 3u) & ~3u;
	imageSize = rowSize * V_FbHeight;
	fileSize = (u32)sizeof(*bmp) + imageSize;
	file = malloc(fileSize);
	if (!file)
		goto out;

	memset(file, 0, fileSize);
	bmp = (struct bmp *)file;
	row = file + sizeof(*bmp);
	memcpy(bmp->bmpHdr.magic, "BM", 2);
	bmp->bmpHdr.size = npll_cpu_to_le32(fileSize);
	bmp->bmpHdr.dataOff = npll_cpu_to_le32(sizeof(*bmp));
	bmp->dibHdr.hdrSize = npll_cpu_to_le32(sizeof(bmp->dibHdr));
	bmp->dibHdr.bmpWidth = (i32)npll_cpu_to_le32(V_FbWidth);
	bmp->dibHdr.bmpHeight = (i32)npll_cpu_to_le32(V_FbHeight);
	bmp->dibHdr.numColorPlanes = npll_cpu_to_le16(1);
	bmp->dibHdr.bpp = npll_cpu_to_le16(24);
	bmp->dibHdr.rawSize = npll_cpu_to_le32(imageSize);

	if (!V_LockFB()) {
		log_puts("V_LockFB failed");
		goto free_file;
	}
	for (y = V_FbHeight; y-- > 0;) {
		for (x = 0; x < V_FbWidth; x++) {
			pixel = V_FbPtr[y * V_FbWidth + x];
			row[x * 3] = (u8)pixel;
			row[x * 3 + 1] = (u8)(pixel >> 8);
			row[x * 3 + 2] = (u8)(pixel >> 16);
		}
		row += rowSize;
	}
	V_UnlockFB();

	fd = FS_Create("npllssht.bmp");
	if (fd < 0) {
		log_printf("FS_Create failed: %d\r\n", fd);
		goto free_file;
	}
	ret = (int)FS_Write(fd, file, fileSize);
	if (ret == (int)fileSize) {
		ret = 0;
		log_printf("screenshot: wrote npllssht.bmp to %s\r\n", targetPart->bdev->name);
	}
	else
		log_printf("FS_Write failed: %d\r\n", ret);
free_file:
	free(file);
out:
	if (fd >= 0)
		FS_Close(fd);
	if (targetPart != oldPart) {
		FS_Unmount();
		if (oldFS && oldPart)
			(void)FS_Mount(oldFS, oldPart);
	}
	if (ret)
		log_printf("screenshot: write failed (%d)\r\n", ret);
	return ret;
}
