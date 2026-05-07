/*
 * NPLL - Flipper/Hollywood Hardware - Drive Interface
 *
 * Copyright (C) 2026 Techflash
 *
 * Drive date checking / DVD-support logic based on: https://github.com/Aeplet/DiscDriveDateChecker
 *
 * Reset handling and format info from gc-linux's gcn-di driver:
 * Copyright (C) 2005-2009 The GameCube Linux Team
 * Copyright (C) 2005,2006,2007,2009 Albert Herranz
 *
 * Portions based on previous work by Scream|CT.
 *
 *
 * GameCube drive firmware patching based on Swiss: https://github.com/emukidid/swiss-gc/blob/master/cube/swiss/source/devices/dvd/dvd.c
 */

#include "di_firmware.h"
#define MODULE "DI"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <npll/block.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/endian.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/regs.h>
#include <npll/timer.h>
#include <npll/types.h>
#include <npll/utils.h>
#include <npll/hollywood/gpio.h>

/* DI commands */
#define DI_CMD_INQUIRY       0x12000000
#define DI_CMD_SETEXTENSION  0x55010000
#define DI_CMD_READ_DISC_ID  0xa8000040
#define DI_CMD_READ_NORMAL   0xa8000000
#define DI_CMD_READ_DVDR     0xd0000000
#define DI_CMD_READ_PHYSINFO 0xad000000
#define DI_CMD_GET_STATUS    0xe0000000
#define DI_CMD_SET_STATUS    0xee060300
#define DI_CMD_DEBUG_BASE    0xfe110000
#define DI_CMD_DEBUG_WRITEMEM 0xfe010100
#define DI_CMD_DEBUG_READMEM 0xfe010000

/* [ff][01]MATSHITA[02][00] */
#define DI_CMD_GCN_UNLOCK1A  0xff014d41 /* [ff][01]MA */
#define DI_CMD_GCN_UNLOCK1B  0x54534849 /* TSHI */
#define DI_CMD_GCN_UNLOCK1C  0x54410200 /* TA[02][00] */

/* [ff][00]DVD-GAME[03][00] */
#define DI_CMD_GCN_UNLOCK2A  0xff004456 /* [ff][00]DV */
#define DI_CMD_GCN_UNLOCK2B  0x442d4741 /* D-GA */
#define DI_CMD_GCN_UNLOCK2C  0x4d450300 /* ME[03][00] */

/* debug bits */
#define DEBUG_STOP_DRIVE 0
#define DEBUG_START_DRIVE 0x100
#define DEBUG_ACCEPT_COPY 0x4000
#define DEBUG_DISC_CHECK 0x8000

/* DISR bits */
#define DI_SR_BRK        BIT(0)
#define DI_SR_DEINTMASK  BIT(1)
#define DI_SR_DEINT      BIT(2)
#define DI_SR_TCINTMASK  BIT(3)
#define DI_SR_TCINT      BIT(4)
#define DI_SR_BRKINTMASK BIT(5)
#define DI_SR_BRKINT     BIT(6)
#define DI_SR_INTMASKS   (DI_SR_DEINTMASK | DI_SR_TCINTMASK | DI_SR_BRKINTMASK)
#define DI_SR_INTS       (DI_SR_DEINT | DI_SR_TCINT | DI_SR_BRKINT)
#define DI_GCN_SR_UNLOCK   (DI_SR_DEINT | DI_SR_TCINT)
#define DI_GCN_SR_DMA_READ (DI_SR_DEINTMASK | DI_SR_DEINT | DI_SR_TCINTMASK | DI_SR_TCINT | DI_SR_BRKINTMASK)

/* DICVR bits */
#define DI_CVR_CVR        BIT(0)
#define DI_CVR_CVRINTMASK BIT(1)
#define DI_CVR_CVRINT     BIT(2)

/* DICR bits */
#define DI_CR_TSTART BIT(0)
#define DI_CR_DMA    BIT(1)

/* DI status bytes */
#define DI_STATUS_OK           0x00
#define DI_STATUS_LID_OPEN     0x01
#define DI_STATUS_DISC_CHANGED 0x02
#define DI_STATUS_NO_DISC      0x03
#define DI_STATUS_MOTOR_OFF    0x04
#define DI_STATUS_ID_NOT_READ  0x05

