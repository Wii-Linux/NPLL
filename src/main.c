/*
 * NPLL - NPLLain loop
 *
 * Copyright (C) 2025 Techflash
 */

#include <npll/drivers.h>
#include <npll/input.h>
#include <npll/menu.h>
#include <npll/video.h>

void __attribute__((noreturn)) mainLoop(void) {
	while (1) {
		D_RunCallbacks();
		IN_HandleInputs();
		M_Redraw();
		V_Flush();
	}
}
