/*
 * NPLL - Wii Init
 *
 * Copyright (C) 2025-2026 Techflash
 *
 * Code to load MINI derived from ARMBootNow:
 * Copyright (c) 2024 Emma / InvoxiPlayGames
 */

#define MODULE "Wii"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/fs.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/cpu.h>
#include <npll/drivers.h>
#include <npll/drivers/exi.h>
#include <npll/flipper/vi.h>
#include <npll/hollywood/ehci.h>
#include <npll/hollywood/gpio.h>
#include <npll/hollywood/ohci.h>
#include <npll/init.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/output.h>
#include <npll/rtc.h>
#include <npll/soc.h>
#include <npll/timer.h>
#include <npll/thread.h>
#include <npll/tiny_usbgecko.h>
#include <npll/types.h>
#include <npll/utils.h>
#include <npll/video.h>
#include "../armboot_bin.h"
#include "../fs/sffs.h"
#include "ios_ipc.h"
#include "ios_es.h"
#include "mini_ipc.h"

enum wiiRev H_WiiRev = 0;
bool H_WiiIsvWii = false;
bool H_WiiIsDevkit = false;
bool H_WiiIsIronic = false;
int H_WiiBootIOS = -1;
u64 H_WiiBootTitleID = 0;
void *H_WiiMEM2Top = NULL;

/* IOS9 is present from pre-launch sysmenu Wiis up to fully updated, even vWii */
#define IOS_VALID_GUESS 9

extern int IOS_DevShaExploit(void);

#define MINI_BOOT_MAGIC_PTR (*(vu32 *)(MEM1_UNCACHED_BASE + 0xfffff0))
/* 'NRST' */
#define MINI_NO_RESET_MAGIC 0x4e525354

#define IOS_IMAGE_PHYS      0x11000000u
#define IOS_IMAGE_MAX_SIZE  0x600000u
#define IOS_TMD_MAX_SIZE    0x4000u
#define IOS_CONTENT_MAP_MAX 0x40000u

#define NAND_REG_BANK      (HOLLYWOOD_NAND_BASE + 0x18)
#define NAND_BANK_SLCCMPT  1

/* TODO: lower? */
#define IOS_BOOT_TIMEOUT_MS 15000

struct iosImageHdr {
	u32 hdrSize;
	u32 loaderSize;
	u32 elfSize;
	u32 argument;
};

static void *iosImage = NULL;
static u32 iosImageSize = 0;
static u32 iosImageVer = 0;
static u32 iosImageTitleVer = 0;

/* where the last preload failure happened, so we can put it in the panic message, to work around our crappy logging */
static const char *iosPreloadStage = "not run";
static char iosPreloadPath[80];
#define PRELOAD_STAGE(x) (iosPreloadStage = (x))


static bool testMEM2(void) {
	u32 low_orig, low_after, high_orig, high_after;

	high_orig = *(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4); sync(); barrier();
	low_orig = *(vu32 *)(MEM2_UNCACHED_BASE); sync(); barrier();
	*(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4) = 0xdeadbeef; sync(); barrier();
	high_after = *(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4); sync(); barrier();
	low_after = *(vu32 *)(MEM2_UNCACHED_BASE); sync(); barrier();

	/* restore the original high value, that may be a MINI infohdr ptr */
	*(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4) = high_orig;
	sync(); barrier();

	/*
	 * ensure no artifacts of memory protection show up:
	 *   - I/O to protected addresses hits bottom address
	 *   - writes to protected addresses do not appear
	 * so, check for:
	 *   - lower MEM2 unexpectedly changed
	 *   - write did not go through
	 */
	if ((high_orig == low_orig && high_after == low_after) || high_after != 0xdeadbeef || low_orig != low_after) {
		#if 0
		_log_puts("unsuccessful???");
		log_printf("low orig: 0x%08x\r\n", low_orig);
		log_printf("high orig: 0x%08x\r\n", high_orig);
		log_printf("low after: 0x%08x\r\n", low_after);
		log_printf("high after: 0x%08x\r\n", high_after);
		panic("Could not unlock MEM2");
		#endif

		return false;
	}
	else
		return true;
}

