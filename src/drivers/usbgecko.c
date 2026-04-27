/*
 * NPLL - EXI devices - USB Gecko
 *
 * Copyright (C) 2025-2026 Techflash
 *
 * Derived in part from the Linux USB Gecko udbg driver:
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 */

#define MODULE "GECKO"

#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/output.h>
#include <npll/drivers/exi.h>
#include <npll/elf.h>
#include <npll/timer.h>
#include <npll/allocator.h>
#include <npll/log.h>
#include <string.h>

static REGISTER_DRIVER(usbgeckoDrv);

#define UG_SLOTA 0
#define UG_SLOTB 1
#define UG_LOAD_TIMEOUT_US (5 * 1000 * 1000)

static uint slot = (uint)-1;
static bool suppressOutput = false;

static u16 usbgeckoTransaction(u16 tx, uint port) {
	u16 rx;

	H_EXISelect(port, 0, 32);
	H_EXIRdWrImm(port, 2, &tx, &rx);
	H_EXIDeselect(port);

	return rx;
}

static int usbgeckoIsAdapterPresent(uint port) {
	return usbgeckoTransaction(0x9000, port) == 0x0470;
}

static int usbgeckoTXReady(uint port) {
	return usbgeckoTransaction(0xc000, port) & 0x0400;
}

static int usbgeckoRXReady(uint port) {
	return usbgeckoTransaction(0xd000, port) & 0x0400;
}

static int usbgeckoGetChar(uint port) {
	u16 data;

	data = usbgeckoTransaction(0xa000, port);
	if (data & 0x0800)
		return data & 0xff;
	else
		return -1;
}

static int usbgeckoTryGetChar(uint port) {
	if (!usbgeckoRXReady(port))
		return -1;

	return usbgeckoGetChar(port);
}


static void usbgeckoWriteCharRaw(const char c) {
	while (!usbgeckoTXReady(slot)) {
		/* spin */
	}

	usbgeckoTransaction(0xb000u | (u16)(c << 4), slot);
}

static void usbgeckoWriteChar(const char c) {
	if (suppressOutput)
		return;

	usbgeckoWriteCharRaw(c);
}

static void usbgeckoWriteStrRaw(const char *str) {
	while (*str) {
		usbgeckoWriteCharRaw(*str);
		str++;
	}
}

static void usbgeckoWriteBufRaw(const char *buf, uint len) {
	while (len) {
		usbgeckoWriteCharRaw(*buf);
		buf++;
		len--;
	}
}

static void usbgeckoWriteStr(const char *str) {
	if (suppressOutput)
		return;

	usbgeckoWriteStrRaw(str);
}

static u8 tinyBuf[16] = { 0 };
static u8 *buf;
static uint bufIdx = 0;
static u32 binSz = 0;
static u64 lastRxTB = 0;
static enum {
	STATE_IDLE = 0,
	STATE_GET_SIZE,
	STATE_GET_DATA
} usbgeckoState = STATE_IDLE;
static const u8 bufMagic[8] = { 'N', 'P', 'L', 'L', 'B', 'I', 'N', 0x7f };
static const char ackMagic = 0x06;

static void usbgeckoAck(void) {
	usbgeckoWriteBufRaw(&ackMagic, sizeof(ackMagic));
}

static void usbgeckoResetMagicSearch(u8 data) {
	if (data == bufMagic[0]) {
		tinyBuf[0] = data;
		bufIdx = 1;
	}
	else {
		bufIdx = 0;
	}
}

static void usbgeckoResetState(void) {
	suppressOutput = false;
	usbgeckoState = STATE_IDLE;
	bufIdx = 0;
	lastRxTB = 0;
}

static int usbgeckoWaitChar(u32 timeoutUsecs) {
	u64 startTB;
	int ret;

	startTB = mftb();
	while (!T_HasElapsed(startTB, timeoutUsecs)) {
		ret = usbgeckoTryGetChar(slot);
		if (ret != -1)
			return ret;
	}

	return -1;
}

static int usbgeckoReceivePayload(void) {
	int ret;

	/* Payload receive is deliberately blocking: the USB Gecko FIFO is small,
	 * so returning to the normal callback loop here can drop bytes.
	 */
	while ((u32)bufIdx != binSz) {
		ret = usbgeckoWaitChar(UG_LOAD_TIMEOUT_US);
		if (ret == -1)
			return -1;

		buf[bufIdx] = (u8)(ret & 0xff);
		bufIdx++;
	}

	return 0;
}

