/*
 * NPLL - Drivers - SD/MMC over SPI
 *
 * Copyright (C) 2026 Techflash
 *
 * Based on ChaN's SD/MMC SPI code:
 * Copyright (C) 2019, ChaN, all right reserved.
 */

#define MODULE "sdspi"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/drivers/exi.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/timer.h>
#include "compat.h"
#include "mmc.h"
#include "sdspi.h"

#define SDSPI_CLK_INIT_MHZ 1
#define SDSPI_CLK_OP_MHZ   16

#define SDSPI_TOKEN_START_BLOCK 0xfe
#define SDSPI_TOKEN_MULTI_WRITE 0xfc
#define SDSPI_TOKEN_STOP_TRAN   0xfd

#define SDSPI_TIMEOUT_READY_US 500000
#define SDSPI_TIMEOUT_DATA_US  100000
#define SDSPI_RESET_TIMEOUT_US 2000000
#define SDSPI_RESET_BACKOFF_US 100000

struct sdspiHost {
	uint channel;
	uint cs;
	uint clk_mhz;
	bool has_ext_detect;
	bool selected;
	bool app_cmd;
	bool invert_tx;
};

static void sdspiIdleClockByte(struct sdspiHost *host);
static int sdspiCommand(struct sdspiHost *host, u8 cmd, u32 arg,
			 u8 *resp, size_t resp_len);
static int sdspiAppCommand(struct sdspiHost *host, u8 acmd, u32 arg,
			     u8 *resp, size_t resp_len);

static inline struct sdspiHost *sdioGetSDSPI(sdio_host_dev_t *sdio) {
	return (struct sdspiHost *)sdio->priv;
}

static void sdspiSelect(struct sdspiHost *host) {
	H_EXISelect(host->channel, host->cs, host->clk_mhz);
	host->selected = true;
}

static void sdspiDeselect(struct sdspiHost *host) {
	H_EXIDeselect(host->channel);
	H_EXISelectSD(host->channel, host->clk_mhz);
	sdspiIdleClockByte(host);
	host->selected = false;
	H_EXIDeselect(host->channel);
}

static int sdspiXfer(struct sdspiHost *host, u8 tx, u8 *rx) {
	u8 in, out;
	in = tx;
	out = 0xff;

	if (host->invert_tx)
		in ^= 0xff;
	assert(host->selected);
	if (H_EXIRdWrImm(host->channel, 1, &in, &out))
		return -1;
	if (rx)
		*rx = out;
	return 0;
}

static void sdspiIdleClockByte(struct sdspiHost *host) {
	u8 in, out;
	in = host->invert_tx ? 0x00 : 0xff;

	(void)H_EXIRdWrImm(host->channel, 1, &in, &out);
}

static u8 sdspiRecvByte(struct sdspiHost *host) {
	u8 v = 0xff;
	(void)sdspiXfer(host, 0xff, &v);
	return v;
}

static void sdspiRecv(struct sdspiHost *host, u8 *buf, size_t len) {
	while (len--)
		*buf++ = sdspiRecvByte(host);
}

static void sdspiSend(struct sdspiHost *host, const u8 *buf, size_t len) {
	while (len--)
		(void)sdspiXfer(host, *buf++, NULL);
}

static int sdspiWriteOnly(struct sdspiHost *host, const u8 *buf, size_t len) {
	u8 tmp;

	while (len--) {
		tmp = *buf++;
		if (host->invert_tx)
			tmp ^= 0xff;
		if (H_EXIWriteImm(host->channel, 1, &tmp))
			return -1;
	}

	return 0;
}

static int sdspiWaitReady(struct sdspiHost *host, u32 timeoutUs) {
	u64 tb = mftb();

	while (sdspiRecvByte(host) != 0xff) {
		if (T_HasElapsed(tb, timeoutUs))
			return -1;
		udelay(100);
	}

	return 0;
}

static int sdspiWaitDataToken(struct sdspiHost *host) {
	u8 token;
	u64 tb = mftb();

	do {
		token = sdspiRecvByte(host);
		if (token != 0xff)
			break;
		udelay(100);
	} while (!T_HasElapsed(tb, SDSPI_TIMEOUT_DATA_US));

	return token == SDSPI_TOKEN_START_BLOCK ? 0 : -1;
}

