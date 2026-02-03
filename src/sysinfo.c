/*
 * NPLL - System Information Menu
 *
 * Copyright (C) 2026 Techflash
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <npll/console.h>
#include <npll/menu.h>

static void sysinfoMenuInit(struct menu *m);
static void sysinfoMenuCleanup(struct menu *m);

static struct menuEntry sysinfoMenuItems[] = {
	{ .name = "Back", .selected = UI_UpLevel }
};

struct menu UI_SysInfoMenu = {
	.header = "NPLL - System Information",
	.footer = FOOTER_CONTROLS,
	.entries = sysinfoMenuItems,
	.numEntries = 1,
	.init = sysinfoMenuInit,
	.cleanup = sysinfoMenuCleanup,
	.destroy = NULL,
	.previous = NULL
};

static void sysinfoMenuInit(struct menu *m) {
	char tmp[256];
	char *name;

	/* allocate scratch space */
	m->content = malloc(4096);
	assert(m->content);
	memset(m->content, 0, 4096);

	/* step 1: report console type */
	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE)
		strcat(m->content, "Console Type: Nintendo GameCube");
	else if (H_ConsoleType == CONSOLE_TYPE_WII) {
		if (H_WiiIsvWii)
			strcat(m->content, "Console Type: Nintendo Wii U (vWii)");
		else
			strcat(m->content, "Console Type: Nintendo Wii");
	}
	else if (H_ConsoleType == CONSOLE_TYPE_WII_U)
		strcat(m->content, "Console Type: Nintendo Wii U (native)");

	strcat(m->content, "\r\n");


	/* step 2: report SoC revision */

	/* step 2a: report Flipper revision if GameCube */
	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
		switch (H_GCNRev) {
		case PI_CHIPID_REV_A: {
			name = "A (?!)";
			break;
		}
		case PI_CHIPID_REV_B: {
			name = "B (?!)";
			break;
		}
		case PI_CHIPID_REV_C: {
			name = "C (retail)";
			break;
		}
		default: {
			name = NULL;
			break;
		}
		}

		if (name)
			sprintf(tmp, "Flipper SoC Revision: %s", name);
		else
			sprintf(tmp, "Flipper SoC Revision: Unknown (%08x) (?!)", name, H_GCNRev);

		strcat(m->content, tmp);
		strcat(m->content, "\r\n");
	}

	/* step 2b: report Hollywood revision if Wii/Wii U */
	if (H_ConsoleType == CONSOLE_TYPE_WII || H_ConsoleType == CONSOLE_TYPE_WII_U) {
		if (H_ConsoleType == CONSOLE_TYPE_WII)
			name = "Hollywood SoC Revision";
		if (H_ConsoleType == CONSOLE_TYPE_WII_U || H_WiiIsvWii)
			name = "Hollywood SoC Revision (vWii Compat)";

		if (H_WiiRev == HW_VERSION_PROD_HOLLYWOOD)
			sprintf(tmp, "%s: 0x11 (Production Hollywood)", name);
		else if (H_WiiRev == HW_VERSION_PROD_BOLLYWOOD)
			sprintf(tmp, "%s: 0x21 (Production Bollywood)", name);
		else
			sprintf(tmp, "%s: 0x%02x (Unknown?!?!?!)", name, H_WiiRev);

		strcat(m->content, tmp);
		strcat(m->content, "\r\n");
	}

	/* step 2c: report Latte revision if Wii U */
	if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		switch (H_WiiURev) {
		case LT_CHIPREVID_REV_LATTE_A11: {
			name = "A11";
			break;
		}
		case LT_CHIPREVID_REV_LATTE_A12: {
			name = "A12";
			break;
		}
		case LT_CHIPREVID_REV_LATTE_A2X: {
			name = "A2X";
			break;
		}
		case LT_CHIPREVID_REV_LATTE_A3X: {
			name = "A3X";
			break;
		}
		case LT_CHIPREVID_REV_LATTE_A4X: {
			name = "A4X";
			break;
		}
		case LT_CHIPREVID_REV_LATTE_A5X: {
			name = "A5X";
			break;
		}
		case LT_CHIPREVID_REV_LATTE_B1X: {
			name = "B1X";
			break;
		}
		default: {
			name = NULL;
			break;
		}
		}

		if (name)
			sprintf(tmp, "Latte SoC Revision: %s", name);
		else
			sprintf(tmp, "Latte SoC Revision: Unknown (%08x)", H_WiiURev);

		strcat(m->content, tmp);
		strcat(m->content, "\r\n");
	}

	/* step 3: report boot method, if applicable */
	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) /* TODO: Determine */
		strcat(m->content, "Boot method: Unknown\r\n");
	else if (H_ConsoleType == CONSOLE_TYPE_WII) {
		if (H_WiiBootIOS > 0) {
			sprintf(tmp, "Boot method: IOS %d", H_WiiBootIOS);
			strcat(m->content, tmp);
		}
		else
			strcat(m->content, "Boot method: MINI");

		strcat(m->content, "\r\n");
	}
	else if (H_ConsoleType == CONSOLE_TYPE_WII_U) /* TODO: Determine */
		strcat(m->content, "Boot method: linux-loader\r\n");

	/* TODO: Storage once that's fully inplemented */
	/* TODO: Memory */
	/* TODO: Clock speeds */
}

static void sysinfoMenuCleanup(struct menu *m) {
	free(m->content);
}
