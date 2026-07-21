/*
 * NPLL - Cleanup
 *
 * Copyright (C) 2026 Techflash
 */

#include <stdio.h>
#include <npll/block.h>
#include <npll/cpu.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/fs.h>
#include <npll/input.h>
#include <npll/irq.h>
#include <npll/iostats.h>
#include <npll/log_internal.h>
#include <npll/output.h>
#include <npll/soc.h>
#include <npll/video.h>
#include <npll/usb.h>

void (*H_PreEntryHook)(void) = NULL;

/*
 * We work backwards to give us the best shot at freeing any allocations made,
 * since the allocator can only free the most recent allocation
 */
void H_PrepareForExecEntry(void) {
	int curType, firstType, lastType;
	struct driver *curDriver;

	L_Method = LOG_METHOD_ALL_ODEV;
	printf("\x1b[1;1H\x1b[2J");

	/* Last chance to report: H_PrepareForExecEntry() takes logging down. */
	IOStats_Dump("linux load");

	/* disable IRQs */
	IRQ_Disable();
	USB_Shutdown();

	/* unmount the current FS */
	FS_Unmount();

	/* shutdown all drivers in the reverse order */
	lastType = DRIVER_TYPE_BLOCK;
	firstType = DRIVER_TYPE_END - 1;
	curType = firstType;

	for (; curType >= lastType; curType--) {
		curDriver = __drivers_start;
		while ((uintptr_t)curDriver < ((uintptr_t)__drivers_end) - 1) {
			if ((curDriver->state == DRIVER_STATE_READY ||
			     curDriver->state == DRIVER_STATE_INITIALIZING_CLEANABLE) &&
			    curDriver->type == (uint)curType && curDriver->cleanup)
				curDriver->cleanup();

			curDriver++;
		}
	}

	/* shutdown the block and FS layers */
	FS_Shutdown();
	B_Shutdown();

	/* kill off the last of them */
	lastType = DRIVER_TYPE_START + 1;
	curType = firstType;
	for (; curType >= lastType; curType--) {
		curDriver = __drivers_start;
		while ((uintptr_t)curDriver < ((uintptr_t)__drivers_end) - 1) {
			if ((curDriver->state == DRIVER_STATE_READY ||
			     curDriver->state == DRIVER_STATE_INITIALIZING_CLEANABLE) &&
			    curDriver->type == (uint)curType && curDriver->cleanup)
				curDriver->cleanup();

			curDriver++;
		}
	}

	/*
	 * disable the L2 cache, Linux has some problems with it...
	 * newer builds can re-enable it post-init if possible
	 */
	CPU_L2Disable();

	/* mask and ack all IRQs */
	switch (H_ConsoleType) {
	case CONSOLE_TYPE_WII_U: {
		LT_PPC0INT1EN = 0;
		LT_PPC1INT1EN = 0;
		LT_PPC2INT1EN = 0;
		LT_PPC0INT2EN = 0;
		LT_PPC1INT2EN = 0;
		LT_PPC2INT2EN = 0;

		LT_PPC0INT1STS = LT_PPC0INT1STS;
		LT_PPC1INT1STS = LT_PPC1INT1STS;
		LT_PPC2INT1STS = LT_PPC2INT1STS;
		LT_PPC0INT2STS = LT_PPC0INT2STS;
		LT_PPC1INT2STS = LT_PPC1INT2STS;
		LT_PPC2INT2STS = LT_PPC2INT2STS;
	}
		/* fallthrough */
	case CONSOLE_TYPE_WII: {
		HW_PPCIRQMASK = 0;
		HW_PPCIRQFLAG = HW_PPCIRQFLAG;
		HW_RESETS |= RESETS_RSTB_DSP;
	}
		/* fallthrough */
	case CONSOLE_TYPE_GAMECUBE: {
		PI_INTMR = 0;
		PI_INTSR = PI_INTSR;
		break;
	}
	}

	/* now somewhat more well prepared for executable entry */
}
