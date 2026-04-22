/*
 * NPLL - Drivers - Hollywood SDMMC
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "hollywood_sdmmc"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/drivers/mmc.h>
#include <npll/drivers/sdio.h>
#include <npll/drivers.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/types.h>
#include <npll/utils.h>
#include "sdmmc/sdhc.h"

static REGISTER_DRIVER(sdmmcDrv);
/* 4 max HCs (SDHCI0-3), 2 max cards (SD on SDHCI0, eMMC on SDHCI2) */
static sdio_host_dev_t sdioDev[4];
static mmc_card_t mmcDev[2];
static struct blockDevice sdmmcBdev[2];
static bool sdmmcRegistered[2];
static bool checkConnected = false;

static inline mmc_card_t bdevToMMC(struct blockDevice *bdev) {
	if (bdev == &sdmmcBdev[0])
		return mmcDev[0];
	else if (bdev == &sdmmcBdev[1])
		return mmcDev[1];
	else
		assert_unreachable();
}

static inline sdio_host_dev_t *irqToSDIO(enum irqDev dev) {
	switch (dev) {
	case IRQDEV_SDHCI0: return &sdioDev[0];
	case IRQDEV_SDHCI1: return &sdioDev[1];
	case IRQDEV_SDHCI2: return &sdioDev[2];
	case IRQDEV_SDHCI3: return &sdioDev[3];
	default: assert_unreachable();
	}
}

static const void *sdhcAddrs[4] = {
	SDHC0_ADDR,
	SDHC1_ADDR,
	SDHC2_ADDR,
	SDHC3_ADDR
};

static const int sdhcToBdevIdx[4] = {
	0,
	-1,
	1,
	-1
};

static const int sdmmcIRQTable[4] = {
	IRQDEV_SDHCI0,
	IRQDEV_SDHCI1,
	IRQDEV_SDHCI2,
	IRQDEV_SDHCI3
};

static const char *bdevNames[2] = {
	"sdhci0",
	"sdhci2"
};

/* TODO: maybe move alignment handling into B_Read? */
static ssize_t sdmmcRead(struct blockDevice *bdev, void *dest, size_t len, u64 off) {
	u8 ALIGN(32) tmp[512];
	uint blkSize;
	size_t ret, startBlock, remaining, headSkip, headBytes, alignedBlocks, tailBytes;

	/* precompute a bunch of junk */
	blkSize = bdev->blockSize;
	startBlock = (size_t)(off / blkSize);
	remaining = len;
	headSkip = (size_t)(off % blkSize);
	headBytes = alignedBlocks = tailBytes = 0;

	//log_printf("read: dest=0x%08x, SB=%u, len=%u, HS=%u, HB=%u\r\n", dest, startBlock, len, headSkip, headBytes);

	/* destination buffer must be 32B aligned else the HC locks up */
	if ((u32)dest & 0x1f) {
		log_puts("sdmmcRead: cannot use unaligned buffer!!");
		return -1;
	}

	if (!bdevToMMC(bdev)) {
		log_puts("sdmmcRead: bogus bdev");
		return -1;
	}

	/* if we have an unaligned first block, read it separately */
	if (headSkip) {
		dcache_invalidate(tmp, sizeof(tmp));
		ret = (size_t)mmc_block_read(bdevToMMC(bdev), startBlock, 1, tmp,
			(uintptr_t)virtToPhys(tmp), NULL, NULL);
		if (ret != blkSize)
			return -1;

		headBytes = blkSize - headSkip;
		if (headBytes > remaining)
			headBytes = remaining;

		memcpy(dest, tmp + headSkip, headBytes);

		dest += headBytes;
		remaining -= headBytes;
		startBlock++;
	}

	alignedBlocks = remaining / blkSize;
	tailBytes = remaining % blkSize;

	/* read the remaining aligned portions in one pass, if any */
	if (alignedBlocks) {
		dcache_invalidate(dest, alignedBlocks * blkSize);
		ret = (size_t)mmc_block_read(bdevToMMC(bdev), startBlock, alignedBlocks, dest,
			(uintptr_t)virtToPhys(dest), NULL, NULL);
		if (ret != alignedBlocks * blkSize)
			return -1;

		dest += alignedBlocks * blkSize;
		startBlock += alignedBlocks;
	}

	/* if we have an unaligned last block, read it separately */
	if (tailBytes) {
		dcache_invalidate(tmp, sizeof(tmp));
		ret = (size_t)mmc_block_read(bdevToMMC(bdev), startBlock, 1, tmp,
			(uintptr_t)virtToPhys(tmp), NULL, NULL);
		if (ret != blkSize)
			return -1;

		memcpy(dest, tmp, tailBytes);
	}

	return (ssize_t)len;
}

