/*
 * NPLL - In-Memory logging
 *
 * Copyright (C) 2025 Techflash
 */

#include <npll/drivers.h>
#include <npll/output.h>

static char *memlogNext = (char *)0x81500000;

static void memlogCallback(void) {
}

static void memlogWriteChar(const char c) {
	if ((u32)memlogNext >= 0x81700000)
		return;
	*memlogNext = c;
	memlogNext++;
}

static void memlogWriteStr(const char *str) {
	while (*str) {
		memlogWriteChar(*str);
		str++;
	}
}

static const struct outputDevice outDev = {
	.writeChar = memlogWriteChar,
	.writeStr = memlogWriteStr,
	.isGraphical = false,
	.rows = 80,
	.columns = 25
};

void O_MemlogInit(void) {
	/* register our callback */
	D_AddCallback(memlogCallback);

	memlogWriteStr("In-Memory logger is now active\r\n");
	O_AddDevice(&outDev);
}

static void memlogCleanup(void) {
	D_RemoveCallback(memlogCallback);
}
