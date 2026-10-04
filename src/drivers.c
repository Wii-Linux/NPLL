/*
 * NPLL - Driver helpers
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "DRV"

#include <npll/types.h>
#include <npll/block.h>
#include <npll/drivers.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/utils.h>
#include <npll/irq.h>
#include <npll/thread.h>
#include <npll/timer.h>

u8 D_DriverMask;
bool D_Initializing = true;
static uint pendingInit;
static bool mountInitStarted;

static inline const char *D_StateToStr(enum driverState state) {
	switch (state) {
	case DRIVER_STATE_NOT_READY: return "Not Ready";
	case DRIVER_STATE_INITIALIZING: return "Initializing";
	case DRIVER_STATE_INITIALIZING_CLEANABLE: return "Initializing (cleanup capable)";
	case DRIVER_STATE_FAULTED: return "Faulted";
	case DRIVER_STATE_NO_HARDWARE: return "No Hardware";
	case DRIVER_STATE_NEED_DEP: return  "Needs Dependencies";
	case DRIVER_STATE_READY: return "Ready";
	default: return NULL;
	}
}

static void initWorker(void *arg) {
	struct driver *driver = arg;
	bool enabled;

	if (driver->initOwnsMount) {
		FS_Lock();
		mountInitStarted = true;
		TH_Wake(&mountInitStarted);
	}
	driver->init();
	if (driver->initOwnsMount)
		FS_Unlock();
	log_printf("Driver %s now in state: %s\r\n", driver->name, D_StateToStr(driver->state));
	enabled = IRQ_DisableSave();
	pendingInit--;
	TH_Wake(&pendingInit);
	IRQ_Restore(enabled);
}

void D_WaitForInit(void) {
	bool enabled = IRQ_DisableSave();

	while (pendingInit)
		TH_WaitLocked(&pendingInit, 0xffffffffu);
	IRQ_Restore(enabled);
	B_WaitForScans();
	D_Initializing = false;
}

void D_Init(void) {
	int curType, firstType, lastType;
	struct driver *curDriver;
	bool enabled;
	firstType = DRIVER_TYPE_START + 1;
	lastType = DRIVER_TYPE_END - 1;
	curType = firstType;

	TRACE();

	/* Initialize drivers in order of driverType */
	for (; curType <= lastType; curType++) {
		if (curType == DRIVER_TYPE_BLOCK) {
			for (curDriver = __drivers_start; curDriver < __drivers_end; curDriver++) {
				if (!curDriver->initOwnsMount || !(curDriver->mask & D_DriverMask) || curDriver->state != DRIVER_STATE_NOT_READY)
					continue;

				log_printf("Initializing driver: %s\r\n", curDriver->name);
				curDriver->state = DRIVER_STATE_INITIALIZING;
				mountInitStarted = false;
				pendingInit++;
				T_QueueEvent(0, initWorker, curDriver);
				enabled = IRQ_DisableSave();
				while (!mountInitStarted)
					TH_WaitLocked(&mountInitStarted, 0xffffffffu);

				IRQ_Restore(enabled);
			}
		}
		/*
		 * Try to initialize all not-yet-ready drivers of type <= curType.
		 * Drivers might set thisDrv->state = DRIVER_STATE_NEED_DEP if a driver they depend on,
		 * (e.g. loading USB Gecko driver but EXI driver is not yet ready) is not yet ready.
		 * In that case it would be beneficial to try to the driver again next time around.
		 */
		curDriver = __drivers_start;
		while ((uintptr_t)curDriver < ((uintptr_t)__drivers_end) - 1) {
			/* already ready, we don't have it, borked, or still working - skip */
			if (curDriver->state == DRIVER_STATE_READY ||
			    curDriver->state == DRIVER_STATE_NO_HARDWARE ||
			    curDriver->state == DRIVER_STATE_FAULTED ||
			    curDriver->state == DRIVER_STATE_INITIALIZING ||
			    curDriver->state == DRIVER_STATE_INITIALIZING_CLEANABLE) {
				goto noload;
			}

			/* not valid on this platform */
			if (!(curDriver->mask & D_DriverMask))
				goto noload;

			/* make sure it's the right time to try / try again to load it */
			if (curDriver->type <= (u32)curType) {
				log_printf("Initializing driver: %s\r\n", curDriver->name);
				if (curDriver->type == DRIVER_TYPE_BLOCK) {
					/* Hardware waits and B_Register run on independent stacks */
					curDriver->state = DRIVER_STATE_INITIALIZING;
					pendingInit++;
					T_QueueEvent(0, initWorker, curDriver);
				}
				else {
					curDriver->init();
					log_printf("Driver %s now in state: %s\r\n", curDriver->name, D_StateToStr(curDriver->state));
				}
			}

noload:
			curDriver++;
		}
	}
}
