/*
 * NPLL - Wii Init
 *
 * Copyright (C) 2025-2026 Techflash
 *
 * Code to load MINI derived from ARMBootNow:
 * Copyright (c) 2024 Emma / InvoxiPlayGames
 */

#define MODULE "Wii"

#include <npll/types.h>
#include <npll/regs.h>
#include <npll/console.h>
#include <npll/init.h>
#include <npll/timer.h>
#include <npll/drivers.h>
#include <npll/cpu.h>
#include <npll/output.h>
#include <npll/irq.h>
#include <npll/cache.h>
#include <npll/tiny_usbgecko.h>
#include <npll/hollywood/gpio.h>
#include <npll/log.h>
#include <string.h>
#include <npll/utils.h>
#include "../armboot_bin.h"
#include "ios_ipc.h"
#include "ios_es.h"
#include "mini_ipc.h"

enum wiiRev H_WiiRev = 0;
int H_WiiIsvWii = 0;
int H_WiiBootIOS = -1;

/* IOS9 is present from pre-launch sysmenu Wiis up to fully updated, even vWii */
#define IOS_VALID_GUESS 9

extern int IOS_DevShaExploit(void);

#define MINI_BOOT_MAGIC_PTR (*(vu32 *)(MEM1_UNCACHED_BASE + 0xfffff0))
/* 'NRST' */
#define MINI_NO_RESET_MAGIC 0x4e525354

static void fixupMEM2(void) {
	u32 low_orig, low_after, high_orig, high_after;

	HW_MEM_PROT_SPL = 0;
	HW_MEM_PROT_SPL_BASE = 0;
	HW_MEM_PROT_SPL_END = 0;
	HW_MEM_PROT_DDR = 0;
	HW_MEM_PROT_DDR_BASE = 0;
	HW_MEM_PROT_DDR_END = 0;

	high_orig = *(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII); sync(); barrier();
	low_orig = *(vu32 *)(MEM2_UNCACHED_BASE); sync(); barrier();
	*(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII) = 0xdeadbeef; sync(); barrier();
	high_after = *(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII); sync(); barrier();
	low_after = *(vu32 *)(MEM2_UNCACHED_BASE); sync(); barrier();

	/*
	 * ensure no artifacts of memory protection show up:
	 *   - I/O to protected addresses hits bottom address
	 *   - writes to protected addresses do not appear
	 * so, check for:
	 *   - lower MEM2 unexpectedly changed
	 *   - write did not go through
	 */
	if ((high_orig == low_orig && high_after == low_after) || high_after != 0xdeadbeef || low_orig != low_after) {
		_log_puts("unsuccessful???");
		log_printf("low orig: 0x%08x\r\n", low_orig);
		log_printf("high orig: 0x%08x\r\n", high_orig);
		log_printf("low after: 0x%08x\r\n", low_after);
		log_printf("high after: 0x%08x\r\n", high_after);
		panic("Could not unlock MEM2");
	}
	else
		_log_puts("success");
}

