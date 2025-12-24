/*
 * NPLL - In-Memory logging
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <string.h>
#include <npll/drivers.h>
#include <npll/output.h>

/* TODO: really ensure that there's nothing here we're clobbering */
#define memlogStart (char *)0x817f8000
static char *memlogNext = memlogStart;
static u32 maxSize = 0x00008000;

static void memlogWriteChar(const char c) {
	if ((u32)memlogNext >= ((u32)memlogNext + maxSize))
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
	.name = "MemLog",
	.driver = NULL,
	.isGraphical = false,
	.rows = 80,
	.columns = 25
};

void O_MemlogInit(void) {
	/*memlogWriteStr("In-Memory logger is now active\r\n"); */
	memset(memlogStart, 0, maxSize);
	O_AddDevice(&outDev);
}

void O_MemlogCleanup(void) {
	O_RemoveDevice(&outDev);
	printf("%s", memlogStart); /* dump out everything we've got */
}
