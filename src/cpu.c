/*
 * NPLL - CPU Cleanup
 *
 * Copyright (C) 2026 Techflash
 */

#include "npll/console.h"
#define MODULE "CPU"
#include <npll/cache.h>
#include <npll/cpu.h>
#include <npll/log.h>

void CPU_L2Enable(void) {
	u32 l2cr, scratch;

	/* invalidate and enable L2$ */
	log_puts("disable L1I$");
	mtspr(HID0, mfspr(HID0) & ~HID0_ICE); sync();
	log_puts("disabling");
	mtspr(L2CR, 0); sync();
	l2cr = L2CR_L2I;
	log_puts("invalidating");
	asm volatile(
		"mtspr 1017, %0\n" /* L2CR */
		"wait_inval_enable:\n"
		"mfspr %0, 1017\n" /* L2CR */
		"andi. %1, %0, 1\n" /* L2IP */
		"cmplwi %1, 0\n"
		"bne wait_inval_enable\n"
		: "+r"(l2cr), "=r"(scratch)
	);

	mtspr(L2CR, L2CR_L2I); sync();
	log_puts("waiting on inval");
	while (mfspr(L2CR) & L2CR_L2IP) barrier();
	log_puts("disabling");
	mtspr(L2CR, 0); sync();
	log_puts("enabling");
	mtspr(L2CR, L2CR_L2E);
	log_puts("enabling L1I$");
	mtspr(HID0, mfspr(HID0) | HID0_ICE); sync();
}

void CPU_L2Disable(void) {
	u32 l2cr, scratch;

	/* invalidate and enable L2$ */
	log_puts("disable L1I$");
	mtspr(HID0, mfspr(HID0) & ~HID0_ICE); sync();
	log_puts("disabling");
	mtspr(L2CR, 0); sync();
	l2cr = L2CR_L2I;
	log_puts("invalidating");
	asm volatile(
		"mtspr 1017, %0\n" /* L2CR */
		"wait_inval_disable:\n"
		"mfspr %0, 1017\n" /* L2CR */
		"andi. %1, %0, 1\n" /* L2IP */
		"cmplwi %1, 0\n"
		"bne wait_inval_disable\n"
		: "+r"(l2cr), "=r"(scratch)
	);

	mtspr(L2CR, L2CR_L2I); sync();
	log_puts("waiting on inval");
	while (mfspr(L2CR) & L2CR_L2IP) barrier();
	log_puts("disabling");
	mtspr(L2CR, 0); sync();
	log_puts("enabling L1I$");
	mtspr(HID0, mfspr(HID0) | HID0_ICE); sync();
}

void CPU_Init(void) {
	log_puts("hid");
	/* clean up the CPU state (mainly trying to repair crap libogc does), doesn't really have anywhere better to go */
	/* DPM/NHR on, BHT/BTIC/DCFA/SPD off */
	mtspr(HID0, (mfspr(HID0) & ~(HID0_BHT | HID0_BTIC | HID0_DCFA | HID0_SPD | HID0_DPM)) | HID0_NHR);
	/* we don't care about Paired Singles nor the Write Gather Pipe right now, sorry */
	mtspr(HID2, mfspr(HID2) & ~(HID2_LSQE | HID2_PSE | HID2_WPE));

	/* Gekko doesn't have HID4, so gate off GCN here */
	/* ST0/LPE/(nonexistent)PS2_CTL off, L2_CCFI on, L2 Fetch Mode=64B, Bus Pipeline Depth=2, L2 2nd BCO on, L2 configured as 2-deep miss-under-miss cache */
	if (H_ConsoleType != CONSOLE_TYPE_GAMECUBE)
		mtspr(HID4, (mfspr(HID4) & ~(HID4_ST0 | HID4_LPE | HID4_PS2_CTL)) | HID4_L2_CCFI | HID4_L2FM_64B | HID4_BPD_2 | HID4_BCO | HID4_L2MUM);

	mtmsr(mfmsr() & ~MSR_ME);

	CPU_L2Enable();
}
