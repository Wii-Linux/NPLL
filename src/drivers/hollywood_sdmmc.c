/*
 * NPLL - Drivers - Hollywood SDMMC
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include "npll/utils.h"
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
#include "sdmmc/sdhc.h"

static REGISTER_DRIVER(sdmmcDrv);
static sdio_host_dev_t sdioDev;
static mmc_card_t mmcDev = NULL;
static struct blockDevice sdmmcBdev;
static bool sdmmcRegistered = false;

static ssize_t sdmmcRead(struct blockDevice *bdev, void *dest, size_t len, u64 off) {
	unsigned long startBlock;
	int nblocks;
	long ret;
	u32 blkSize = bdev->blockSize;

	/* must be block-aligned */
	if (off % blkSize || len % blkSize)
		return -1;

	/* dma addr must (?) be 32B aligned */
	if ((u32)dest & 0x1f)
		return -2;

	startBlock = (unsigned long)(off / blkSize);
	nblocks = (int)(len / blkSize);

	dcache_flush(dest, len);
	ret = mmc_block_read(mmcDev, startBlock, nblocks, dest,
			     (uintptr_t)virtToPhys(dest), NULL, NULL);
	dcache_invalidate(dest, len);

	return ret;
}

static ssize_t sdmmcWrite(struct blockDevice *bdev, const void *src, size_t len, u64 off) {
	unsigned long startBlock;
	int nblocks;
	long ret;
	u32 blkSize = bdev->blockSize;

	/* must be block-aligned */
	if (off % blkSize || len % blkSize)
		return -1;

	/* dma addr must (?) be 32B aligned */
	if ((u32)src & 0x1f)
		return -2;

	startBlock = (unsigned long)(off / blkSize);
	nblocks = (int)(len / blkSize);

	dcache_flush((void *)src, len);
	ret = mmc_block_write(mmcDev, startBlock, nblocks, src,
			      (uintptr_t)virtToPhys(src), NULL, NULL);

	return ret;
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

static void sdmmcInit(void) {
	u32 pstate;
	int ret;

	D_AddCallback(sdmmcCB);

	/* initialize the controller */
	ret = sdhc_init(SDHC0_ADDR, NULL, 0, &sdioDev);
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
