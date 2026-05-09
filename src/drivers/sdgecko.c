/*
 * NPLL - Drivers - SDGecko
 *
 * Copyright (C) 2026 Techflash
 */

#include "npll/irq.h"
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
#include <npll/timer.h>
#include <npll/types.h>
#include "sdmmc/sdspi.h"

static REGISTER_DRIVER(sdgeckoDrv);

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
static bool sdgeckoEnabled[NUM_SDGECKO_SLOTS];
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

static bool sdgeckoSlotPresent(uint i) {
	if (!sdgeckoSlots[i].hotplug)
		return true;

	return H_EXIExtPresent(sdgeckoSlots[i].channel);
}

/* FIXME: USB Gecko driver should probably just expose this info tbh */
static bool sdgeckoSlotHasUSBGecko(uint i) {
	bool irqs;
	u16 rx, tx = 0x9000;

	if (!sdgeckoSlots[i].hotplug)
		return false;

	irqs = IRQ_DisableSave();
	H_EXISelect(sdgeckoSlots[i].channel, sdgeckoSlots[i].cs, 32);
	(void)H_EXIRdWrImm(sdgeckoSlots[i].channel, 2, &tx, &rx);
	H_EXIDeselect(sdgeckoSlots[i].channel);
	IRQ_Restore(irqs);

	return rx == 0x0470;
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
	sdgeckoProbeFailed[i] = false;
	sdgeckoUpdateDriverState();
}

static void sdgeckoProbe(uint i) {
	int ret;

	if (!sdgeckoEnabled[i] || sdgeckoRegistered[i] || sdgeckoProbeFailed[i])
		return;
	if (!sdgeckoSlotPresent(i))
		return;
	if (sdgeckoSlots[i].hotplug) {
		udelay(250000);
		if (!sdgeckoSlotPresent(i))
			return;
	}
	if (sdgeckoSlotHasUSBGecko(i)) {
		log_printf("skipping %s: USB Gecko detected\r\n", sdgeckoSlots[i].name);
		sdgeckoProbeFailed[i] = true;
		return;
	}

	ret = mmc_init(&sdgeckoSDIO[i], &sdgeckoMMC[i]);
	if (ret) {
		log_printf("mmc_init failed for %s with %d\r\n",
			   sdgeckoSlots[i].name, ret);
		sdgeckoProbeFailed[i] = true;
		return;
	}

	log_printf("initialized %s\r\n", sdgeckoSlots[i].name);
	if (sdgeckoRegisterBlock(i)) {
		free(sdgeckoMMC[i]);
		sdgeckoMMC[i] = NULL;
		sdgeckoProbeFailed[i] = true;
	}
}

static void sdgeckoHotplug(void *dummy) {
	uint i;
	(void)dummy;

	for (i = 0; i < NUM_SDGECKO_SLOTS; i++) {
		if (!sdgeckoEnabled[i] || !sdgeckoSlots[i].hotplug)
			continue;

		if (!sdgeckoSlotPresent(i)) {
			H_EXIClearExt(sdgeckoSlots[i].channel);
			if (sdgeckoRegistered[i] || sdgeckoProbeFailed[i]) {
				log_printf("removed %s\r\n", sdgeckoSlots[i].name);
				sdgeckoRemove(i);
			}
			continue;
		}

		H_EXIClearExt(sdgeckoSlots[i].channel);
		sdgeckoProbe(i);
	}
}

static void sdgeckoInit(void) {
	uint i;

	memset(sdgeckoSDIO, 0, sizeof(sdgeckoSDIO));
	memset(sdgeckoMMC, 0, sizeof(sdgeckoMMC));
	memset(sdgeckoBdev, 0, sizeof(sdgeckoBdev));
	memset(sdgeckoEnabled, 0, sizeof(sdgeckoEnabled));
	memset(sdgeckoRegistered, 0, sizeof(sdgeckoRegistered));
	memset(sdgeckoProbeFailed, 0, sizeof(sdgeckoProbeFailed));

	sdgeckoDrv.state = DRIVER_STATE_INITIALIZING;

	for (i = 0; i < NUM_SDGECKO_SLOTS; i++) {
		if (sdgeckoSlots[i].gamecube_only && H_ConsoleType != CONSOLE_TYPE_GAMECUBE)
			continue;

		if (sdspiInit(sdgeckoSlots[i].channel, sdgeckoSlots[i].cs, sdgeckoSlots[i].hotplug, &sdgeckoSDIO[i])) {
			log_printf("failed to init SPI host for %s\r\n",
				   sdgeckoSlots[i].name);
			continue;
		}

		sdgeckoEnabled[i] = true;
		sdgeckoProbe(i);
	}

	sdgeckoUpdateDriverState();
	T_QueueRepeatingEvent(500 * 1000, sdgeckoHotplug, NULL);
}

static void sdgeckoCleanup(void) {
	uint i;

	for (i = 0; i < NUM_SDGECKO_SLOTS; i++)
		sdgeckoRemove(i);

	sdgeckoDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(sdgeckoDrv) = {
	.name = "SDGecko",
	.mask = DRIVER_ALLOW_ALL,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_BLOCK,
	.init = sdgeckoInit,
	.cleanup = sdgeckoCleanup
};
