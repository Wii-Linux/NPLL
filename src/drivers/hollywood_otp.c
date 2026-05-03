/*
 * NPLL - Hollywood/Latte Hardware - OTP / eFuse memory
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "OTP"

#include <assert.h>
#include <errno.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/hollywood/otp.h>
#include <npll/regs.h>
#include <npll/irq.h>
#include <npll/utils.h>

static REGISTER_DRIVER(otpDrv);
struct otp H_OTPContents;

static void otpInit(void) {
	uint maxBank, bank, off;
	bool irqs = IRQ_DisableSave();
	if (H_ConsoleType == CONSOLE_TYPE_WII)
		maxBank = 1;
	else if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		LT_EFUSEPROT = 0;
		maxBank = 8;
	}
	else
		assert_unreachable();

	/* read the whole thing */
	for (bank = 0; bank < maxBank; bank++) {
		for (off = 0; off < 0x20; off++) {
			HW_OTP_COMMAND = HW_OTP_COMMAND_RD | (bank << HW_OTP_COMMAND_BANK_SHIFT) | off;
			sync();
			H_OTPContents.u32[(bank * 0x20) + off] = HW_OTP_DATA;
		}
	}

	IRQ_Restore(irqs);
	otpDrv.state = DRIVER_STATE_READY;
}

static void otpCleanup(void) {
	otpDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(otpDrv) = {
	.name = "Hollywood/Latte OTP / eFuse Memory",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_OTHER,
	.init = otpInit,
	.cleanup = otpCleanup
};