static int sdspiCommandRaw(struct sdspiHost *host, u8 cmd, u32 arg, u8 crc, u8 *resp, size_t respLen) {
	u8 pkt[6];
	u8 r1;
	uint tries;

	pkt[0] = 0x40u | cmd;
	pkt[1] = (u8)(arg >> 24);
	pkt[2] = (u8)(arg >> 16);
	pkt[3] = (u8)(arg >> 8);
	pkt[4] = (u8)arg;
	pkt[5] = crc;

	if (cmd != MMC_STOP_TRANSMISSION) {
		sdspiDeselect(host);
		sdspiSelect(host);
		for (uint i = 0; i < 10; i++)
			(void)sdspiRecvByte(host);
		if (cmd != MMC_GO_IDLE_STATE &&
		    sdspiWaitReady(host, SDSPI_TIMEOUT_READY_US))
			return -1;
	}

	if (sdspiWriteOnly(host, pkt, sizeof(pkt)))
		return -1;
	if (cmd == MMC_STOP_TRANSMISSION)
		(void)sdspiRecvByte(host);

	for (tries = 100; tries; tries--) {
		r1 = sdspiRecvByte(host);
		if (!(r1 & 0x80))
			break;
	}
	if (!tries)
		return -1;

	if (respLen) {
		resp[0] = r1;
		if (respLen > 1)
			sdspiRecv(host, resp + 1, respLen - 1);
	}

	return 0;
}

static void sdspiIdleClocks(struct sdspiHost *host, uint bytes) {
	sdspiDeselect(host);
	H_EXISelectSD(host->channel, host->clk_mhz);
	host->selected = true;
	for (uint i = 0; i < bytes; i++)
		sdspiIdleClockByte(host);
	sdspiDeselect(host);
}

static void sdspiRecoverBus(struct sdspiHost *host)
{
	sdspiSelect(host);
	for (uint i = 0; i < 32; i++) {
		if (sdspiRecvByte(host) == 0xff)
			break;
	}
	sdspiDeselect(host);
}

static int sdspiGoIdle(struct sdspiHost *host, bool waitReady, u8 *r1) {
	uint tries;
	u8 pkt[6] = { 0x40u | MMC_GO_IDLE_STATE, 0, 0, 0, 0, 0x95 };

	sdspiIdleClocks(host, 80);
	sdspiSelect(host);
	if (waitReady && sdspiWaitReady(host, SDSPI_TIMEOUT_READY_US)) {
		sdspiDeselect(host);
		return -1;
	}

	if (sdspiWriteOnly(host, pkt, sizeof(pkt))) {
		sdspiDeselect(host);
		return -1;
	}

	for (tries = 100; tries; tries--) {
		*r1 = sdspiRecvByte(host);
		if (!(*r1 & 0x80))
			break;
	}
	sdspiDeselect(host);
	return tries ? 0 : -1;
}

static int sdspiActiveOCR(struct sdspiHost *host) {
	u8 resp[5];
	u32 ocr;

	if (sdspiCommand(host, MMC_READ_OCR, 0, resp, sizeof(resp)) || resp[0])
		return -1;

	ocr = ((u32)resp[1] << 24) | ((u32)resp[2] << 16)
	    | ((u32)resp[3] << 8) | (u32)resp[4];
	return (ocr & BIT(31)) ? 0 : -1;
}

static int sdspiCommand(struct sdspiHost *host, u8 cmd, u32 arg, u8 *resp, size_t resp_len) {
	u8 crc = 0x01;

	if (cmd == MMC_GO_IDLE_STATE)
		crc = 0x95;
	else if (cmd == MMC_SEND_EXT_CSD)
		crc = 0x87;

	return sdspiCommandRaw(host, cmd, arg, crc, resp, resp_len);
}

static int sdspiAppCommand(struct sdspiHost *host, u8 acmd, u32 arg, u8 *resp, size_t respLen) {
	u8 r1[1];

	if (sdspiCommand(host, MMC_APP_CMD, 0, r1, sizeof(r1)))
		return -1;
	if (r1[0] > 1)
		return -1;

	return sdspiCommand(host, acmd, arg, resp, respLen);
}

static int sdspi_read_data(struct sdspiHost *host, u8 *buf, size_t len) {
	u8 crc[2];

	if (sdspiWaitDataToken(host))
		return -1;

	sdspiRecv(host, buf, len);
	sdspiRecv(host, crc, sizeof(crc));
	return 0;
}