/* DI error codes */
#define DI_ERROR_OK                  0x000000
#define DI_ERROR_MOTOR_STOPPED       0x020400
#define DI_ERROR_DISC_ID_NOT_READ    0x020401
#define DI_ERROR_COVER_OPENED        0x023a00
#define DI_ERROR_SEEK_FAILED         0x030200
#define DI_ERROR_UNRECOVERABLE_READ  0x031100
#define DI_ERROR_PROTOCOL_ERROR      0x040800
#define DI_ERROR_ILLEGAL_COMMAND     0x052000
#define DI_ERROR_AUDIO_BUF_NOT_SET   0x052001
#define DI_ERROR_LBA_OUT_OF_RANGE    0x052100
#define DI_ERROR_ILLEGAL_CMD_FIELD   0x052400
#define DI_ERROR_ILLEGAL_AUDIO_CMD   0x052401
#define DI_ERROR_CONFIG_OUT_OF_PER   0x052402
/* TODO: find a real source for this; found this in CleanRip's driver but unnamed */
#define DI_ERROR_NORMAL_READ_ON_DVDR 0x053000
#define DI_ERROR_END_OF_USER_AREA    0x056300
#define DI_ERROR_MEDIA_CHANGED       0x062800
#define DI_ERROR_MEDIUM_REMOVAL_REQ  0x0b5a01

#define DI_STATUS_GET_STATUS(status) (u8)((status & 0xff000000) >> 24)
#define DI_STATUS_GET_ERR(status)    (status & 0x00ffffff)

/* Wii drive dates to drivechip names */
#define DMS_D2A_DATE 0x20060526
#define D2B_DATE     0x20060907
#define D2C_D2E_DATE 0x20070213
#define D3_DATE      0x20080714
#define D4v1_DATE    0x20081218
#define D4v2_DATE_1  0x20091121
#define D4v2_DATE_2  0x20101207
#define MINI_DATE    0x20120629
#define WIIU_DATE_1  0x20110628
#define WIIU_DATE_2  0x20120712


#define DVD_BLOCK_SIZE 2048
#define DI_COMMAND_TIMEOUT_US (1000 * 1000 * 1000)
#define GCN_DISC_SIZE ((u64)712880 * DVD_BLOCK_SIZE)
#define WII_SL_DISC_SIZE 405012480ull

static REGISTER_DRIVER(diDrv);
struct diRegs {
	vu32 sr;
	vu32 cvr;
	vu32 cmdbuf[3];
	vu32 mar;
	vu32 length;
	vu32 cr;
	vu32 immbuf;
	vu32 cfg;
} __attribute__((packed));
static volatile struct diRegs *regs;
static u32 driveDate;
static u32 lastDICVR;
static bool bdevRegistered = false;
static bool usingDVDR = false;

struct diInquiryResponse {
	u32 unk;
	u32 date; /* e.g. 0x20080714 */
	u32 pad[6];
};

struct discID {
	u8 bytes[32];
};

struct diPhysFormatInfo {
	u8 discCategoryAndVersion;
	u8 discSizeAndRate;
	u8 discStructure;
	u8 recordingDensity;
	u32 firstDataPSN;
	u32 lastDataPSN;
	u8 pad[2036];
} __attribute__((packed));

static const struct blockTransfer diTransfers[] = {
	{
		.size = DVD_BLOCK_SIZE,
		.mode = BLOCK_TRANSFER_MULTIPLE,
		.dmaAlign = 32
	}
};

static ssize_t diRead(struct blockDevice *bdev, void *dest, size_t len, u64 off);
static struct blockDevice diBdev = {
	.name = "di",
	.size = 0, /* calculate based on actual disc at runtime */
	.blockSize = DVD_BLOCK_SIZE,
	.drvData = NULL,
	.transfers = diTransfers,
	.numTransfers = sizeof(diTransfers) / sizeof(diTransfers[0]),
	.blockAlignMode = BLOCK_ALIGN_BOUNCE,
	.dmaAlignMode = BLOCK_ALIGN_BOUNCE,
	.read = diRead,
	.write = NULL,
	.probePartitions = true,
	.flags = BLOCK_FLAG_OPTICAL
};

