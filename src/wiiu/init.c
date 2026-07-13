/*
 * NPLL - Wii U Init
 *
 * Copyright (C) 2025 Techflash
 */

#include <npll/types.h>
#include <npll/soc.h>
#include <npll/console.h>
#include <npll/init.h>
#include <npll/timer.h>
#include <npll/utils.h>
#include <npll/drivers.h>
#include <npll/cpu.h>
#include <npll/latte/ipc.h>

enum wiiuRev H_WiiURev;


static void wiiuDebugChar(const char c) {
	u32 msg;

	msg = LATTE_IPC_CMD_PRINT | ((u32)c << 16);

	/* linux-loader sits on the "legacy" Hollywood IPC block */
	HW_IPC_PPCMSG = msg; /* set our data */
	HW_IPC_PPCCTRL = HW_IPC_PPCCTRL_X1; /* tell Starbuck we're ready */

	/* spin until Starbuck processes our message */
	while (HW_IPC_PPCCTRL & HW_IPC_PPCCTRL_X1);
}

static void wiiuDebugStr(const char *str) {
	while (*str) {
		wiiuDebugChar(*str);
		str++;
	}
}

static __attribute__((noreturn)) void wiiuReboot(void) {
	HW_IPC_PPCMSG = LATTE_IPC_CMD_REBOOT; /* set our data */
	HW_IPC_PPCCTRL = HW_IPC_PPCCTRL_X1; /* tell Starbuck we're ready */

	while (1); /* hang in the hopes that it'll eventually process it */
}

static __attribute__((noreturn)) void wiiuShutdown(void) {
	HW_IPC_PPCMSG = LATTE_IPC_CMD_POWEROFF; /* set our data */
	HW_IPC_PPCCTRL = HW_IPC_PPCCTRL_X1; /* tell Starbuck we're ready */

	while (1); /* hang in the hopes that it'll eventually process it */
}

static __attribute__((noreturn)) void wiiuPanic(const char *str) {
	(void)str;

	wiiuDebugStr("PANIC: ");
	wiiuDebugStr(str);

	udelay(10 * 1000 * 1000);

	wiiuReboot();
}

static struct platOps wiiuPlatOps = {
	.panic = wiiuPanic,
	.debugWriteChar = wiiuDebugChar,
	.debugWriteStr = wiiuDebugStr,
	.reboot = wiiuReboot,
	.shutdown = wiiuShutdown,
	.exit = NULL,
	.ejectDisc = NULL, /* maybe once we do an entire AHCI stack.... */
};

void __attribute__((noreturn)) H_InitWiiU(void) {
	u32 batl, batu, hid4;

	wiiuDebugStr("Hello from NPLL in H_InitWiiU\n");

	/* We'd better have AHBPROT... */
	if (HW_AHBPROT != 0xffffffff) {
		/*
		 * Uh oh.  This doesn't bode well.
		 * Unlikely, but depending on the config,
		 * we *might* just be able to set it and pray.
		 * Might as well try...
		 */
		HW_AHBPROT = 0xffffffff;
		if (HW_AHBPROT != 0xffffffff)
			wiiuPanic("Can't turn on AHBPROT, cannot continue."); /* well crap */
	}

	/* we have AHBPROT, safe to continue */

	/* set up SRAM access */
	HW_SRNPROT |= SRNPROT_AHPEN;

	/* set plat ops */
	H_PlatOps = &wiiuPlatOps;

	/*
	 * Map lower MEM2
	 */

	/* BPRN = MEM2_PHYS_BASE, WIMG=0000, PP=RW */
	batl = MEM2_PHYS_BASE | 2;

	/* BEPI = MEM2_CACHED_BASE, BL=256MB, Vs=1, Vp=1 */
	batu = MEM2_CACHED_BASE | 0x1fff;

	setbat(2, SETBAT_TYPE_BOTH, batu, batl);

	/* BPRN = MEM2_PHYS_BASE, WIMG=0101, PP=RW */
	batl = MEM2_PHYS_BASE | 0x2a;

	/* BEPI = MEM2_UNCACHED_BASE, BL=256MB, Vs=1, Vp=1 */
	batu = MEM2_UNCACHED_BASE | 0x1fff;

	setbat(3, SETBAT_TYPE_BOTH, batu, batl);


	/*
	 * Set up HID4 to set SBE so we can access more BATs
	 */
	hid4 = mfspr(HID4);
	hid4 |= HID4_SBE;
	mtspr(HID4, hid4);

	/* immediately zero out the other BATs */
	batl = 0x00000000;
	batu = 0x00000000;

	setbat(4, SETBAT_TYPE_BOTH, batu, batl);
	setbat(5, SETBAT_TYPE_BOTH, batu, batl);
	setbat(6, SETBAT_TYPE_BOTH, batu, batl);
	setbat(7, SETBAT_TYPE_BOTH, batu, batl);

	/*
	 * map higher portions of MEM2
	 */
	/* BPRN = second 256MB of MEM2, WIMG=0000, PP=RW */
	batl = (MEM2_PHYS_BASE + 0x10000000) | 2;

	/* BEPI = second 256MB of cached MEM2, BL=256MB, Vs=1, Vp=1 */
	batu = (MEM2_CACHED_BASE + 0x10000000) | 0x1fff;

	setbat(4, SETBAT_TYPE_BOTH, batu, batl);
	batl += 0x10000000; batu += 0x10000000;
	setbat(5, SETBAT_TYPE_BOTH, batu, batl);
	/*
	 * we can't map anything later due to intersecting w/ uncached MEM1+MMIO
	 * so this only gives us the first 768MB of MEM2.... that's still plenty
	 * though.
	 */


	/* we want to load Wii U drivers */
	D_DriverMask = DRIVER_ALLOW_WIIU;

	/* kick off the real init */
	wiiuDebugStr("About to enter I_InitCommon\n");
	I_InitCommon();

	wiiuPanic("I_InitCommon should not return");
	__builtin_unreachable();
}