static void armbootnow(void) {
	u32 srnprot, trampoline_pointer, trampoline_addr;
	vu32 *sram, *armbuf;
	/* put them in low MEM1 */
	tikview_t tikviews[4] ALIGN(32);
	int i, trampoline_off, iosVer;
	uint retries = 10;
	u32 num_tikviews ALIGN(32);
	u64 titleID;
	iosVer = IOS_GetVersion();
	titleID = TITLE_ID_IOS | (u8)iosVer;

	H_WiiBootIOS = iosVer;
	log_printf("IOS (%d) detected, trying to load MINI\r\n", iosVer);
	if (iosVer <= 0) {
		log_printf("IOS version in lowmem is bogus, guessing that IOS%d should exist\r\n", IOS_VALID_GUESS);
		iosVer = IOS_VALID_GUESS;
	}

	srnprot = HW_SRNPROT;
	log_printf("HW_SRNPROT: 0x%08x", srnprot);

	if (!(srnprot & SRNPROT_AHPEN)) {
		_log_puts("; no PPC SRAM access, enabling it");
		srnprot |= SRNPROT_AHPEN;
		HW_SRNPROT = srnprot;
	}
	else
		_log_puts("");

	sram = (vu32 *)(HOLLYWOOD_SRAM_BASE + 0x10000);
	armbuf = (vu32 *)(MEM2_CACHED_BASE + 0x01000000);

	/* copy it into MEM2 */
	memcpy((void *)armbuf, __mini_armboot_bin_data, (uint)__mini_armboot_bin_size);
	dcache_flush((void *)armbuf, (uint)__mini_armboot_bin_size);

	/* find the "mov pc, r0" trampoline used to launch a new IOS image */
	trampoline_addr = 0;
	for (i = 0; i < 0x1000; i++) {
		if (sram[i] == 0xE1A0F000u) {
			trampoline_addr = 0xFFFF0000u + (uint)(i * 4);
			log_printf("found LaunchIOS trampoline at %08x\n", trampoline_addr);
			break;
		}
	}

	/*
	 * if we found it, find the pointer to aforementioned trampoline,
	 * this is called in the function that launches the next kernel
	 */
	trampoline_pointer = 0;
	trampoline_off = 0;
	if (trampoline_addr != 0) {
		for (i = 0; i < 0x1000; i++) {
			if (sram[i] == trampoline_addr) {
				trampoline_pointer = 0xFFFF0000u + (uint)(i * 4);
				trampoline_off = i;
				log_printf("found LaunchIOS trampoline pointer at 0x%08x/0x%08x\r\n", trampoline_pointer, (u32)(uintptr_t)&sram[i]);
				break;
			}
		}
	}

	if (!trampoline_pointer)
		panic("ARMBootNow: Couldn't find LaunchIOS trampoline pointer");

	/* write the pointer to our code there instead */
	sram[trampoline_off] = (u32)((uintptr_t)virtToPhys(armbuf) + armbuf[0]);
	log_printf("set trampoline ptr to %08x\r\n", sram[trampoline_off]);

	/* tell MINI not to reset us pretty please */
	MINI_BOOT_MAGIC_PTR = MINI_NO_RESET_MAGIC;

	/* ES stuff so we can reload */
	log_puts("Reloading...");
	while (retries) {
		retries--;
		mdelay(20);

		if (ES_GetTikViewsCount(titleID, &num_tikviews)) {
			if (!retries)
				panic("ES_GetTikViewsCount failed");
			else {
				IOS_Reset();
				ES_Init();
				continue;
			}
		}
		log_printf("Number of tikviews for IOS%d: %d\r\n", iosVer, num_tikviews);
		if (num_tikviews > 4)
			panic("too many tikviews");

		if (ES_GetTikViews(titleID, tikviews, num_tikviews)) {
			if (!retries)
				panic("ES_GetTikViews failed");
			else {
				IOS_Reset();
				ES_Init();
				continue;
			}
		}
		log_puts("Got tikviews, launching...");

		/* actually kick off the reload */
		if (ES_LaunchTitle(titleID, &tikviews[0]) != -1) {
			if (!retries)
				panic("ES_LaunchTitle failed with none-timeout error");
			else {
				IOS_Reset();
				ES_Init();
				continue;
			}
		}
		break;
	}


	log_puts("IOS should now be replaced with MINI");
}

static __attribute__((noreturn)) void wiiShutdown(void) {
	u32 cmd;
	MINI_BOOT_MAGIC_PTR = 0;

	if (H_WiiIsvWii) {
		/*
		 * vWii needs a completely different shutdown sequence where
		 * we talk to the RTC.  linux-loader does this for us on
		 * native Wii U, but it's obviously not present here.
		 */
		H_EXISelect(RTC_CHAN, RTC_CS, 2);
		cmd = RTC_WR | RTC_OFFTMR; H_EXIWriteImm(RTC_CHAN, 4, &cmd);
		cmd = 0; H_EXIWriteImm(RTC_CHAN, 4, &cmd);
		H_EXIDeselect(RTC_CHAN);
		H_EXISelect(RTC_CHAN, RTC_CS, 2);
		cmd = RTC_WR | RTC_CONTROL1; H_EXIWriteImm(RTC_CHAN, 4, &cmd);
		cmd = RTC_CONTROL1_SLEEP_EN | RTC_CONTROL1_4COUNT_EN; H_EXIWriteImm(RTC_CHAN, 4, &cmd);
		H_EXIDeselect(RTC_CHAN);
		H_EXISelect(RTC_CHAN, RTC_CS, 2);
		cmd = RTC_WR | RTC_CONTROL1; H_EXIWriteImm(RTC_CHAN, 4, &cmd);
		cmd = RTC_CONTROL1_SLEEP_EN | RTC_CONTROL1_4COUNT_EN | RTC_CONTROL1_POFF_EXE; H_EXIWriteImm(RTC_CHAN, 4, &cmd);
		H_EXIDeselect(RTC_CHAN);

		while (1); /* weird... */
	}
	HW_GPIO_OWNER |= GPIO_SHUTDOWN;
	HW_GPIO_DIR |= GPIO_SHUTDOWN;
	HW_GPIOB_OUT |= GPIO_SHUTDOWN;

	while (1); /* weird... */
}

static __attribute__((noreturn)) void wiiReboot(void) {
	u32 cmd;

	MINI_BOOT_MAGIC_PTR = 0;

	if (H_WiiIsvWii) {
		/* see comment in wiiShutdown */
		H_EXISelect(RTC_CHAN, RTC_CS, 2);
		cmd = RTC_WR | RTC_CONTROL1; H_EXIWriteImm(RTC_CHAN, 4, &cmd);
		cmd = RTC_CONTROL1_4COUNT_EN | RTC_CONTROL1_SLEEP_EN | RTC_CONTROL1_DEVPWR_EN; H_EXIWriteImm(RTC_CHAN, 4, &cmd);
		H_EXIDeselect(RTC_CHAN);
		H_EXISelect(RTC_CHAN, RTC_CS, 2);
		cmd = RTC_WR | RTC_OFFTMR; H_EXIWriteImm(RTC_CHAN, 4, &cmd);
		cmd = 0; H_EXIWriteImm(RTC_CHAN, 4, &cmd);
		H_EXIDeselect(RTC_CHAN);

		LT_RESETS &= ~BIT(0);

		while (1); /* weird... */
	}

	/* try a HW_RESETS reset */
	HW_RESETS |= RESETS_RSTBINB;
	mdelay(35);
	HW_RESETS &= ~RESETS_RSTBINB;
	mdelay(35);

	/* try a PI reset */
	PI_RESET = 0x00;

	/* wacky, just hang */
	while (1);
}

