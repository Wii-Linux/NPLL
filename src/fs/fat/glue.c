/*
 * NPLL - Filesystems - FAT - FS Layer Glue
 * Copyright (C) 2026 Techflash
 */

#define MODULE "fat"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <npll/endian.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/partition.h>
#include <npll/types.h>
#include <npll/utils.h>

/* hacked up from FatFs's internal impl */
#include "ff.h"
#include "ffconf.h"
#define BS_JmpBoot			0		/* x86 jump instruction (3-byte) */
#define BS_OEMName			3		/* OEM name (8-byte) */
#define BPB_BytsPerSec		11		/* Sector size [byte] (WORD) */
#define BPB_SecPerClus		13		/* Cluster size [sector] (BYTE) */
#define BPB_RsvdSecCnt		14		/* Size of reserved area [sector] (WORD) */
#define BPB_NumFATs			16		/* Number of FATs (BYTE) */
#define BPB_RootEntCnt		17		/* Size of root directory area for FAT [entry] (WORD) */
#define BPB_TotSec16		19		/* Volume size (16-bit) [sector] (WORD) */
#define BPB_Media			21		/* Media descriptor byte (BYTE) */
#define BPB_FATSz16			22		/* FAT size (16-bit) [sector] (WORD) */
#define BPB_SecPerTrk		24		/* Number of sectors per track for int13h [sector] (WORD) */
#define BPB_NumHeads		26		/* Number of heads for int13h (WORD) */
#define BPB_HiddSec			28		/* Volume offset from top of the drive (DWORD) */
#define BPB_TotSec32		32		/* Volume size (32-bit) [sector] (DWORD) */
#define BS_DrvNum			36		/* Physical drive number for int13h (BYTE) */
#define BS_NTres			37		/* WindowsNT error flag (BYTE) */
#define BS_BootSig			38		/* Extended boot signature (BYTE) */
#define BS_VolID			39		/* Volume serial number (DWORD) */
#define BS_VolLab			43		/* Volume label string (8-byte) */
#define BS_FilSysType		54		/* Filesystem type string (8-byte) */
#define BS_BootCode			62		/* Boot code (448-byte) */
#define BS_55AA				510		/* Boot signature (WORD, for VBR and MBR) */

#define BPB_FATSz32			36		/* FAT32: FAT size [sector] (DWORD) */
#define BPB_ExtFlags32		40		/* FAT32: Extended flags (WORD) */
#define BPB_FSVer32			42		/* FAT32: Filesystem version (WORD) */
#define BPB_RootClus32		44		/* FAT32: Root directory cluster (DWORD) */
#define BPB_FSInfo32		48		/* FAT32: Offset of FSINFO sector (WORD) */
#define BPB_BkBootSec32		50		/* FAT32: Offset of backup boot sector (WORD) */
#define BS_DrvNum32			64		/* FAT32: Physical drive number for int13h (BYTE) */
#define BS_NTres32			65		/* FAT32: Error flag (BYTE) */
#define BS_BootSig32		66		/* FAT32: Extended boot signature (BYTE) */
#define BS_VolID32			67		/* FAT32: Volume serial number (DWORD) */
#define BS_VolLab32			71		/* FAT32: Volume label string (8-byte) */
#define BS_FilSysType32		82		/* FAT32: Filesystem type string (8-byte) */
#define BS_BootCode32		90		/* FAT32: Boot code (420-byte) */

#define BPB_ZeroedEx		11		/* exFAT: MBZ field (53-byte) */
#define BPB_VolOfsEx		64		/* exFAT: Volume offset from top of the drive [sector] (QWORD) */
#define BPB_TotSecEx		72		/* exFAT: Volume size [sector] (QWORD) */
#define BPB_FatOfsEx		80		/* exFAT: FAT offset from top of the volume [sector] (DWORD) */
#define BPB_FatSzEx			84		/* exFAT: FAT size [sector] (DWORD) */
#define BPB_DataOfsEx		88		/* exFAT: Data offset from top of the volume [sector] (DWORD) */
#define BPB_NumClusEx		92		/* exFAT: Number of clusters (DWORD) */
#define BPB_RootClusEx		96		/* exFAT: Root directory start cluster (DWORD) */
#define BPB_VolIDEx			100		/* exFAT: Volume serial number (DWORD) */
#define BPB_FSVerEx			104		/* exFAT: Filesystem version (WORD) */
#define BPB_VolFlagEx		106		/* exFAT: Volume flags (WORD, out of check sum calculation) */
#define BPB_BytsPerSecEx	108		/* exFAT: Log2 of sector size in unit of byte (BYTE) */
#define BPB_SecPerClusEx	109		/* exFAT: Log2 of cluster size in unit of sector (BYTE) */
#define BPB_NumFATsEx		110		/* exFAT: Number of FATs (BYTE) */
#define BPB_DrvNumEx		111		/* exFAT: Physical drive number for int13h (BYTE) */
#define BPB_PercInUseEx		112		/* exFAT: Percent in use (BYTE, out of check sum calculation) */
#define BPB_RsvdEx			113		/* exFAT: Reserved (7-byte) */
#define BS_BootCodeEx		120		/* exFAT: Boot code (390-byte) */