static int sdspiWriteData(struct sdspiHost *host, const u8 *buf, u8 token) {
	u8 resp;
	u8 crc[2] = { 0xff, 0xff };

	if (sdspiWaitReady(host, SDSPI_TIMEOUT_READY_US))
		return -1;

	(void)sdspiXfer(host, token, NULL);
	if (token == SDSPI_TOKEN_STOP_TRAN)
		return 0;

	sdspiSend(host, buf, 512);
	sdspiSend(host, crc, sizeof(crc));
	resp = sdspiRecvByte(host);

	return (resp & 0x1f) == 0x05 ? 0 : -1;
}

static void sdspiFillR2Response(struct mmc_cmd *cmd, const u8 raw[16]) {
	u32 shifted[4];

	shifted[0] = ((u32)raw[12] << 24) | ((u32)raw[13] << 16)
		   | ((u32)raw[14] << 8)  | (u32)raw[15];
	shifted[1] = ((u32)raw[8] << 24) | ((u32)raw[9] << 16)
		   | ((u32)raw[10] << 8) | (u32)raw[11];
	shifted[2] = ((u32)raw[4] << 24) | ((u32)raw[5] << 16)
		   | ((u32)raw[6] << 8)  | (u32)raw[7];
	shifted[3] = ((u32)raw[0] << 24) | ((u32)raw[1] << 16)
		   | ((u32)raw[2] << 8)  | (u32)raw[3];

	cmd->response[0] = (shifted[0] >> 8) | ((shifted[1] & 0xff) << 24);
	cmd->response[1] = (shifted[1] >> 8) | ((shifted[2] & 0xff) << 24);
	cmd->response[2] = (shifted[2] >> 8) | ((shifted[3] & 0xff) << 24);
	cmd->response[3] = shifted[3] >> 8;
}

static int sdspiReadRegister(struct sdspiHost *host, u8 cmdIdx, struct mmc_cmd *cmd) {
	u8 r1[1];
	u8 raw[16];

	if (sdspiCommand(host, cmdIdx, 0, r1, sizeof(r1))) {
		log_printf("CMD%u register command timed out\r\n", cmdIdx);
		return -1;
	}
	if (r1[0]) {
		log_printf("CMD%u register command returned R1=0x%02x\r\n", cmdIdx, r1[0]);
		return -1;
	}
	if (sdspi_read_data(host, raw, sizeof(raw))) {
		log_printf("CMD%u register data token timed out\r\n", cmdIdx);
		return -1;
	}

	sdspiFillR2Response(cmd, raw);
	return 0;
}

static int sdspiBlocksRead(struct sdspiHost *host, struct mmc_cmd *cmd) {
	u8 r1[1];
	u32 i;
	u8 *buf = (u8 *)cmd->data->vbuf;

	if (!buf)
		return -1;

	if (sdspiCommand(host, (u8)cmd->index, cmd->arg, r1, sizeof(r1)) || r1[0])
		return -1;

	for (i = 0; i < cmd->data->blocks; i++) {
		if (sdspi_read_data(host, buf, cmd->data->block_size))
			return -1;
		buf += cmd->data->block_size;
	}

	if (cmd->index == MMC_READ_MULTIPLE_BLOCK) {
		(void)sdspiCommand(host, MMC_STOP_TRANSMISSION, 0, r1, sizeof(r1));
		(void)sdspiWaitReady(host, SDSPI_TIMEOUT_READY_US);
	}

	return 0;
}

static int sdspiBlocksWrite(struct sdspiHost *host, struct mmc_cmd *cmd) {
	u8 r1[1];
	u32 i;
	const u8 *buf = (const u8 *)cmd->data->vbuf;

	if (!buf)
		return -1;

	if (cmd->index == MMC_WRITE_MULTIPLE_BLOCK)
		(void)sdspiAppCommand(host, SD_SET_WR_BLK_ERASE_COUNT, cmd->data->blocks, r1, sizeof(r1));

	if (sdspiCommand(host, (u8)cmd->index, cmd->arg, r1, sizeof(r1)) || r1[0])
		return -1;

	if (cmd->index == MMC_WRITE_BLOCK) {
		if (sdspiWriteData(host, buf, SDSPI_TOKEN_START_BLOCK))
			return -1;
		return sdspiWaitReady(host, SDSPI_TIMEOUT_READY_US);
	}

	for (i = 0; i < cmd->data->blocks; i++) {
		if (sdspiWriteData(host, buf, SDSPI_TOKEN_MULTI_WRITE))
			return -1;
		buf += cmd->data->block_size;
	}
	if (sdspiWriteData(host, NULL, SDSPI_TOKEN_STOP_TRAN))
		return -1;

	return sdspiWaitReady(host, SDSPI_TIMEOUT_READY_US);
}