static __attribute__((noreturn)) void wiiPanic(const char *str) {
	int i;
	(void)str;
	MINI_BOOT_MAGIC_PTR = 0;

	/* can't hurt to try... */
	HW_GPIO_OWNER = 0xffffffff;
	HW_GPIOB_DIR |= GPIO_SLOT_LED;

	/* 3x blink-blink-blink.... bliiiiiiink, reboot */
	for (i = 0; i < 3; i++) {
		HW_GPIOB_OUT |= GPIO_SLOT_LED;
		mdelay(100);
		HW_GPIOB_OUT &= ~GPIO_SLOT_LED;
		mdelay(100);

		HW_GPIOB_OUT |= GPIO_SLOT_LED;
		mdelay(100);
		HW_GPIOB_OUT &= ~GPIO_SLOT_LED;
		mdelay(100);

		HW_GPIOB_OUT |= GPIO_SLOT_LED;
		mdelay(100);
		HW_GPIOB_OUT &= ~GPIO_SLOT_LED;
		mdelay(100);

		HW_GPIOB_OUT |= GPIO_SLOT_LED;
		mdelay(1000);
		HW_GPIOB_OUT &= ~GPIO_SLOT_LED;
		mdelay(500);
	}

	wiiReboot();
}

static struct partition *findSFFSPartition(void) {
	uint i, j;
	struct partition *part;

	for (i = 0; i < B_NumDevices; i++) {
		if (!(B_Devices[i]->flags & BLOCK_FLAG_HLWD_NAND))
			continue;

		for (j = 0; j < B_Devices[i]->numPartitions; j++) {
			part = B_Devices[i]->partitions[j];
			if (part->fsName && !strcmp(part->fsName, FS_SFFS.name))
				return part;
		}
	}

	return NULL;
}

static ssize_t readWholeFile(const char *path, size_t maxSize, void **out, const struct memRange *avoid, size_t avoidCount) {
	int fd;
	ssize_t size, got;
	void *buf;

	PRELOAD_STAGE("open");
	snprintf(iosPreloadPath, sizeof(iosPreloadPath), "%s", path);
	fd = FS_Open(path);
	if (fd < 0) {
		log_printf("IOS: failed to open %s: %d\r\n", path, fd);
		return fd;
	}

	PRELOAD_STAGE("size");
	size = FS_GetSize(fd);
	if (size <= 0 || (size_t)size > maxSize) {
		log_printf("IOS: %s has unreasonable size %d\r\n", path, (int)size);
		FS_Close(fd);
		return size < 0 ? (int)size : -EINVAL;
	}

	PRELOAD_STAGE("alloc");
	buf = M_PoolAllocAvoid(POOL_MEM2, (size_t)size, 32, avoid, avoidCount);
	if (!buf) {
		FS_Close(fd);
		return -ENOMEM;
	}

	PRELOAD_STAGE("read");
	got = FS_Read(fd, buf, (size_t)size);
	FS_Close(fd);
	if (got != size) {
		log_printf("IOS: short read of %s: %d/%d\r\n", path, (int)got, (int)size);
		free(buf);
		return got < 0 ? got : -EIO;
	}

	*out = buf;
	return size;
}

/* make sure SFFS is the mounted FS */
static int mountSFFS(void) {
	struct partition *sffs;
	int ret;

	PRELOAD_STAGE("find SFFS partition");
	sffs = findSFFSPartition();
	if (!sffs) {
		log_puts("IOS: no SFFS partition found");
		return -ENODEV;
	}

	if (FS_Mounted != &FS_SFFS || FS_MountedPartition != sffs) {
		PRELOAD_STAGE("mount SFFS");
		ret = FS_Mount(&FS_SFFS, sffs);
		if (ret) {
			log_printf("IOS: FS_Mount(SFFS) failed: %d\r\n", ret);
			return ret;
		}
	}
	return 0;
}

/* put back whatever was mounted before */
static void restoreFS(struct filesystem *prevFS, struct partition *prevPart) {
	if (prevFS && (prevFS != FS_Mounted || prevPart != FS_MountedPartition))
		FS_Mount(prevFS, prevPart);
}

static void freeIOSImage(void) {
	free(iosImage);
	iosImage = NULL;
	iosImageSize = 0;
	iosImageVer = 0;
}

