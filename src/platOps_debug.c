/*
 * NPLL - H_PlatOps debugging
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <string.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/output.h>

static void debugConWriteChar(const char c) {
	if (!H_PlatOps || !H_PlatOps->debugWriteChar)
		return;

	H_PlatOps->debugWriteChar(c);
}

static void debugConWriteStr(const char *str) {
	if (!H_PlatOps)
		return;

	if (H_PlatOps->debugWriteStr) {
		H_PlatOps->debugWriteStr(str);
		return;
	}
	else if (H_PlatOps->debugWriteChar) {
		while (*str) {
			debugConWriteChar(*str);
			str++;
		}
	}
	return;
}

static const struct outputDevice outDev = {
	.writeChar = debugConWriteChar,
	.writeStr = debugConWriteStr,
	.name = "H_PlatOps Debug Console",
	.driver = NULL,
	.isGraphical = false,
	.rows = 80,
	.columns = 25
};

void O_DebugInit(void) {
	O_AddDevice(&outDev);
	debugConWriteStr("H_PlatOps Debug Console initialized\n");
	debugConWriteStr("Leaving O_DebugInit\n");
}

void O_DebugCleanup(void) {
	O_RemoveDevice(&outDev);
}