static void crashIOSAndFixupMEM2(void) {
	u32 srnprot, trampoline_pointer, trampoline_addr;
	vu32 *sram, *armbuf;
	/* put them in low MEM1 */
	tikview_t tikviews[4] ALIGN(32);
	int i, trampoline_off, iosVer;
	u32 num_tikviews ALIGN(32);
	u64 titleID;
	iosVer = IOS_GetVersion();
	titleID = TITLE_ID_IOS | iosVer;

	H_WiiBootIOS = iosVer;
	log_printf("IOS (%d) detected, trying to load MINI\r\n", iosVer);
	if (iosVer <= 0) {
		log_printf("IOS version in lowmem is bogus, guessing that IOS%d should exist\r\n", IOS_VALID_GUESS);
		iosVer = IOS_VALID_GUESS;
	}

	srnprot = HW_SRNPROT;
	log_printf("HW_SRNPROT: 0x%08x", srnprot);

	if (!(srnprot & SRNPROT_AHPEN)) {
		_log_puts("; no PPC SRAM access, enabling it");
		srnprot |= SRNPROT_AHPEN;
		HW_SRNPROT = srnprot;
	}
	else
		_log_puts("");

	sram = (vu32 *)(UNCACHED_BASE + 0x0d410000);
	armbuf = (vu32 *)(MEM2_CACHED_BASE + 0x01000000);
	IOS_Reset();
	ES_Init();

	/* copy it into MEM2 */
	memcpy((void *)armbuf, __mini_armboot_bin_data, __mini_armboot_bin_size);
	dcache_flush((void *)armbuf, __mini_armboot_bin_size);

	/* find the "mov pc, r0" trampoline used to launch a new IOS image */
	trampoline_addr = 0;
	for (i = 0; i < 0x1000; i++) {
		if (sram[i] == 0xE1A0F000) {
			trampoline_addr = 0xFFFF0000 + (i * 4);
			log_printf("found LaunchIOS trampoline at %08x\n", trampoline_addr);
			break;
		}
	}

	/*
	 * if we found it, find the pointer to aforementioned trampoline,
	 * this is called in the function that launches the next kernel
	 */
	trampoline_pointer = 0;
	trampoline_off = 0;
	if (trampoline_addr != 0) {
		for (i = 0; i < 0x1000; i++) {
			if (sram[i] == trampoline_addr) {
				trampoline_pointer = 0xFFFF0000 + (i * 4);
				trampoline_off = i;
				log_printf("found LaunchIOS trampoline pointer at 0x%08x/0x%08x\r\n", trampoline_pointer, &sram[i]);
				break;
			}
		}
	}

	/* write the pointer to our code there instead */
	sram[trampoline_off] = (u32)virtToPhys(armbuf) + armbuf[0];
	log_printf("set trampoline ptr to %08x\r\n", sram[trampoline_off]);

	/* tell MINI not to reset us pretty please */
	MINI_BOOT_MAGIC_PTR = MINI_NO_RESET_MAGIC;

	/* ES stuff so we can reload */
	log_puts("Reloading...");
	if (ES_GetTikViewsCount(titleID, &num_tikviews))
		panic("ES_GetTikViewsCount failed");
	log_printf("Number of tikviews for IOS%d: %d\r\n", iosVer, num_tikviews);
	if (ES_GetTikViews(titleID, tikviews, num_tikviews))
		panic("ES_GetTikViews failed");
	log_puts("Got tikviews, launching...");

	/* actually kick off the reload */
	/* we don't check if this fails because it *will* time out... */
	ES_LaunchTitle(titleID, &tikviews[0]);

	log_printf("IOS is now replaced with MINI, enabling full MEM2 access... ");
	fixupMEM2();

	/* give MINI a bit to start up, we don't want to race it */
	udelay(250 * 1000);
}

static __attribute__((noreturn)) void wiiShutdown(void) {
	HW_GPIO_OWNER |= GPIO_SHUTDOWN;
	HW_GPIO_DIR |= GPIO_SHUTDOWN;
	HW_GPIOB_OUT |= GPIO_SHUTDOWN;

	while (1); /* weird... */
}

static __attribute__((noreturn)) void wiiReboot(void) {
	/* try a HW_RESETS reset */
	HW_RESETS |= RESETS_RSTBINB;
	udelay(1000 * 35);
	HW_RESETS &= ~RESETS_RSTBINB;
	udelay(1000 * 35);

	/* try a PI reset */
	PI_RESET = 0x00;

	/* wacky, just hang */
	while (1);
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

	wiiReboot();
}

