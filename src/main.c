/*
 * NPLL - Main loop
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <npll/thread.h>

void __attribute__((noreturn)) mainLoop(void) {
	while (1)
		TH_Sleep(0x7fffffffu);
}
