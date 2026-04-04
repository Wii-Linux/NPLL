/*
 * NPLL - Drivers - MMC
 * Copyright (C) 2025-2026 Techflash
 *
 * Derived from seL4 projects_libs libsdhcdrivers:
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "mmc.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <npll/allocator.h>
#include <npll/timer.h>
#include "compat.h"

#define CSD_VERSION_1       0
#define CSD_VERSION_2_AND_3 1

struct mmc_completion_token {
	struct mmc_card *card;
	mmc_cb cb;
	void *token;
};


static u32 slice_bits(u32 *val, int start, int size)
{
	int idx;
	int high, low;
	u32 ret = 0;

	/* Can not return more than 32 bits. */
	assert(size <= 32);

	idx = start / 32;
	low = start % 32;
	high = (start + size) % 32;

	if (high == 0 && low == 0) {
		ret = val[idx];
	} else if (high == 0 && low != 0) {
		ret = val[idx] >> low;
	} else {
		if (high > low) {
			ret = val[idx] & ((1U << high) - 1);
			ret = ret >> low;
		} else {
			ret = val[idx] >> low;
			ret |= (val[idx + 1] & ((1U << high) - 1)) << (32 - low);
		}

	}

	return ret;
}

#if 0 /* Commenting this out as it appears unused. */
static int mmc_decode_cid(mmc_card_t mmc_card, struct cid *cid)
{
	if (mmc_card == NULL || cid == NULL) {
		return -1;
	}

	if (mmc_card->type == CARD_TYPE_SD) {
		cid->manfid         = slice_bits(mmc_card->raw_cid, 120,  8);
		cid->sd_cid.oemid   = slice_bits(mmc_card->raw_cid, 104, 16);
		cid->sd_cid.name[0] = slice_bits(mmc_card->raw_cid,  96,  8);
		cid->sd_cid.name[1] = slice_bits(mmc_card->raw_cid,  88,  8);
		cid->sd_cid.name[2] = slice_bits(mmc_card->raw_cid,  80,  8);
		cid->sd_cid.name[3] = slice_bits(mmc_card->raw_cid,  72,  8);
		cid->sd_cid.name[4] = slice_bits(mmc_card->raw_cid,  64,  8);
		cid->sd_cid.rev     = slice_bits(mmc_card->raw_cid,  56,  8);
		cid->sd_cid.serial  = slice_bits(mmc_card->raw_cid,  24, 32);
		cid->sd_cid.date    = slice_bits(mmc_card->raw_cid,   8, 12);

		ZF_LOGD("manfid(%x), oemid(%x), name(%c%c%c%c%c), rev(%x), serial(%x), date(%x)",
				cid->manfid, cid->sd_cid.oemid,
				cid->sd_cid.name[0], cid->sd_cid.name[1], cid->sd_cid.name[2],
				cid->sd_cid.name[3], cid->sd_cid.name[4],
				cid->sd_cid.rev, cid->sd_cid.serial, cid->sd_cid.date);
	} else {
		ZF_LOGD("Not Implemented!");
		return -1;
	}

	return 0;
}
#endif

static int mmc_decode_csd(mmc_card_t mmc_card, struct csd *csd)
{
	if (mmc_card == NULL || csd == NULL) {
		return -1;
	}

#define CSD_BITS(start, size) \
	slice_bits(mmc_card->raw_csd, start, size)

	csd->structure = (u8)CSD_BITS(126, 2);

	if (csd->structure == CSD_VERSION_1) {
		ZF_LOGD("CSD Version 1.0");
		csd->c_size      = CSD_BITS(62, 12);
		csd->c_size_mult = (u8)CSD_BITS(47,  3);
		csd->read_bl_len = (u8)CSD_BITS(80,  4);
		csd->tran_speed  = (u8)CSD_BITS(96,  8);
	} else if (csd->structure == CSD_VERSION_2_AND_3) {
		ZF_LOGD("CSD Version 2.0");
		csd->c_size      = CSD_BITS(48, 22);
		csd->c_size_mult = 0;
		csd->read_bl_len = (u8)CSD_BITS(80,  4);
		csd->tran_speed  = (u8)CSD_BITS(96,  8);
	} else {
		ZF_LOGE("Unknown CSD version!");
		return -1;
	}

	return 0;
}

