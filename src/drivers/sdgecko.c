/*
 * NPLL - Drivers - SDGecko
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "sdgecko"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <npll/block.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/drivers/exi.h>
#include <npll/drivers/mmc.h>
#include <npll/drivers/sdio.h>
#include <npll/log.h>
#include <npll/types.h>
#include "sdmmc/sdspi.h"

static REGISTER_DRIVER(sdgeckoDrv);
struct exi_device_driver sdgeckoEXIDriver;

struct sdgecko_slot {
	uint channel;
	uint cs;
	bool gamecube_only;
	bool hotplug;
	const char *name;
};

static const struct sdgecko_slot sdgeckoSlots[] = {
	{ 0, 0, false, true,  "sdgecko-a"  },
	{ 0, 2, true,  false, "sdgecko-sp1" },
	{ 1, 0, false, true,  "sdgecko-b"  },
	{ 2, 0, true,  false, "sdgecko-sp2" },
};

#define NUM_SDGECKO_SLOTS (sizeof(sdgeckoSlots) / sizeof(sdgeckoSlots[0]))

static sdio_host_dev_t sdgeckoSDIO[NUM_SDGECKO_SLOTS];
static mmc_card_t sdgeckoMMC[NUM_SDGECKO_SLOTS];
static struct blockDevice sdgeckoBdev[NUM_SDGECKO_SLOTS];
static bool sdgeckoRegistered[NUM_SDGECKO_SLOTS];
static bool sdgeckoProbeFailed[NUM_SDGECKO_SLOTS];

static const struct blockTransfer sdgeckoTransfers[] = {
	{
		.size = 512,
		.mode = BLOCK_TRANSFER_MULTIPLE,
		.dmaAlign = 0
	}
};

static uint bdevToIdx(struct blockDevice *bdev) {
	uint i;

	for (i = 0; i < NUM_SDGECKO_SLOTS; i++) {
		if (bdev == &sdgeckoBdev[i])
			return i;
	}

	assert_unreachable();
}

static bool sdgeckoAnyRegistered(void) {
	uint i;

	for (i = 0; i < NUM_SDGECKO_SLOTS; i++) {
		if (sdgeckoRegistered[i])
			return true;
	}

	return false;
}

static void sdgeckoUpdateDriverState(void) {
	sdgeckoDrv.state = sdgeckoAnyRegistered() ? DRIVER_STATE_READY : DRIVER_STATE_NO_HARDWARE;
}

static ssize_t sdgeckoRead(struct blockDevice *bdev, void *dest, size_t len, u64 off) {
	uint i = bdevToIdx(bdev);
	uint blkSize;
	size_t startBlock, nblocks;
	long ret;

	if (!sdgeckoMMC[i])
		return -1;

	blkSize = bdev->blockSize;
	startBlock = (size_t)(off / blkSize);
	nblocks = len / blkSize;

	ret = mmc_block_read(sdgeckoMMC[i], startBlock, (uint)nblocks, dest, 0, NULL, NULL);
	if (ret != (long)len)
		return -1;

	return (ssize_t)len;
}

static ssize_t sdgeckoWrite(struct blockDevice *bdev, const void *src, size_t len, u64 off) {
	uint i = bdevToIdx(bdev);
	uint blkSize;
	size_t startBlock, nblocks;
	long ret;

	if (!sdgeckoMMC[i])
		return -1;

	blkSize = bdev->blockSize;
	startBlock = (size_t)(off / blkSize);
	nblocks = len / blkSize;

	ret = mmc_block_write(sdgeckoMMC[i], startBlock, (uint)nblocks, src, 0, NULL, NULL);
	if (ret != (long)len)
		return -1;

	return (ssize_t)len;
}

static int sdgeckoRegisterBlock(uint i) {
	struct blockDevice *bdev = &sdgeckoBdev[i];
	long long capacity;

	capacity = mmc_card_capacity(sdgeckoMMC[i]);
	if (capacity <= 0) {
		log_printf("failed to get capacity for %s\r\n", sdgeckoSlots[i].name);
		return -1;
	}

	memset(bdev, 0, sizeof(*bdev));
	bdev->name = (char *)sdgeckoSlots[i].name;
	bdev->size = (u64)capacity;
	bdev->blockSize = (u32)mmc_block_size(sdgeckoMMC[i]);
	bdev->drvData = sdgeckoMMC[i];
	bdev->transfers = sdgeckoTransfers;
	bdev->numTransfers = sizeof(sdgeckoTransfers) / sizeof(sdgeckoTransfers[0]);
	bdev->blockAlignMode = BLOCK_ALIGN_BOUNCE;
	bdev->dmaAlignMode = BLOCK_ALIGN_BOUNCE;
	bdev->read = sdgeckoRead;
	bdev->write = sdgeckoWrite;
	bdev->probePartitions = true;
	bdev->flags = BLOCK_FLAG_STANDARD;

	B_Register(bdev);
	sdgeckoRegistered[i] = true;
	sdgeckoProbeFailed[i] = false;
	sdgeckoUpdateDriverState();
	return 0;
}

static void sdgeckoRemove(uint i) {
	if (sdgeckoRegistered[i]) {
		B_Unregister(&sdgeckoBdev[i]);
		sdgeckoRegistered[i] = false;
	}
	if (sdgeckoMMC[i]) {
		free(sdgeckoMMC[i]);
		sdgeckoMMC[i] = NULL;
	}
	memset(&sdgeckoBdev[i], 0, sizeof(sdgeckoBdev[i]));
	memset(&sdgeckoSDIO[i], 0, sizeof(sdgeckoSDIO[i]));
	sdgeckoProbeFailed[i] = false;
	sdgeckoUpdateDriverState();
}

static int sdgeckoSlotIndex(uint channel, uint cs) {
	uint i;

	for (i = 0; i < NUM_SDGECKO_SLOTS; i++) {
		if (sdgeckoSlots[i].channel == channel && sdgeckoSlots[i].cs == cs)
			return (int)i;
	}

	return -1;
}

static int sdgeckoProbeDevice(struct exi_device *dev) {
	int idx;
	uint i;
	int ret;

	idx = sdgeckoSlotIndex(dev->channel, dev->cs);
	if (idx < 0)
		return -1;
	i = (uint)idx;

	if (sdgeckoRegistered[i])
		return 0;
	if (sdgeckoProbeFailed[i])
		return -1;
	if (sdgeckoSlots[i].gamecube_only && H_ConsoleType != CONSOLE_TYPE_GAMECUBE)
		return -1;

	if (sdspiInit(dev->channel, dev->cs, dev->hotplug, &sdgeckoSDIO[i])) {
		log_printf("failed to init SPI host for %s\r\n", sdgeckoSlots[i].name);
		sdgeckoProbeFailed[i] = true;
		return -1;
	}

	dev->drv_data = &sdgeckoSDIO[i];

	ret = mmc_init(&sdgeckoSDIO[i], &sdgeckoMMC[i]);
	if (ret) {
		log_printf("mmc_init failed for %s with %d\r\n",
			   sdgeckoSlots[i].name, ret);
		sdgeckoProbeFailed[i] = true;
		return -1;
	}

	log_printf("initialized %s\r\n", sdgeckoSlots[i].name);
	if (sdgeckoRegisterBlock(i)) {
		free(sdgeckoMMC[i]);
		sdgeckoMMC[i] = NULL;
		sdgeckoProbeFailed[i] = true;
		return -1;
	}

	return 0;
}

static void sdgeckoRemoveDevice(struct exi_device *dev) {
	int idx = sdgeckoSlotIndex(dev->channel, dev->cs);

	if (idx >= 0)
		sdgeckoRemove((uint)idx);
	dev->drv_data = NULL;
}

static void sdgeckoInit(void) {
	if (exiDrv.state != DRIVER_STATE_READY) {
		sdgeckoDrv.state = DRIVER_STATE_NEED_DEP;
		return;
	}

	memset(sdgeckoSDIO, 0, sizeof(sdgeckoSDIO));
	memset(sdgeckoMMC, 0, sizeof(sdgeckoMMC));
	memset(sdgeckoBdev, 0, sizeof(sdgeckoBdev));
	memset(sdgeckoRegistered, 0, sizeof(sdgeckoRegistered));
	memset(sdgeckoProbeFailed, 0, sizeof(sdgeckoProbeFailed));

	sdgeckoDrv.state = DRIVER_STATE_INITIALIZING;

	(void)H_EXIRegisterDriver(&sdgeckoEXIDriver);
	sdgeckoUpdateDriverState();
}

static void sdgeckoCleanup(void) {
	uint i;

	H_EXIUnregisterDriver(&sdgeckoEXIDriver);
	for (i = 0; i < NUM_SDGECKO_SLOTS; i++)
		sdgeckoRemove(i);

	sdgeckoDrv.state = DRIVER_STATE_NOT_READY;
}

struct exi_device_driver sdgeckoEXIDriver = {
	.name = "SDGecko",
	.probe = sdgeckoProbeDevice,
	.remove = sdgeckoRemoveDevice,
};

static REGISTER_DRIVER(sdgeckoDrv) = {
	.name = "SDGecko",
	.mask = DRIVER_ALLOW_ALL,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_BLOCK,
	.init = sdgeckoInit,
	.cleanup = sdgeckoCleanup
};
