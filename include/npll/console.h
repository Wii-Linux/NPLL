/*
 * NPLL - Console types
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _CONSOLE_H
#define _CONSOLE_H

enum consoleType {
	CONSOLE_TYPE_GAMECUBE = 1,
	CONSOLE_TYPE_WII,
	CONSOLE_TYPE_WII_U
};

/*
 * I highly suspect that the values listed in Wiibrew for HW_VERSION are utter nonsense.
 * I have a boot1b console that still reports HW_VERSION=0x11, a supposed "2.0" chip.
 * As far as I can tell, 0x11 is "v1", and 0x21 is "v2" (found on RVL-CPU-40 boards and newer).
 * There is nothing that I can find that is public that is older, and there is nothing newer.
 */
enum wiiRev {
	HW_VERSION_PROD_11 = 0x11,
	HW_VERSION_PROD_21 = 0x21
};

/* TODO */
enum wiiuRev {
	LT_CHIPREVID_x = 0
};

struct platOps {
	void __attribute__((noreturn)) (*panic)(const char *str);
};

extern enum consoleType H_ConsoleType;
extern enum wiiRev H_WiiRev;
extern enum wiiuRev H_WiiURev;
extern struct platOps *H_PlatOps;

#endif /* _CONSOLE_H */