static struct mmc_cmd *mmc_cmd_new(u32 index, u32 arg, enum mmc_rsp_type rsp_type)
{
	struct mmc_cmd *cmd;
	cmd = malloc(sizeof(*cmd));
	if (cmd) {
		/* Command */
		cmd->index = index;
		cmd->arg = arg;
		cmd->rsp_type = rsp_type;
		cmd->data = NULL;
		/* Transaction maintenance */
		cmd->cb = NULL;
		cmd->token = NULL;
		cmd->next = NULL;
		cmd->complete = 0;
	}
	return cmd;
}

static int mmc_cmd_add_data(struct mmc_cmd *cmd, void *vbuf, uintptr_t pbuf, u32 addr,
			    u32 block_size, u32 blocks)
{
	struct mmc_data *d;
	assert(cmd->data == NULL);
	d = (struct mmc_data *)malloc(sizeof(*d));
	if (d) {
		d->pbuf = pbuf;
		d->vbuf = vbuf;
		d->data_addr = addr;
		d->block_size = block_size;
		d->blocks = blocks;
		cmd->data = d;
		return 0;
	} else {
		return -1;
	}
}

static void mmc_cmd_destroy(struct mmc_cmd *cmd)
{
	if (cmd->data) {
		free(cmd->data);
	}
	free(cmd);
}

static struct mmc_completion_token *mmc_new_completion_token(mmc_card_t mmc_card, mmc_cb cb, void *token)
{
	struct mmc_completion_token *t;
	t = (struct mmc_completion_token *)malloc(sizeof(*t));
	if (t) {
		t->card = mmc_card;
		t->cb = cb;
		t->token = token;
	}
	return t;
}

static void mmc_completion_token_destroy(struct mmc_completion_token *t)
{
	free(t);
}

/**
 * MMC/SD/SDIO card registry.
 */
static int mmc_card_registry(mmc_card_t card)
{
	struct mmc_cmd cmd = {.data = NULL};
	int ret;

	/* Get card ID */
	cmd.index = MMC_ALL_SEND_CID;
	cmd.arg = 0;
	cmd.rsp_type = MMC_RSP_TYPE_R2;
	ret = host_send_command(card, &cmd, NULL, NULL);
	if (ret) {
		ZF_LOGE("No response!");
		card->status = CARD_STS_INACTIVE;
		return -1;
	} else {
		card->status = CARD_STS_ACTIVE;
	}

	/* Left shift the response by 8. Consult SDHC manual. */
	cmd.response[3] = ((cmd.response[3] << 8) | (cmd.response[2] >> 24));
	cmd.response[2] = ((cmd.response[2] << 8) | (cmd.response[1] >> 24));
	cmd.response[1] = ((cmd.response[1] << 8) | (cmd.response[0] >> 24));
	cmd.response[0] = (cmd.response[0] << 8);
	memcpy(card->raw_cid, cmd.response, sizeof(card->raw_cid));


	/* Retrieve RCA number. */
	cmd.index = MMC_SEND_RELATIVE_ADDR;
	cmd.arg = 0;
	cmd.rsp_type = MMC_RSP_TYPE_R6;
	host_send_command(card, &cmd, NULL, NULL);
	card->raw_rca = (u16)(cmd.response[0] >> 16);
	ZF_LOGD("New Card RCA: %x", card->raw_rca);

	/* Read CSD, Status */
	cmd.index = MMC_SEND_CSD;
	cmd.arg = (u32)card->raw_rca << 16;
	cmd.rsp_type = MMC_RSP_TYPE_R2;
	host_send_command(card, &cmd, NULL, NULL);

	/* Left shift the response by 8. Consult SDHC manual. */
	cmd.response[3] = ((cmd.response[3] << 8) | (cmd.response[2] >> 24));
	cmd.response[2] = ((cmd.response[2] << 8) | (cmd.response[1] >> 24));
	cmd.response[1] = ((cmd.response[1] << 8) | (cmd.response[0] >> 24));
	cmd.response[0] = (cmd.response[0] << 8);
	memcpy(card->raw_csd, cmd.response, sizeof(card->raw_csd));

	cmd.index = MMC_SEND_STATUS;
	cmd.rsp_type = MMC_RSP_TYPE_R1;
	host_send_command(card, &cmd, NULL, NULL);

	/* Select the card */
	cmd.index = MMC_SELECT_CARD;
	cmd.arg = (u32)card->raw_rca << 16;
	cmd.rsp_type = MMC_RSP_TYPE_R1b;
	host_send_command(card, &cmd, NULL, NULL);

	/**
	 * The default bus width of the card after power up or GO_IDLE (CMD0) is
	 * 1 bit. As the HostController is initialzed to 4-bit bus width,
	 * the card also needs to switch to 4-bit mode.
	 */
	cmd.index = MMC_APP_CMD;
	cmd.arg = (u32)card->raw_rca << 16;
	cmd.rsp_type = MMC_RSP_TYPE_R1;
	host_send_command(card, &cmd, NULL, NULL);
	cmd.index = SD_SET_BUS_WIDTH;
	cmd.arg = MMC_MODE_4BIT;
	host_send_command(card, &cmd, NULL, NULL);

	/* Set read/write block length for byte addressed standard capacity cards */
	if (!card->high_capacity) {
		cmd.index = MMC_SET_BLOCKLEN;
		cmd.arg = mmc_block_size(card);
		cmd.rsp_type = MMC_RSP_TYPE_R1;
		ret = host_send_command(card, &cmd, NULL, NULL);
	}

	return 0;
}


