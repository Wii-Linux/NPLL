/*
 * NPLL - Drivers - Hollywood/Latte Hardware - NAND Interface
 *
 * Copyright (C) 2026 Techflash
 *
 * Mainly based on MINI's NAND code:
 * Copyright (C) 2008, 2009	Haxx Enterprises <bushing@gmail.com>
 * Copyright (C) 2008, 2009	Sven Peter <svenpeter@gmail.com>
 * Copyright (C) 2008, 2009	Hector Martin "marcan" <marcan@marcansoft.com>
 *
 * Wii U specifics based on linux-loader's NAND code:
 *  Copyright (C) 2021          rw-r-r-0644
 *  Copyright (C) 2016          SALT
 *  Copyright (C) 2016          Daz Jones <daz@dazzozo.com>
 *
 *  Copyright (C) 2008, 2009    Haxx Enterprises <bushing@gmail.com>
 *  Copyright (C) 2008, 2009    Sven Peter <svenpeter@gmail.com>
 *  Copyright (C) 2008, 2009    Hector Martin "marcan" <marcan@marcansoft.com>
 */

#define MODULE "hollywood_nand"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/types.h>
#include <npll/utils.h>

static REGISTER_DRIVER(nandDrv);
#define NAND_ECC_SIZE 64
#define NAND_PAGE_SIZE 2048
#define NAND_SIZE (512 * 1024 * 1024)

#define NAND_BANK_SLCCMPT 1
#define NAND_BANK_SLC     2

/* NAND Commands */
#define NAND_CMD_RESET      0xff
#define NAND_CMD_CHIPID     0x90
#define NAND_CMD_GETSTATUS  0x70
#define NAND_CMD_ERASE_PRE  0x60
#define NAND_CMD_ERASE_POST 0xd0
#define NAND_CMD_READ_PRE   0x00
#define NAND_CMD_READ_POST  0x30
#define NAND_CMD_WRITE_PRE  0x80
#define NAND_CMD_WRITE_POST 0x10

/* NAND_CTRL bits */
#define NAND_CTRL_EXEC      BIT(31)
#define NAND_CTRL_IRQ       BIT(30)
#define NAND_CTRL_ERROR     BIT(29)
#define NAND_CTRL_WAIT      BIT(15)
#define NAND_CTRL_WR        BIT(14)
#define NAND_CTRL_RD        BIT(13)
#define NAND_CTRL_ECC       BIT(12)

/* NAND_CONFIG bits */
#define NAND_CONFIG_WP_WIIU BIT(31)
#define NAND_CONFIG_ENABLE  BIT(27)
/* magic values from linux-loader */
#define NAND_CONFIG_MAGIC_WIIU_INIT 0x743e3effu
#define NAND_CONFIG_MAGIC_WIIU_NORM 0x550f1effu
/* magic value from MINI */
#define NAND_CONFIG_MAGIC_WII 0x4b3e0e7fu

/* NAND_BANK bits */
#define NAND_BANK_FL_4 0x00000004u /* set by bsp:fla for revisions after latte A2X */

struct nandRegs {
	vu32 ctrl;
	vu32 config;
	vu32 addr1;
	vu32 addr2;
	vu32 databuf;
	vu32 eccbuf;
	vu32 bank;
	vu32 unk1;
	vu32 unk2;
	vu32 unk3;
	vu32 unk4;
	vu32 unk5;
	vu32 bankCtrl;
	vu32 unk6;
	vu32 unk7;
	vu32 unk8;
	vu32 unk9;
};

struct nandDevInfo {
	u32 bank;
};

static ssize_t nandRead(struct blockDevice *bdev, void *dest, size_t len, u64 off);

static volatile struct nandRegs *const regs = (volatile struct nandRegs *)0xcd010000;
static const struct blockTransfer nandTransfers[] = {
	/* ECC only; ECC needs insane alignment */
	{
		.size = NAND_ECC_SIZE,
		.mode = BLOCK_TRANSFER_EXACT,
		.dmaAlign = 128
	},
	/* data only */
	{
		.size = NAND_PAGE_SIZE,
		.mode = BLOCK_TRANSFER_EXACT,
		.dmaAlign = 32
	},
	/* data + ECC */
	{
		.size = NAND_PAGE_SIZE + NAND_ECC_SIZE,
		.mode = BLOCK_TRANSFER_EXACT,
		.dmaAlign = 128
	}
};

static struct nandDevInfo wiiNANDInfo = { .bank = 0 };
static struct blockDevice wiiBdev;
static struct partition wiiBoot1Part = { .bdev = &wiiBdev, .index = 0, .offset = 0 * NAND_PAGE_SIZE, .size = 0x40 * NAND_PAGE_SIZE };
static struct partition wiiBoot2Part = { .bdev = &wiiBdev, .index = 1, .offset = 0x40 * NAND_PAGE_SIZE, .size = 0x1c0 * NAND_PAGE_SIZE };
static struct partition wiiSffsPart  = { .bdev = &wiiBdev, .index = 2, .offset = 0x200 * NAND_PAGE_SIZE, .size = 0x3fe00 * NAND_PAGE_SIZE };