static const char *dateToRevWii(u32 date) {
	switch (date) {
	case DMS_D2A_DATE: return "DMS or D2A";
	case D2B_DATE: return "D2B";
	case D2C_D2E_DATE: return "D2C or D2E";
	case D3_DATE: return "D3 or D3-2";
	case D4v1_DATE: return "D4v1";
	case D4v2_DATE_1:
	case D4v2_DATE_2: return "D4v2";
	case MINI_DATE: return "Wii Mini Disc Drive?";
	case WIIU_DATE_1: return "Wii U Drive 1";
	case WIIU_DATE_2: return "Wii U Drive 2";
	default: return "Unknown";
	}
}

static int diReset(void) {
	u64 tb;

	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
		PI_RESET = (PI_RESET & ~PI_RESET_DI) | 1;
		udelay(200 * 1000); /* seems to need much longer than the Wii's */
		PI_RESET = PI_RESET | PI_RESET_DI | 1;
		udelay(100 * 1000);
	}
	else {
		HW_GPIOB_DIR |= GPIO_DI_SPIN;
		HW_GPIOB_OUT &= ~GPIO_DI_SPIN;
		HW_RESETS &= ~RESETS_RSTB_DIRSTB;
		udelay(1000);
		HW_RESETS |= RESETS_RSTB_DIRSTB;
	}

	/* wait until DI registers seem to actually work */
	tb = mftb();
	while (!(regs->sr & DI_SR_TCINTMASK)) {
		barrier();
		regs->sr = (regs->sr & ~(DI_SR_BRKINT | DI_SR_DEINT | DI_SR_TCINT)) | DI_SR_TCINTMASK;
		barrier();

		if (T_HasElapsed(tb, 1000 * 1000)) {
			log_printf("DI seems hosed, still couldn't set TCINTMASK after 1s; SR=%08x\r\n", regs->sr);
			return -ETIMEDOUT;
		}
	}

	/* mask all ints */
	regs->sr = regs->sr & ~(DI_SR_BRKINT | DI_SR_DEINT | DI_SR_TCINT | DI_SR_BRKINTMASK | DI_SR_DEINTMASK | DI_SR_TCINTMASK);
	regs->cvr = regs->cvr & ~(DI_CVR_CVRINTMASK | DI_CVR_CVRINT);

	/* ack all ints */
	regs->sr |= DI_SR_BRKINT | DI_SR_DEINT | DI_SR_TCINT;
	regs->cvr |= DI_CVR_CVRINT;

	/* unmask cvrint */
	regs->sr = (regs->sr & ~(DI_SR_BRKINT | DI_SR_DEINT | DI_SR_TCINT)) | DI_SR_BRKINTMASK | DI_SR_DEINTMASK | DI_SR_TCINTMASK;
	regs->cvr = (regs->cvr & ~DI_CVR_CVRINT) | DI_CVR_CVRINTMASK;

	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE)
		udelay(100 * 1000);

	return 0;
}

static void diAckSR(u32 ints) {
	regs->sr = (regs->sr & DI_SR_INTMASKS) | (ints & DI_SR_INTS);
	barrier();
}

static int diWaitIdle(uint timeoutUs) {
	u64 tb;

	tb = mftb();
	while (regs->cr & DI_CR_TSTART) {
		if (T_HasElapsed(tb, timeoutUs))
			return -ETIMEDOUT;
		udelay(1000);
	}

	return 0;
}