int H_WiiPreloadIOS(u32 iosVer) {
	struct memRange avoid = { .start = IOS_IMAGE_PHYS, .size = IOS_IMAGE_MAX_SIZE };
	struct filesystem *prevFS = FS_Mounted;
	struct partition *prevPart = FS_MountedPartition;
	const struct tmd *tmd;
	const struct iosImageHdr *hdr;
	void *tmdBuf = NULL, *img = NULL;
	char path[96];
	ssize_t tmdSize, imgSize;
	u32 numContents, bootIdx, cid = 0, expectSize = 0, elfOff, i, contentType = 0;
	u8 contentHash[20] = { 0 };
	u64 expectTid = TITLE_ID_IOS | iosVer;
	u64 tid;
	int ret = -EINVAL;

	if (iosImage && iosImageVer == iosVer)
		return 0;
	freeIOSImage();

	ret = mountSFFS();
	if (ret) {
		restoreFS(prevFS, prevPart);
		return ret;
	}

	PRELOAD_STAGE("read title.tmd");
	snprintf(path, sizeof(path), "/title/00000001/%08x/content/title.tmd", iosVer);
	tmdSize = readWholeFile(path, IOS_TMD_MAX_SIZE, &tmdBuf, NULL, 0);
	if (tmdSize < 0) {
		ret = (int)tmdSize;
		goto out;
	}

	PRELOAD_STAGE("parse TMD");
	tmd = tmdBuf;
	numContents = tmd->numContents;
	bootIdx = tmd->bootIndex;
	tid = tmd->titleID;
	if (tmd->sigType != TMD_SIGTYPE_RSA2048 || tid != expectTid || bootIdx >= numContents ||
	    (size_t)tmdSize < sizeof(struct tmd) + (size_t)numContents * sizeof(struct tmdContentRecord)) {
		log_printf("IOS: unexpected TMD layout (sig %08x tid %08x%08x contents %u boot %u)\r\n",
		           tmd->sigType, (u32)(tid >> 32), (u32)tid, numContents, bootIdx);
		goto out;
	}

	/* find the content record whose index is the boot index */
	for (i = 0; i < numContents; i++) {
		const struct tmdContentRecord *rec = (const struct tmdContentRecord *)(((u8 *)tmd) + sizeof(struct tmd) + i * sizeof(struct tmdContentRecord));

		if (rec->index != bootIdx)
			continue;

		cid = rec->cid;
		contentType = rec->type;
		expectSize = (u32)rec->size;
		memcpy(contentHash, rec->hash, sizeof(contentHash));
		break;
	}
	if (i == numContents) {
		log_puts("IOS: boot content not found in TMD");
		goto out;
	}

	if (contentType & TMD_CONTENT_SHARED) {
		void *mapBuf = NULL;
		ssize_t mapSize;
		bool found = false;
		struct contentMapEntry *ent;

		PRELOAD_STAGE("read shared content.map");
		mapSize = readWholeFile("/shared1/content.map", IOS_CONTENT_MAP_MAX, &mapBuf, NULL, 0);
		if (mapSize < 0) {
			ret = (int)mapSize;
			goto out;
		}

		ent = mapBuf;
		for (ent = mapBuf; ((u8 *)ent - (u8 *)mapBuf) < mapSize; ent++) {
			if (memcmp(ent->sha1, contentHash, sizeof(contentHash)))
				continue;

			snprintf(path, sizeof(path), "/shared1/%.8s.app", ent->name);
			found = true;
			break;
		}
		free(mapBuf);
		if (!found) {
			log_puts("IOS: shared boot content not in content.map");
			PRELOAD_STAGE("shared content not in content.map");
			ret = -ENOENT;
			goto out;
		}
	}
	else
		snprintf(path, sizeof(path), "/title/00000001/%08x/content/%08x.app", iosVer, cid);

	PRELOAD_STAGE("read boot content");
	imgSize = readWholeFile(path, IOS_IMAGE_MAX_SIZE, &img, &avoid, 1);
	if (imgSize < 0) {
		ret = (int)imgSize;
		goto out;
	}

	PRELOAD_STAGE("validate boot content");
	hdr = img;
	elfOff = hdr->hdrSize + hdr->loaderSize;
	if ((u32)imgSize != expectSize || (size_t)imgSize < sizeof(*hdr) || hdr->hdrSize < sizeof(*hdr) ||
	    elfOff + hdr->elfSize != (u32)imgSize || *(u32 *)((const u8 *)img + elfOff) != 0x7f454c46) {
		log_printf("IOS: boot content failed validation (size %u/%u hdr %x loader %x elf %x)\r\n",
		           (u32)imgSize, expectSize, hdr->hdrSize, hdr->loaderSize, hdr->elfSize);
		goto out;
	}

	iosImage = img;
	iosImageSize = (u32)imgSize;
	iosImageVer = iosVer;
	iosImageTitleVer = tmd->titleVersion;
	img = NULL;
	ret = 0;
	log_printf("IOS: preloaded IOS%u boot content (%u bytes)\r\n", iosVer, iosImageSize);

out:
	free(img);
	free(tmdBuf);
	restoreFS(prevFS, prevPart);
	return ret;
}

int H_WiiPreloadChannel(u32 titleHi, u32 titleLo, u32 *iosVer) {
	struct filesystem *prevFS = FS_Mounted;
	struct partition *prevPart = FS_MountedPartition;
	void *tmdBuf = NULL;
	const struct tmd *tmd;
	char path[96];
	ssize_t tmdSize;
	u64 tid;
	u32 sysHi, sysLo;
	int ret;

	ret = mountSFFS();
	if (ret) {
		restoreFS(prevFS, prevPart);
		return ret;
	}

	PRELOAD_STAGE("read title.tmd");
	snprintf(path, sizeof(path), "/title/%08x/%08x/content/title.tmd", titleHi, titleLo);
	tmdSize = readWholeFile(path, IOS_TMD_MAX_SIZE, &tmdBuf, NULL, 0);
	if (tmdSize < 0) {
		ret = (int)tmdSize;
		goto out;
	}

	PRELOAD_STAGE("parse TMD");
	tmd = tmdBuf;
	tid = tmd->titleID;
	sysHi = (u32)(tmd->sysVersion >> 32);
	sysLo = (u32)(tmd->sysVersion);
	if (tmd->sigType != TMD_SIGTYPE_RSA2048 || (size_t)tmdSize < sizeof(struct tmd) ||
	    tid != (((u64)titleHi << 32) | titleLo)) {
		log_printf("Title: unexpected TMD layout for %08x-%08x\r\n", titleHi, titleLo);
		ret = -EINVAL;
		goto out;
	}
	if (sysHi != 1 || !sysLo || sysLo > 0xffff) {
		log_printf("Title: %08x-%08x has no usable IOS (%08x-%08x)\r\n", titleHi, titleLo, sysHi, sysLo);
		ret = -EINVAL;
		goto out;
	}
	log_printf("Title %08x-%08x requires IOS%u\r\n", titleHi, titleLo, sysLo);

	free(tmdBuf);
	tmdBuf = NULL;
	restoreFS(prevFS, prevPart);

	ret = H_WiiPreloadIOS(sysLo);
	if (!ret)
		*iosVer = sysLo;
	return ret;

out:
	free(tmdBuf);
	restoreFS(prevFS, prevPart);
	return ret;
}

static void jumpToIOSImage(u32 iosVer) {
	struct iosImageHdr *hdr;
	void *dest;
	enum MINI_Err err;

	if (!iosImage || iosImageVer != iosVer)
		panic("IOS reload without a preloaded image");

	dest = physToCached(IOS_IMAGE_PHYS);
	memmove(dest, iosImage, iosImageSize);
	hdr = dest;
	dcache_flush(dest, iosImageSize);

	/* IOS drives its own pins, don't leave them with us */
	H_GPIOReleaseForIOS();

	/* IOS hangs if we don't write its version */
	*(vu32 *)(MEM1_UNCACHED_BASE + 0x3140) = (iosVer << 16) | iosImageTitleVer;
	*(vu32 *)NAND_REG_BANK = NAND_BANK_SLCCMPT;
	sync();

	log_printf("IOS: image placed at %08x, jumping to %08x\r\n", IOS_IMAGE_PHYS, IOS_IMAGE_PHYS + hdr->hdrSize);

	/* SYS_JUMP has no reply */
	err = MINI_IPCPost(IPC_MINI_CODE_JUMP, 0, 1, IOS_IMAGE_PHYS + hdr->hdrSize);
	if (err != MINI_OK)
		panic("MINI JUMP to IOS failed");
}

