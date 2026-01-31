/*
 * NPLL - Panic / error handling
 *
 * Copyright (C) 2025 Techflash
 */

#include <npll/log.h>
#include <stdbool.h>
#include <npll/console.h>
#include <npll/video.h>

/*extern bool __putcharNoGraphics;*/

void __attribute__((noreturn)) panic(const char *str) {
	/*__putcharNoGraphics = true;*/
	log_printf("FATAL: PANIC: %s\r\n", str);
	if (V_ActiveDriver)
		V_Flush();

	H_PlatOps->panic(str);
}