static int diDoCMD(u32 cmdbuf0, u32 cmdbuf1, u32 cmdbuf2, void *data, uint dataLen) {
	u64 tb;
	u32 sr;
	int ret;

	ret = diWaitIdle(DI_COMMAND_TIMEOUT_US);
	if (ret) {
		log_printf("timed out waiting for DI idle; CR=%08x\r\n", regs->cr);
		return ret;
	}

	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
		if (cmdbuf0 == DI_CMD_GCN_UNLOCK1A || cmdbuf0 == DI_CMD_GCN_UNLOCK2A)
			regs->sr |= DI_GCN_SR_UNLOCK;
		else if (data)
			regs->sr = DI_GCN_SR_DMA_READ;
		else
			diAckSR(DI_SR_DEINT | DI_SR_TCINT);
	}
	else
		diAckSR(DI_SR_INTS);

	regs->cmdbuf[0] = cmdbuf0;
	regs->cmdbuf[1] = cmdbuf1;
	regs->cmdbuf[2] = cmdbuf2;
	dcache_flush_invalidate(data, dataLen);
	regs->mar = (u32)virtToPhys(data);
	regs->length = dataLen;
	barrier();
	regs->cr = DI_CR_TSTART | (data ? DI_CR_DMA : 0);

	tb = mftb();
	while (regs->cr & DI_CR_TSTART) {
		if (T_HasElapsed(tb, DI_COMMAND_TIMEOUT_US)) {
			log_puts("timed out waiting on cmd");
			log_printf("cmd: %08x %08x %08x\r\n", cmdbuf0, cmdbuf1, cmdbuf2);
			log_printf("DMA of %uB @ %08x\r\n", dataLen, data);
			log_printf("CR=%08x\r\n", regs->cr);
			return -ETIMEDOUT;
		}
	}

	sr = regs->sr & DI_SR_INTS;
	diAckSR(sr);
	if (sr & DI_SR_BRKINT)
		return -EINTR;
	if (sr & DI_SR_DEINT)
		return -EIO;

	return 0;
}

static int diUnlockGC(void) {
	int ret;

	/* [ff][01]MATSHITA[02][00] */
	ret = diDoCMD(DI_CMD_GCN_UNLOCK1A, DI_CMD_GCN_UNLOCK1B, DI_CMD_GCN_UNLOCK1C, NULL, 0);
	/* expected to produce DEINT */
	if (ret && ret != -EIO) {
		log_printf("GC unlock 1 failed: %d SR=%08x\r\n", ret, regs->sr);
		return ret;
	}

	/* [ff][00]DVD-GAME[03][00] */
	ret = diDoCMD(DI_CMD_GCN_UNLOCK2A, DI_CMD_GCN_UNLOCK2B, DI_CMD_GCN_UNLOCK2C, NULL, 0);
	if (ret)
		log_printf("GC unlock 2 failed: %d SR=%08x\r\n", ret, regs->sr);

	return ret;
}

static const char *diStatusUpperToStr(u8 status) {
	switch (status) {
	case DI_STATUS_OK: return "OK";
	case DI_STATUS_LID_OPEN: return "Lid Open";
	case DI_STATUS_DISC_CHANGED: return "No Disc/Disc Changed";
	case DI_STATUS_NO_DISC: return "No Disc";
	case DI_STATUS_MOTOR_OFF: return "Motor Off";
	case DI_STATUS_ID_NOT_READ: return "Disc ID Not Read";
	default: return NULL;
	}
}

static const char *diStatusErrToStr(u32 error) {
	switch (error) {
	case DI_ERROR_OK: return "OK";
	case DI_ERROR_MOTOR_STOPPED: return "Motor Stopped";
	case DI_ERROR_DISC_ID_NOT_READ: return "Disc ID Not Read";
	case DI_ERROR_COVER_OPENED: return "Medium Not Present/Cover Opened";
	case DI_ERROR_SEEK_FAILED: return "Seek Failed";
	case DI_ERROR_UNRECOVERABLE_READ: return "Unrecoverable Read";
	case DI_ERROR_PROTOCOL_ERROR: return "Protocol Error";
	case DI_ERROR_ILLEGAL_COMMAND: return "Illegal Command";
	case DI_ERROR_AUDIO_BUF_NOT_SET: return "Audio Buffer Not Set";
	case DI_ERROR_LBA_OUT_OF_RANGE: return "LBA Out Of Range";
	case DI_ERROR_ILLEGAL_CMD_FIELD: return "Illegal Command Field";
	case DI_ERROR_ILLEGAL_AUDIO_CMD: return "Illegal Audio Command";
	case DI_ERROR_CONFIG_OUT_OF_PER: return "Config Out Of Period";
	case DI_ERROR_NORMAL_READ_ON_DVDR: return "Normal Read on DVD-R";
	case DI_ERROR_END_OF_USER_AREA: return "End Of User Area On Track";
	case DI_ERROR_MEDIA_CHANGED: return "Media Changed";
	case DI_ERROR_MEDIUM_REMOVAL_REQ: return "Operator Medium Removal Request";
	default: return NULL;
	}
}

