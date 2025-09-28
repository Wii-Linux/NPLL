/*
 * NPLL - Wii Init
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
#include <npll/hollywood/gpio.h>

enum wiiRev H_WiiRev;

static __attribute__((noreturn)) void wiiPanic(const char *str) {
	int i;
	(void)str;

	/* can't hurt to try... */
	HW_GPIO_OWNER = 0xffffffff;
	HW_GPIOB_DIR |= GPIO_SLOT_LED;

	/* 3x blink-blink-blink.... bliiiiiiink, reboot */
	for (i = 0; i < 3; i++) {
		HW_GPIOB_OUT |= GPIO_SLOT_LED;
		udelay(1000 * 100);
		HW_GPIOB_OUT &= ~GPIO_SLOT_LED;
		udelay(1000 * 100);

		HW_GPIOB_OUT |= GPIO_SLOT_LED;
		udelay(1000 * 100);
		HW_GPIOB_OUT &= ~GPIO_SLOT_LED;
		udelay(1000 * 100);

		HW_GPIOB_OUT |= GPIO_SLOT_LED;
		udelay(1000 * 100);
		HW_GPIOB_OUT &= ~GPIO_SLOT_LED;
		udelay(1000 * 100);

		HW_GPIOB_OUT |= GPIO_SLOT_LED;
		udelay(1000 * 1000);
		HW_GPIOB_OUT &= ~GPIO_SLOT_LED;
		udelay(1000 * 500);
	}

	/* try a HW_RESETS reset */
	HW_RESETS |= ~RESETS_RSTBINB;
	udelay(1000 * 35);
	HW_RESETS &= ~RESETS_RSTBINB;
	udelay(1000 * 35);

	/* try a PI reset */
	PI_RESET = 0x00;

	/* wacky, just hang */
	while (1) {

	}
}

static struct platOps wiiPlatOps = {
	.panic = wiiPanic
};

void __attribute__((noreturn)) H_InitWii(void) {
	u32 batl, batu, hid4;

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
			wiiPanic("Can't turn on AHBPROT, cannot continue."); /* well crap */
	}

	/* we have AHBPROT, safe to continue */

	/* set plat ops */
	H_PlatOps = &wiiPlatOps;

	/* set up basic GPIOs for panic indicator */
	HW_GPIO_OWNER |= GPIO_SLOT_LED;
	HW_GPIO_ENABLE |= GPIO_SLOT_LED;
	HW_GPIOB_DIR |= GPIO_SLOT_LED;

	/*
	 * Map MEM2
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
	 * NOTE: even though we don't actually use the higher BATs here,
	 * we need to write to HID4 for Dolphin to detect us as a Wii binary,
	 * and it's just common courtesy to do that for the kerenl before we load it
	 */
	hid4 = mfspr(HID4);
	hid4 |= HID4_SBE;
	mtspr(HID4, hid4);

	/* zero out the other BATs */
	batl = 0x00000000;
	batu = 0x00000000;

	setbat(4, SETBAT_TYPE_BOTH, batu, batl);
	setbat(5, SETBAT_TYPE_BOTH, batu, batl);
	setbat(6, SETBAT_TYPE_BOTH, batu, batl);
	setbat(7, SETBAT_TYPE_BOTH, batu, batl);


	/* we want to load Wii drivers */
	D_DriverMask = DRIVER_ALLOW_WII;

	/* kick off the real init */
	I_InitCommon();

	wiiPanic("I_InitCommon should not return");
	__builtin_unreachable();
}
