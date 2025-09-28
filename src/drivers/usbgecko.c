/*
 * NPLL - EXI devices - USB Gecko
 *
 * Copyright (C) 2025 Techflash
 *
 * Derived in part from the Linux USB Gecko udbg driver:
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 */

#include <npll/drivers.h>
#include <npll/output.h>
#include <npll/drivers/exi.h>

static REGISTER_DRIVER(usbgeckoDrv);

#define UG_SLOTA 0
#define UG_SLOTB 1

static int slot = -1;

static u32 usbgeckoTransaction(u32 tx, int port) {
	*EXI_CSR(port) = EXI_CSR_CLK_32MHZ | EXI_CSR_CS_0;
	*EXI_DATA(port) = tx;
	*EXI_CR(port) = EXI_CR_TLEN(2) | EXI_CR_RW | EXI_CR_TSTART;

	while (*EXI_CR(port) & EXI_CR_TSTART) {
		/* spin */
	}

	*EXI_CSR(port) = 0;
	return *EXI_DATA(port);
}

static int usbgeckoIsAdapterPresent(int port) {
	return usbgeckoTransaction(0x90000000, port) == 0x04700000;
}
static int usbgeckoTXReady(int port) {
	return usbgeckoTransaction(0xc0000000, port) & 0x04000000;
}

static void usbgeckoWriteChar(const char c) {
	while (!usbgeckoTXReady(slot)) {
		/* spin */
	}

	usbgeckoTransaction(0xb0000000 | (c << 20), slot);
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
	.driver = &usbgeckoDrv,
	.isGraphical = false,
	.rows = 80,
	.columns = 25
};

static void usbgeckoInit(void) {
	if (exiDrv.state != DRIVER_STATE_READY) {
		usbgeckoDrv.state = DRIVER_STATE_NEED_DEP;
		return;
	}

	if (usbgeckoIsAdapterPresent(UG_SLOTB))
		slot = UG_SLOTB;
	else if (usbgeckoIsAdapterPresent(UG_SLOTA))
		slot = UG_SLOTA;
	else {
		slot = -1;
		usbgeckoDrv.state = DRIVER_STATE_NO_HARDWARE;
		return;
	}

	/* register our callback */
	D_AddCallback(usbgeckoCallback);

	/* we're all good */
	usbgeckoDrv.state = DRIVER_STATE_READY;

	usbgeckoWriteStr("USB Gecko driver is now enabled\r\n");
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
