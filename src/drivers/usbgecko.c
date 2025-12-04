/*
 * NPLL - EXI devices - USB Gecko
 *
 * Copyright (C) 2025 Techflash
 *
 * Derived in part from the Linux USB Gecko udbg driver:
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 */

#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/output.h>
#include <npll/drivers/exi.h>

static REGISTER_DRIVER(usbgeckoDrv);

#define UG_SLOTA 0
#define UG_SLOTB 1

static int slot = -1;

static u16 usbgeckoTransaction(u16 tx, int port) {
	u16 rx;

	H_EXISelect(port, 0, 32);
	H_EXIRdWrImm(port, 2, &tx, &rx);
	H_EXIDeselect(port);

	return rx;
}

static int usbgeckoIsAdapterPresent(int port) {
	return usbgeckoTransaction(0x9000, port) == 0x0470;
}
static int usbgeckoTXReady(int port) {
	return usbgeckoTransaction(0xc000, port) & 0x0400;
}

static void usbgeckoWriteChar(const char c) {
	while (!usbgeckoTXReady(slot)) {
		/* spin */
	}

	usbgeckoTransaction(0xb000 | (c << 4), slot);
}

static void usbgeckoWriteStr(const char *str) {
	while (*str) {
		usbgeckoWriteChar(*str);
		str++;
	}
}

static void usbgeckoCallback(void) {
	/* TODO: Check if it's still attached */	
}

static const struct outputDevice outDev = {
	.writeChar = usbgeckoWriteChar,
	.writeStr = usbgeckoWriteStr,
	.name = "USB Gecko",
	.driver = &usbgeckoDrv,
	.isGraphical = false,
	.rows = 80,
	.columns = 25
};

static void usbgeckoInit(void) {
	void *str = NULL, *chr = NULL;

	if (exiDrv.state != DRIVER_STATE_READY) {
		usbgeckoDrv.state = DRIVER_STATE_NEED_DEP;
		return;
	}

	/* it conflicts with our probing */
	if (H_ConsoleType != CONSOLE_TYPE_WII_U) {
		str = H_PlatOps->debugWriteStr;
		H_PlatOps->debugWriteStr = NULL;
		chr = H_PlatOps->debugWriteChar;
		H_PlatOps->debugWriteChar = NULL;
	}


	if (usbgeckoIsAdapterPresent(UG_SLOTB))
		slot = UG_SLOTB;
	else if (usbgeckoIsAdapterPresent(UG_SLOTA))
		slot = UG_SLOTA;
	else {
		slot = -1;
		usbgeckoDrv.state = DRIVER_STATE_NO_HARDWARE;

		/* restore */
		if (H_ConsoleType != CONSOLE_TYPE_WII_U) {
			H_PlatOps->debugWriteStr = str;
			H_PlatOps->debugWriteChar = chr;
		}
		return;
	}

	/* register our callback */
	D_AddCallback(usbgeckoCallback);

	/* we're all good */
	usbgeckoDrv.state = DRIVER_STATE_READY;

	usbgeckoWriteStr("USB Gecko driver is now enabled in Slot-");
	usbgeckoWriteChar('A' + slot);
	usbgeckoWriteChar('\r');
	usbgeckoWriteChar('\n');
	O_AddDevice(&outDev);
}

static void usbgeckoCleanup(void) {
	D_RemoveCallback(usbgeckoCallback);
	usbgeckoDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(usbgeckoDrv) = {
	.name = "USB Gecko",
	.mask = DRIVER_ALLOW_ALL,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_CRITICAL,
	.init = usbgeckoInit,
	.cleanup = usbgeckoCleanup
};