/**
 * Card voltage validation.
 */
static int mmc_voltage_validation(mmc_card_t card)
{
	struct mmc_cmd cmd = {.data = NULL};
	int ret, retries = 5;
	unsigned int voltage;

	/* Send CMD55 to issue an application specific command. */
	cmd.index = MMC_APP_CMD;
	cmd.arg = 0;
	cmd.rsp_type = MMC_RSP_TYPE_R1;
	ret = host_send_command(card, &cmd, NULL, NULL);
	if (!ret) {
		/* It is a SD card. */
		cmd.index = SD_SD_APP_OP_COND;
		cmd.arg = 0;
		cmd.rsp_type = MMC_RSP_TYPE_R3;
		card->type = CARD_TYPE_SD;
	} else {
		/* It is a MMC card. */
		cmd.index = MMC_SEND_OP_COND;
		cmd.arg = 0;
		cmd.rsp_type = MMC_RSP_TYPE_R3;
		card->type = CARD_TYPE_MMC;
	}
	ret = 1;
	while (ret && retries--)
		ret = host_send_command(card, &cmd, NULL, NULL);

	if (ret) {
		ZF_LOGE("Unknown card");
		card->type = CARD_TYPE_UNKNOWN;
		return 1;
	}
	card->ocr = cmd.response[0];

	/* TODO: Check uSDHC compatibility */
	voltage = MMC_VDD_29_30 | MMC_VDD_30_31;
	if (host_is_voltage_compatible(card, 3300) && (card->ocr & voltage)) {
		/* Voltage compatible */
		voltage |= BIT(30);
		voltage |= BIT(25);
		voltage |= BIT(24);
	}

	retries = 5;

	/* Wait until the voltage level is set. */
	do {
		if (card->type == CARD_TYPE_SD) {
			cmd.index = MMC_APP_CMD;
			cmd.arg = 0;
			cmd.rsp_type = MMC_RSP_TYPE_R1;
			host_send_command(card, &cmd, NULL, NULL);
		}

		cmd.index = SD_SD_APP_OP_COND;
		cmd.arg = voltage;
		cmd.rsp_type = MMC_RSP_TYPE_R3;
		host_send_command(card, &cmd, NULL, NULL);
		udelay(100000);
	} while (!(cmd.response[0] & BIT(31)) && retries--);
	card->ocr = cmd.response[0];
	if (!(cmd.response[0] & BIT(31))) {
		ZF_LOGE("Timeout waiting on voltage to be set");
		return 1;
	}

	/* Check CCS bit */
	if (card->ocr & BIT(30)) {
		card->high_capacity = 1;
	} else {
		card->high_capacity = 0;
	}

	ZF_LOGD("Voltage set!");

	return 0;
}


