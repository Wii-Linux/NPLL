/*
 * NPLL - GameCube Init
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "GCN"
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/flipper/vi.h>
#include <npll/init.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/panic.h>
#include <npll/soc.h>
#include <npll/timer.h>
#include <npll/tiny_usbgecko.h>
#include <npll/types.h>

enum gcnRev H_GCNRev = 0;
bool H_GCNIsDevkit = false;
char *H_GCNIPLRev = NULL;

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

static __attribute__((noreturn)) void gamecubeExit(void) {
	void (*stub)(void);

	IRQ_Disable();
	H_PrepareForExecEntry();
	/*
	 * libogc skips __VIInit when ENB is set, then waits for a retrace, which
	 * will never happen since we disable the display interrupts; force the
	 * loader to re-initialize VI.
	 */
	H_VIDisable();
	stub = (void (*)(void))(MEM1_CACHED_BASE + 0x1800);
	stub();
	__builtin_unreachable();
}

static struct platOps gamecubePlatOps = {
	.panic = gamecubePanic,
	.debugWriteChar = NULL,
	.debugWriteStr = NULL,
	.reboot = gamecubeReboot,
	.shutdown = NULL,
	.exit = NULL,
	.ejectDisc = NULL
};

void __attribute__((noreturn)) H_InitGameCube(void) {
	u32 *sig;

	/* we want to load GameCube drivers */
	D_DriverMask = DRIVER_ALLOW_GAMECUBE;

	/* set plat ops */
	H_PlatOps = &gamecubePlatOps;

	/* debug console */
	H_TinyUGInit();

	sig = (u32 *)(MEM1_CACHED_BASE + 0x1804);
	/* ASCII 'STUBHAXX' */
	if ((*sig++ == 0x53545542 || *sig++ == 0x53545542) && *sig == 0x48415858) {
		log_puts("Detected Swiss-compatible reload stub");
		H_PlatOps->exit = gamecubeExit;
	}

	/* kick off the real init */
	I_InitCommon();

	gamecubePanic("I_InitCommon should not return");
	__builtin_unreachable();

}
