/*
 * NPLL - Main loop
 *
 * Copyright (C) 2025 Techflash
 */

#include <npll/drivers.h>
#include <npll/menu.h>
#include <npll/video.h>

void __attribute__((noreturn)) mainLoop(void) {
	while (1) {
		D_RunCallbacks();
		UI_HandleInputs();
		UI_Redraw();
		V_Flush();
	}
}
