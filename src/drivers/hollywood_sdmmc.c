/*
 * NPLL - Drivers - Hollywood SDMMC
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "hollywood_sdmmc"

#include <stdlib.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/cache.h>
#include <npll/drivers/mmc.h>
#include <npll/drivers/sdio.h>
#include <npll/drivers.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/utils.h>
#include "sdmmc/sdhc.h"

static REGISTER_DRIVER(sdmmcDrv);
static sdio_host_dev_t sdioDev;
static mmc_card_t mmcDev = NULL;
static struct blockDevice sdmmcBdev;
static bool sdmmcRegistered = false;
static bool checkConnected = false;

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

	/* if we have an unaligned first block, read it separately */
	if (headSkip) {
		dcache_invalidate(tmp, sizeof(tmp));
		ret = (size_t)mmc_block_read(mmcDev, startBlock, 1, tmp,
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
		ret = (size_t)mmc_block_read(mmcDev, startBlock, alignedBlocks, dest,
			(uintptr_t)virtToPhys(dest), NULL, NULL);
		if (ret != alignedBlocks * blkSize)
			return -1;

		dest += alignedBlocks * blkSize;
		startBlock += alignedBlocks;
	}

	/* if we have an unaligned last block, read it separately */
	if (tailBytes) {
		dcache_invalidate(tmp, sizeof(tmp));
		ret = (size_t)mmc_block_read(mmcDev, startBlock, 1, tmp,
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

static void sdmmcRegisterBlock(void) {
	long long capacity;

	if (sdmmcRegistered)
		return;

	capacity = mmc_card_capacity(mmcDev);
	if (capacity <= 0) {
		log_puts("failed to get card capacity");
		return;
	}

	memset(&sdmmcBdev, 0, sizeof(sdmmcBdev));
	sdmmcBdev.name = "sdhci0"; /* FIXME: also work for Wii U eMMC, which is on (iirc) sdhci3 */
	sdmmcBdev.size = (u64)capacity;
	sdmmcBdev.blockSize = (u32)mmc_block_size(mmcDev);
	sdmmcBdev.drvData = mmcDev;
	sdmmcBdev.read = sdmmcRead;
	sdmmcBdev.write = sdmmcWrite;

	B_Register(&sdmmcBdev);
	sdmmcRegistered = true;
}

static void sdmmcUnregisterBlock(void) {
	if (!sdmmcRegistered)
		return;

	B_Unregister(&sdmmcBdev);
	sdmmcRegistered = false;
}

static void sdmmcCB(void) {
	u32 pstate;
	int ret;
	if (!checkConnected)
		return;

	checkConnected = false;

	if (sdmmcDrv.state == DRIVER_STATE_NO_HARDWARE) {
		/* check for a card */
		pstate = sdio_get_present_state(&sdioDev);
		if (!(pstate & SDHC_PRES_STATE_CINST))
			return; /* nothing */

		/* ooh, we have a card */
		ret = mmc_init(&sdioDev, &mmcDev);
		if (ret) {
			log_printf("mmc_init failed with %d\r\n", ret);
			return;
		}

		log_puts("card init success in CB");
		sdmmcDrv.state = DRIVER_STATE_READY;
		sdmmcRegisterBlock();
	}
	else {
		/* check for card removed */
		pstate = sdio_get_present_state(&sdioDev);
		if (pstate & SDHC_PRES_STATE_CINST)
			return; /* nothing */

		log_puts("card removed!");
		sdmmcUnregisterBlock();
		sdmmcDrv.state = DRIVER_STATE_NO_HARDWARE;
		free(mmcDev);
	}
}

static void sdmmcIRQ(enum irqDev dev) {
	// log_puts("sdmmcIRQ");
	sdio_handle_irq(&sdioDev, dev);
	checkConnected = true;
}

static const int sdmmcIRQTable[2] = {
	IRQDEV_SDHCI0,
	IRQDEV_SDHCI1
};

static void sdmmcInit(void) {
	u32 pstate;
	int ret;

	D_AddCallback(sdmmcCB);
	IRQ_RegisterHandler(IRQDEV_SDHCI0, sdmmcIRQ);
	IRQ_RegisterHandler(IRQDEV_SDHCI1, sdmmcIRQ);

	/* initialize the controller */
	ret = sdhc_init(SDHC0_ADDR, sdmmcIRQTable, 2, &sdioDev);
	if (ret) {
		log_printf("sdio_init failed with %d\r\n", ret);
		sdmmcDrv.state = DRIVER_STATE_NO_HARDWARE;
		return;
	}
	log_puts("sdhc_init success");

	ret = sdio_reset(&sdioDev);
	if (ret) {
		log_printf("sdio_reset failed with %d\r\n", ret);
		sdmmcDrv.state = DRIVER_STATE_NO_HARDWARE;
		return;
	}

	/* initialize an attached MMC/SD Card */
	pstate = sdio_get_present_state(&sdioDev);
	if (!(pstate & SDHC_PRES_STATE_CINST)) {
		log_puts("no card inserted");
		sdmmcDrv.state = DRIVER_STATE_NO_HARDWARE;
		return;
	}

	ret = mmc_init(&sdioDev, &mmcDev);
	if (ret) {
		log_printf("mmc_init failed with %d\r\n", ret);
		sdmmcDrv.state = DRIVER_STATE_NO_HARDWARE;
		return;
	}

	sdmmcDrv.state = DRIVER_STATE_READY;
	sdmmcRegisterBlock();
}

static void sdmmcCleanup(void) {
	sdmmcUnregisterBlock();
	sdio_reset(&sdioDev);
}

static REGISTER_DRIVER(sdmmcDrv) = {
	.name = "Hollywood/Latte SDMMC",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_BLOCK,
	.init = sdmmcInit,
	.cleanup = sdmmcCleanup
};