static struct blockDevice wiiBdev = {
	.name = "nand0",
	.size = NAND_SIZE, /* TODO: calculate */
	.blockSize = NAND_PAGE_SIZE, /* for data this is technically right */
	.drvData = &wiiNANDInfo,
	.transfers = nandTransfers,
	.numTransfers = sizeof(nandTransfers) / sizeof(nandTransfers[0]),
	.blockAlignMode = BLOCK_ALIGN_REJECT,
	.dmaAlignMode = BLOCK_ALIGN_BOUNCE,
	.read = nandRead,
	.write = NULL,
	.partitions = { &wiiBoot1Part, &wiiBoot2Part, &wiiSffsPart },
	.numPartitions = 3,
	.probePartitions = false,
	.flags = BLOCK_FLAG_HLWD_NAND
};

static struct nandDevInfo wiiuSLCInfo = { .bank = NAND_BANK_SLC };
static struct blockDevice wiiuSLCBdev;
static struct partition wiiuSLCSFFSPart = { .bdev = &wiiuSLCBdev, .index = 0, .offset = 0, .size = NAND_SIZE };

static struct blockDevice wiiuSLCBdev = {
	.name = "nand0",
	.size = NAND_SIZE, /* TODO: calculate */
	.blockSize = NAND_PAGE_SIZE, /* for data this is technically right */
	.drvData = &wiiuSLCInfo,
	.transfers = nandTransfers,
	.numTransfers = sizeof(nandTransfers) / sizeof(nandTransfers[0]),
	.blockAlignMode = BLOCK_ALIGN_REJECT,
	.dmaAlignMode = BLOCK_ALIGN_BOUNCE,
	.read = nandRead,
	.write = NULL,
	.partitions = { &wiiuSLCSFFSPart },
	.numPartitions = 1,
	.probePartitions = false,
	.flags = BLOCK_FLAG_HLWD_NAND
};

static struct nandDevInfo wiiuSLCCmptNANDInfo = { .bank = NAND_BANK_SLCCMPT };
static struct blockDevice wiiuSLCCmptBdev;
static struct partition wiiuSLCCmptSFFSPart  = { .bdev = &wiiuSLCCmptBdev, .index = 0, .offset = 0x200 * NAND_PAGE_SIZE, .size = 0x3fe00 * NAND_PAGE_SIZE };

static struct blockDevice wiiuSLCCmptBdev = {
	.name = "nand1",
	.size = NAND_SIZE, /* TODO: calculate */
	.blockSize = NAND_PAGE_SIZE, /* for data this is technically right */
	.drvData = &wiiuSLCCmptNANDInfo,
	.transfers = nandTransfers,
	.numTransfers = sizeof(nandTransfers) / sizeof(nandTransfers[0]),
	.blockAlignMode = BLOCK_ALIGN_REJECT,
	.dmaAlignMode = BLOCK_ALIGN_BOUNCE,
	.read = nandRead,
	.write = NULL,
	.partitions = { &wiiuSLCCmptSFFSPart },
	.numPartitions = 1,
	.probePartitions = false,
	.flags = BLOCK_FLAG_HLWD_NAND
};

static void nandSetConfig(u32 bank, bool writeEnable) {
	if (H_ConsoleType == CONSOLE_TYPE_WII)
		return;

	regs->ctrl = 0;
	regs->config = 0;
	regs->config = (writeEnable ? 0 : NAND_CONFIG_WP_WIIU) |
		NAND_CONFIG_ENABLE |
		NAND_CONFIG_MAGIC_WIIU_NORM;
	regs->bank = bank | NAND_BANK_FL_4;
}

static void nandCalculateBufs(void **ecc, void **data, size_t len, void *buf) {
	if (len == NAND_ECC_SIZE) {
		*ecc = buf;
		*data = NULL;
	}
	else if (len == NAND_PAGE_SIZE) {
		*ecc = NULL;
		*data = buf;
	}
	else if (len == NAND_ECC_SIZE + NAND_PAGE_SIZE) {
		*ecc = buf + NAND_PAGE_SIZE;
		*data = buf;
	}
	else
		assert_unreachable();
}

static int nandSendCommand(u32 command, u32 addrMask, u32 flags, u32 len) {
	if (regs->ctrl & NAND_CTRL_EXEC) {
		log_puts("command in progress while attempting to send command");
		log_printf("NAND_CTRL=0x%08x\r\n", regs->ctrl);
		return -EIO;
	}

	if (H_ConsoleType == CONSOLE_TYPE_WII)
		regs->ctrl = ~NAND_CTRL_EXEC;

	regs->ctrl = 0;

	regs->ctrl = NAND_CTRL_EXEC | (addrMask << 24) | (command << 16) | flags | len;
	return 0;
}

