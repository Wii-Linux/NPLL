/*
 * NPLL - Drivers - SDHC
 *
 * Derived from seL4 projects_libs libsdhcdrivers:
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _INTERNAL_SDMMC_SDHC_H
#define _INTERNAL_SDMMC_SDHC_H
#include <npll/drivers/sdio.h>

struct sdhc {
    /* Device data */
    volatile void *base;
    int version;
    int nirqs;
    const int *irq_table;
    /* Transaction queue */
    struct mmc_cmd *cmd_list_head;
    struct mmc_cmd **cmd_list_tail;
    unsigned int blocks_remaining;
};
typedef struct sdhc *sdhc_dev_t;

int sdhc_init(void *iobase, const int *irq_table, int nirqs, sdio_host_dev_t *dev);

#define SDHC_INIT_TIMEOUT_US 1000 * 1000

#endif /* _INTERNAL_SDMMC_SDHC_H */
