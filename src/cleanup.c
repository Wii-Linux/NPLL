/*
 * NPLL - Cleanup
 *
 * Copyright (C) 2026
 */

#include <npll/block.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/fs.h>
#include <npll/input.h>
#include <npll/irq.h>
#include <npll/output.h>
#include <npll/regs.h>
#include <npll/video.h>

/*
 * We work backwards to give us the best shot at freeing any allocations made,
 * since the allocator can only free the most recent allocation
 */
void H_PrepareForExecEntry(void) {
	uint i;
	struct driver *curDriver;

	/* unmount the current FS */
	FS_Unmount();

	/* Unregister all block devices, and as such, any partitions associated with them as well */
	for (i = B_NumDevices; i > 0; i--) {
		if (B_Devices[i])
			B_Unregister(B_Devices[i]);
	}

	/* do one final V_Flush */
	V_Flush();

	/* shutdown all remaining drivers */
	curDriver = __drivers_start;
	while ((u32)curDriver < ((u32)__drivers_end) - 1) {
		if (curDriver->state == DRIVER_STATE_READY && curDriver->cleanup)
			curDriver->cleanup();

		curDriver++;
	}

	/* shutdown the block and FS layers */
	FS_Shutdown();
	B_Shutdown();

	/* disable IRQs */
	IRQ_Disable();

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
		/* fallthrough */
	}
	case CONSOLE_TYPE_WII: {
		HW_PPCIRQMASK = 0;
		HW_PPCIRQFLAG = HW_PPCIRQFLAG;
		/* fallthrough */
	}
	case CONSOLE_TYPE_GAMECUBE: {
		PI_INTMR = 0;
		PI_INTSR = PI_INTSR;
		break;
	}
	}

	/* now somewhat more well prepared for executable entry */
}