static void diStatusToStr(u32 status, char *buf, uint len) {
	u8 upperStatus;
	u32 err;
	const char *statusStr, *errStr;
	upperStatus = DI_STATUS_GET_STATUS(status);
	err = DI_STATUS_GET_ERR(status);

	statusStr = diStatusUpperToStr(upperStatus);
	errStr = diStatusErrToStr(err);

	if (statusStr && errStr)
		snprintf(buf, len, "[ %s, %s ]", statusStr, errStr);
	else if (statusStr && !errStr)
		snprintf(buf, len, "[ %s, Unknown (0x%06x) ]", statusStr, err);
	else if (!statusStr && errStr)
		snprintf(buf, len, "[ Unknown (0x%02x), %s ]", upperStatus, errStr);
	else if (!statusStr && !errStr)
		snprintf(buf, len, "[ Unknown (0x%02x), Unknown (0x%06x) ]", upperStatus, err);
	else
		assert_unreachable();
}

static int diGetStatusRaw(u32 *status) {
	int ret;

	regs->immbuf = 0;
	barrier();

	ret = diDoCMD(DI_CMD_GET_STATUS, 0, 0, NULL, 0);
	if (ret)
		return ret;

	*status = regs->immbuf;
	return 0;
}

static u32 diGetStatus(void) {
	u32 status;
	int ret;

	ret = diGetStatusRaw(&status);
	if (ret)
		return (u32)ret;

	return status;
}

static int diWaitForStatus(const char *ctx, u32 *status) {
	u64 tb;
	int ret;

	tb = mftb();
	do {
		ret = diGetStatusRaw(status);
		if (!ret)
			return 0;

		udelay(10 * 1000);
	} while (!T_HasElapsed(tb, 30 * 1000 * 1000));

	log_printf("%s: DI did not become ready; ret=%d CR=%08x SR=%08x\r\n", ctx, ret, regs->cr, regs->sr);
	return ret ? ret : -ETIMEDOUT;
}

static ssize_t diRead(struct blockDevice *bdev, void *dest, size_t len, u64 off) {
	u32 cmdbuf1, cmdbuf2;
	int ret;
	(void)bdev;

	if ((off % DVD_BLOCK_SIZE) || (len % DVD_BLOCK_SIZE))
		return -1;

	if (usingDVDR) {
		cmdbuf1 = (u32)(off / DVD_BLOCK_SIZE);
		cmdbuf2 = (u32)(len / DVD_BLOCK_SIZE);
		ret = diDoCMD(DI_CMD_READ_DVDR, cmdbuf1, cmdbuf2, dest, (uint)len);
	}
	else {
		if (off >> 2 > 0xffffffff)
			return -1;
		cmdbuf1 = (u32)(off >> 2);
		cmdbuf2 = (u32)len;
		ret = diDoCMD(DI_CMD_READ_NORMAL, cmdbuf1, cmdbuf2, dest, (uint)len);
	}

	if (ret)
		return -1;

	return (ssize_t)len;
}

