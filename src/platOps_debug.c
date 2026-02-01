/*
 * NPLL - H_PlatOps debugging
 *
 * Copyright (C) 2025-2026 Techflash
 */

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
	/* possible but cannot be garuanteed */
	.ansiEscSupport = false,
	.rows = 80,
	.columns = 25
};

static int initialized = false;
void O_DebugInit(void) {
	if (initialized)
		return;

	initialized = true;

	O_AddDevice(&outDev);
	debugConWriteStr("H_PlatOps Debug Console initialized\n");
	debugConWriteStr("Leaving O_DebugInit\n");
}

void O_DebugCleanup(void) {
	O_RemoveDevice(&outDev);
}