static ssize_t sdmmcWrite(struct blockDevice *bdev, const void *src, size_t len, u64 off) {
	/* TODO: rewrite this like the new sdmmcRead once I care about writes */
	(void)bdev;
	(void)src;
	(void)len;
	(void)off;
	return -1;
}

static void sdmmcRegisterBlock(struct blockDevice *bdev, const char *name) {
	long long capacity;

	capacity = mmc_card_capacity(bdevToMMC(bdev));
	if (capacity <= 0) {
		log_puts("failed to get card capacity");
		return;
	}

	memset(bdev, 0, sizeof(*bdev));
	bdev->name = (char *)name; /* FIXME: also work for Wii U eMMC, which is on (iirc) sdhci3 */
	bdev->size = (u64)capacity;
	bdev->blockSize = (u32)mmc_block_size(bdevToMMC(bdev));
	bdev->drvData = mmcDev;
	bdev->read = sdmmcRead;
	bdev->write = sdmmcWrite;

	B_Register(bdev);
}

static void sdmmcCB(void) {
	u32 pstate;
	int ret;
	if (!checkConnected)
		return;

	checkConnected = false;

	if (!sdmmcRegistered[0]) {
		/* check for a card */
		pstate = sdio_get_present_state(&sdioDev[0]);
		if (!(pstate & SDHC_PRES_STATE_CINST))
			return; /* nothing */

		/* ooh, we have a card */
		ret = mmc_init(&sdioDev[0], &mmcDev[0]);
		if (ret) {
			log_printf("mmc_init failed with %d\r\n", ret);
			return;
		}

		log_puts("card init success in CB");
		sdmmcDrv.state = DRIVER_STATE_READY;
		sdmmcRegisterBlock(&sdmmcBdev[0], bdevNames[0]);
		sdmmcRegistered[0] = true;
	}
	else {
		/* check for card removed */
		pstate = sdio_get_present_state(&sdioDev[0]);
		if (pstate & SDHC_PRES_STATE_CINST)
			return; /* nothing */

		log_puts("card removed!");
		sdmmcRegistered[0] = false;
		B_Unregister(&sdmmcBdev[0]);
		sdmmcDrv.state = DRIVER_STATE_NO_HARDWARE;
		if (mmcDev[0]) {
			free(mmcDev[0]);
			mmcDev[0] = NULL;
		}
		memset(&sdmmcBdev[0], 0, sizeof(sdmmcBdev[0]));
		/*
		 * Hot-remove/reinsert needs a full host reset here. Without it,
		 * the next probe can limp along with CRC errors and then degrade
		 * into the old read-active/data-active wedge.
		 */
		if (sdio_reset(&sdioDev[0])) {
			log_puts("sdmmcCB: host reset after removal failed");
		}
	}
}

static void sdmmcIRQ(enum irqDev dev) {
	sdio_handle_irq(irqToSDIO(dev), (int)dev);
	checkConnected = true;
}