static int diReadDiscSize(u64 *size) {
	struct diPhysFormatInfo info ALIGN(32);
	u32 first, last, sectors;
	int ret;

	memset(&info, 0, sizeof(info));
	ret = diDoCMD(DI_CMD_READ_PHYSINFO, 0, 0, &info, sizeof(info));
	if (ret) {
		if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
			*size = GCN_DISC_SIZE;
			log_printf("Using fallback GameCube disc size: %llu bytes\r\n", *size);
			return 0;
		}

		log_printf("Read physical format info failed: %d\r\n", ret);
		return ret;
	}

	first = npll_be32_to_cpu(info.firstDataPSN);
	last = npll_be32_to_cpu(info.lastDataPSN);
	if (last < first || (!usingDVDR && !first && !last)) {
		if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
			*size = GCN_DISC_SIZE;
			log_printf("Using fallback GameCube disc size: %llu bytes\r\n", *size);
			return 0;
		}

		*size = WII_SL_DISC_SIZE;
		log_printf("Using fallback Wii single-layer disc size: %llu bytes\r\n", *size);
		return 0;
	}

	sectors = last - first + 1;
	*size = (u64)sectors * DVD_BLOCK_SIZE;
	log_printf("Disc format: first PSN=%08x last PSN=%08x, size=%llu bytes\r\n", first, last, *size);
	return 0;
}

static int diRegisterMedia(void) {
	int ret;

	if (bdevRegistered) {
		B_Unregister(&diBdev);
		bdevRegistered = false;
		diBdev.size = 0;
	}

	ret = diReadDiscSize(&diBdev.size);
	if (ret) {
		diBdev.size = 0;
		return ret;
	}

	B_Register(&diBdev);
	bdevRegistered = true;
	return 0;
}

static void diIRQHandler(enum irqDev dev) {
	u32 cvr, sr, srReason;
	(void)dev;

	cvr = regs->cvr;
	sr  = regs->sr;

	if (cvr & DI_CVR_CVRINT)
		regs->cvr = cvr | DI_CVR_CVRINT;

	srReason = sr & (DI_SR_BRKINT | DI_SR_DEINT | DI_SR_TCINT);
	if (srReason)
		regs->sr = sr | srReason;
}

static int diProbeNewMedia(void) {
	struct discID discID ALIGN(32);
	u32 status;
	char statusStr[128];
	int ret;
	bool again = false;

	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
		ret = diUnlockGC();
		if (ret)
			return ret;
	}

tryAgain:
	memset(&discID, 0, sizeof(discID));

	ret = diDoCMD(DI_CMD_READ_DISC_ID, 0, 0x20, &discID, sizeof(discID));
	status = diGetStatus();
	diStatusToStr(status, statusStr, sizeof(statusStr));
	log_printf("Drive status: %08x %s\r\n", status, statusStr);
	if (DI_STATUS_GET_ERR(status) == DI_ERROR_NORMAL_READ_ON_DVDR) {
		log_puts("DVD-R style read commands required");
		usingDVDR = true;
		return diRegisterMedia();
	}
	else if (DI_STATUS_GET_STATUS(status) == DI_STATUS_NO_DISC ||
	    DI_STATUS_GET_ERR(status) == DI_ERROR_COVER_OPENED) {
		log_puts("No disc present");
		usingDVDR = false;
		return -ENOMEDIUM;
	}
	else if (DI_STATUS_GET_STATUS(status) == DI_STATUS_DISC_CHANGED ||
	    DI_STATUS_GET_ERR(status) == DI_ERROR_MEDIA_CHANGED) {
		if (!again) {
			again = true;
			goto tryAgain;
		}
		log_puts("No media ready after lid close");
		usingDVDR = false;
		return -EAGAIN;
	}
	else if (ret) {
		log_printf("Read Disc ID failed: %d\r\n", ret);
		usingDVDR = false;
		return ret;
	}
	else {
		log_printf("Disc ID: %02x%02x%02x%02x%02x%02x\r\n",
			discID.bytes[0], discID.bytes[1], discID.bytes[2],
			discID.bytes[3], discID.bytes[4], discID.bytes[5]);
		usingDVDR = false;
		return diRegisterMedia();
	}
}

static u32 diReadMem32(u32 addr) {
	regs->sr = 0x2E;
	regs->cvr = 0;
	regs->cmdbuf[0] = DI_CMD_DEBUG_READMEM;
	regs->cmdbuf[1] = addr;
	regs->cmdbuf[2] = 0x00010000;
	regs->immbuf = 0;
	regs->cr = DI_CR_TSTART;

	while (regs->cr & DI_CR_TSTART);
	return regs->immbuf;
}

