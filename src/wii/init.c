/*
 * NPLL - Wii Init
 *
 * Copyright (C) 2025-2026 Techflash
 *
 * Code to load MINI derived from ARMBootNow:
 * Copyright (c) 2024 Emma / InvoxiPlayGames
 */

#define MODULE "Wii"

#include <assert.h>
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
#include <npll/hollywood/ohci.h>
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
u64 H_WiiBootTitleID = 0;
void *H_WiiMEM2Top = NULL;

/* IOS9 is present from pre-launch sysmenu Wiis up to fully updated, even vWii */
#define IOS_VALID_GUESS 9

extern int IOS_DevShaExploit(void);

#define MINI_BOOT_MAGIC_PTR (*(vu32 *)(MEM1_UNCACHED_BASE + 0xfffff0))
/* 'NRST' */
#define MINI_NO_RESET_MAGIC 0x4e525354

static bool testMEM2(void) {
	u32 low_orig, low_after, high_orig, high_after;

	high_orig = *(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4); sync(); barrier();
	low_orig = *(vu32 *)(MEM2_UNCACHED_BASE); sync(); barrier();
	*(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4) = 0xdeadbeef; sync(); barrier();
	high_after = *(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4); sync(); barrier();
	low_after = *(vu32 *)(MEM2_UNCACHED_BASE); sync(); barrier();

	/* restore the original high value, that may be a MINI infohdr ptr */
	*(vu32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4) = high_orig;
	sync(); barrier();

	/*
	 * ensure no artifacts of memory protection show up:
	 *   - I/O to protected addresses hits bottom address
	 *   - writes to protected addresses do not appear
	 * so, check for:
	 *   - lower MEM2 unexpectedly changed
	 *   - write did not go through
	 */
	if ((high_orig == low_orig && high_after == low_after) || high_after != 0xdeadbeef || low_orig != low_after) {
		#if 0
		_log_puts("unsuccessful???");
		log_printf("low orig: 0x%08x\r\n", low_orig);
		log_printf("high orig: 0x%08x\r\n", high_orig);
		log_printf("low after: 0x%08x\r\n", low_after);
		log_printf("high after: 0x%08x\r\n", high_after);
		panic("Could not unlock MEM2");
		#endif

		return false;
	}
	else
		return true;
}

