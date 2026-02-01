/*
 * NPLL - Drivers - Hollywood SD/MMC interface
 *
 * Copyright (C) 2025 Techflash
 *
 * Derived from mini's sdmmc.c:
 * Copyright (C) 2008, 2009	Sven Peter <svenpeter@gmail.com>
 *
 * # This code is licensed to you under the terms of the GNU GPL, version 2;
 * # see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 */

#define MODULE "sdmmc"

#include <npll/allocator.h>
#include <npll/cache.h>
#include <npll/drivers.h>
#include <npll/log.h>
#include <npll/drivers/hollywood_sdhc.h>
#include <npll/drivers/hollywood_sdmmc.h>
#include <npll/timer.h>
#include <string.h>

static int sdmmc_select(void);

static REGISTER_DRIVER(sdmmcDrv);

/* see the comment in hollywood_sdhc.c */
//#define SDMMC_DEBUG
#ifdef SDMMC_DEBUG
static int sdmmcdebug = 0;
#define DPRINTF(n,s)	do { if ((n) <= sdmmcdebug) log_printf s; else udelay(1 * 1000); } while (0)
#else
#define DPRINTF(n,s)	do {udelay(1 * 1000);} while(0)
#endif

#define ISSET(var, mask) (((var) & (mask)) ? 1 : 0)
#define SET(var, mask) ((var) |= (mask))

struct sdmmc_card {
	sdmmc_chipset_handle_t handle;
	int inserted;
	int sdhc_blockmode;
	int selected;
	int new_card; // set to 1 everytime a new card is inserted

	u32 timeout;
	u32 num_sectors;
	u32 cid;
	u16 rca;
};

static struct sdmmc_card card;

void sdmmc_attach(sdmmc_chipset_handle_t handle) {
	memset(&card, 0, sizeof(card));

	card.handle = handle;

	DPRINTF(0, ("attached new SD/MMC card\r\n"));

	sdhc_host_reset(card.handle);

	if (sdhc_card_detect(card.handle)) {
		DPRINTF(1, ("card is inserted. starting init sequence.\r\n"));
		sdmmc_needs_discover();
	}
}

#if 0
static void sdmmc_abort(void) {
	struct sdmmc_command cmd;
	log_printf("abortion kthx\r\n");

	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = MMC_STOP_TRANSMISSION;
	cmd.c_arg = 0;
	cmd.c_flags = SCF_RSP_R1B;
	sdhc_exec_command(card.handle, &cmd);
}
#endif