/* 0:FAT/FAT32 VBR, 1:exFAT VBR, 2:Not FAT and valid BS, 3:Not FAT and invalid BS */
static int check_fs (u8 *_vbr) {
	u16 w;
	u8 b;
	struct mbr *vbr = (struct mbr *)_vbr;

#if FF_FS_EXFAT
	if (vbr->sig[0] == 0x55 && vbr->sig[1] == 0xAA && !memcmp(&_vbr[BS_JmpBoot], "\xEB\x76\x90" "EXFAT   ", 11)) return 1;	/* It is an exFAT VBR */
#endif
	b = _vbr[BS_JmpBoot];
	if (b == 0xEB || b == 0xE9 || b == 0xE8) {	/* Valid JumpBoot code? (short jump, near jump or near call) */
		if (vbr->sig[0] == 0x55 && vbr->sig[1] == 0xAA && !memcmp(&_vbr[BS_FilSysType32], "FAT32   ", 8)) {
			return 0;	/* It is an FAT32 VBR */
		}
		/* FAT volumes created in the early MS-DOS era lack BS_55AA and BS_FilSysType, so FAT VBR needs to be identified without them. */
		w = npll_le16_to_cpu(*(u16 *)&_vbr[BPB_BytsPerSec]);
		b = _vbr[BPB_SecPerClus];
		if ((w & (w - 1)) == 0 && w >= FF_MIN_SS && w <= FF_MAX_SS	/* Properness of sector size (512-4096 and 2^n) */
			&& b != 0 && (b & (b - 1)) == 0				/* Properness of cluster size (2^n) */
			&& npll_le16_to_cpu(*(u16 *)&_vbr[BPB_RsvdSecCnt]) != 0		/* Properness of number of reserved sectors (MNBZ) */
			&& (u32)_vbr[BPB_NumFATs] - 1 <= 1		/* Properness of number of FATs (1 or 2) */
			&& npll_le16_to_cpu(*(u16 *)&_vbr[BPB_RootEntCnt]) != 0		/* Properness of root dir size (MNBZ) */
			&& (npll_le16_to_cpu(*(u16 *)&_vbr[BPB_TotSec16]) >= 128 || npll_le32_to_cpu(*(u32 *)&_vbr[BPB_TotSec32]) >= 0x10000)	/* Properness of volume size (>=128) */
			&& npll_le16_to_cpu(*(u16 *)&_vbr[BPB_FATSz16]) != 0) {		/* Properness of FAT size (MNBZ) */
				return 0;	/* It can be presumed an FAT VBR */
		}
	}
	return (vbr->sig[0] == 0x55 && vbr->sig[1] == 0xAA) ? 2 : 3;	/* Not an FAT VBR (with valid or invalid BS) */
}

#define MAX_FILES 16
struct filesystem FS_FAT, FS_exFAT;
extern struct partition *partitions[FF_VOLUMES];
extern const char *VolumeStr[FF_VOLUMES];
static FATFS fatfsObj;
static FIL openFiles[MAX_FILES];
#define VALIDATE_FD(ret) if (fd < 0 || fd > MAX_FILES || !openFiles[fd].obj.fs) { return ret; }
static int allocateFd(void) {
	int i;

	for (i = 0; i < MAX_FILES; i++) {
		if (!openFiles[i].obj.fs)
			return i;
	}

	return -1;
}

static bool fatProbe(struct filesystem *fs, struct partition *part) {
	u8 ALIGN(32) vbr[512];
	int ret;

	ret = B_Read(part, vbr, 512, 0);
	if (ret != 512) {
		log_printf("fatProbe: B_Read returned %d\r\n", ret);
		return false;
	}

	ret = check_fs(vbr);
	log_printf("check fs ret: %d\r\n", ret);
	switch (ret) {
	case 0: {
		if (fs == &FS_FAT) {
			log_puts("Found FAT VBR");
			return true;
		}
		else return false;
	}
	case 1: {
		if (fs == &FS_exFAT) {
			log_puts("Found exFAT VBR");
			return true;
		}
		else return false;
	}
	case 3:
	case 4:
		return false;
	default:
		__builtin_unreachable();
	}
}