static void sdmmcInit(void) {
	u32 pstate;
	int ret;
	uint i, maxHC;
	void *addr;

	memset(sdioDev, 0, sizeof(sdioDev));
	memset(mmcDev, 0, sizeof(mmcDev));
	memset(sdmmcBdev, 0, sizeof(sdmmcBdev));
	memset(sdmmcRegistered, 0, sizeof(sdmmcRegistered));

	if (H_ConsoleType == CONSOLE_TYPE_WII_U)
		maxHC = 3;
	else
		maxHC = 2;

	D_AddCallback(sdmmcCB);
	IRQ_RegisterHandler(IRQDEV_SDHCI0, sdmmcIRQ);
	IRQ_RegisterHandler(IRQDEV_SDHCI1, sdmmcIRQ);
	if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		IRQ_RegisterHandler(IRQDEV_SDHCI2, sdmmcIRQ);
		IRQ_RegisterHandler(IRQDEV_SDHCI3, sdmmcIRQ);
	}

	sdmmcDrv.state = DRIVER_STATE_READY;
	for (i = 0; i < maxHC; i++) {
		addr = (void *)sdhcAddrs[i];

		/* initialize the controller */
		ret = sdhc_init(addr, sdmmcIRQTable, 4, &sdioDev[i]);
		if (ret) {
			log_printf("sdio_init (SDHCI%d) failed with %d\r\n", i, ret);
			continue;
		}
		// log_printf("sdhc_init (SDHCI%d) success\r\n", i);

		ret = sdio_reset(&sdioDev[i]);
		if (ret) {
			log_printf("sdio_reset (SDHCI%d) failed with %d\r\n", i, ret);
			continue;
		}

		/* initialize an attached MMC/SD Card */
		pstate = sdio_get_present_state(&sdioDev[i]);
		if (!(pstate & SDHC_PRES_STATE_CINST)) {
			log_printf("no card inserted (SDHCI%d)\r\n", i);
			continue;
		}

		if (i != 0 && i != 2)
			continue; /* don't do MMC init on WiFi/Toucan */

		ret = mmc_init(&sdioDev[i], &mmcDev[sdhcToBdevIdx[i]]);
		if (ret) {
			log_printf("mmc_init failed (SDHCI%d) with %d\r\n", i, ret);
			continue;
		}
		log_printf("mmc_init (SDHCI%d) success\r\n", i);

		sdmmcRegisterBlock(&sdmmcBdev[sdhcToBdevIdx[i]], bdevNames[sdhcToBdevIdx[i]]);
		sdmmcRegistered[sdhcToBdevIdx[i]] = true;
	}

	IRQ_Unmask(IRQDEV_SDHCI0);
	IRQ_Unmask(IRQDEV_SDHCI1);
	IRQ_Unmask(IRQDEV_SDHCI2);
	IRQ_Unmask(IRQDEV_SDHCI3);
}

static void sdmmcCleanup(void) {
	IRQ_Mask(IRQDEV_SDHCI0);
	sdio_reset(&sdioDev[0]);
	IRQ_Mask(IRQDEV_SDHCI1);
	sdio_reset(&sdioDev[1]);
	if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		IRQ_Mask(IRQDEV_SDHCI2);
		sdio_reset(&sdioDev[2]);
		IRQ_Mask(IRQDEV_SDHCI3);
		sdio_reset(&sdioDev[3]);
	}

	if (sdmmcRegistered[0]) {
		sdmmcRegistered[0] = false;
		free(mmcDev[0]);
		B_Unregister(&sdmmcBdev[0]);
	}
	if (sdmmcRegistered[1]) {
		sdmmcRegistered[1] = false;
		free(mmcDev[1]);
		B_Unregister(&sdmmcBdev[1]);
	}
}

static REGISTER_DRIVER(sdmmcDrv) = {
	.name = "Hollywood/Latte SDMMC",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_BLOCK,
	.init = sdmmcInit,
	.cleanup = sdmmcCleanup
};