void sdmmc_needs_discover(void) {
	struct sdmmc_command cmd;
	u32 ocr;

	DPRINTF(0, ("card needs discovery.\r\n"));
	sdhc_host_reset(card.handle);
	card.new_card = 1;

	if (!sdhc_card_detect(card.handle)) {
		DPRINTF(1, ("card (no longer?) inserted.\r\n"));
		card.inserted = 0;
		return;
	}

	DPRINTF(1, ("enabling power\r\n"));
	if (sdhc_bus_power(card.handle, 1) != 0) {
		log_printf("powerup failed for card\r\n");
		goto out;
	}

	DPRINTF(1, ("enabling clock\r\n"));
	if (sdhc_bus_clock(card.handle, SDMMC_DEFAULT_CLOCK) != 0) {
		log_printf("could not enable clock for card\r\n");
		goto out_power;
	}

	DPRINTF(1, ("sending GO_IDLE_STATE\r\n"));

	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = MMC_GO_IDLE_STATE;
	cmd.c_flags = SCF_RSP_R0;
	sdhc_exec_command(card.handle, &cmd);

	if (cmd.c_error) {
		log_printf("GO_IDLE_STATE failed with %d\r\n", cmd.c_error);
		goto out_clock;
	}
	DPRINTF(2, ("GO_IDLE_STATE response: %x\r\n", MMC_R1(cmd.c_resp)));

	DPRINTF(1, ("sending SEND_IF_COND\r\n"));

	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = SD_SEND_IF_COND;
	cmd.c_arg = 0x1aa;
	cmd.c_flags = SCF_RSP_R7;
	cmd.c_timeout = 100;
	sdhc_exec_command(card.handle, &cmd);

	ocr = card.handle->ocr;
	if (cmd.c_error || (cmd.c_resp[0] & 0xff) != 0xaa)
		ocr &= ~SD_OCR_SDHC_CAP;
	else
		ocr |= SD_OCR_SDHC_CAP;
	DPRINTF(2, ("SEND_IF_COND ocr: %x\r\n", ocr));

	int tries;
	for (tries = 100; tries > 0; tries--) {
		udelay(100000);

		memset(&cmd, 0, sizeof(cmd));
		cmd.c_opcode = MMC_APP_CMD;
		cmd.c_arg = 0;
		cmd.c_flags = SCF_RSP_R1;
		sdhc_exec_command(card.handle, &cmd);

		if (cmd.c_error)
			continue;

		memset(&cmd, 0, sizeof(cmd));
		cmd.c_opcode = SD_APP_OP_COND;
		cmd.c_arg = ocr;
		cmd.c_flags = SCF_RSP_R3;
		sdhc_exec_command(card.handle, &cmd);
		if (cmd.c_error)
			continue;

		DPRINTF(3, ("response for SEND_IF_COND: %08x\r\n",
					MMC_R1(cmd.c_resp)));
		if (ISSET(MMC_R1(cmd.c_resp), MMC_OCR_MEM_READY))
			break;
	}
	if (!ISSET(cmd.c_resp[0], MMC_OCR_MEM_READY)) {
		log_printf("card failed to powerup.\r\n");
		goto out_power;
	}

	if (ISSET(MMC_R1(cmd.c_resp), SD_OCR_SDHC_CAP))
		card.sdhc_blockmode = 1;
	else
		card.sdhc_blockmode = 0;
	DPRINTF(2, ("SDHC: %d\r\n", card.sdhc_blockmode));

	u8 *resp;
	DPRINTF(2, ("MMC_ALL_SEND_CID\r\n"));
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = MMC_ALL_SEND_CID;
	cmd.c_arg = 0;
	cmd.c_flags = SCF_RSP_R2;
	sdhc_exec_command(card.handle, &cmd);
	if (cmd.c_error) {
		log_printf("MMC_ALL_SEND_CID failed with %d\r\n", cmd.c_error);
		goto out_clock;
	}

	card.cid = MMC_R1(cmd.c_resp);
	resp = (u8 *)cmd.c_resp;
	log_printf("CID: mid=%02x name='%c%c%c%c%c%c%c' prv=%d.%d psn=%02x%02x%02x%02x mdt=%d/%d\r\n", resp[14],
		resp[13],resp[12],resp[11],resp[10],resp[9],resp[8],resp[7], resp[6], resp[5] >> 4, resp[5] & 0xf,
		resp[4], resp[3], resp[2], resp[0] & 0xf, 2000 + (resp[0] >> 4));

	DPRINTF(2, ("SD_SEND_RELATIVE_ADDRESS\r\n"));
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = SD_SEND_RELATIVE_ADDR;
	cmd.c_arg = 0;
	cmd.c_flags = SCF_RSP_R6;
	sdhc_exec_command(card.handle, &cmd);
	if (cmd.c_error) {
		log_printf("SD_SEND_RCA failed with %d\r\n", cmd.c_error);
		goto out_clock;
	}

	card.rca = MMC_R1(cmd.c_resp)>>16;
	DPRINTF(2, ("rca: %08x\r\n", card.rca));

	card.selected = 0;
	card.inserted = 1;

	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = MMC_SEND_CSD;
	cmd.c_arg = ((u32)card.rca)<<16;
	cmd.c_flags = SCF_RSP_R2;
	sdhc_exec_command(card.handle, &cmd);
	if (cmd.c_error) {
		log_printf("MMC_SEND_CSD failed with %d\r\n", cmd.c_error);
		goto out_power;
	}

	resp = (u8 *)cmd.c_resp;

	int i;
	log_printf("csd: ");
	for(i=15; i>=0; i--) log_printf("%02x ", (u32) resp[i]);
	log_printf("\r\n");

	if (resp[13] == 0xe) { // sdhc
		unsigned int c_size = resp[7] << 16 | resp[6] << 8 | resp[5];
		log_printf("sdhc mode, c_size=%u, card size = %uk\r\n", c_size, (c_size + 1)* 512);
		card.timeout = 250 * 1000000; // spec says read timeout is 100ms and write/erase timeout is 250ms
		card.num_sectors = (c_size + 1) * 1024; // number of 512-byte sectors
	}
	else {
		unsigned int taac, nsac, read_bl_len, c_size, c_size_mult;
		taac = resp[13];
		nsac = resp[12];
		read_bl_len = resp[9] & 0xF;

		c_size = (resp[8] & 3) << 10;
		c_size |= (resp[7] << 2);
		c_size |= (resp[6] >> 6);
		c_size_mult = (resp[5] & 3) << 1;
		c_size_mult |= resp[4] >> 7;
		log_printf("taac=%u nsac=%u read_bl_len=%u c_size=%u c_size_mult=%u card size=%u bytes\r\n",
			taac, nsac, read_bl_len, c_size, c_size_mult, (c_size + 1) * (4 << c_size_mult) * (1 << read_bl_len));
		static const unsigned int time_unit[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000};
		static const unsigned int time_value[] = {1, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80}; // must div by 10
		card.timeout = time_unit[taac & 7] * time_value[(taac >> 3) & 0xf] / 10;
		log_printf("calculated timeout =  %uns\r\n", card.timeout);
		card.num_sectors = (c_size + 1) * (4 << c_size_mult) * (1 << read_bl_len) / 512;
	}

	sdmmc_select();
	DPRINTF(2, ("MMC_SET_BLOCKLEN\r\n"));
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = MMC_SET_BLOCKLEN;
	cmd.c_arg = SDMMC_DEFAULT_BLOCKLEN;
	cmd.c_flags = SCF_RSP_R1;
	sdhc_exec_command(card.handle, &cmd);
	if (cmd.c_error) {
		log_printf("MMC_SET_BLOCKLEN failed with %d\r\n", cmd.c_error);
		card.inserted = card.selected = 0;
		goto out_clock;
	}
	return;

out_clock:
	sdhc_bus_clock(card.handle, SDMMC_SDCLK_OFF);

out_power:
	sdhc_bus_power(card.handle, 0);
out:
	return;
}


