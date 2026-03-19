/*
 * NPLL - Drivers - MMC
 *
 * Derived from seL4 projects_libs libsdhcdrivers:
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _DRIVERS_MMC_H
#define _DRIVERS_MMC_H

#include <npll/types.h>
#include "sdio.h"

typedef struct mmc_card *mmc_card_t;


typedef void (*mmc_cb)(mmc_card_t mmc_card, int status, size_t bytes_transferred, void *token);


static inline size_t mmc_block_size(mmc_card_t mmc_card)
{
    return 512;
}

/** Initialise an MMC card
 * @param[in]  sdio_dev      An sdio device structure to bind the MMC driver to
 *                           probe
 * @param[out] mmc_card      On success, this will be filled with
 *                           a handle to the MMC card
 *                           associated with the provided id.
 * @return                   0 on success.
 */
int mmc_init(sdio_host_dev_t *sdio, mmc_card_t *mmc_card);

/** Read blocks from the MMC
 * The client may use either physical or virtual address for the transfer depending
 * on the DMA requirements of the underlying driver. It is recommended to provide
 * both for rebustness.
 * @param[in] mmc_card  A handle to an initialised MMC card
 * @param[in] start     the starting block number of the operation
 * @param[in] nblocks   The number of blocks to read
 * @param[in] vbuf      The virtual address of a buffer to read the data into
 * @param[in] pbuf      The physical address of a buffer to read the data into
 * @param[in] cb        A callback function to call when the transaction completes.
 *                      If NULL is passed as this argument, the call will be blocking.
 * @param[in] token     A token to pass, unmodified, to the provided callback function.

 * @return              The number of bytes read, negative on failure.
 */
long mmc_block_read(mmc_card_t mmc_card, unsigned long start_block, int nblocks,
                    void *vbuf, uintptr_t pbuf, mmc_cb cb, void *token);

/** Write blocks to the MMC
 * The client may use either physical or virtual address for the transfer depending
 * on the DMA requirements of the underlying driver. It is recommended to provide
 * both for rebustness.
 * @param[in] mmc_card  A handle to an initialised MMC card
 * @param[in] start     The starting block number of the operation
 * @param[in] nblocks   The number of blocks to write
 * @param[in] vbuf      The virtual address of a buffer that contains the data to be written
 * @param[in] pbuf      The physical address of a buffer that contains the data to be written
 * @param[in] cb        A callback function to call when the transaction completes.
 *                      If NULL is passed as this argument, the call will be blocking.
 * @param[in] token     A token to pass, unmodified, to the provided callback function.
 * @return              The number of bytes read, negative on failure.
 */
long mmc_block_write(mmc_card_t mmc_card, unsigned long start_block, int nblocks,
                     const void *vbuf, uintptr_t pbuf, mmc_cb cb, void *token);

/**
 * Returns the nth IRQ that this underlying device generates
 * @param[in] mmc  A handle to an initialised MMC card
 * @param[in] n    Index of the desired IRQ.
 * @return         The IRQ number, or -1 if n is invalid
 */
int mmc_nth_irq(mmc_card_t mmc, int n);

/**
 * Passes control to the IRQ handler of the MMC host controller
 * @param[in] mmc  A handle to an initialised MMC card
 * @param[in] irq  The IRQ number that was triggered.
 * @return         0 if an IRQ was handled
 */
int mmc_handle_irq(mmc_card_t mmc, int irq);

/** Get card capacity
 * @param[in] mmc_card  A handle to an initialised MMC card
 * @return              Card capacity in bytes, negative on failure.
 */
long long mmc_card_capacity(mmc_card_t mmc_card);

#endif /* _DRIVERS_MMC_H */
