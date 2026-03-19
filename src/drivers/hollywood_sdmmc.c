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
	}
	else {
		/* check for card removed */
		pstate = sdio_get_present_state(&sdioDev);
		if (pstate & SDHC_PRES_STATE_CINST)
			return; /* nothing */

		log_puts("card removed!");
		sdmmcDrv.state = DRIVER_STATE_NO_HARDWARE;
		/* FIXME: notify the MMC driver somehow? */
		free(mmcDev);
	}
}

static void sdmmcInit(void) {
	u32 pstate;
	u8 *tmp;
	int ret;

	//IRQ_RegisterHandler(IRQDEV_SDHCI0, sdmmcIRQ);
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

	tmp = M_PoolAlloc(POOL_MEM2, 512, 32 /* FIXME: is this actually right?  need to double check SD Spec Part A2 */);
	memset(tmp, 0, 512);
	dcache_flush(tmp, 512);
	ret = mmc_block_read(mmcDev, 0, 1, tmp, (uintptr_t)virtToPhys(tmp), NULL, NULL);
	dcache_invalidate(tmp, 512);
	log_printf("mmc_block_read ret: %d, last bytes: %2x %2x\r\n", ret, tmp[510], tmp[511]);
	free(tmp);

}

static void sdmmcCleanup(void) {
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