static void nandSetupDMA(void *data, void *ecc) {
	if (data)
		regs->databuf = (u32)virtToPhys(data);
	if (ecc)
		regs->eccbuf = (u32)virtToPhys(ecc);
}

static int nandWait(void) {
	u32 ctrl;
	u64 tb = mftb();

	while (true) {
		ctrl = regs->ctrl;

		if (ctrl & NAND_CTRL_ERROR) {
			log_printf("NAND error, ctrl=0x%08x\r\n", ctrl);
			return -EIO;
		}
		if (!(ctrl & NAND_CTRL_EXEC))
			return 0;

		if (T_HasElapsed(tb, 500 * 1000)) {
			log_printf("NAND timeout, ctrl=0x%08x\r\n", ctrl);
			return -EIO;
		}
	}
}

static ssize_t nandRead(struct blockDevice *bdev, void *dest, size_t len, u64 off) {
	int err;
	void *data, *ecc;
	struct nandDevInfo *info = bdev->drvData;

	nandCalculateBufs(&ecc, &data, len, dest);

	nandSetConfig(info->bank, false);
	regs->addr1 = 0;
	regs->addr2 = (u32)(off / NAND_PAGE_SIZE);

	err = nandSendCommand(NAND_CMD_READ_PRE, 0x1f, 0, 0);
	if (err)
		return err;

	dcache_invalidate(dest, len);

	err = nandWait();
	if (err)
		return err;

	nandSetupDMA(data, ecc);

	err = nandSendCommand(NAND_CMD_READ_POST, 0, NAND_CTRL_WAIT | NAND_CTRL_RD | (ecc ? NAND_CTRL_ECC : 0), len);
	if (err)
		return err;

	err = nandWait();
	if (err)
		return err;

	return (ssize_t)len;
}

static void nandCleanupWiiU(void) {
	uint i;

	regs->bankCtrl = 0;
	while (regs->bankCtrl & BIT(31))
		barrier();
	regs->bankCtrl = 0;

	for (i = 0; i < 0xc0; i += 0x18) {
		/* FIXME: wtf is this????? */
		*(u32 *)(((u8 *)regs) + 0x40 + i) = 0;
		*(u32 *)(((u8 *)regs) + 0x44 + i) = 0;
		*(u32 *)(((u8 *)regs) + 0x48 + i) = 0;
		*(u32 *)(((u8 *)regs) + 0x4c + i) = 0;
		*(u32 *)(((u8 *)regs) + 0x50 + i) = 0;
		*(u32 *)(((u8 *)regs) + 0x54 + i) = 0;
	}

	regs->ctrl = 0;
	while (regs->ctrl & NAND_CTRL_EXEC)
		barrier();
	regs->ctrl = 0;

	regs->config = NAND_CONFIG_MAGIC_WIIU_INIT;
	regs->bank = NAND_BANK_SLCCMPT;
}

static int nandResetWiiU(void) {
	int i, ret;

	for (i = 0; i < 2; i++) {
		nandCleanupWiiU();

		regs->config = NAND_CONFIG_MAGIC_WIIU_INIT | NAND_CONFIG_ENABLE;

		regs->bank = NAND_BANK_FL_4 | /* ??? */
			(i ? 3 : 1); /* ??? */

		ret = nandSendCommand(NAND_CMD_RESET, 0, NAND_CTRL_WAIT, 0);
		if (ret)
			return ret;

		regs->ctrl = 0;
	}

	nandSetConfig(NAND_BANK_SLC, false);
	return 0;
}

static int nandResetWii(void) {
	int ret;

	ret = nandSendCommand(NAND_CMD_RESET, 0, NAND_CTRL_WAIT, 0);
	if (ret)
		return ret;

	ret = nandWait();
	if (ret)
		return ret;

	regs->config = NAND_CONFIG_ENABLE;
	regs->config = NAND_CONFIG_MAGIC_WII;
	return 0;
}

static void nandInit(void) {
	log_printf("NAND_CONFIG=%08x\r\n", regs->config);

	if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		if (nandResetWiiU())
			goto failed;

		B_Register(&wiiuSLCBdev);
		B_Register(&wiiuSLCCmptBdev);
	}
	else {
		if (nandResetWii())
			goto failed;

		B_Register(&wiiBdev);
	}

	nandDrv.state = DRIVER_STATE_READY;
	return;
failed:
	nandDrv.state = DRIVER_STATE_FAULTED;
}

static void nandCleanup(void) {
	if (nandDrv.state != DRIVER_STATE_READY)
		return;

	if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		B_Unregister(&wiiuSLCCmptBdev);
		B_Unregister(&wiiuSLCBdev);
		nandCleanupWiiU();
	}
	else {
		B_Unregister(&wiiBdev);
		nandResetWii();
	}

	nandDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(nandDrv) = {
	.name = "Hollywood/Latte NAND Interface",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_BLOCK,
	.init = nandInit,
	.cleanup = nandCleanup
};
