/*
 * NPLL - Driver helpers
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _DRIVERS_H
#define _DRIVERS_H

#include <npll/types.h>

typedef void (*drvCallback_t)(void);

enum driverState {
	DRIVER_STATE_NOT_READY,
	DRIVER_STATE_INITIALIZING,
	DRIVER_STATE_FAULTED,
	DRIVER_STATE_NO_HARDWARE,
	DRIVER_STATE_NEED_DEP,
	DRIVER_STATE_READY
};

enum driverType {
	DRIVER_TYPE_CRITICAL,
	DRIVER_TYPE_BLOCK,
	DRIVER_TYPE_FS,
	DRIVER_TYPE_GFX,
	DRIVER_TYPE_INPUT,
	DRIVER_TYPE_OTHER
};

struct driver {
	const char *name;
	u8 mask;
	enum driverState state;
	enum driverType type;

	void (*init)(void);
	void (*cleanup)(void);
};

#define REGISTER_DRIVER(drv) \
	__attribute__((used, section(".drivers"))) struct driver drv

#define DECLARE_DRIVER(drv) \
	extern __attribute__((section(".drivers"))) struct driver drv

#define DRIVER_ALLOW_GAMECUBE (1 << 0)
#define DRIVER_ALLOW_WII      (1 << 1)
#define DRIVER_ALLOW_WIIU     (1 << 2)
#define DRIVER_ALLOW_ALL      (DRIVER_ALLOW_GAMECUBE | DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU)

extern struct driver __drivers_start[];
extern struct driver __drivers_end[];
extern u8 D_DriverMask;

/*
 * Initialize all possible drivers
 */
extern void D_Init(void);

/*
 * Add a callback to be processed during the main loop
 */
extern void D_AddCallback(drvCallback_t cb);

/*
 * Remove a callback from being processed during the main loop
 */
extern void D_RemoveCallback(drvCallback_t cb);

/*
 * Run all callbacks
 */
extern void D_RunCallbacks(void);


#endif /* _DRIVERS_H */
