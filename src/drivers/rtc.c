/*
 * NPLL - GameCube/Wii/Wii U RTC/SRAM/IPL/UART support
 *
 * Copyright (C) 2026 Techflash
 *
 * ROM reading derived from libogc2:
 * Copyright (C) 2004 - 2026
 * Michael Wiedenbauer (shagkur)
 * Dave Murphy (WinterMute)
 * Extrems' Corner.org
 */

#define MODULE "RTC"

#include <string.h>
#include <npll/allocator.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/drivers/exi.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/rtc.h>
#include <npll/types.h>

static REGISTER_DRIVER(rtcDrv);
extern struct exi_device_driver rtcEXIDriver;

/* the RTC is slow, and the register pokes are not performance critical */
#define RTC_REG_CLK_MHZ 2
#define RTC_ROM_CLK_MHZ 32

void H_RTCWriteReg(u32 reg, u32 val) {
	u32 cmd = RTC_WR | reg;
	bool irqs;

	irqs = IRQ_DisableSave();
	H_EXISelect(RTC_CHAN, RTC_CS, RTC_REG_CLK_MHZ);
	H_EXIWriteImm(RTC_CHAN, 4, &cmd);
	H_EXIWriteImm(RTC_CHAN, 4, &val);
	H_EXIDeselect(RTC_CHAN);
	IRQ_Restore(irqs);
}

u32 H_RTCReadReg(u32 reg) {
	bool irqs;
	u32 val;

	irqs = IRQ_DisableSave();
	H_EXISelect(RTC_CHAN, RTC_CS, RTC_REG_CLK_MHZ);
	H_EXIWriteImm(RTC_CHAN, 4, &reg);
	H_EXIReadImm(RTC_CHAN, 4, &val);
	H_EXIDeselect(RTC_CHAN);
	IRQ_Restore(irqs);

	return val;
}

static int romXfer(void *buf, u32 len, u32 offset) {
	int ret;
	u32 off;
	bool irqs;

	irqs = IRQ_DisableSave();
	H_EXISelect(RTC_CHAN, RTC_CS, RTC_ROM_CLK_MHZ);
	off = RTC_ROM_BASE + (offset << 6);
	ret = H_EXIWriteImm(RTC_CHAN, 4, &off);
	if (!ret)
		ret = H_EXIXferDMA(RTC_CHAN, buf, len, EXI_MODE_READ);
	H_EXIDeselect(RTC_CHAN);
	IRQ_Restore(irqs);

	return ret;
}

int H_RTCReadROM(void *buf, uint len, uint offset) {
	int ret = 0;
	u32 count;

	while (len > 0) {
		count = 1024 - (offset % 1024);
		count = (len > count) ? count : len;
		ret = romXfer(buf, count, offset);
		if (ret)
			return ret;
		offset += count;
		buf = (u8 *)buf + count;
		len -= count;
	}

	return ret;
}

static int rtcEXIProbe(struct exi_device *dev) {
	u8 ALIGN(32) ipl[256];
	uint len;

	if (dev->channel != RTC_CHAN || dev->cs != RTC_CS)
		return -1;

	dev->drv_data = &rtcDrv;
	rtcDrv.state = DRIVER_STATE_READY;

	/* revision / region */
	if (H_RTCReadROM(ipl, sizeof(ipl), 0))
		return 0;

	len = (uint)strnlen((char *)(ipl + 0x55), sizeof(ipl) - 0x55);
	free(H_GCNIPLRev);
	H_GCNIPLRev = malloc(len + 1);
	if (!H_GCNIPLRev)
		return 0;
	memcpy(H_GCNIPLRev, ipl + 0x55, len);
	H_GCNIPLRev[len] = '\0';

	return 0;
}

static void rtcEXIRemove(struct exi_device *dev) {
	dev->drv_data = NULL;
	rtcDrv.state = DRIVER_STATE_NO_HARDWARE;
}

struct exi_device_driver rtcEXIDriver = {
	.name = "RTC",
	.probe = rtcEXIProbe,
	.remove = rtcEXIRemove,
};

static void rtcInit(void) {
	if (exiDrv.state != DRIVER_STATE_READY) {
		rtcDrv.state = DRIVER_STATE_NEED_DEP;
		return;
	}

	(void)H_EXIRegisterDriver(&rtcEXIDriver);
	if (rtcDrv.state != DRIVER_STATE_READY)
		rtcDrv.state = DRIVER_STATE_NO_HARDWARE;
}

static void rtcCleanup(void) {
	H_EXIUnregisterDriver(&rtcEXIDriver);
	rtcDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(rtcDrv) = {
	.name = "RTC",
	.mask = DRIVER_ALLOW_ALL,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_CRITICAL,
	.init = rtcInit,
	.cleanup = rtcCleanup
};
