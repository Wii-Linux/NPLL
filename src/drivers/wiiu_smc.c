/*
 * NPLL - Wii U system-management controller
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "SMC"

#include <errno.h>
#include <npll/drivers.h>
#include <npll/i2c.h>
#include <npll/input.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/latte/smc.h>

static REGISTER_DRIVER(smcDrv);

#define SMC_POLL_US             20000

static u8 previousEvents;
static bool pollErrorLogged;
u8 H_WiiUSMCFWRev = 0, H_WiiUSMCChipRev = 0;

static int smcReadRegister(u8 reg, u8 *value) {
	struct i2cMsg msgs[2] = {
		{
			.addr = SMC_ADDRESS,
			.flags = 0,
			.buf = &reg,
			.len = 1,
		},
		{
			.addr = SMC_ADDRESS,
			.flags = I2C_MSG_READ,
			.buf = value,
			.len = 1,
		},
	};
	int ret = I2C_Transfer(I2C_BUS_SMC, msgs, 2);

	if (ret < 0)
		return ret;
	return ret == 2 ? 0 : -EIO;
}

static void smcPoll(void) {
	u8 events, pressed;
	int ret = smcReadRegister(SMC_REG_SYSTEM_EVENT, &events);

	if (ret) {
		if (!pollErrorLogged) {
			log_printf("SMC event read failed: %d\r\n", ret);
			pollErrorLogged = true;
		}
		return;
	}
	pollErrorLogged = false;

	pressed = (events & ~previousEvents) & SMC_EVENT_BUTTONS;
	if (pressed & SMC_EVENT_EJECT_BUTTON)
		IN_NewEvent(INPUT_EV_SELECT);
	if (pressed & SMC_EVENT_POWER_BUTTON)
		IN_NewEvent(INPUT_EV_DOWN);
	if ((events ^ previousEvents) & SMC_EVENT_DISC_INSERT)
		log_printf("SMC disc %s\r\n",
			events & SMC_EVENT_DISC_INSERT ? "inserted" : "removed");

	previousEvents = events;
}

static void smcPollTimer(void *data) {
	(void)data;
	smcPoll();
}

static void smcInit(void) {
	u8 oddFlag, events;
	int ret;

	ret = smcReadRegister(SMC_REG_PROGRAM_REV, &H_WiiUSMCFWRev);
	if (ret) {
		log_printf("SMC probe failed: %d\r\n", ret);
		smcDrv.state = DRIVER_STATE_FAULTED;
		return;
	}
	ret = smcReadRegister(SMC_REG_CHIP_REV, &H_WiiUSMCFWRev);
	if (ret) {
		log_printf("SMC chip revision read failed: %d\r\n", ret);
		smcDrv.state = DRIVER_STATE_FAULTED;
		return;
	}
	ret = smcReadRegister(SMC_REG_ODD_FLAG, &oddFlag);
	if (ret) {
		log_printf("SMC optical-drive flag read failed: %d\r\n", ret);
		smcDrv.state = DRIVER_STATE_FAULTED;
		return;
	}
	ret = smcReadRegister(SMC_REG_SYSTEM_EVENT, &events);
	if (ret) {
		log_printf("SMC system-event read failed: %d\r\n", ret);
		smcDrv.state = DRIVER_STATE_FAULTED;
		return;
	}

	log_printf("SMC detected: program rev=%02x, chip rev=%02x, ODD=%02x, events=%02x\r\n",
		H_WiiUSMCFWRev, H_WiiUSMCChipRev, oddFlag, events);

	previousEvents = events;
	pollErrorLogged = false;
	T_QueueRepeatingEvent(SMC_POLL_US, smcPollTimer, NULL);
	smcDrv.state = DRIVER_STATE_READY;
}

static void smcCleanup(void) {
	T_CancelRepeatingEvent(smcPollTimer, NULL);
	smcDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(smcDrv) = {
	.name = "Wii U System Management Controller",
	.mask = DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_INPUT,
	.init = smcInit,
	.cleanup = smcCleanup,
};