static int mmc_reset(mmc_card_t card)
{
	/* Reset the card with CMD0 */
	struct mmc_cmd cmd = {.data = NULL};
	cmd.index = MMC_GO_IDLE_STATE;
	cmd.arg = 0;
	cmd.rsp_type = MMC_RSP_TYPE_NONE;
	host_send_command(card, &cmd, NULL, NULL);

	/* TODO: review this command. */
	cmd.index = MMC_SEND_EXT_CSD;
	cmd.arg = 0x1AA;
	cmd.rsp_type = MMC_RSP_TYPE_R1;
	host_send_command(card, &cmd, NULL, NULL);
	return 0;
}

static void mmc_blockop_completion_cb(struct sdio_host_dev *sdio, int stat, struct mmc_cmd *cmd,
				      void *token)
{
	struct mmc_completion_token *t;
	size_t bytes;
	(void)sdio;

	t = (struct mmc_completion_token *)token;
	if (stat == 0) {
		bytes = cmd->data->block_size * cmd->data->blocks;
	} else {
		bytes = 0;
	}
	/* Call the registered function */
	t->cb(t->card, stat, bytes, t->token);
	/* Free memory */
	mmc_cmd_destroy(cmd);
	mmc_completion_token_destroy(t);
}

int mmc_init(sdio_host_dev_t *sdio, mmc_card_t *mmc_card)
{
	mmc_card_t mmc;

	/* Allocate the mmc card structure */
	mmc = (mmc_card_t)malloc(sizeof(*mmc));
	assert(mmc);
	if (!mmc) {
		return -1;
	}
	mmc->sdio = sdio;
	/* Reset the host controller */
	if (host_reset(mmc)) {
		ZF_LOGE("Failed to reset host controller");
		free(mmc);
		return -1;
	}
	/* Initialise the card */
	if (mmc_reset(mmc)) {
		ZF_LOGE("Failed to reset SD/MMC card");
		free(mmc);
		return -1;
	}
	if (mmc_voltage_validation(mmc)) {
		ZF_LOGE("Failed to perform voltage validation");
		free(mmc);
		return -1;
	}
	/* Register the card */
	if (mmc_card_registry(mmc)) {
		ZF_LOGE("Failed to register card");
		free(mmc);
		return -1;
	}

	/* Switch host controller to operational settings */
	if (host_set_operational(mmc)) {
		ZF_LOGE("Failed to switch the host controller to the operational mode");
		free(mmc);
		return -1;
	}

	*mmc_card = mmc;
	assert(mmc);
	return 0;
}

static
long transfer_data(
	mmc_card_t mmc_card,
	unsigned long start,
	unsigned int nblocks,
	void *vbuf,
	uintptr_t pbuf,
	mmc_cb cb,
	void *token,
	u32 command)
{
	struct mmc_cmd *cmd;
	const size_t block_size = mmc_block_size(mmc_card);

	/* Determine command argument */
	const u32 arg = (mmc_card->high_capacity)
						 ? (u32)start
						 : (u32)(start * block_size);

	/* Allocate command structure
	 *
	 * Please note that `cmd` is dynamically allocated, so it must be destroyed.
	 *
	 * Clean up will be done prior to exiting this function OR in the
	 * `mmc_blockop_completion_cb` if callback was given.
	 *
	 * In case of an unexpected error, there will be a jump to
	 * `exit_transfer_data` label, so that memory leak can be avoided.
	 */
	cmd = mmc_cmd_new(command, arg, MMC_RSP_TYPE_R1);
	if (cmd == NULL) {
		// `cmd` was NOT allocated, so we are exiting without destroying it.
		return -1;
	}

	long ret = -1;
	struct mmc_completion_token *mmc_token = NULL;

	/* Add a data segment */
	ret = mmc_cmd_add_data(cmd, vbuf, pbuf, (u32)start, block_size, nblocks);
	if (ret < 0) {
		goto exit_transfer_data;
	}

	if (cb) {
		mmc_token = mmc_new_completion_token(mmc_card, cb, token);

		if (NULL == mmc_token) {
			ret = -1;
			goto exit_transfer_data;
		}
	}

	ret = host_send_command(
			  mmc_card,
			  cmd,
			  cb ? &mmc_blockop_completion_cb : NULL,
			  cb ? mmc_token : NULL);

exit_transfer_data:
	;
	const bool is_success = (0 == ret);

	// Clean up usually will happen during the callback, so we only clean up
	// here if no callback was given or failure has been encountered.
	if (!cb || !is_success) {
		if (mmc_token) {
			mmc_completion_token_destroy(mmc_token);
		}
		if (cmd) {
			mmc_cmd_destroy(cmd);
		}
	}

	const size_t bytes_transferred = cb ? 0 : (block_size * nblocks);
	return is_success ? (long)bytes_transferred : ret;
}