static int sdmmc_select(void) {
	struct sdmmc_command cmd;

	DPRINTF(2, ("MMC_SELECT_CARD\r\n"));
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = MMC_SELECT_CARD;
	cmd.c_arg = ((u32)card.rca)<<16;
	cmd.c_flags = SCF_RSP_R1B;
	sdhc_exec_command(card.handle, &cmd);
	log_printf("%s: resp=%x\r\n", __FUNCTION__, MMC_R1(cmd.c_resp));
//	sdhc_dump_regs(card.handle);

//	log_printf("present state = %x\r\n", HREAD4(hp, SDHC_PRESENT_STATE));
	if (cmd.c_error) {
		log_printf("MMC_SELECT card failed with %d.\r\n", cmd.c_error);
		return -1;
	}

	card.selected = 1;
	return 0;
}

static int sdmmc_check_card(void) {
	if (card.inserted == 0)
		return SDMMC_NO_CARD;

	if (card.new_card == 1)
		return SDMMC_NEW_CARD;

	return SDMMC_INSERTED;
}

static int sdmmc_ack_card(void) {
	if (card.new_card == 1) {
		card.new_card = 0;
		return 0;
	}

	return -1;
}

static int sdmmc_read(u32 blk_start, u32 blk_count, void *data) {
	struct sdmmc_command cmd;

//	log_printf("%s(%u, %u, %p)\r\n", __FUNCTION__, blk_start, blk_count, data);
	if (card.inserted == 0) {
		log_printf("READ: no card inserted.\r\n");
		return -1;
	}

	if (card.selected == 0) {
		if (sdmmc_select() < 0) {
			log_printf("READ: cannot select card.\r\n");
			return -1;
		}
	}

	if (card.new_card == 1) {
		log_printf("new card inserted but not acknowledged yet.\r\n");
		return -1;
	}

	DPRINTF(2, ("MMC_READ_BLOCK_MULTIPLE\r\n"));
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = MMC_READ_BLOCK_MULTIPLE;
	if (card.sdhc_blockmode)
		cmd.c_arg = blk_start;
	else
		cmd.c_arg = blk_start * SDMMC_DEFAULT_BLOCKLEN;
	cmd.c_data = data;
	cmd.c_datalen = blk_count * SDMMC_DEFAULT_BLOCKLEN;
	cmd.c_blklen = SDMMC_DEFAULT_BLOCKLEN;
	cmd.c_flags = SCF_RSP_R1 | SCF_CMD_READ;
	sdhc_exec_command(card.handle, &cmd);

	if (cmd.c_error) {
		log_printf("MMC_READ_BLOCK_MULTIPLE failed with %d\r\n", cmd.c_error);
		return -1;
	}
	DPRINTF(2, ("MMC_READ_BLOCK_MULTIPLE done\r\n"));

	return 0;
}

