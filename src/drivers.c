/*
 * NPLL - Driver helpers
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "DRV"

#include <npll/types.h>
#include <npll/drivers.h>
#include <npll/panic.h>
#include <npll/log.h>
#include <npll/irq.h>
#include <npll/utils.h>

#define MAX_CALLBACKS 64

u8 D_DriverMask;
static drvCallback_t callbacks[MAX_CALLBACKS];

static inline const char *D_StateToStr(enum driverState state) {
	switch (state) {
	case DRIVER_STATE_NOT_READY: return "Not Ready";
	case DRIVER_STATE_INITIALIZING: return "Initializing";
	case DRIVER_STATE_FAULTED: return "Faulted";
	case DRIVER_STATE_NO_HARDWARE: return "No Hardware";
	case DRIVER_STATE_NEED_DEP: return  "Needs Dependencies";
	case DRIVER_STATE_READY: return "Ready";
	default: return NULL;
	}
}

void D_Init(void) {
	int curType, firstType, lastType;
	struct driver *curDriver;
	firstType = DRIVER_TYPE_START + 1;
	lastType = DRIVER_TYPE_END - 1;
	curType = firstType;

	TRACE();

	/* Initialize drivers in order of driverType */
	for (; curType <= lastType; curType++) {
		/*
		 * Try to initialize all not-yet-ready drivers of type <= curType.
		 * Drivers might set thisDrv->state = DRIVER_STATE_NEED_DEP if a driver they depend on,
		 * (e.g. loading USB Gecko driver but EXI driver is not yet ready) is not yet ready.
		 * In that case it would be beneficial to try to the driver again next time around.
		 */
		curDriver = __drivers_start;
		while ((u32)curDriver < ((u32)__drivers_end) - 1) {
			/* already ready, we don't have it, borked, or still working - skip */
			if (curDriver->state == DRIVER_STATE_READY ||
			    curDriver->state == DRIVER_STATE_NO_HARDWARE ||
			    curDriver->state == DRIVER_STATE_FAULTED ||
			    curDriver->state == DRIVER_STATE_INITIALIZING) {
				goto noload;
			}

			/* not valid on this platform */
			if (!(curDriver->mask & D_DriverMask))
				goto noload;

			/* make sure it's the right time to try / try again to load it */
			if (curDriver->type <= (u32)curType) {
				log_printf("Initializing driver: %s\r\n", curDriver->name);
				curDriver->init();
				log_printf("Driver %s now in state: %s\r\n", curDriver->name, D_StateToStr(curDriver->state));
			}

noload:
			curDriver++;
		}
	}
}

void D_AddCallback(drvCallback_t cb) {
	int i;
	bool irqs;

	for (i = 0; i < MAX_CALLBACKS; i++) {
		if (!callbacks[i]) {
			irqs = IRQ_DisableSave();
			callbacks[i] = cb;
			IRQ_Restore(irqs);
			return;
		}
	}
	panic("Out of callback slots");
}


void D_RemoveCallback(drvCallback_t cb) {
	int i;
	bool irqs;

	for (i = 0; i < MAX_CALLBACKS; i++) {
		if (callbacks[i] == cb) {
			irqs = IRQ_DisableSave();
			callbacks[i] = NULL;
			IRQ_Restore(irqs);
			return;
		}
	}
	log_printf("WARNING: Tried to remove nonexistant callback %p\r\n", cb);
}

void D_RunCallbacks(void) {
	int i;
	for (i = 0; i < MAX_CALLBACKS; i++) {
		if (callbacks[i])
			callbacks[i]();
	}
}
