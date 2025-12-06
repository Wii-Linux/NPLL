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
#include <npll/elf.h>
#include <npll/timer.h>
#include <stdio.h>
#include <string.h>

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

static int usbgeckoRXEmpty(int port) {
	return usbgeckoTransaction(0xd000, port) & 0x0400;
}

static int usbgeckoGetChar(int port) {
	u16 data;
	int i;

	for (i = 0; i < 5; i++)
		if (!usbgeckoRXEmpty(port))
			return -1;

	data = usbgeckoTransaction(0xa000, port);
	if (data & 0x0800)
		return data & 0xff;
	else
		return -1;
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

#if 0
static u64 timeoutStart = 0;
static int timeout = 0;
#endif
static u8 buf[512 * 1024] = { 0 };
static int bufIdx = 0;
static u32 binSz = 0;
static enum {
	STATE_IDLE = 0,
	STATE_GET_SIZE,
	STATE_GET_DATA,
	STATE_LAUNCH
} usbgeckoState = STATE_IDLE;
static const char bufMagic[7] = "NPLLBIN";
static void usbgeckoCallback(void) {
	int ret;
	u8 data;

#if 0
	/* are we still in timeout? */
	if (timeout && !T_HasElapsed(timeoutStart, 1000))
		return;
	timeout = 0;
#endif

	/* try to get a byte */
	ret = usbgeckoGetChar(slot);

	/* did we get anything? */
	if (ret == -1) {
#if 0
		/* nope, go back in timeout */
		timeout = 1;
		timeoutStart = mftb();
#endif
		return;
	}

	/* we got a byte */
	data = (u8)(ret & 0xff);

	switch (usbgeckoState) {
	case STATE_IDLE: {
		buf[bufIdx] = data;
		bufIdx++;

		/* is what we have so far right? */
		if (memcmp(buf, bufMagic, bufIdx)) {
			/* nope :( */
			puts("GECKO: bogus data");
			bufIdx = 0;
			break;
		}

		/* yes, do we have enough? */
		if (bufIdx != sizeof(bufMagic))
			break; /* not yet */

		/* yes, move on to the next state */
		puts("GECKO: awaiting size");
		usbgeckoState = STATE_GET_SIZE;
		bufIdx = 0;
		break;
	}
	case STATE_GET_SIZE: {
		buf[bufIdx] = data;
		bufIdx++;

		/* do we have enough? */
		if (bufIdx != 4)
			break; /* not yet */

		/* yes, interpret the size as a big-endian 32-bit integer */
		binSz = ((u32 *)buf)[0];

		/* is it sensical? */
		if (binSz > sizeof(buf) || binSz <= sizeof(Elf32_Ehdr)) {
			/* hey now you can't do that */
			printf("GECKO: Cannot possibly receive binary of nonsense size: %u\r\n", binSz);
			usbgeckoState = STATE_IDLE;
			bufIdx = 0;
			break;
		}

		/* yes, we can receive that */
		printf("GECKO: Receiving binary with size: %u...\r\n", binSz);
		usbgeckoState = STATE_GET_DATA;
		bufIdx = 0;
		break;
	}
	case STATE_GET_DATA: {
		buf[bufIdx] = data;
		bufIdx++;

		/* do we have enough? */
		if ((u32)bufIdx != binSz)
			break; /* not yet */

		/* yes, let's do this thing */
		puts("GECKO: Preparing to launch...");
		usbgeckoState = STATE_LAUNCH;
		break;
	}
	case STATE_LAUNCH: {
		int ret;
		ret = ELF_CheckValid(buf);
		if (ret) {
			printf("GECKO: Invalid ELF: %d\r\n", ret);
			usbgeckoState = STATE_IDLE;
			bufIdx = 0;
			break;
		}

		/* valid ELF, let's load it... */
		printf("GECKO: Launching ELF, goodbye!\r\n", ret);
		ret = ELF_LoadMem(buf);
		printf("GECKO: ELF launch failed: %d\r\n");
		usbgeckoState = STATE_IDLE;
		bufIdx = 0;
		break;
	}
	}
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