void H_WiiReloadIOS(u32 iosVer) {
	int i = 0, ret;
	u64 tb;

	log_printf("Trying to reload into IOS%d\r\n", iosVer);
	jumpToIOSImage(iosVer);
	log_puts("Waiting for IOS...");

	while ((u32)IOS_GetVersion() != iosVer) {
		mdelay(1);
		i++;
		if (i >= IOS_BOOT_TIMEOUT_MS)
			panic("IOS reload stuck");
	}

	/* wait for PPCCTRL to match expected value */
	i = 0;
	while (!(HW_IPC_PPCCTRL & (HW_IPC_PPCCTRL_Y2 | HW_IPC_PPCCTRL_Y1))) {
		mdelay(1);
		i++;
		if (i >= IOS_BOOT_TIMEOUT_MS)
			panic("IOS reload succeeded but stuck waiting for IPC");
	}

	tb = mftb();
	while (true) {
		IOS_Reset();
		ret = ES_Init();
		if (!ret)
			break;

		if (T_HasElapsed(tb, 1000 * 1000))
			panic("IOS reload succeeded and IPC is ready but IPC init failed");
	}
}

static void __attribute__((noreturn)) wiiExitThread(void *data) {
	int iosVer = H_WiiBootIOS;
	void (*stub)(void);

	(void)data;
	L_Method = LOG_METHOD_ALL_ODEV;
	log_printf("\x1b[1;1H\x1b[2JReloading...\r\n");
	H_PrepareForExecEntry();
	V_Flush();
	if (iosVer <= 0)
		iosVer = IOS_VALID_GUESS;

	H_WiiReloadIOS((u32)iosVer);

	/* IOS is reloaded and ready, hand off to the stub! */
	log_puts("Handing off to stub...");
	H_VIDisable();
	stub = (void (*)(void))(MEM1_CACHED_BASE + 0x1800);
	stub();
	__builtin_unreachable();
}

static void __attribute__((noreturn)) wiiExit(void) {
	int iosVer = H_WiiBootIOS, ret;
	static char msg[192];

	if (iosVer <= 0)
		iosVer = IOS_VALID_GUESS;

	/* the reload below needs the IOS image in RAM, read it now while NAND still works */
	ret = H_WiiPreloadIOS((u32)iosVer);
	if (ret) {
		snprintf(msg, sizeof(msg), "IOS%d preload failed at '%s' (%s): %d", iosVer, iosPreloadStage, iosPreloadPath, ret);
		panic(msg);
	}

	/* IOS protects upper MEM2, which is where our stack is */
	TH_Handoff(POOL_MEM1, wiiExitThread, NULL);
}

/* launch a title like IOS */
struct channelBootArgs {
	u32 titleHi, titleLo, iosVer;
};

static void __attribute__((noreturn)) channelBootThread(void *data) {
	const struct channelBootArgs *args = data;
	tikview_t tikviews[4] ALIGN(32);
	u64 titleID = ((u64)args->titleHi << 32) | args->titleLo;
	u32 numViews = 0;
	int retries = 20;

	L_Method = LOG_METHOD_ALL_ODEV;
	log_printf("\x1b[1;1H\x1b[2JBooting title %08x-%08x via IOS%u\r\n", args->titleHi, args->titleLo, args->iosVer);
	H_PrepareForExecEntry();
	V_Flush();
	H_VIDisable();

	H_WiiReloadIOS(args->iosVer);

	while (retries--) {
		mdelay(20);

		if (ES_GetTikViewsCount(titleID, &numViews) || !numViews || numViews > 4) {
			if (!retries)
				panic("channel boot: no usable tickets");
			IOS_Reset();
			ES_Init();
			continue;
		}
		if (ES_GetTikViews(titleID, tikviews, numViews)) {
			if (!retries)
				panic("channel boot: ES_GetTikViews failed");
			IOS_Reset();
			ES_Init();
			continue;
		}

		ES_LaunchTitle(titleID, &tikviews[0]);
		break;
	}

	/* the title will take over from here; if it doesn't, give up */
	mdelay(10000);
	panic("channel boot: title didn't start");
}

void __attribute__((noreturn)) H_WiiBootChannel(u32 titleHi, u32 titleLo, u32 iosVer) {
	static struct channelBootArgs args;

	args.titleHi = titleHi;
	args.titleLo = titleLo;
	args.iosVer = iosVer;

	L_Method = LOG_METHOD_ALL_ODEV;
	TH_Handoff(POOL_MEM1, channelBootThread, &args);
	__builtin_unreachable();
}

static void wiiEject(void) {
	HW_GPIOB_OUT |= GPIO_DO_EJECT;
	barrier();
	mdelay(500);
	HW_GPIOB_OUT &= ~GPIO_DO_EJECT;
}

static struct platOps wiiPlatOps = {
	.panic = wiiPanic,
	.debugWriteChar = NULL,
	.debugWriteStr = NULL,
	.reboot = wiiReboot,
	.shutdown = wiiShutdown,
	.exit = NULL,
	.ejectDisc = wiiEject
};

enum wiiInitState {
	STATE_ANALYZE,
	STATE_ANALYZE_IOS,
	STATE_IOS_INIT,
	STATE_IOS_PRIV_ESC,
	STATE_IOS_ARMBOOTNOW,
	STATE_HW_UNRESTRICT,
	STATE_ANALYZE_MINI,
	STATE_MINI_INIT,
	STATE_MINI_RELOAD,
	STATE_MINI_PRIV_ESC,
	STATE_POST_IOS_SANITIZE,
	STATE_READY
};

#define GOTO_STATE(x) { \
	prevStateStr = stateStr; \
	stateStr = __stringify(x); \
	state = x; \
}

#define SFLAG_AHBPROT_PERMS BIT(0)
#define SFLAG_SRNPROT_PERMS BIT(1)
#define SFLAG_MEM2_ACCESS   BIT(2)
#define SFLAG_ORIG_MINI     BIT(3)
#define SFLAG_CUR_MINI      BIT(4)
#define SFLAG_RAN_DEVSHA    BIT(5)
#define SFLAG_RAN_ABN       BIT(6)
#define SFLAG_IOS_INIT      BIT(7)
#define SFLAG_MINI_INIT     BIT(8)
#define SFLAG_MINI_RELOADED BIT(9)

