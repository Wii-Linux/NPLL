/*
 * NPLL - Logging
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <npll/drivers.h>
#include <npll/log_internal.h>
#include <npll/menu.h>
#include <npll/output.h>

enum logMethod L_Method = LOG_METHOD_ALL_ODEV;

/* TODO: really ensure that there's nothing here we're clobbering */
#define memlogStart (char *)0x817f0000
static char *memlogNext = memlogStart;
static u32 maxSize = 0x00010000;
static bool outOfSpace = false;

static void memlogWriteChar(const char c) {
	if ((u32)memlogNext >= ((u32)memlogNext + maxSize)) {
		if (outOfSpace) /* prevent recursion */
			return;
		outOfSpace = true;
		panic("memlog is out of space");
	}
	*memlogNext = c;
	memlogNext++;
}

void _log_puts(const char *str) {
	switch (L_Method) {
	case LOG_METHOD_ALL_ODEV: {
		while (*str) {
			memlogWriteChar(*str);
			putchar(*str++);
		}
		memlogWriteChar('\r');
		memlogWriteChar('\n');
		putchar('\r');
		putchar('\n');
		break;
	}
	case LOG_METHOD_MENU_WINDOW: {
		while (*str) {
			memlogWriteChar(*str);
			UI_LogPutchar(*str++);
		}
		memlogWriteChar('\r');
		memlogWriteChar('\n');
		UI_LogPutchar('\r');
		UI_LogPutchar('\n');
		break;
	}
	case LOG_METHOD_NONE: {
		while (*str)
			memlogWriteChar(*str);
		memlogWriteChar('\r');
		memlogWriteChar('\n');
		break;
	}
	default: {
		assert_unreachable();
	}
	}
}


static void allOdevOut(char c, void *dummy) {
	(void)dummy;

	memlogWriteChar(c);
	putchar(c);
}

static void menuWindowOut(char c, void *dummy) {
	(void)dummy;

	memlogWriteChar(c);
	UI_LogPutchar(c);
}

static void noneOut(char c, void *dummy) {
	(void)dummy;

	memlogWriteChar(c);
}

void _log_printf(const char *fmt, ...) {
	va_list va;
	void (*out)(char character, void* arg);
	va_start(va, fmt);

	switch (L_Method) {
	case LOG_METHOD_ALL_ODEV: {
		out = allOdevOut;
		break;
	}
	case LOG_METHOD_MENU_WINDOW: {
		out = menuWindowOut;
		break;
	}
	case LOG_METHOD_NONE: {
		out = noneOut;
		break;
	}
	default: {
		assert_unreachable();
	}
	}

	fctvprintf(out, NULL, fmt, va);
	va_end(va);
}

void L_Init(void) {
	/*memlogWriteStr("In-Memory logger is now active\r\n"); */
	memset(memlogStart, 0, maxSize);
}
