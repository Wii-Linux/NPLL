/*
 * NPLL - Driver helpers
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <npll/types.h>
#include <npll/drivers.h>
#include <npll/panic.h>

#define MAX_CALLBACKS 64

u8 D_DriverMask;
static drvCallback_t callbacks[MAX_CALLBACKS];

void D_Init(void) {
	int curType, firstType, lastType;
	struct driver *curDriver;
	firstType = DRIVER_TYPE_CRITICAL;
	lastType = DRIVER_TYPE_OTHER;
	curType = firstType;
	
	puts("D_Init entered");

	/* Initialize drivers in order of driverType */		
	for (; curType <= lastType; curType++) {
		printf("  - Working on driver type: %d\r\n", curType);
		/*
		 * Try to initialize all not-yet-ready drivers of type <= curType.
		 * Drivers might set thisDrv->state = DRIVER_STATE_NEED_DEP if a driver they depend on,
		 * (e.g. loading USB Gecko driver but EXI driver is not yet ready) is not yet ready.
		 * In that case it would be beneficial to try to the driver again next time around.
		 */
		curDriver = __drivers_start;
		while ((u32)curDriver < ((u32)__drivers_end) - 1) {
			printf("    - Checking driver: %s...", curDriver->name);
			/* already ready, we don't have it, borked, or still working - skip */
			if (curDriver->state == DRIVER_STATE_READY ||
			    curDriver->state == DRIVER_STATE_NO_HARDWARE ||
			    curDriver->state == DRIVER_STATE_FAULTED ||
			    curDriver->state == DRIVER_STATE_INITIALIZING) {
				puts(" skipping (state is ready/no_hardware/faulted/initializing)");
				goto noload;
			}

			/* not valid on this platform */
			if (!(curDriver->mask & D_DriverMask)) {
				puts(" skipping (not valid on this platform)");
				goto noload;
			}

			/* make sure it's the right time to try / try again to load it */
			if (curDriver->type <= curType) {
				//printf("Initializing driver: %s\r\n", curDriver->name);
				printf(" initializing...");
				curDriver->init();
				puts(" done!");
				//printf("Driver initialized: %s\r\n", curDriver->name);
			}
			else {
				puts(" skipping (type is too high)");
			}

noload:
			/* TODO: maybe debug log that it was ignored if we ever implement multiple log levels */
			curDriver++;
		}
	}
}

void D_AddCallback(drvCallback_t cb) {
	int i;
	for (i = 0; i < MAX_CALLBACKS; i++) {
		if (!callbacks[i]) {
			callbacks[i] = cb;
			return;
		}
	}
	panic("Out of callback slots");
}


void D_RemoveCallback(drvCallback_t cb) {
	int i;
	for (i = 0; i < MAX_CALLBACKS; i++) {
		if (callbacks[i] == cb) {
			callbacks[i] = NULL;
			return;
		}
	}
	printf("WARNING: Tried to remove nonexistant callback %p\r\n", cb);
}

void D_RunCallbacks(void) {
	int i;
	for (i = 0; i < MAX_CALLBACKS; i++) {
		if (callbacks[i])
			callbacks[i]();
	}
}

