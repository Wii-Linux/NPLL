/*
 * NPLL - Drivers - SD/MMC SPI
 *
 * Copyright (C) 2026 Techflash
 *
 * Based on ChaN's SD/MMC SPI code:
 * Copyright (C) 2019, ChaN, all right reserved.
 */

#ifndef _INTERNAL_SDMMC_SDSPI_H
#define _INTERNAL_SDMMC_SDSPI_H

#include <npll/drivers/sdio.h>

int sdspiInit(uint exiChannel, uint exiCS, bool hasExtDetect, sdio_host_dev_t *dev);

#endif /* _INTERNAL_SDMMC_SDSPI_H */