static int fatMount(struct filesystem *fs, struct partition *part) {
	int ret;

	(void)fs;
	/* do we already have a mounted partition? */
	assert(!partitions[0]);
	assert(part);

	/* reset internal state */
	memset(&fatfsObj, 0, sizeof(fatfsObj));
	memset(&openFiles, 0, sizeof(openFiles));
	partitions[0] = part;
	VolumeStr[0] = part->bdev->name;

	/* actually mount it */
	ret = f_mount(&fatfsObj, part->bdev->name, 1);
	if (ret) {
		log_printf("f_mount failed: %d\r\n", ret);
		return -1;
	}

	return 0;
}

static void fatUnmount(struct filesystem *fs) {
	(void)fs;
	f_unmount(VolumeStr[0]);
	partitions[0] = NULL;
	VolumeStr[0] = NULL;
	memset(&fatfsObj, 0, sizeof(fatfsObj));
	memset(&openFiles, 0, sizeof(openFiles));
}

static int fatOpen(struct filesystem *fs, const char *path) {
	FRESULT fr;
	int fd;
	(void)fs;

	fd = allocateFd();
	if (fd < 0)
		return -EMFILE;

	/* TODO: other modes when doing write support */
	fr = f_open(&openFiles[fd], path, FA_READ);
	switch (fr) {
	case FR_OK:
		return fd;
	case FR_NO_PATH:
	case FR_NO_FILE: {
		memset(&openFiles[fd], 0, sizeof(FIL));
		return -ENOENT;
	}
	case FR_DISK_ERR: {
		memset(&openFiles[fd], 0, sizeof(FIL));
		return -EIO;
	}
	case FR_INVALID_PARAMETER: {
		memset(&openFiles[fd], 0, sizeof(FIL));
		return -EINVAL;
	}
	default: {
		memset(&openFiles[fd], 0, sizeof(FIL));
		return -ENOENT;
	}
	}
}

static void fatClose(struct filesystem *fs, int fd) {
	FRESULT fr;

	(void)fs;
	VALIDATE_FD();
	fr = f_close(&openFiles[fd]);
	if (fr != FR_OK)
		log_printf("f_close returned weird %d\r\n", fr);

	memset(&openFiles[fd], 0, sizeof(FIL));
}

static int fatRead(struct filesystem *fs, int fd, void *dest, size_t len) {
	FRESULT fr;
	UINT bytesRead;

	(void)fs;
	VALIDATE_FD(-EBADF);

	fr = f_read(&openFiles[fd], dest, len, &bytesRead);
	if (fr == FR_OK && bytesRead == len)
		return len;

	/* TODO: more error codes */
	switch (fr) {
	case FR_DISK_ERR:
		return -EIO;
	case FR_INVALID_PARAMETER:
		return -EINVAL;
	default:
		return -EIO;
	}
}

#if 0
static int fatWrite(struct filesystem *fs, int fd, const void *src, size_t len) {
	FRESULT fr;
	UINT bytesWritten;

	(void)fs;
	VALIDATE_FD(-EBADF);

	fr = f_write(&openFiles[fd], src, len, &bytesWritten);
	if (fr == FR_OK && bytesWritten == len)
		return len;

	/* TODO: more error codes */
	switch (fr) {
	case FR_DISK_ERR:
		return -EIO;
	case FR_INVALID_PARAMETER:
		return -EINVAL;
	default:
		return -EIO;
	}
}
#endif

static ssize_t fatSeek(struct filesystem *fs, int fd, ssize_t off) {
	FRESULT fr;

	(void)fs;
	VALIDATE_FD(-EBADF);

	fr = f_lseek(&openFiles[fd], off);
	if (fr == FR_OK)
		return off;

	/* TODO: more error codes */
	switch (fr) {
	case FR_DISK_ERR:
		return -EIO;
	case FR_INVALID_PARAMETER:
		return -EINVAL;
	default:
		return -EIO;
	}
}

static ssize_t fatGetSize(struct filesystem *fs, int fd) {
	(void)fs;
	VALIDATE_FD(-EBADF);

	return (ssize_t)f_size(&openFiles[fd]);
}


struct filesystem VISIBLE FS_FAT = {
	.name = "FAT",
	.drvData = NULL,
	.probe = fatProbe,
	.mount = fatMount,
	.unmount = fatUnmount,
	.open = fatOpen,
	.close = fatClose,
	.read = fatRead,
	.seek = fatSeek,
	.getSize = fatGetSize,
};

struct filesystem VISIBLE FS_exFAT = {
	.name = "exFAT",
	.drvData = NULL,
	.probe = fatProbe,
	.mount = fatMount,
	.unmount = fatUnmount,
	.open = fatOpen,
	.close = fatClose,
	.read = fatRead,
	.seek = fatSeek,
	.getSize = fatGetSize
};
