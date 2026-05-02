/*
 * NPLL - Drivers - Hollywood/Latte Hardware - NAND Interface
 *
 * Copyright (C) 2026 Techflash
 *
 * Based on MINI's NAND code:
 * Copyright (C) 2008, 2009	Haxx Enterprises <bushing@gmail.com>
 * Copyright (C) 2008, 2009	Sven Peter <svenpeter@gmail.com>
 * Copyright (C) 2008, 2009	Hector Martin "marcan" <marcan@marcansoft.com>
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
#define NAND_CONFIG_ENABLE  BIT(27)

struct nandRegs {
	vu32 ctrl;
	vu32 config;
	vu32 addr1;
	vu32 addr2;
	vu32 databuf;
	vu32 eccbuf;
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

static struct blockDevice nandBdev;
static struct partition boot1part = { .bdev = &nandBdev, .index = 0, .offset = 0 * NAND_PAGE_SIZE, .size = 0x40 * NAND_PAGE_SIZE };
static struct partition boot2part = { .bdev = &nandBdev, .index = 1, .offset = 0x40 * NAND_PAGE_SIZE, .size = 0x1c0 * NAND_PAGE_SIZE };
static struct partition sffspart  = { .bdev = &nandBdev, .index = 2, .offset = 0x200 * NAND_PAGE_SIZE, .size = 0x3fe00 * NAND_PAGE_SIZE };

static struct blockDevice nandBdev = {
	.name = "nand0",
	.size = 512 * 1024 * 1024, /* TODO: calculate */
	.blockSize = NAND_PAGE_SIZE, /* for data this is technically right */
	.drvData = NULL,
	.transfers = nandTransfers,
	.numTransfers = sizeof(nandTransfers) / sizeof(nandTransfers[0]),
	.blockAlignMode = BLOCK_ALIGN_REJECT,
	.dmaAlignMode = BLOCK_ALIGN_BOUNCE,
	.read = nandRead,
	.write = NULL,
	.partitions = { &boot1part, &boot2part, &sffspart },
	.numPartitions = 3,
	.probePartitions = false
};

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

	/* TODO: is this really necessary? */
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
	(void)bdev;

	nandCalculateBufs(&ecc, &data, len, dest);

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

static ssize_t nandWrite(struct blockDevice *bdev, const void *src, size_t len, u64 off) {
	/* TODO: this */
	(void)bdev;
	(void)src;
	(void)len;
	(void)off;
	return -1;
}

static void nandReset(void) {
	nandSendCommand(NAND_CMD_RESET, 0, NAND_CTRL_WAIT, 0);
	nandWait();
	regs->config = NAND_CONFIG_ENABLE;
	regs->config = 0x4b3e0e7f; /* magic value from MINI */
}

static void nandInit(void) {
	nandReset();
	B_Register(&nandBdev);
	nandDrv.state = DRIVER_STATE_READY;
}

static void nandCleanup(void) {
	B_Unregister(&nandBdev);
	nandReset();
}

static REGISTER_DRIVER(nandDrv) = {
	.name = "Hollywood/Latte NAND Interface",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_BLOCK,
	.init = nandInit,
	.cleanup = nandCleanup
};