static void diWriteMem32(u32 addr, u32 dat) {
	/* Swiss does this jank, and I don't think it could be adapted well into a real command (DMA 0B to address 0?) */
	regs->sr = 0x2E;
	regs->cvr = 0;
	regs->cmdbuf[0] = DI_CMD_DEBUG_WRITEMEM;
	regs->cmdbuf[1] = addr;
	regs->cmdbuf[2] = 0x00040000;
	regs->mar = 0;
	regs->length = 0;
	regs->cr = DI_CR_TSTART | DI_CR_DMA;
	while (regs->cr & DI_CR_TSTART);

	/* executing it as a command????? */
	regs->sr = 0x2E;
	regs->cvr = 0;
	regs->cmdbuf[0] = dat;
	regs->cr = DI_CR_TSTART;
	while (regs->cr & DI_CR_TSTART);
}

static void diReadMemArray(u32 addr, void *buf, u32 size) {
	u32 *ptr = (u32 *)buf;
	int rem = (int)size;

	while (rem > 0) {
		*ptr++ = diReadMem32(addr);
		addr += 4;
		rem -= 4;
	}
}

static void diWriteMemArray(u32 addr, const void *buf, u32 size) {
	u32 *ptr = (u32 *)buf;
	int rem = (int)size;

	while (rem > 0) {
		diWriteMem32(addr, *ptr++);
		addr += 4;
		rem -= 4;
	}
}

static const void *drivePatchPtr(void) {
	if (driveDate == 0x20020402)
		return &drive04firmware;
	if (driveDate == 0x20010608)
		return &drive06firmware;
	if (driveDate == 0x20010831)
		return &driveQfirmware;
	if (driveDate == 0x20020823)
		return &drive08firmware;
	return NULL;
}

static void diApplyGCNPatches() {
	const void *patchCode;

	patchCode = drivePatchPtr();
	if(patchCode == NULL)
		return;    /* Unsupported drive */

	log_printf("Drive date %08x\r\n", driveDate);
	log_puts("Unlocking drive");
	diUnlockGC();
	log_puts("Write patch");
	diWriteMemArray(0xff40d000, patchCode, 0x1F0);
	diWriteMem32(0x804c, 0x00d04000);
	log_printf("Set extension %08x\r\n", diGetStatus());
	diDoCMD(DI_CMD_SETEXTENSION, 0, 0, NULL, 0);
	log_printf("Unlock again %08x\r\n", diGetStatus());
	diUnlockGC();
	log_printf("Debug Motor On %08x\r\n", diGetStatus());
	diDoCMD(DI_CMD_DEBUG_BASE | DEBUG_ACCEPT_COPY | DEBUG_START_DRIVE, 0, 0, NULL, 0);
	log_printf("Set Status %08x\r\n", diGetStatus());
	diDoCMD(DI_CMD_SET_STATUS, 0, 0, NULL, 0);
	log_printf("Set Status - done %08x\r\n", diGetStatus());
}

