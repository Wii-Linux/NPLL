/*
 * NPLL - Wii U Init
 *
 * Copyright (C) 2025 Techflash
 */

#include <npll/types.h>
#include <npll/regs.h>
#include <npll/console.h>
#include <npll/init.h>
#include <npll/timer.h>
#include <npll/drivers.h>
#include <npll/cpu.h>
#include <npll/latte/ipc.h>

enum wiiuRev H_WiiURev;


static void wiiuDebugStr(const char *str) {
	u32 msg;
	while (*str) {
		msg = LATTE_IPC_CMD_PRINT | (*str << 16);

		/* linux-loader sits on the "legacy" Hollywood IPC block */
		HW_IPC_PPCMSG = msg; /* set our data */
		HW_IPC_PPCCTRL = HW_IPC_PPCCTRL_X1; /* tell Starbuck we're ready */

		/* spin until Starbuck processes our message */
		while (HW_IPC_PPCCTRL & HW_IPC_PPCCTRL_X1);

		str++;
	}
}


static __attribute__((noreturn)) void wiiuPanic(const char *str) {
	int i;
	(void)str;

	wiiuDebugStr(str);

#if 0
	udelay(1000 * 2500);

	/* try a HW_RESETS reset */
	HW_RESETS |= ~RESETS_RSTBINB;
	udelay(1000 * 35);
	HW_RESETS &= ~RESETS_RSTBINB;
	udelay(1000 * 35);

	/* try a PI reset */
	PI_RESET = 0x00;
#endif

	/* wacky, just hang */
	while (1) {

	}
}

static struct platOps wiiuPlatOps = {
	.panic = wiiuPanic
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
		 *
		 * TODO: Use IOS exploits to set it?
		 * Would need to implement an IPC stack, and
		 * it assumes that we even *have* IOS...
		 */
		HW_AHBPROT = 0xfffffff;
		udelay(1000 * 10); /* give it a sec to stick */
		if (HW_AHBPROT != 0xffffffff)
			wiiuPanic("Can't turn on AHBPROT, cannot continue."); /* well crap */
	}

	/* we have AHBPROT, safe to continue */

	/* set plat ops */
	H_PlatOps = &wiiuPlatOps;

	/*
	 * Map lower MEM2
	 */

	/* BPRN = 0x10000000, WIMG=0000, PP=RW */
	batl = 0x10000002;

	/* BEPI = 0x90000000, BL=256MB, Vs=1, Vp=1 */
	batu = 0x90001fff;

	setbat(2, SETBAT_TYPE_BOTH, batu, batl);

	/* BPRN = 0x10000000, WIMG=0101, PP=RW */
	batl = 0x1000002a;

	/* BEPI = 0xd0000000, BL=256MB, Vs=1, Vp=1 */
	batu = 0xd0001fff;
	
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
	/* BPRN = 0x20000000, WIMG=0000, PP=RW */
	batl = 0x20000002;

	/* BEPI = 0xa0000000, BL=256MB, Vs=1, Vp=1 */
	batu = 0xa0001fff;

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
	I_InitCommon();

	wiiuPanic("I_InitCommon should not return");
	__builtin_unreachable();
}
