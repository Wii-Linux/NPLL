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

/* see values here: https://wiiubrew.org/wiki/Hardware/Latte_registers#LT_CHIPREVID */
enum wiiuRev {
	LT_CHIPREVID_REV_LATTE_A11 = 0xCAFE0010,
	LT_CHIPREVID_REV_LATTE_A12 = 0xCAFE0018,
	LT_CHIPREVID_REV_LATTE_A2X = 0xCAFE0021,
	LT_CHIPREVID_REV_LATTE_A3X = 0xCAFE0030,
	LT_CHIPREVID_REV_LATTE_A4X = 0xCAFE0040,
	LT_CHIPREVID_REV_LATTE_A5X = 0xCAFE0050,
	LT_CHIPREVID_REV_LATTE_B1X = 0xCAFE0060,
	LT_CHIPREVID_REV_LATTE_UNK = 0xCAFE0070,
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