static int sdspiReset(sdio_host_dev_t *sdio) {
	static const uint init_clocks[] = { 1, 2, 4 };
	struct sdspiHost *host = sdioGetSDSPI(sdio);
	u8 r1[1];
	uint attempt;
	u32 backoff_us;
	u64 reset_start;
	bool irqs;

	host->app_cmd = false;
	irqs = IRQ_DisableSave();
	reset_start = mftb();
	backoff_us = 10000;
	attempt = 0;

	while (!T_HasElapsed(reset_start, SDSPI_RESET_TIMEOUT_US)) {
		host->clk_mhz = init_clocks[attempt % (sizeof(init_clocks) / sizeof(init_clocks[0]))];
		host->invert_tx = attempt & 8;
		udelay(backoff_us);
		sdspiIdleClocks(host, 80);
		sdspiRecoverBus(host);

		if (sdspiGoIdle(host, attempt & 1, r1)) {
			if (attempt >= 2 && sdspiActiveOCR(host) == 0) {
				IRQ_Restore(irqs);
				return 0;
			}
			attempt++;
			if (backoff_us < SDSPI_RESET_BACKOFF_US)
				backoff_us *= 2;
			if (backoff_us > SDSPI_RESET_BACKOFF_US)
				backoff_us = SDSPI_RESET_BACKOFF_US;
			continue;
		}

		if (r1[0] == 1) {
			IRQ_Restore(irqs);
			return 0;
		}

		log_printf("reset: CMD0 returned R1=0x%02x\r\n", r1[0]);
		attempt++;
		if (backoff_us < SDSPI_RESET_BACKOFF_US)
			backoff_us *= 2;
		if (backoff_us > SDSPI_RESET_BACKOFF_US)
			backoff_us = SDSPI_RESET_BACKOFF_US;
	}

	host->invert_tx = false;
	IRQ_Restore(irqs);
	log_printf("reset: CMD0 timed out after %u attempt(s)\r\n", attempt);
	return -1;
}

static int sdspiSetOperational(sdio_host_dev_t *sdio) {
	struct sdspiHost *host = sdioGetSDSPI(sdio);

	host->clk_mhz = SDSPI_CLK_OP_MHZ;
	return 0;
}

static int sdspiSetBusWidth(sdio_host_dev_t *sdio UNUSED, u32 width) {
	return width == 1 ? 0 : -1;
}

static int sdspiIsVoltageCompatible(sdio_host_dev_t *sdio UNUSED, int mv) {
	return mv == 3300;
}

static int sdspiNthIRQ(sdio_host_dev_t *sdio UNUSED, int n UNUSED) {
	return -1;
}

static int sdspiHandleIRQ(sdio_host_dev_t *sdio UNUSED, int irq UNUSED) {
	return -1;
}

static u32 sdspiGetPresentState(sdio_host_dev_t *sdio) {
	struct sdspiHost *host = sdioGetSDSPI(sdio);

	if (!host->has_ext_detect || H_EXIExtPresent(host->channel))
		return SDHC_PRES_STATE_CINST | SDHC_PRES_STATE_WPSPL;

	return 0;
}

