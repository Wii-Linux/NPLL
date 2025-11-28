/*
 * NPLL - H_PlatOps debug console for GCN/Wii
 *
 * A cut down static version of drivers/usbgecko.c, for suitability
 * as an H_PlatOps debug console.
 *
 * Copyright (C) 2025 Techflash
 *
 * Derived in part from the Linux USB Gecko udbg driver:
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 */


#include <npll/regs.h>
#include <npll/console.h>

#define UG_SLOTA 0
#define UG_SLOTB 1

/* EXI defines since we can't use the standard EXI headers */
#define EXI_CLK_32MHZ           5

#define   EXI_CSR_CLKMASK       (0x7<<4)
#define     EXI_CSR_CLK_32MHZ   (EXI_CLK_32MHZ<<4)
#define   EXI_CSR_CSMASK        (0x7<<7)
#define     EXI_CSR_CS_0        (0x1<<7)  /* Chip Select 001 */

#define   EXI_CR_TSTART         (1<<0)
#define   EXI_CR_WRITE		(1<<2)
#define   EXI_CR_RW             (2<<2)
#define   EXI_CR_TLEN(len)      (((len)-1)<<4)

static int slot = -1;
static vu32 *exi_regs;

static vu32 *EXI_CSR(int chan) {
	return (vu32 *)((u32)exi_regs + (0x14 * chan) + 0x00);
}

static vu32 *EXI_CR(int chan) {
	return (vu32 *)((u32)exi_regs + (0x14 * chan) + 0x0c);
}

static vu32 *EXI_DATA(int chan) {
	return (vu32 *)((u32)exi_regs + (0x14 * chan) + 0x10);
}

static u32 tinyUGTransaction(u32 tx, int port) {
	*EXI_CSR(port) = EXI_CSR_CLK_32MHZ | EXI_CSR_CS_0;
	*EXI_DATA(port) = tx;
	*EXI_CR(port) = EXI_CR_TLEN(2) | EXI_CR_RW | EXI_CR_TSTART;

	while (*EXI_CR(port) & EXI_CR_TSTART) {
		/* spin */
	}

	*EXI_CSR(port) = 0;
	return *EXI_DATA(port);
}

static int tinyUGIsAdapterPresent(int port) {
	return tinyUGTransaction(0x90000000, port) == 0x04700000;
}
static int tinyUGTXReady(int port) {
	return tinyUGTransaction(0xc0000000, port) & 0x04000000;
}

static void tinyUGWriteChar(const char c) {
	while (!tinyUGTXReady(slot)) {
		/* spin */
	}

	tinyUGTransaction(0xb0000000 | (c << 20), slot);
}

static void tinyUGWriteStr(const char *str) {
	while (*str) {
		tinyUGWriteChar(*str);
		str++;
	}
}

void H_TinyUGInit(void) {
	/* not applicable on Wii U */
	if (H_ConsoleType == CONSOLE_TYPE_WII_U)
		return;

	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: {
		exi_regs = (vu32 *)0xcc006800;
		break;
	}
	case CONSOLE_TYPE_WII:
	case CONSOLE_TYPE_WII_U: {
		exi_regs = (vu32 *)0xcd806800;
		HW_AIPROT |= (1 << 0);
		break;
	}
	default:
		break;
	}

	if (tinyUGIsAdapterPresent(UG_SLOTB))
		slot = UG_SLOTB;
	else if (tinyUGIsAdapterPresent(UG_SLOTA))
		slot = UG_SLOTA;
	else {
		slot = -1;
		return;
	}

	H_PlatOps->debugWriteStr = tinyUGWriteStr;
	H_PlatOps->debugWriteChar = tinyUGWriteChar;

	tinyUGWriteStr("Tiny USB Gecko driver is now enabled\r\n");
}
