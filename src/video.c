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
static volatile bool fbLocked = false;

/* 15 FPS */
#define FRAME_MS_TARGET 66
#define MAX_NAME 128
#define FONT_WIDTH 8
#define FONT_HEIGHT 16

extern u8 font[];

static struct outputDevice videoOutDev;

static char odevName[MAX_NAME];
static uint posX = 0, posY = 0;
static bool isInEscape = false;
static uint escapeLen = 0;
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

static uint colorIdx[2];
static u32 color[2];
static void odevWriteChar(char c);

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
			memset(V_FbPtr, 0, V_FbStride * V_FbHeight);
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
	fontSz = ((V_FbWidth * 4) * FONT_HEIGHT);
	srcAddr = (((u8 *)V_FbPtr) + fontSz);
	size = ((V_FbWidth * 4u) * V_FbHeight) - fontSz;
	memmove(V_FbPtr, srcAddr, size);

	startZeroAddr = (srcAddr + size) - fontSz;
	memset(startZeroAddr, 0, fontSz);
}

static void odevWriteChar(char c) {
	u8 *row, dat;
	u32 pix;
	uint x, y, spc, off;

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

	for (y = 0; y < FONT_HEIGHT; y++) {
		dat = *row;
		for (x = 0; x < FONT_WIDTH; x++) {
			off = (V_FbWidth * FONT_HEIGHT * posY) + /* v offset in buf */
				  (V_FbWidth * y) + /* v offset within char */
				  (FONT_WIDTH * posX) + /* h offset in buf */
				  x; /* h offset within char */

			if (dat & (1 << (7 - x)))
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

#ifdef VID_BENCH
static u64 flushTB;
static u64 tbStart[300] = {0};
static u64 tbEnd[300] = {0};
static uint numFlushes = 0;
extern u32 ticksPerUsec;
#endif
void V_Flush(void) {
	#ifdef VID_BENCH
	u64 total;
	int i;
	#endif
	assert_msg(V_ActiveDriver, "Tried to V_Flush with no driver");

	if (!V_ActiveDriver->flush)
		return;

	if (!V_LockFB())
		return;

	#ifdef VID_BENCH
	tbStart[numFlushes] = mftb();
	#endif
	V_ActiveDriver->flush();
	#ifdef VID_BENCH
	tbEnd[numFlushes] = mftb();
	numFlushes++;

	if (T_HasElapsed(flushTB, 5000 * 1000)) {
		total = 0;
		for (i = 0; i < numFlushes; i++)
			total += tbEnd[i] - tbStart[i];
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
	assert_msg(info, "Tried to register NULL videoInfo");
	assert_msg(!V_ActiveDriver, "Tried to register video driver but there's already an active one");

	log_printf("Registering driver %s: %dx%d @ 0x%08x\r\n", info->driver->name, info->width, info->height, info->fb);
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

	#ifdef VID_BENCH
	flushTB = mftb();
	#endif
	O_AddDevice(&videoOutDev);
	T_QueueRepeatingEvent(FRAME_MS_TARGET * 1000, flushWrapper, NULL);
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