static int sdspiSendCommand(sdio_host_dev_t *sdio, struct mmc_cmd *cmd, sdio_cb cb, void *token) {
	struct sdspiHost *host = sdioGetSDSPI(sdio);
	u8 resp[5] = { 0 };
	int ret = 0;
	bool irqs;

	memset(cmd->response, 0, sizeof(cmd->response));
	cmd->complete = 0;
	cmd->cb = cb;
	cmd->token = token;
	cmd->next = NULL;

	irqs = IRQ_DisableSave();

	switch (cmd->index) {
	case MMC_GO_IDLE_STATE: {
		ret = sdspiCommand(host, MMC_GO_IDLE_STATE, 0, resp, 1);
		break;
	}
	case MMC_SEND_EXT_CSD: {
		ret = sdspiCommand(host, MMC_SEND_EXT_CSD, cmd->arg, resp, sizeof(resp));
		if (!ret) {
			cmd->response[0] = ((u32)resp[1] << 24) | ((u32)resp[2] << 16)
					 | ((u32)resp[3] << 8) | (u32)resp[4];
		}
		break;
	}
	case MMC_READ_OCR: {
		ret = sdspiCommand(host, MMC_READ_OCR, 0, resp, sizeof(resp));
		if (!ret && resp[0] <= 1) {
			cmd->response[0] = ((u32)resp[1] << 24) | ((u32)resp[2] << 16)
					 | ((u32)resp[3] << 8) | (u32)resp[4];
		}
		break;
	}
	case MMC_APP_CMD: {
		ret = sdspiCommand(host, MMC_APP_CMD, 0, resp, 1);
		host->app_cmd = !ret && resp[0] <= 1;
		cmd->response[0] = resp[0];
		if (!ret && resp[0] > 1)
			ret = -1;
		break;
	}
	case SD_SD_APP_OP_COND: {
		if (!host->app_cmd) {
			ret = -1;
			break;
		}
		ret = sdspiCommand(host, SD_SD_APP_OP_COND, cmd->arg, resp, 1);
		host->app_cmd = false;
		if (!ret && resp[0] == 0) {
			ret = sdspiCommand(host, MMC_READ_OCR, 0, resp, sizeof(resp));
			if (!ret) {
				cmd->response[0] = ((u32)resp[1] << 24) | ((u32)resp[2] << 16)
						 | ((u32)resp[3] << 8) | (u32)resp[4];
			}
		}
		else if (!ret && resp[0] == 1)
			cmd->response[0] = 0;
		else if (!ret) {
			log_printf("ACMD41 returned R1=0x%02x\r\n", resp[0]);
			ret = -1;
		}
		break;
	}
	case MMC_SEND_OP_COND: {
		ret = -1;
		break;
	}
	case MMC_ALL_SEND_CID: {
		ret = sdspiReadRegister(host, MMC_SEND_CID, cmd);
		break;
	}
	case MMC_SEND_RELATIVE_ADDR: {
		cmd->response[0] = 1u << 16;
		ret = 0;
		break;
	}
	case MMC_SEND_CSD: {
		ret = sdspiReadRegister(host, MMC_SEND_CSD, cmd);
		break;
	}
	case MMC_SEND_STATUS: {
		ret = sdspiCommand(host, MMC_SEND_STATUS, 0, resp, 2);
		cmd->response[0] = ret ? 0 : (((u32)resp[0] << 8) | (u32)resp[1]);
		break;
	}
	case MMC_SELECT_CARD: {
		ret = 0;
		break;
	}
	case MMC_SET_BLOCKLEN: {
		ret = sdspiCommand(host, MMC_SET_BLOCKLEN, cmd->arg, resp, 1);
		if (!ret && resp[0])
			ret = -1;
		break;
	}
	case MMC_READ_SINGLE_BLOCK:
	case MMC_READ_MULTIPLE_BLOCK: {
		ret = cmd->data ? sdspiBlocksRead(host, cmd) : -1;
		break;
	}
	case MMC_WRITE_BLOCK:
	case MMC_WRITE_MULTIPLE_BLOCK: {
		ret = cmd->data ? sdspiBlocksWrite(host, cmd) : -1;
		break;
	}
	case MMC_STOP_TRANSMISSION: {
		ret = sdspiCommand(host, MMC_STOP_TRANSMISSION, 0, resp, 1);
		break;
	}
	default: {
		ZF_LOGE("unsupported SPI SD command %u", cmd->index);
		ret = -1;
		break;
	}
	}

	sdspiDeselect(host);
	cmd->complete = ret ? -1 : 1;
	IRQ_Restore(irqs);
	if (cb)
		cb(sdio, ret, cmd, token);

	return ret;
}

int sdspiInit(uint exiChannel, uint exiCS, bool hasExtDetect, sdio_host_dev_t *dev) {
	struct sdspiHost *host;

	host = malloc(sizeof(*host));
	if (!host)
		return -1;

	memset(host, 0, sizeof(*host));
	host->channel = exiChannel;
	host->cs = exiCS;
	host->clk_mhz = SDSPI_CLK_INIT_MHZ;
	host->has_ext_detect = hasExtDetect;

	dev->reset = sdspiReset;
	dev->set_operational = sdspiSetOperational;
	dev->set_bus_width = sdspiSetBusWidth;
	dev->send_command = sdspiSendCommand;
	dev->handle_irq = sdspiHandleIRQ;
	dev->is_voltage_compatible = sdspiIsVoltageCompatible;
	dev->nth_irq = sdspiNthIRQ;
	dev->get_present_state = sdspiGetPresentState;
	dev->flags = SDIO_HOST_SPI;
	dev->priv = host;

	return 0;
}
