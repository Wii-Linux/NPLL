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
#include <npll/output.h>
#include <npll/cache.h>
#include <npll/tiny_usbgecko.h>
#include <npll/hollywood/gpio.h>
#include <stdio.h>
#include <string.h>

enum wiiRev H_WiiRev;

/* infloop: b [pc - 8] */
static const u32 bombVal = 0xeafffffc;

static void crashIOSAndFixupMEM2(void) {
	u32 srnprot, tmp, tmp2, tmp3, tmp4;
	vu32 *sram;
	puts("IOS detected, trying to nuke it out of existance...");

	printf("MEM_PROT_DDR: 0x%04x\n", HW_MEM_PROT_DDR);
	printf("MEM_PROT_DDR_BASE: 0x%04x\n", HW_MEM_PROT_DDR_BASE);
	printf("MEM_PROT_DDR_BASE (interpreted): 0x%08x\n", (HW_MEM_PROT_DDR_BASE << 12) | 0x10000000);
	printf("MEM_PROT_DDR_END: 0x%04x\n", HW_MEM_PROT_DDR_END);
	printf("MEM_PROT_DDR_END (interpreted): 0x%08x\n", (HW_MEM_PROT_DDR_END << 12) | 0x10000000);

	tmp = *(vu32 *)0xd3fffffc; sync(); barrier();
	tmp2 = *(vu32 *)0xd1000000; sync(); barrier();
	printf("end orig: 0x%08x\r\n", tmp);
	printf("start orig: 0x%08x\r\n", tmp2);
	*(vu32 *)0xd3fffffc = 0xdeadbeef; sync(); barrier();
	tmp3 = *(vu32 *)0xd3fffffc; sync(); barrier();
	tmp4 = *(vu32 *)0xd1000000; sync(); barrier();
	printf("end after: 0x%08x\r\n", tmp3);
	printf("start after: 0x%08x\r\n", tmp4);

	/*
	 * ensure no artifacts of memory protection show up:
	 *   - I/O to protected addresses hits bottom address
	 *   - writes to protected addresses do not appear
	 * so, check for:
	 *   - lower MEM2 unexpectedly changed
	 *   - write did not go through
	 */
	if ((tmp == tmp2 && tmp3 == tmp4) || tmp3 != 0xdeadbeef || tmp2 != tmp4)
		puts("test value write unsuccessful (expected)");
	else
		puts("test value write successful???");

	srnprot = HW_SRNPROT;
	printf("HW_SRNPROT: 0x%08x\r\n", srnprot);

	if (!(srnprot & 0x8)) {
		puts("no PPC SRAM access, enabling it");
		srnprot |= 0x8;
		HW_SRNPROT = srnprot;
	}

	sram = (vu32 *)0xcd400000;
	if (srnprot & 0x20) {
		while (sram != (vu32 *)0xcd418000) {
			*sram = bombVal;
			sram++;
		}
	}
	else {
		while (sram != (vu32 *)0xcd408000) {
			*sram = bombVal;
			sram++;
		}
		sram = (vu32 *)0xcd410000;
		while (sram != (vu32 *)0xcd420000) {
			*sram = bombVal;
			sram++;
		}
	}

	puts("IOS is now probably crashed, giving ourselves access to all of MEM2...");
	HW_MEM_PROT_SPL = 0;
	HW_MEM_PROT_SPL_BASE = 0;
	HW_MEM_PROT_SPL_END = 0;
	HW_MEM_PROT_DDR = 0;
	HW_MEM_PROT_DDR_BASE = 0;
	HW_MEM_PROT_DDR_END = 0;

	puts("Testing again...");

	tmp = *(vu32 *)0xd3fffffc; sync(); barrier();
	tmp2 = *(vu32 *)0xd1000000; sync(); barrier();
	printf("end orig: 0x%08x\r\n", tmp);
	printf("start orig: 0x%08x\r\n", tmp2);
	*(vu32 *)0xd3fffffc = 0xdeadbeef; sync(); barrier();
	tmp3 = *(vu32 *)0xd3fffffc; sync(); barrier();
	tmp4 = *(vu32 *)0xd1000000; sync(); barrier();
	printf("end after: 0x%08x\r\n", tmp3);
	printf("start after: 0x%08x\r\n", tmp4);

	/*
	 * ensure no artifacts of memory protection show up:
	 *   - I/O to protected addresses hits bottom address
	 *   - writes to protected addresses do not appear
	 * so, check for:
	 *   - lower MEM2 unexpectedly changed
	 *   - write did not go through
	 */
	if ((tmp == tmp2 && tmp3 == tmp4) || tmp3 != 0xdeadbeef || tmp2 != tmp4)
		panic("test value write unsuccessful??");
	else
		puts("test value write successful");
}

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
	.panic = wiiPanic,
	.debugWriteChar = NULL,
	.debugWriteStr = NULL
};

void __attribute__((noreturn)) H_InitWii(void) {
	u32 batl, batu, hid4, infohdr;

	/* set plat ops */
	H_PlatOps = &wiiPlatOps;

	/* debug console */
	H_TinyUGInit();

	/* we want to get logs out immediately for crashing IOS or failing to get AHBPROT */
	O_DebugInit();

	/* We'd better have AHBPROT... */
	if (HW_AHBPROT != 0xffffffff) {
		/*
		 * Uh oh.  This doesn't bode well.
		 * Unlikely, but depending on the config,
		 * we *might* just be able to set it and pray.
		 * Might as well try...
		 */
		HW_AHBPROT = 0xfffffff;
		udelay(1000 * 10); /* give it a sec to stick */
		if (HW_AHBPROT != 0xffffffff) {
			printf("failed to turn on AHBPROT, cur value = 0x%08x\r\n", HW_AHBPROT);
			panic("Can't turn on AHBPROT, cannot continue."); /* well crap */
		}
	}

	/* we have AHBPROT, safe to continue */

	/* set up SRAM access */
	HW_SRNPROT |= SRNPROT_AHPEN;



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


	/* now that we've mapped MEM2, check for MINI infohdr */
	/* MINI stores a pointer to the infohdr at the tail end of MEM2 */
	infohdr = *(vu32 *)0xd13ffffc;
	if (infohdr > 0x13fffffc || infohdr < 0x13f00000) {
		/* no valid infohdr pointer, must be IOS */
		crashIOSAndFixupMEM2();
	}
	else {
		/* keep digging... check if what it points to is a valid infohdr */
		if (memcmp((void *)(infohdr | 0xc0000000), "IPC", 3)) {
			/* it isn't, we must be running under IOS and got fooled by garbage data for the pointer */
			crashIOSAndFixupMEM2();
		}
	}

	/* got here with Starlet not getting in our way (MINI or crashed IOS), and MEM2 unrestricted (MINI or us) */

	/* we want to load Wii drivers */
	D_DriverMask = DRIVER_ALLOW_WII;

	/* kick off the real init */
	I_InitCommon();

	wiiPanic("I_InitCommon should not return");
	__builtin_unreachable();
}