static long _mmc_block_read(mmc_card_t mmc_card, unsigned long start,
				uint nblocks, void *vbuf, uintptr_t pbuf, mmc_cb cb, void *token)
{
	u32 command = (nblocks > 1)
			? MMC_READ_MULTIPLE_BLOCK
			: MMC_READ_SINGLE_BLOCK;
	return transfer_data(
			   mmc_card,
			   start,
			   nblocks,
			   vbuf,
			   pbuf,
			   cb,
			   token,
			   command);
}

long mmc_block_read(mmc_card_t mmc_card, unsigned long start,
			uint nblocks, void *vbuf, uintptr_t pbuf, mmc_cb cb, void *token) {
	long ret = -1;
	int tries = 3;

	while (tries--) {
		ret = _mmc_block_read(mmc_card, start, nblocks, vbuf, pbuf, cb, token);
		if ((unsigned long)ret == nblocks * 512)
			break;
	}

	return ret;
}

static long _mmc_block_write(mmc_card_t mmc_card, unsigned long start, uint nblocks,
				const void *vbuf, uintptr_t pbuf, mmc_cb cb, void *token)
{
	// vbuf's `const` gets dropped during the cast as the underlying layer
	// accepts only non-const buffer, however it is ok, as we are sending the
	// write command, what quarantees that the buffer won't be overwritten.
	u32 command = (nblocks > 1)
			? MMC_WRITE_MULTIPLE_BLOCK
			: MMC_WRITE_BLOCK;
	return transfer_data(
			mmc_card,
			start,
			nblocks,
			(void *)vbuf,
			pbuf,
			cb,
			token,
			command);
}

long mmc_block_write(mmc_card_t mmc_card, unsigned long start, uint nblocks,
			const void *vbuf, uintptr_t pbuf, mmc_cb cb, void *token) {
	long ret = -1;
	int tries = 3;

	while (tries--) {
		ret = _mmc_block_write(mmc_card, start, nblocks, vbuf, pbuf, cb, token);
		if ((unsigned long)ret == nblocks * 512)
			break;
	}

	return ret;
}

long long mmc_card_capacity(mmc_card_t mmc_card)
{
	int ret;
	struct csd csd;

	ret = mmc_decode_csd(mmc_card, &csd);
	if (ret) {
		return -1;
	}

	long long c_size = (long long)csd.c_size;
	switch (csd.structure) {
	case CSD_VERSION_1: {
		return (c_size + 1) * (1U << (csd.c_size_mult + 2))
			   * (1U << csd.read_bl_len);
	}
	case CSD_VERSION_2_AND_3: {
		return (c_size + 1) * 512 * 1024;
	}
	default: {
		return -1;
	}
	}
}


int mmc_nth_irq(mmc_card_t mmc, int n)
{
	return host_nth_irq(mmc, n);
}

int mmc_handle_irq(mmc_card_t mmc, int irq)
{
	return host_handle_irq(mmc, irq);
}