static int sdmmc_write(u32 blk_start, u32 blk_count, void *data) {
	struct sdmmc_command cmd;

	if (card.inserted == 0) {
		log_printf("READ: no card inserted.\r\n");
		return -1;
	}

	if (card.selected == 0) {
		if (sdmmc_select() < 0) {
			log_printf("READ: cannot select card.\r\n");
			return -1;
		}
	}

	if (card.new_card == 1) {
		log_printf("new card inserted but not acknowledged yet.\r\n");
		return -1;
	}

	DPRINTF(2, ("MMC_WRITE_BLOCK_MULTIPLE\r\n"));
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = MMC_WRITE_BLOCK_MULTIPLE;
	if (card.sdhc_blockmode)
		cmd.c_arg = blk_start;
	else
		cmd.c_arg = blk_start * SDMMC_DEFAULT_BLOCKLEN;
	cmd.c_data = data;
	cmd.c_datalen = blk_count * SDMMC_DEFAULT_BLOCKLEN;
	cmd.c_blklen = SDMMC_DEFAULT_BLOCKLEN;
	cmd.c_flags = SCF_RSP_R1;
	sdhc_exec_command(card.handle, &cmd);

	if (cmd.c_error) {
		log_printf("MMC_READ_BLOCK_MULTIPLE failed with %d\r\n", cmd.c_error);
		return -1;
	}
	DPRINTF(2, ("MMC_WRITE_BLOCK_MULTIPLE done\r\n"));

	return 0;
}

static int sdmmc_get_sectors(void) {
	if (card.inserted == 0) {
		log_printf("READ: no card inserted.\r\n");
		return -1;
	}

	if (card.new_card == 1) {
		log_printf("new card inserted but not acknowledged yet.\r\n");
		return -1;
	}

//	sdhc_error(sdhci->reg_base, "num sectors = %u", sdhci->num_sectors);

	return card.num_sectors;
}

static void sdmmcDrvInit(void) {
	u8 *tmp;
	int state;
	sdhc_init();

	state = sdmmc_check_card();

	if (state == SDMMC_NO_CARD) {
		sdmmcDrv.state = DRIVER_STATE_NO_HARDWARE;
		return;
	}
	else if (state != SDMMC_INSERTED && state != SDMMC_NEW_CARD) {
		/* something's gone wrong */
		sdmmcDrv.state = DRIVER_STATE_FAULTED;
		return;
	}

	sdmmc_ack_card();


	tmp = M_PoolAlloc(POOL_MEM2, SDMMC_DEFAULT_BLOCKLEN);
	memset(tmp, 0, SDMMC_DEFAULT_BLOCKLEN);
	dcache_flush(tmp, SDMMC_DEFAULT_BLOCKLEN);
	sdmmc_read(0, 1, tmp);
	dcache_invalidate(tmp, SDMMC_DEFAULT_BLOCKLEN);
	log_printf("sdmmc_read last bytes: %2x %2x\r\n", tmp[510], tmp[511]);
	free(tmp);


	/* TODO: once we have real block support, register it with the block layer */

	sdmmcDrv.state = DRIVER_STATE_READY;
	return;
}

static void sdmmcDrvCallback(void) {
	/* this also checks for media changed */
	sdhc_intr();
}

static void sdmmcDrvCleanup(void) {
	sdhc_shutdown();
	D_RemoveCallback(sdmmcDrvCallback);
	sdmmcDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(sdmmcDrv) = {
	.init = sdmmcDrvInit,
	.cleanup = sdmmcDrvCleanup,
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_BLOCK,
	.name = "Hollywood/Latte SD/MMC"
};