static void __attribute__((noreturn)) wiiExit(void) {
	u32 iosVer = H_WiiBootIOS;
	int i = 0;
	void (*stub)(void);

	IRQ_Disable();
	if (iosVer <= 0)
		iosVer = IOS_VALID_GUESS;

	log_printf("Trying to reload into IOS%d\r\n", iosVer);
	*(u32 *)(MEM1_UNCACHED_BASE + 0x3140) = 0;
	MINI_IPCPost(IPC_MINI_CODE_BOOT2_RUN, 0, 2, TITLE_ID_IOS, iosVer);
	log_puts("Waiting for IOS...");

	while ((u32)IOS_GetVersion() != iosVer) {
		udelay(1000);
		i++;
		if (i >= 1000)
			panic("IOS reload stuck");
	}

	/* wait for PPCCTRL to match expected value */
	i = 0;
	while (!(HW_IPC_PPCCTRL & (HW_IPC_PPCCTRL_Y2 | HW_IPC_PPCCTRL_Y1))) {
		udelay(1000);
		i++;
		if (i >= 1000)
			panic("IOS reload succeeded but stuck waiting for IPC");
	}

	/* IOS is reloaded and ready, hand off to the stub! */
	stub = (void (*)(void))(MEM1_CACHED_BASE + 0x1800);
	stub();
	__builtin_unreachable();
}

static struct platOps wiiPlatOps = {
	.panic = wiiPanic,
	.debugWriteChar = NULL,
	.debugWriteStr = NULL,
	.reboot = wiiReboot,
	.shutdown = wiiShutdown,
	.exit = NULL
};

void __attribute__((noreturn)) H_InitWii(void) {
	u32 batl, batu, hid4, infohdr;
	int error;

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
			log_printf("failed to turn on AHBPROT manually, cur value = 0x%08x\r\n", HW_AHBPROT);
			error = IOS_DevShaExploit();
			if (error || HW_AHBPROT != 0xffffffff) {
				log_printf("failed to turn on AHBPROT with IOS exploit, cur value = 0x%08x\r\n", HW_AHBPROT);
				panic("Can't turn on AHBPROT, cannot continue."); /* well crap */
			}
			else
				log_printf("Successfully enabled HW_AHBPROT with IOS exploit, cur value = 0x%08x\r\n", HW_AHBPROT);
		}
	}

	/* we have AHBPROT, safe to continue */

	/* set up SRAM access */
	HW_SRNPROT |= SRNPROT_AHPEN;

	/* we can only access this after we've gained some perms in AHBPROT */
	if ((LT_CHIPREVID & 0xffff0000) == 0xcafe0000) {
		H_WiiIsvWii = 1;
		log_puts("Detected Wii U vWii");
	}

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
	infohdr = *(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII);
	if (infohdr > MEM2_PHYS_BASE || infohdr < MEM2_PHYS_BASE + MEM2_SIZE_WII) {
		/* no valid infohdr pointer, must be IOS */
		log_printf("No valid MINI infohdr pointer (got 0x%08x), must be IOS\r\n", infohdr);
		crashIOSAndFixupMEM2();
	}
	else {
		/* keep digging... check if what it points to is a valid infohdr */
		if (memcmp(physToUncached(infohdr), "IPC", 3)) {
			/* it isn't, we must be running under IOS and got fooled by garbage data for the pointer */
			log_printf("supposed MINI infohdr pointer (0x%08x) has invalid magic, must be IOS\r\n", infohdr);
			crashIOSAndFixupMEM2();
		}
	}

	/* ASCII 'STUBHAXX' */
	if ((*(vu64 *)(MEM1_UNCACHED_BASE + 0x1804)) == 0x5354554248415858ULL) {
		log_puts("Detected HBC-complatible reload stub");
		H_PlatOps->exit = wiiExit;
	}

	/* got here with Starlet not getting in our way (MINI or crashed IOS), and MEM2 unrestricted (MINI or us) */

	/* we want to load Wii drivers */
	D_DriverMask = DRIVER_ALLOW_WII;

	/* final sanity check, these should pass if all went well; don't use assert() since it gets compiled out of release builds */
	if (HW_AHBPROT != 0xffffffff)
		panic("AHBPROT not enabled at end of H_InitWii");
	fixupMEM2(); /* will panic itself on fail */

	/* kick off the real init */
	I_InitCommon();

	wiiPanic("I_InitCommon should not return");
	__builtin_unreachable();
}
