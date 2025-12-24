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
 * https://wiki.wii-linux.org/wiki/Wii_Hardware/Hollywood/Revisions
 */
enum wiiRev {
	HW_VERSION_PROD_HOLLYWOOD = 0x11,
	HW_VERSION_PROD_BOLLYWOOD = 0x21
};

/* TODO */
enum wiiuRev {
	LT_CHIPREVID_x = 0
};

struct platOps {
	void __attribute__((noreturn)) (*panic)(const char *str);
	void (*debugWriteChar)(const char c);
	void (*debugWriteStr)(const char *str);
};

extern enum consoleType H_ConsoleType;
extern enum wiiRev H_WiiRev;
extern int H_WiiIsvWii;
extern enum wiiuRev H_WiiURev;
extern struct platOps *H_PlatOps;

#endif /* _CONSOLE_H */
