/*
 * NPLL - Top-Level Init
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <npll/irq.h>
#include <npll/types.h>
#include <npll/regs.h>
#include <npll/console.h>
#include <npll/init.h>
#include <npll/drivers.h>
#include <npll/output.h>
#include <npll/exceptions.h>
#include <npll/menu.h>
#include <npll/allocator.h>
#include <npll/utils.h>
#include <npll/log_internal.h>

enum consoleType H_ConsoleType = CONSOLE_TYPE_GAMECUBE;
struct platOps *H_PlatOps = NULL;

extern void mainLoop(void);

void __attribute__((noreturn)) init(void) {
	u32 hw_version, lt_chiprevid;

	/* Initialize super early in-memory logging */
	L_Init();

	/*
	 * Alright, first thing's first.  What console are we?
	 * This is actually not a trivial question to answer.
	 * The algorithm I came up with (improvements welcome), is:
	 * 1. Check HW_VERSION.
	 * 2. Is it nonsense?  If so, we must be a GameCube.  Otherwise continue.
	 * 3. Check LT_CHIPREVID.
	 * 4. Is it nonsense?  If so (and we know HW_VERSION is valid from above),
	 *    then we must be a Wii (or vWii).  If it's valid, continue.
	 * 5. Is LT_PIMCOMPAT non-zero?  If so, it must be a Wii U in Wii U mode.
	 *    If not, it must be a Wii U in vWii mode.
	 */
	hw_version = HW_VERSION;
	switch (hw_version) {
	/* see console.h for reason behind not more types */
	case HW_VERSION_PROD_HOLLYWOOD:
	case HW_VERSION_PROD_BOLLYWOOD: {
		H_GCNRev = 0;
		H_WiiRev = hw_version;
		H_WiiURev = 0;
		H_ConsoleType = CONSOLE_TYPE_WII;
		break;
	}
	default: {
		H_GCNRev = PI_CHIPID;
		H_WiiRev = 0;
		H_WiiURev = 0;
		H_ConsoleType = CONSOLE_TYPE_GAMECUBE;
		break;
	}
	}

	/* it's a GameCube, there is nothing else to determine */
	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
		_log_puts("Detected hardware: Nintendo GameCube");
		H_InitGameCube();
		__builtin_unreachable();
	}

	/* it might be a Wii or a Wii U... check LT_CHIPREVID */
	lt_chiprevid = LT_CHIPREVID;

	/*
	 * Quick check, do we even have the magic signature?  If not, it's certainly a Wii...
	 * or Wii U in vWii mode whilst we don't have perms in AHBPROT, which works almost
	 * the same way (short of one minor GPIO quirk), but we can still detect that
	 * once we've gained AHBPROT perms in H_InitWii.
	 */
	if ((lt_chiprevid & 0xFFFF0000) != 0xCAFE0000) {
		_log_puts("Detected hardware: Nintendo Wii");
detectedWii:
		H_InitWii();
		__builtin_unreachable();
	}

	/* it's a Wii U */
	/* check if LT_PIMCOMPAT is non-zero, if it is, we must be in native mode, else it's vWii */
	if (LT_PIMCOMPAT) {
		_log_puts("Detected hardware: Nintendo Wii U (native)");
		H_ConsoleType = CONSOLE_TYPE_WII_U;
		H_WiiURev = lt_chiprevid;
		H_InitWiiU();
		__builtin_unreachable();
	}
	else {
		_log_puts("Detected hardware: Nintendo Wii U (vWii)");
		H_WiiIsvWii = 1;
		goto detectedWii;
	}
	__builtin_unreachable();
}

void __attribute__((noreturn)) I_InitCommon(void) {
	TRACE();
	O_DebugInit();
	E_Init();
	IRQ_Init();
	IRQ_Enable();
	M_Init();
	D_Init();
	UI_Init();
	_log_puts("Driver initialization done, entering mainLoop");
	mainLoop();
	__builtin_unreachable();
}