static void usbgeckoLaunchPayload(void) {
	int ret;

	suppressOutput = false;
	log_puts("Preparing to launch...");

	ret = ELF_CheckValid(buf);
	if (ret) {
		log_printf("Invalid ELF: %d\r\n", ret);
		free(buf);
		buf = NULL;
		usbgeckoResetState();
		return;
	}

	/* valid ELF, let's load it... */
	log_printf("Launching ELF, goodbye!\r\n", ret);
	ret = ELF_LoadMem(buf);
	log_printf("ELF launch failed: %d\r\n", ret);
	free(buf);
	buf = NULL;
	usbgeckoResetState();
}

static void usbgeckoCallback(void) {
	int ret;
	u8 data;

	if (usbgeckoState != STATE_IDLE && lastRxTB && T_HasElapsed(lastRxTB, UG_LOAD_TIMEOUT_US)) {
		if (buf)
			free(buf);
		buf = NULL;
		usbgeckoResetState();
		log_puts("USB Gecko receive timed out");
	}

again:
	/* try to get a byte */
	ret = usbgeckoTryGetChar(slot);

	if (ret == -1) /* didn't get anything */
		return;

	/* we got a byte */
	data = (u8)(ret & 0xff);
	lastRxTB = mftb();

	switch (usbgeckoState) {
	case STATE_IDLE: {
		tinyBuf[bufIdx] = data;
		bufIdx++;

		/* is what we have so far right? */
		if (memcmp(tinyBuf, bufMagic, bufIdx)) {
			usbgeckoResetMagicSearch(data);
			break;
		}

		/* yes, do we have enough? */
		if (bufIdx != sizeof(bufMagic))
			break; /* not yet */

		/* yes, move on to the next state */
		suppressOutput = true;
		usbgeckoState = STATE_GET_SIZE;
		lastRxTB = mftb();
		bufIdx = 0;
		break;
	}
	case STATE_GET_SIZE: {
		tinyBuf[bufIdx] = data;
		bufIdx++;

		/* do we have enough? */
		if (bufIdx != 4)
			break; /* not yet */

		/* yes, interpret the size as a big-endian 32-bit integer */
		binSz = ((u32)tinyBuf[0] << 24) |
			((u32)tinyBuf[1] << 16) |
			((u32)tinyBuf[2] << 8) |
			(u32)tinyBuf[3];

		/* is it sensical? */
		/* a binary of 16M should never OOM, even on GameCube (though the load would fail since it'd overwrite itself) - any larger doesn't make any sense to load over USB Gecko anyways since it'd be so miserably slow */
		if (binSz > (16 * 1024 * 1024) || binSz <= sizeof(Elf32_Ehdr)) {
			/* hey now you can't do that */
			usbgeckoResetState();
			log_printf("Cannot possibly receive binary of nonsense size: %u\r\n", binSz);
			break;
		}

		/* allocate our scratch buffer in MEM2 if possible */
		if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE)
			buf = M_PoolAlloc(POOL_MEM1, binSz, 4);
		else
			buf = M_PoolAlloc(POOL_MEM2, binSz, 4);

		if (!buf) {
			usbgeckoResetState();
			log_printf("Memory allocation for binary of size %u failed\r\n", binSz);
			break;
		}

		memset(buf, 0, binSz);

		/* yes, we can receive that */
		log_printf("Receiving binary with size: %u...\r\n", binSz);
		usbgeckoState = STATE_GET_DATA;
		bufIdx = 0;
		usbgeckoAck();
		ret = usbgeckoReceivePayload();
		if (ret) {
			uint received = bufIdx;
			u32 expected = binSz;

			free(buf);
			buf = NULL;
			usbgeckoResetState();
			log_printf("USB Gecko receive timed out after %u/%u bytes\r\n", received, expected);
			break;
		}
		usbgeckoAck();
		usbgeckoLaunchPayload();
		break;
	}
	case STATE_GET_DATA: {
		(void)data;
		usbgeckoResetState();
		break;
	}
	}

	goto again;
}

static const struct outputDevice outDev = {
	.writeChar = usbgeckoWriteChar,
	.writeStr = usbgeckoWriteStr,
	.name = "USB Gecko",
	.driver = &usbgeckoDrv,
	.ansiEscSupport = true,
	.columns = 80,
	.rows = 25
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
		slot = (uint)-1;
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
	usbgeckoWriteChar('A' + (char)slot);
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
