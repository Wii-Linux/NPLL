/*
 * NPLL - Panic / error handling
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <stdio.h>
#include <stdbool.h>
#include <npll/console.h>
#include <npll/video.h>

void __attribute__((noreturn)) panic(const char *str) {
	printf("FATAL: PANIC: %s\r\n", str);
	if (V_ActiveDriver)
		V_Flush();

	H_PlatOps->panic(str);
}