/* can't rely on IRQs sadly, CVRINT only detects falling edge (open->closed) */
static void diCoverPoller(void *dummy) {
	static const char *const cvrToStr[2] = { "Closed", "Open" };
	struct discID discID ALIGN(32);
	int prevState, curState;
	u32 cvr, status;
	char statusStr[128];
	int ret;
	bool triedFWPatch = false;
	(void)dummy;

	cvr = regs->cvr;
	prevState = !!(lastDICVR & DI_CVR_CVR);
	curState = !!(cvr & DI_CVR_CVR);
	lastDICVR = cvr;
	if (prevState == curState)
		return;

	log_printf("DI Cover status: %s -> %s\r\n", cvrToStr[prevState], cvrToStr[curState]);

again:
	status = diGetStatus();
	diStatusToStr(status, statusStr, sizeof(statusStr));
	log_printf("Drive status: %08x %s\r\n", status, statusStr);

	if (curState && bdevRegistered) {
		B_Unregister(&diBdev);
		bdevRegistered = false;
		diBdev.size = 0;
	}
	else if (!curState) {
		if (H_ConsoleType != CONSOLE_TYPE_GAMECUBE) {
			log_puts("Resetting DI after lid close");
			diReset();
			if (diWaitForStatus("lid close reset", &status))
				return;
		}
		ret = diProbeNewMedia();
		if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE && ret == -EAGAIN) {
			log_puts("Resetting DI after media-changed probe");
			diReset();
			if (diWaitForStatus("lid close reset", &status))
				return;

			/* need to try to read discid to see the new status */
			ret = diDoCMD(DI_CMD_READ_DISC_ID, 0, 0x20, &discID, sizeof(discID));
			status = diGetStatus();

			diProbeNewMedia();
			diStatusToStr(status, statusStr, sizeof(statusStr));
			log_printf("Drive status from probe: %08x %s\r\n", status, statusStr);

			/*
			 * Is it the suspicious combo?  We got here from EAGAIN
			 * (from Media Changed error on first diProbeMedia),
			 * if we now get no disc + medium not present/cover opened, then
			 * it is quite probable that the drive rejected a valid DVD-R(OM).
			 */
			if (DI_STATUS_GET_STATUS(status) == DI_STATUS_NO_DISC &&
			    DI_STATUS_GET_ERR(status) == DI_ERROR_COVER_OPENED) {
				if (triedFWPatch) {
					log_puts("Firmware patch didn't work...");
					return;
				}
				log_puts("suspected stock GCN drive fw rejecting DVD-R, trying patches");
				diApplyGCNPatches();
				triedFWPatch = true;
				goto again;
			}
		}
	}
}

static void diInit(void) {
	struct diInquiryResponse resp ALIGN(32);
	char status[128];
	u32 rawStatus;

	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE)
		regs = (volatile struct diRegs *)0xcc006000;
	else {
		regs = (volatile struct diRegs *)0xcd806000;
		HW_COMPAT &= ~HW_COMPAT_DVDVIDEO;
	}

	memset(&resp, 0, sizeof(resp));

	/* reset the drive */
	diReset();

	if (diWaitForStatus("init reset", &rawStatus)) {
		log_puts("Failed to get drive status after reset, seems dead / disconnected");
		diDrv.state = DRIVER_STATE_NO_HARDWARE;
		return;
	}

	diStatusToStr(rawStatus, status, 128);
	log_printf("Drive status: %08x %s\r\n", rawStatus, status);

	/* get an inquiry */
	diDoCMD(DI_CMD_INQUIRY, 0, sizeof(resp), &resp, sizeof(resp));

	log_printf("Drive date: (MM/DD/YYYY) %02x/%02x/%04x\r\n", (resp.date & 0x0000ff00) >> 8, resp.date & 0x000000ff, (resp.date & 0xffff0000) >> 16);
	if (H_ConsoleType != CONSOLE_TYPE_GAMECUBE)
		log_printf("Drive type: %s\r\n", dateToRevWii(resp.date));
	driveDate = resp.date;

	/* register our IRQ handler */
	IRQ_RegisterHandler(IRQDEV_DI, diIRQHandler);
	IRQ_Unmask(IRQDEV_DI);

	rawStatus = diGetStatus();
	diStatusToStr(rawStatus, status, 128);
	log_printf("Drive status: %08x %s\r\n", rawStatus, status);

	lastDICVR = regs->cvr;
	if (!(lastDICVR & DI_CVR_CVR))
		diProbeNewMedia();

	T_QueueRepeatingEvent(200 * 1000, diCoverPoller, NULL);

	rawStatus = diGetStatus();
	diStatusToStr(rawStatus, status, 128);
	log_printf("Drive status: %08x %s\r\n", rawStatus, status);

	diDrv.state = DRIVER_STATE_READY;
}

static void diCleanup(void) {
	if (bdevRegistered) {
		B_Unregister(&diBdev);
		bdevRegistered = false;
		diBdev.size = 0;
	}

	diDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(diDrv) = {
	.name = "Flipper/Hollywood Drive Interface",
	.mask = DRIVER_ALLOW_GAMECUBE | DRIVER_ALLOW_WII,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_BLOCK,
	.init = diInit,
	.cleanup = diCleanup
};
