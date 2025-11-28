/*
 * NPLL - Flipper/Hollywood/Latte Hardware - EXI
 *
 * Copyright (C) 2025 Techflash
 */

#include <npll/regs.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/drivers/exi.h>

static vu32 *regs;

static void exiCallback(void) {
	
}

static void exiInit(void) {
	u32 val;
	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: {
		regs = (vu32 *)0xcc006800;
		break;
	}
	case CONSOLE_TYPE_WII:
	case CONSOLE_TYPE_WII_U: {
		regs = (vu32 *)0xcd806800;
		HW_AIPROT |= (1 << 0);
		break;
	}
	default:
		break;
	}

	/* register our callback */
	D_AddCallback(exiCallback);

	/* we're all good */
	exiDrv.state = DRIVER_STATE_READY;
}

static void exiCleanup(void) {
	D_RemoveCallback(exiCallback);
	exiDrv.state = DRIVER_STATE_NOT_READY;
}


vu32 *EXI_CSR(int chan) {
	return (vu32 *)((u32)regs + (0x14 * chan) + 0x00);
}

vu32 *EXI_CR(int chan) {
	return (vu32 *)((u32)regs + (0x14 * chan) + 0x0c);
}

vu32 *EXI_DATA(int chan) {
	return (vu32 *)((u32)regs + (0x14 * chan) + 0x10);
}

REGISTER_DRIVER(exiDrv) = {
	.name = "EXI",
	.mask = DRIVER_ALLOW_ALL,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_CRITICAL,
	.init = exiInit,
	.cleanup = exiCleanup
};
