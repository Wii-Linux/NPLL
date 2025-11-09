/*
 * NPLL - Wii U DRC (GamePad) text over linux-loader IPC
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <string.h>
#include <npll/drivers.h>
#include <npll/output.h>
#include <npll/regs.h>
#include <npll/latte/ipc.h>

static REGISTER_DRIVER(drcDrv);

static void drcWriteChar(char c) {
	u32 msg;
	msg = LATTE_IPC_CMD_PRINT | (c << 16);

	/* linux-loader sits on the "legacy" Hollywood IPC block */
	HW_IPC_PPCMSG = msg; /* set our data */
	HW_IPC_PPCCTRL = HW_IPC_PPCCTRL_X1; /* tell Starbuck we're ready */

	/* spin until Starbuck processes our message */
	while (HW_IPC_PPCCTRL & HW_IPC_PPCCTRL_X1);
}

static void drcWriteStr(const char *str) {
	u32 msg, tmp, len = strlen(str);

	/* linux-loader IPC can write 3 characters at a time */
	while (len) {
		if (len >= 3)
			tmp = 3;
		else
			tmp = len;
		msg = LATTE_IPC_CMD_PRINT;
		switch (tmp) {
		case 3: {
			msg |= (str[0] << 16) | (str[1] << 8) | str[2];
			break;
		}
		case 2: {
			msg |= (str[0] << 16) | (str[1] << 8);
			break;
		}
		case 1: {
			msg |= (str[0] << 16);
			break;
		}
		default: {
			/* we should not get here... */
			break;
		}
		}
		HW_IPC_PPCMSG = msg;
		HW_IPC_PPCCTRL = HW_IPC_PPCCTRL_X1;

		while (HW_IPC_PPCCTRL & HW_IPC_PPCCTRL_X1);

		len -= tmp;
		str += tmp;
	}
}

static const struct outputDevice outDev = {
	.writeChar = drcWriteChar,
	.writeStr = drcWriteStr,
	.name = "Wii U GamePad Text Console",
	.driver = &drcDrv,
	.isGraphical = true,
	.rows = 896 / 8, /* screen width / linux-loader font width */
	.columns = 504 / 8 /* screen height / linux-loader font height */
};

static void drcInit(void) {
	/* we're all good */
	drcDrv.state = DRIVER_STATE_READY;

	drcWriteStr("Wii U GamePad Text Console driver is now enabled\n");
	O_AddDevice(&outDev);
	
}

static REGISTER_DRIVER(drcDrv) = {
	.name = "Wii U GamePad Text Console",
	.mask = DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_CRITICAL,
	.init = drcInit,
	.cleanup = NULL
};