#define SET_SFLAG(flag, cond) { \
	if ((cond)) \
		stateFlags |= ((u16)(flag)); \
	else \
		stateFlags &= (u16)~((u16)(flag)); \
}

#define GET_SFLAG(flag) !!(stateFlags & (flag))

#define DUMP_STATE() \
	log_printf("H_InitWii: state %s -> %s\r\n", prevStateStr, stateStr); \
	log_printf("H_InitWii: ahb=%d, srn=%d, mem2=%d, origMINI=%d, curMINI=%d, devsha=%d, ABN=%d, miniInit=%d, miniRld=%d\r\n", \
		GET_SFLAG(SFLAG_AHBPROT_PERMS), GET_SFLAG(SFLAG_SRNPROT_PERMS), GET_SFLAG(SFLAG_MEM2_ACCESS), \
		GET_SFLAG(SFLAG_ORIG_MINI), GET_SFLAG(SFLAG_CUR_MINI), GET_SFLAG(SFLAG_RAN_DEVSHA), GET_SFLAG(SFLAG_RAN_ABN), \
		GET_SFLAG(SFLAG_MINI_INIT), GET_SFLAG(SFLAG_MINI_RELOADED));

void __attribute__((noreturn)) H_InitWii(void) {
	u64 tb;
	u32 batl, batu, hid4;
	vu32 *armbuf;
	enum wiiInitState state;
	enum MINI_Err miniErr;
	const char *stateStr, *prevStateStr;
	u16 stateFlags;
	uint waitMiniRetries = 10;

	/* set plat ops */
	H_PlatOps = &wiiPlatOps;

	/* we want I/O to be on the AHB, where we expect it */
	HW_AIPPROT |= AIPPROT_ENAHBIOPI;

	/* debug console */
	H_TinyUGInit();

	/* we want to get logs out immediately for crashing IOS or failing to get AHBPROT */
	O_DebugInit();

	/*
	 * Map MEM2
	 */
	/* BPRN = MEM2_PHYS_BASE, WIMG=0000, PP=RW */
	batl = MEM2_PHYS_BASE | 2;

	/* BEPI = MEM2_CACHED_BASE, BL=256MB, Vs=1, Vp=1 */
	batu = MEM2_CACHED_BASE | 0x1fff;

	setbat(2, SETBAT_TYPE_BOTH, batu, batl);

	/* BPRN = MEM2_PHYS_BASE, WIMG=0101, PP=RW */
	batl = MEM2_PHYS_BASE | 0x2a;

	/* BEPI = MEM2_UNCACHED_BASE, BL=256MB, Vs=1, Vp=1 */
	batu = MEM2_UNCACHED_BASE | 0x1fff;

	setbat(3, SETBAT_TYPE_BOTH, batu, batl);

	/*
	 * Set up HID4 to set SBE so we can access more BATs
	 * NOTE: even though we don't actually use the higher BATs here,
	 * we need to write to HID4 for Dolphin to detect us as a Wii binary,
	 * and it's just common courtesy to do that for the kerenl before we load it
	 */
	hid4 = mfspr(HID4);
	hid4 |= HID4_SBE;
	mtspr(HID4, hid4);

	/* zero out the other BATs */
	batl = 0x00000000;
	batu = 0x00000000;

	setbat(4, SETBAT_TYPE_BOTH, batu, batl);
	setbat(5, SETBAT_TYPE_BOTH, batu, batl);
	setbat(6, SETBAT_TYPE_BOTH, batu, batl);
	setbat(7, SETBAT_TYPE_BOTH, batu, batl);

	/* seed initial state */
	stateStr = "None";
	stateFlags = 0;
	GOTO_STATE(STATE_ANALYZE);
	SET_SFLAG(SFLAG_AHBPROT_PERMS, HW_AHBPROT == 0xffffffff);
	SET_SFLAG(SFLAG_SRNPROT_PERMS, HW_SRNPROT & SRNPROT_AHPEN);
	SET_SFLAG(SFLAG_MEM2_ACCESS, testMEM2());
	SET_SFLAG(SFLAG_ORIG_MINI, GET_SFLAG(SFLAG_MEM2_ACCESS) ? MINI_ValidInfoHdr() == MINI_OK : false);
	SET_SFLAG(SFLAG_CUR_MINI, GET_SFLAG(SFLAG_ORIG_MINI));

	while (true) {
		DUMP_STATE();

		switch (state) {
		case STATE_ANALYZE: {
			SET_SFLAG(SFLAG_AHBPROT_PERMS, HW_AHBPROT == 0xffffffff);
			SET_SFLAG(SFLAG_SRNPROT_PERMS, HW_SRNPROT & SRNPROT_AHPEN);
			SET_SFLAG(SFLAG_MEM2_ACCESS, testMEM2());
			SET_SFLAG(SFLAG_CUR_MINI, GET_SFLAG(SFLAG_MEM2_ACCESS) ? MINI_ValidInfoHdr() == MINI_OK : false);

			if (!GET_SFLAG(SFLAG_CUR_MINI)) {
				GOTO_STATE(STATE_ANALYZE_IOS);
				break;
			}
			else {
				GOTO_STATE(STATE_ANALYZE_MINI);
				break;
			}
		}
		case STATE_ANALYZE_IOS: {
			assert(!GET_SFLAG(SFLAG_CUR_MINI));

			/* prevent infinite loops */
			if (GET_SFLAG(SFLAG_RAN_DEVSHA) && !GET_SFLAG(SFLAG_RAN_ABN) && !GET_SFLAG(SFLAG_AHBPROT_PERMS))
				panic("Wii: IOS /dev/sha exploit failed");
			if (GET_SFLAG(SFLAG_RAN_ABN) && !GET_SFLAG(SFLAG_CUR_MINI)) {
				if (!waitMiniRetries)
					panic("Wii: ARMBootNow failed");

				/* loading MINI can be slow in some cases (e.g. Ironic emulator), give it a few retries */
				waitMiniRetries--;
				mdelay(50);
				GOTO_STATE(STATE_ANALYZE);
				break;
			}

			/* determine where to go next */
			if (!GET_SFLAG(SFLAG_IOS_INIT)) {
				GOTO_STATE(STATE_IOS_INIT);
				break;
			}
			/* IOS, no perms: -> STATE_IOS_PRIV_ESC */
			else if (GET_SFLAG(SFLAG_IOS_INIT) && !GET_SFLAG(SFLAG_AHBPROT_PERMS)) {
				GOTO_STATE(STATE_IOS_PRIV_ESC);
				break;
			}
			/* IOS, has perms: -> STATE_IOS_ARMBOOTNOW */
			else if (GET_SFLAG(SFLAG_IOS_INIT) && GET_SFLAG(SFLAG_AHBPROT_PERMS) && !GET_SFLAG(SFLAG_RAN_ABN)) {
				GOTO_STATE(STATE_IOS_ARMBOOTNOW);
				break;
			}
			else
				__builtin_unreachable(); /* impossible to reach due to the above guards */
		}
		case STATE_IOS_INIT: {
			assert(!GET_SFLAG(SFLAG_CUR_MINI));

			H_WiiBootIOS = IOS_GetVersion();
			IOS_Reset();
			ES_Init();
			SET_SFLAG(SFLAG_IOS_INIT, true);
			GOTO_STATE(STATE_ANALYZE_IOS);
			break;
		}
		case STATE_IOS_PRIV_ESC: {
			assert(!GET_SFLAG(SFLAG_CUR_MINI));

			if (IOS_DevShaExploit())
				panic("Wii: IOS /dev/sha exploit failed");

			SET_SFLAG(SFLAG_RAN_DEVSHA, true);
			GOTO_STATE(STATE_HW_UNRESTRICT);
			break;
		}
		case STATE_IOS_ARMBOOTNOW: {
			assert(!GET_SFLAG(SFLAG_CUR_MINI));

			armbootnow();
			SET_SFLAG(SFLAG_RAN_ABN, true);
			GOTO_STATE(STATE_ANALYZE);
			break;
		}
		case STATE_HW_UNRESTRICT: {
			/* set up SRAM access */
			HW_SRNPROT |= SRNPROT_AHPEN;

			/* set up basic GPIOs for panic indicator */
			HW_GPIO_OWNER |= GPIO_SLOT_LED;
			HW_GPIO_ENABLE |= GPIO_SLOT_LED;
			HW_GPIOB_DIR |= GPIO_SLOT_LED;

			/* unrestrict memory */
			HW_MEM_PROT_SPL = 0;
			HW_MEM_PROT_SPL_BASE = 0;
			HW_MEM_PROT_SPL_END = 0;
			HW_MEM_PROT_DDR = 0;
			HW_MEM_PROT_DDR_BASE = 0;
			HW_MEM_PROT_DDR_END = 0;

			GOTO_STATE(STATE_ANALYZE);
			break;
		}
		case STATE_ANALYZE_MINI: {
			assert(GET_SFLAG(SFLAG_CUR_MINI));

			/* MINI, not initialized -> STATE_MINI_INIT */
			if (!GET_SFLAG(SFLAG_MINI_INIT)) {
				GOTO_STATE(STATE_MINI_INIT);
				break;
			}
			/* MINI, initialized, no perms -> STATE_MINI_PRIV_ESC */
			else if (GET_SFLAG(SFLAG_MINI_INIT) && (!GET_SFLAG(SFLAG_MEM2_ACCESS) || !GET_SFLAG(SFLAG_AHBPROT_PERMS) || !GET_SFLAG(SFLAG_SRNPROT_PERMS))) {
				GOTO_STATE(STATE_MINI_PRIV_ESC);
				break;
			}
			/* MINI, was originally MINI, initialized, has perms -> STATE_MINI_RELOAD */
			else if (GET_SFLAG(SFLAG_MINI_INIT) && GET_SFLAG(SFLAG_ORIG_MINI) && !GET_SFLAG(SFLAG_MINI_RELOADED) && GET_SFLAG(SFLAG_MEM2_ACCESS) && GET_SFLAG(SFLAG_AHBPROT_PERMS) && GET_SFLAG(SFLAG_SRNPROT_PERMS)) {
				GOTO_STATE(STATE_MINI_RELOAD);
				break;
			}
			/* MINI, was originally MINI, initialized, reloaded, has perms -> STATE_READY */
			else if (GET_SFLAG(SFLAG_MINI_INIT) && GET_SFLAG(SFLAG_ORIG_MINI) && GET_SFLAG(SFLAG_MINI_RELOADED) && GET_SFLAG(SFLAG_MEM2_ACCESS) && GET_SFLAG(SFLAG_AHBPROT_PERMS) && GET_SFLAG(SFLAG_SRNPROT_PERMS)) {
				GOTO_STATE(STATE_READY);
				break;
			}
			/* MINI, was originally IOS, initialized, has perms -> STATE_POST_IOS_SANITIZE */
			else if (GET_SFLAG(SFLAG_MINI_INIT) && !GET_SFLAG(SFLAG_ORIG_MINI) && GET_SFLAG(SFLAG_RAN_ABN) &&
				GET_SFLAG(SFLAG_MEM2_ACCESS) && GET_SFLAG(SFLAG_AHBPROT_PERMS) && GET_SFLAG(SFLAG_SRNPROT_PERMS)) {
				GOTO_STATE(STATE_POST_IOS_SANITIZE);
				break;
			}
			else
				__builtin_unreachable();
		}
		case STATE_MINI_INIT: {
			assert(GET_SFLAG(SFLAG_CUR_MINI));
			if (GET_SFLAG(SFLAG_ORIG_MINI))
				mdelay(250); /* FIXME: HACK to wait for MINI to actually fully boot first */
			miniErr = MINI_Init();
			if (miniErr != MINI_OK) {
				if (miniErr == MINI_TIMEOUT) {
					GOTO_STATE(STATE_ANALYZE);
					break;
				}
				log_printf("MINI_Init failed with %u\r\n", miniErr);
				panic("MINI_Init failed");
			}
			SET_SFLAG(SFLAG_MINI_INIT, true);
			GOTO_STATE(STATE_ANALYZE);
			break;
		}
		case STATE_MINI_RELOAD: {
			assert(GET_SFLAG(SFLAG_CUR_MINI) && GET_SFLAG(SFLAG_MINI_INIT));

			/* load _our_ copy of MINI into scratch MEM2 */
			armbuf = (vu32 *)(MEM2_CACHED_BASE + 0x01000000);
			memcpy((void *)armbuf, __mini_armboot_bin_data, (uint)__mini_armboot_bin_size);
			dcache_flush((void *)armbuf, (uint)__mini_armboot_bin_size);

			/* tell MINI to pretty please not reset us kthxbye */
			MINI_BOOT_MAGIC_PTR = MINI_NO_RESET_MAGIC;

			/* clear the IPC infohdr so we can keep track of when the new MINI has reloaded */
			*(u32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4) = 0;
			MINI_IPCPost(IPC_MINI_CODE_JUMP, 0, 1, virtToPhys(armbuf) + armbuf[0]);
			mdelay(500); /* hardcoded time to give it a sec, mainly so our logs don't overlap */

			SET_SFLAG(SFLAG_MINI_INIT, false);
			SET_SFLAG(SFLAG_MINI_RELOADED, true);
			SET_SFLAG(SFLAG_CUR_MINI, false);
			GOTO_STATE(STATE_ANALYZE);
			break;
		}
		case STATE_MINI_PRIV_ESC: {
			assert(GET_SFLAG(SFLAG_CUR_MINI));

			miniErr = MINI_IPCPost(IPC_MINI_CODE_WRITE32, 0, 2, virtToPhys(&HW_AHBPROT), 0xffffffff);
			if (miniErr != MINI_OK)
				panic("Failed to write AHBPROT via MINI");

			miniErr = MINI_IPCPost(IPC_MINI_CODE_SET32, 0, 2, virtToPhys(&HW_SRNPROT), SRNPROT_AHPEN);
			if (miniErr != MINI_OK)
				panic("Failed to write SRNPROT via MINI");

			/* do more general hw unrestriction now that we have perms */
			GOTO_STATE(STATE_HW_UNRESTRICT);
			break;
		}
		case STATE_POST_IOS_SANITIZE: {
			assert(GET_SFLAG(SFLAG_CUR_MINI) && !GET_SFLAG(SFLAG_ORIG_MINI) && GET_SFLAG(SFLAG_AHBPROT_PERMS));

			/* TODO: all of this */
			tb = mftb();
			OHCI_HC_COMMAND_STATUS(0) |= OHCI_HC_COMMAND_STATUS_HCR;
			OHCI_HC_COMMAND_STATUS(1) |= OHCI_HC_COMMAND_STATUS_HCR;
			while ((OHCI_HC_COMMAND_STATUS(0) | OHCI_HC_COMMAND_STATUS(1)) & OHCI_HC_COMMAND_STATUS_HCR) {
				if (T_HasElapsed(tb, 1000))
					panic("OHCI reset timed out");
			}
			OHCI_HC_INTERRUPT_DISABLE(0) = 0xffffffff;
			OHCI_HC_INTERRUPT_DISABLE(1) = 0xffffffff;
			OHCI_HC_INTERRUPT_STATUS(0) = OHCI_HC_INTERRUPT_STATUS(0);
			OHCI_HC_INTERRUPT_STATUS(1) = OHCI_HC_INTERRUPT_STATUS(1);


			tb = mftb();
			EHCI_USBCMD(0) &= ~EHCI_USBCMD_RS;
			while (!(EHCI_USBSTS(0) & EHCI_USBSTS_HCHALTED)) {
				if (T_HasElapsed(tb, 10 * 1000))
					panic("EHCI halt timed out");
			}
			EHCI_USBCMD(0) |= EHCI_USBCMD_HCRESET;
			EHCI_USBINTR(0) = 0;
			EHCI_USBSTS(0) = EHCI_USBSTS(0);
			GOTO_STATE(STATE_READY);
			break;
		}
		case STATE_READY:
			goto out;
		default:
			panic("H_InitWii state machine in invalid state");
		}
	}
out:

	/* ASCII 'STUBHAXX' */
	if ((*(vu64 *)(MEM1_UNCACHED_BASE + 0x1804)) == 0x5354554248415858ULL && !GET_SFLAG(SFLAG_ORIG_MINI)) {
		log_puts("Detected HBC-complatible reload stub");
		H_PlatOps->exit = wiiExit;
	}

	/* we want to load Wii drivers */
	D_DriverMask = DRIVER_ALLOW_WII;

	/* final sanity check, these should pass if all went well; don't use assert() since it gets compiled out of release builds */
	if (HW_AHBPROT != 0xffffffff)
		panic("AHBPROT not enabled at end of H_InitWii");
	if (!testMEM2())
		panic("MEM2 not fully accessible at end of H_InitWii");


	/* now check for Wii variants */
	if ((LT_CHIPREVID & 0xffff0000) == 0xcafe0000) {
		H_WiiIsvWii = true;
		log_puts("Detected Wii U vWii");
	}
	if (LT_CHIPREVID == 0x8badf00d) {
		H_WiiIsIronic = true;
		log_puts("Detected Ironic emulator");
	}
	/*
	 * The stock boot chain under Ironic seems to leave MEM_RANKSET=1, which
	 * normally indicates 128MiB MEM2 on real hardware, but here it does not
	 * in fact mean this, and trying to use upper addresses will fail.
	 */
	else if (HW_MEM_RANKSEL == 1) {
		H_WiiIsDevkit = true;
		log_puts("Detected Nintendo Wii NDEV/RVT-H (128 MiB MEM2)");
	}


	/* memory controller registers are accessible now */
	if (H_WiiIsvWii) {
		/* enable 256MiB MEM2 */
		HW_MEM_ROWMSK = 0x3fff;
		sync();
		H_MEM2Size = MEM2_SIZE_VWII;
	}
	else if (H_WiiIsDevkit && !H_WiiIsIronic)
		H_MEM2Size = MEM2_SIZE_NDEV;

	MINI_BOOT_MAGIC_PTR = 0;

	/* kick off the real init */
	I_InitCommon();

	wiiPanic("I_InitCommon should not return");
	__builtin_unreachable();
}
