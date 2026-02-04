/*
 * NPLL - GameCube Init
 *
 * Copyright (C) 2025 Techflash
 */

#include <npll/types.h>
#include <npll/regs.h>
#include <npll/console.h>
#include <npll/init.h>
#include <npll/panic.h>
#include <npll/timer.h>
#include <npll/tiny_usbgecko.h>
#include <npll/drivers.h>

enum gcnRev H_GCNRev = 0;

static __attribute__((noreturn)) void gamecubeReboot(void) {
	/* try a PI reset */
	PI_RESET = 0x00;

	/* wacky, just hang */
	while (1);
}

static __attribute__((noreturn)) void gamecubePanic(const char *str) {
	(void)str;

	udelay(1000 * 2500);

	gamecubeReboot();
}

static struct platOps gamecubePlatOps = {
	.panic = gamecubePanic,
	.debugWriteChar = NULL,
	.debugWriteStr = NULL,
	.reboot = gamecubeReboot,
	.shutdown = NULL,
	.exit = NULL /* does Swiss or whatever offer any way? */
};

void __attribute__((noreturn)) H_InitGameCube(void) {
	/* we want to load GameCube drivers */
	D_DriverMask = DRIVER_ALLOW_GAMECUBE;

	/* set plat ops */
	H_PlatOps = &gamecubePlatOps;

	/* debug console */
	H_TinyUGInit();

	/* kick off the real init */
	I_InitCommon();

	gamecubePanic("I_InitCommon should not return");
	__builtin_unreachable();

}
