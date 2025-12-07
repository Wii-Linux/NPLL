/*
 * NPLL - Top-Level Init
 *
 * Copyright (C) 2025 Techflash
 */

#include <npll/types.h>
#include <npll/regs.h>
#include <npll/console.h>
#include <npll/init.h>
#include <npll/drivers.h>
#include <npll/output.h>
#include <npll/exceptions.h>
#include <npll/menu.h>
#include <npll/allocator.h>
#include <stdio.h>

enum consoleType H_ConsoleType = CONSOLE_TYPE_GAMECUBE;
struct platOps *H_PlatOps = NULL;

extern void mainLoop(void);

int init(void) {
	u32 hw_version, lt_chiprevid;

	/* Initialize super early in-memory logging */
	O_MemlogInit();

	/*
	 * Alright, first thing's first.  What console are we?
	 * This is actually not a trivial question to answer.
	 * The algorithm I came up with (improvements welcome), is:
	 * 1. Check HW_VERSION.
	 * 2. Is it nonsense?  If so, we must be a GameCube.  Otherwise continue.
	 * 3. Check LT_CHIPREVID.
	 * 4. Is it nonsense?  If so (and we know HW_VERSION is valid from above),
	 *    then we must be a Wii.  If it's valid, we must be a Wii U.
	 */
	hw_version = HW_VERSION;
	switch (hw_version) {
	/* see console.h for reason behind not more types */
	case HW_VERSION_PROD_HOLLYWOOD:
	case HW_VERSION_PROD_BOLLYWOOD: {
		H_WiiRev = hw_version;
		H_WiiURev = 0;
		H_ConsoleType = CONSOLE_TYPE_WII;
		break;
	}
	default: {
		H_WiiRev = 0;
		H_WiiURev = 0;
		H_ConsoleType = CONSOLE_TYPE_GAMECUBE;
		break;
	}
	}

	/* it's a GameCube, there is nothing else to determine */
	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
		puts("Detected hardware: Nintendo GameCube");
		H_InitGameCube();
		__builtin_unreachable();
	}

	/* it might be a Wii or a Wii U... check LT_CHIPREVID */
	lt_chiprevid = LT_CHIPREVID;
	
	/* Quick check, do we even have the magic bytes?  If not, it's certainly a Wii. */
	if ((lt_chiprevid & 0xFFFF0000) != 0xCAFE0000) {
		puts("Detected hardware: Nintendo Wii");
		H_InitWii();
		__builtin_unreachable();
	}

	/* it's a Wii U */
	puts("Detected hardware: Nintendo Wii U");
	H_ConsoleType = CONSOLE_TYPE_WII_U;
	H_WiiURev = lt_chiprevid;
	H_InitWiiU();
	__builtin_unreachable();
}

void __attribute__((noreturn)) I_InitCommon(void) {
	puts("I_InitCommon entered");
	O_DebugInit();
	E_Init();
	M_Init();
	D_Init();
	UI_Init();
	puts("Driver initialization done, entering mainLoop");
	mainLoop();
	__builtin_unreachable();
}