static void armbootnow(void) {
	u32 srnprot, trampoline_pointer, trampoline_addr;
	vu32 *sram, *armbuf;
	/* put them in low MEM1 */
	tikview_t tikviews[4] ALIGN(32);
	int i, trampoline_off, iosVer;
	uint retries = 10;
	u32 num_tikviews ALIGN(32);
	u64 titleID;
	iosVer = IOS_GetVersion();
	titleID = TITLE_ID_IOS | (u8)iosVer;

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

	/* copy it into MEM2 */
	memcpy((void *)armbuf, __mini_armboot_bin_data, (uint)__mini_armboot_bin_size);
	dcache_flush((void *)armbuf, (uint)__mini_armboot_bin_size);

	/* find the "mov pc, r0" trampoline used to launch a new IOS image */
	trampoline_addr = 0;
	for (i = 0; i < 0x1000; i++) {
		if (sram[i] == 0xE1A0F000u) {
			trampoline_addr = 0xFFFF0000u + (uint)(i * 4);
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
				trampoline_pointer = 0xFFFF0000u + (uint)(i * 4);
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
	while (retries) {
		retries--;
		udelay(20 * 1000);

		if (ES_GetTikViewsCount(titleID, &num_tikviews)) {
			if (!retries)
				panic("ES_GetTikViewsCount failed");
			else {
				IOS_Reset();
				ES_Init();
				continue;
			}
		}
		log_printf("Number of tikviews for IOS%d: %d\r\n", iosVer, num_tikviews);
		if (ES_GetTikViews(titleID, tikviews, num_tikviews)) {
			if (!retries)
				panic("ES_GetTikViews failed");
			else {
				IOS_Reset();
				ES_Init();
				continue;
			}
		}
		log_puts("Got tikviews, launching...");

		/* actually kick off the reload */
		if (ES_LaunchTitle(titleID, &tikviews[0]) != -1) {
			if (!retries)
				panic("ES_LaunchTitle failed with none-timeout error");
			else {
				IOS_Reset();
				ES_Init();
				continue;
			}
		}
		break;
	}


	log_puts("IOS should now be replaced with MINI");
}

static __attribute__((noreturn)) void wiiShutdown(void) {
	MINI_BOOT_MAGIC_PTR = 0;
	HW_GPIO_OWNER |= GPIO_SHUTDOWN;
	HW_GPIO_DIR |= GPIO_SHUTDOWN;
	HW_GPIOB_OUT |= GPIO_SHUTDOWN;

	while (1); /* weird... */
}

static __attribute__((noreturn)) void wiiReboot(void) {
	MINI_BOOT_MAGIC_PTR = 0;
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
	MINI_BOOT_MAGIC_PTR = 0;

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
	u32 iosVer = (u32)H_WiiBootIOS;
	int i = 0;
	void (*stub)(void);
	struct ipc_request_mini req;

	IRQ_Disable();
	if (iosVer <= 0)
		iosVer = IOS_VALID_GUESS;

	log_printf("Trying to reload into IOS%d\r\n", iosVer);
	*(u32 *)(MEM1_UNCACHED_BASE + 0x3140) = 0;
	MINI_IPCExchange(&req, IPC_MINI_CODE_BOOT2_RUN, 5, 5, 2, 1, iosVer);
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

enum wiiInitState {
	STATE_ANALYZE,
	STATE_ANALYZE_IOS,
	STATE_IOS_INIT,
	STATE_IOS_PRIV_ESC,
	STATE_IOS_ARMBOOTNOW,
	STATE_HW_UNRESTRICT,
	STATE_ANALYZE_MINI,
	STATE_MINI_INIT,
	STATE_MINI_RELOAD,
	STATE_MINI_PRIV_ESC,
	STATE_POST_IOS_SANITIZE,
	STATE_READY
};

#define GOTO_STATE(x) { \
	prevStateStr = stateStr; \
	stateStr = __stringify(x); \
	state = x; \
}

#define SFLAG_AHBPROT_PERMS BIT(0)
#define SFLAG_SRNPROT_PERMS BIT(1)
#define SFLAG_MEM2_ACCESS   BIT(2)
#define SFLAG_ORIG_MINI     BIT(3)
#define SFLAG_CUR_MINI      BIT(4)
#define SFLAG_RAN_DEVSHA    BIT(5)
#define SFLAG_RAN_ABN       BIT(6)
#define SFLAG_IOS_INIT      BIT(7)
#define SFLAG_MINI_INIT     BIT(8)
#define SFLAG_MINI_RELOADED BIT(9)

#define SET_SFLAG(flag, cond) { \
	if ((cond)) \
		stateFlags |= ((u16)(flag)); \
	else \
		stateFlags &= (u16)~((u16)(flag)); \
}

#define GET_SFLAG(flag) !!(stateFlags & (flag))

#define DUMP_STATE() \
	log_printf("H_InitWii: state %s -> %s\r\n", prevStateStr, stateStr); \
	log_printf("H_InitWii: ahb=%d, srn=%d, mem2=%d, origMINI=%d, curMINI=%d, devsha=%d, ABN=%d, miniInit=%d, miniRld=%d\r\n", \
		GET_SFLAG(SFLAG_AHBPROT_PERMS), GET_SFLAG(SFLAG_SRNPROT_PERMS), GET_SFLAG(SFLAG_MEM2_ACCESS), \
		GET_SFLAG(SFLAG_ORIG_MINI), GET_SFLAG(SFLAG_CUR_MINI), GET_SFLAG(SFLAG_RAN_DEVSHA), GET_SFLAG(SFLAG_RAN_ABN), \
		GET_SFLAG(SFLAG_MINI_INIT), GET_SFLAG(SFLAG_MINI_RELOADED));

void __attribute__((noreturn)) H_InitWii(void) {
	u64 tb;
	u32 batl, batu, hid4;
	vu32 *armbuf;
	enum wiiInitState state;
	enum MINI_Err miniErr;
	const char *stateStr, *prevStateStr;
	u16 stateFlags;

	/* set plat ops */
	H_PlatOps = &wiiPlatOps;

	/* debug console */
	H_TinyUGInit();

	/* we want to get logs out immediately for crashing IOS or failing to get AHBPROT */
	O_DebugInit();

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

	/* seed initial state */
	stateStr = "None";
	stateFlags = 0;
	GOTO_STATE(STATE_ANALYZE);
	SET_SFLAG(SFLAG_AHBPROT_PERMS, HW_AHBPROT == 0xffffffff);
	SET_SFLAG(SFLAG_SRNPROT_PERMS, HW_SRNPROT & SRNPROT_AHPEN);
	SET_SFLAG(SFLAG_MEM2_ACCESS, testMEM2());
	SET_SFLAG(SFLAG_ORIG_MINI, GET_SFLAG(SFLAG_MEM2_ACCESS) ? MINI_ValidInfoHdr() == MINI_OK : false);
	SET_SFLAG(SFLAG_CUR_MINI, GET_SFLAG(SFLAG_ORIG_MINI));

	while (true) {
		DUMP_STATE();

		switch (state) {
		case STATE_ANALYZE: {
			SET_SFLAG(SFLAG_AHBPROT_PERMS, HW_AHBPROT == 0xffffffff);
			SET_SFLAG(SFLAG_SRNPROT_PERMS, HW_SRNPROT & SRNPROT_AHPEN);
			SET_SFLAG(SFLAG_MEM2_ACCESS, testMEM2());
			SET_SFLAG(SFLAG_CUR_MINI, GET_SFLAG(SFLAG_MEM2_ACCESS) ? MINI_ValidInfoHdr() == MINI_OK : false);

			if (!GET_SFLAG(SFLAG_CUR_MINI)) {
				GOTO_STATE(STATE_ANALYZE_IOS);
				break;
			}
			else {
				GOTO_STATE(STATE_ANALYZE_MINI);
				break;
			}
		}
		case STATE_ANALYZE_IOS: {
			assert(!GET_SFLAG(SFLAG_CUR_MINI));

			/* prevent infinite loops */
			if (GET_SFLAG(SFLAG_RAN_DEVSHA) && !GET_SFLAG(SFLAG_AHBPROT_PERMS))
				panic("Wii: IOS /dev/sha exploit failed");
			if (GET_SFLAG(SFLAG_RAN_ABN) && !GET_SFLAG(SFLAG_CUR_MINI))
				panic("Wii: ARMBootNow failed");

			/* determine where to go next */
			if (!GET_SFLAG(SFLAG_IOS_INIT)) {
				GOTO_STATE(STATE_IOS_INIT);
				break;
			}
			/* IOS, no perms: -> STATE_IOS_PRIV_ESC */
			else if (GET_SFLAG(SFLAG_IOS_INIT) && !GET_SFLAG(SFLAG_AHBPROT_PERMS)) {
				GOTO_STATE(STATE_IOS_PRIV_ESC);
				break;
			}
			/* IOS, has perms: -> STATE_IOS_ARMBOOTNOW */
			else if (GET_SFLAG(SFLAG_IOS_INIT) && GET_SFLAG(SFLAG_AHBPROT_PERMS) && !GET_SFLAG(SFLAG_RAN_ABN)) {
				GOTO_STATE(STATE_IOS_ARMBOOTNOW);
				break;
			}
			else
				__builtin_unreachable(); /* impossible to reach due to the above guards */
		}
		case STATE_IOS_INIT: {
			assert(!GET_SFLAG(SFLAG_CUR_MINI));

			H_WiiBootIOS = IOS_GetVersion();
			IOS_Reset();
			ES_Init();
			SET_SFLAG(SFLAG_IOS_INIT, true);
			GOTO_STATE(STATE_ANALYZE_IOS);
			break;
		}
		case STATE_IOS_PRIV_ESC: {
			assert(!GET_SFLAG(SFLAG_CUR_MINI));

			if (IOS_DevShaExploit())
				panic("Wii: IOS /dev/sha exploit failed");

			SET_SFLAG(SFLAG_RAN_DEVSHA, true);
			GOTO_STATE(STATE_HW_UNRESTRICT);
			break;
		}
		case STATE_IOS_ARMBOOTNOW: {
			assert(!GET_SFLAG(SFLAG_CUR_MINI));

			armbootnow();
			SET_SFLAG(SFLAG_RAN_ABN, true);
			GOTO_STATE(STATE_ANALYZE);
			break;
		}
		case STATE_HW_UNRESTRICT: {
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

			/* unrestrict memory */
			HW_MEM_PROT_SPL = 0;
			HW_MEM_PROT_SPL_BASE = 0;
			HW_MEM_PROT_SPL_END = 0;
			HW_MEM_PROT_DDR = 0;
			HW_MEM_PROT_DDR_BASE = 0;
			HW_MEM_PROT_DDR_END = 0;

			GOTO_STATE(STATE_ANALYZE);
			break;
		}
		case STATE_ANALYZE_MINI: {
			assert(GET_SFLAG(SFLAG_CUR_MINI));

			/* MINI, not initialized -> STATE_MINI_INIT */
			if (!GET_SFLAG(SFLAG_MINI_INIT)) {
				GOTO_STATE(STATE_MINI_INIT);
				break;
			}
			/* MINI, initialized, no perms -> STATE_MINI_PRIV_ESC */
			else if (GET_SFLAG(SFLAG_MINI_INIT) && (!GET_SFLAG(SFLAG_MEM2_ACCESS) || !GET_SFLAG(SFLAG_AHBPROT_PERMS) || !GET_SFLAG(SFLAG_SRNPROT_PERMS))) {
				GOTO_STATE(STATE_MINI_PRIV_ESC);
				break;
			}
			/* MINI, was originally MINI, initialized, has perms -> STATE_MINI_RELOAD */
			else if (GET_SFLAG(SFLAG_MINI_INIT) && GET_SFLAG(SFLAG_ORIG_MINI) && !GET_SFLAG(SFLAG_MINI_RELOADED) && GET_SFLAG(SFLAG_MEM2_ACCESS) && GET_SFLAG(SFLAG_AHBPROT_PERMS) && GET_SFLAG(SFLAG_SRNPROT_PERMS)) {
				GOTO_STATE(STATE_MINI_RELOAD);
				break;
			}
			/* MINI, was originally MINI, initialized, reloaded, has perms -> STATE_READY */
			else if (GET_SFLAG(SFLAG_MINI_INIT) && GET_SFLAG(SFLAG_ORIG_MINI) && GET_SFLAG(SFLAG_MINI_RELOADED) && GET_SFLAG(SFLAG_MEM2_ACCESS) && GET_SFLAG(SFLAG_AHBPROT_PERMS) && GET_SFLAG(SFLAG_SRNPROT_PERMS)) {
				GOTO_STATE(STATE_READY);
				break;
			}
			/* MINI, was originally IOS, initialized, has perms -> STATE_POST_IOS_SANITIZE */
			else if (GET_SFLAG(SFLAG_MINI_INIT) && !GET_SFLAG(SFLAG_ORIG_MINI) && GET_SFLAG(SFLAG_RAN_ABN) &&
				GET_SFLAG(SFLAG_MEM2_ACCESS) && GET_SFLAG(SFLAG_AHBPROT_PERMS) && GET_SFLAG(SFLAG_SRNPROT_PERMS)) {
				GOTO_STATE(STATE_POST_IOS_SANITIZE);
				break;
			}
			else
				__builtin_unreachable();
		}
		case STATE_MINI_INIT: {
			assert(GET_SFLAG(SFLAG_CUR_MINI));
			if (GET_SFLAG(SFLAG_ORIG_MINI))
				udelay(250 * 1000); /* FIXME: HACK to wait for MINI to actually fully boot first */
			miniErr = MINI_Init();
			if (miniErr != MINI_OK) {
				if (miniErr == MINI_TIMEOUT) {
					GOTO_STATE(STATE_ANALYZE);
					break;
				}
				log_printf("MINI_Init failed with %u\r\n", miniErr);
				panic("MINI_Init failed");
			}
			SET_SFLAG(SFLAG_MINI_INIT, true);
			GOTO_STATE(STATE_ANALYZE);
			break;
		}
		case STATE_MINI_RELOAD: {
			assert(GET_SFLAG(SFLAG_CUR_MINI) && GET_SFLAG(SFLAG_MINI_INIT));

			/* load _our_ copy of MINI into scratch MEM2 */
			armbuf = (vu32 *)(MEM2_CACHED_BASE + 0x01000000);
			memcpy((void *)armbuf, __mini_armboot_bin_data, (uint)__mini_armboot_bin_size);
			dcache_flush((void *)armbuf, (uint)__mini_armboot_bin_size);

			/* tell MINI to pretty please not reset us kthxbye */
			MINI_BOOT_MAGIC_PTR = MINI_NO_RESET_MAGIC;

			/* clear the IPC infohdr so we can keep track of when the new MINI has reloaded */
			*(u32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4) = 0;
			MINI_IPCPost(IPC_MINI_CODE_JUMP, 0, 1, virtToPhys(armbuf));
			udelay(500 * 1000); /* hardcoded time to give it a sec, mainly so our logs don't overlap */

			SET_SFLAG(SFLAG_MINI_INIT, false);
			SET_SFLAG(SFLAG_MINI_RELOADED, true);
			SET_SFLAG(SFLAG_CUR_MINI, false);
			GOTO_STATE(STATE_ANALYZE);
			break;
		}
		case STATE_MINI_PRIV_ESC: {
			assert(GET_SFLAG(SFLAG_CUR_MINI));

			miniErr = MINI_IPCPost(IPC_MINI_CODE_WRITE32, 0, 2, virtToPhys(&HW_AHBPROT), 0xffffffff);
			if (miniErr != MINI_OK)
				panic("Failed to write AHBPROT via MINI");

			miniErr = MINI_IPCPost(IPC_MINI_CODE_SET32, 0, 2, virtToPhys(&HW_SRNPROT), SRNPROT_AHPEN);
			if (miniErr != MINI_OK)
				panic("Failed to write SRNPROT via MINI");

			/* do more general hw unrestriction now that we have perms */
			GOTO_STATE(STATE_HW_UNRESTRICT);
			break;
		}
		case STATE_POST_IOS_SANITIZE: {
			assert(GET_SFLAG(SFLAG_CUR_MINI) && !GET_SFLAG(SFLAG_ORIG_MINI) && GET_SFLAG(SFLAG_AHBPROT_PERMS));

			/* TODO: all of this */
			tb = mftb();
			OHCI_HC_COMMAND_STATUS(0) |= OHCI_HC_COMMAND_STATUS_HCR;
			OHCI_HC_COMMAND_STATUS(1) |= OHCI_HC_COMMAND_STATUS_HCR;
			while ((OHCI_HC_COMMAND_STATUS(0) | OHCI_HC_COMMAND_STATUS(1)) & OHCI_HC_COMMAND_STATUS_HCR) {
				if (T_HasElapsed(tb, 1000))
					panic("OHCI reset timed out");
			}
			OHCI_HC_INTERRUPT_DISABLE(0) = 0xffffffff;
			OHCI_HC_INTERRUPT_DISABLE(1) = 0xffffffff;
			OHCI_HC_INTERRUPT_STATUS(0) = OHCI_HC_INTERRUPT_STATUS(0);
			OHCI_HC_INTERRUPT_STATUS(1) = OHCI_HC_INTERRUPT_STATUS(1);
			GOTO_STATE(STATE_READY);
			break;
		}
		case STATE_READY:
			goto out;
		default:
			panic("H_InitWii state machine in invalid state");
		}
	}
out:

	/* ASCII 'STUBHAXX' */
	if ((*(vu64 *)(MEM1_UNCACHED_BASE + 0x1804)) == 0x5354554248415858ULL && !GET_SFLAG(SFLAG_ORIG_MINI)) {
		log_puts("Detected HBC-complatible reload stub");
		H_PlatOps->exit = wiiExit;
	}

	/* we want to load Wii drivers */
	D_DriverMask = DRIVER_ALLOW_WII;

	/* final sanity check, these should pass if all went well; don't use assert() since it gets compiled out of release builds */
	if (HW_AHBPROT != 0xffffffff)
		panic("AHBPROT not enabled at end of H_InitWii");
	if (!testMEM2())
		panic("MEM2 not fully accessible at end of H_InitWii");

	MINI_BOOT_MAGIC_PTR = 0;

	/* kick off the real init */
	I_InitCommon();

	wiiPanic("I_InitCommon should not return");
	__builtin_unreachable();
}
