/*
 * NPLL - IRQ handling
 *
 * Copyright (C) 2026 Techflash
 */

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <npll/cpu.h>
#include <npll/console.h>
#include <npll/regs.h>
#include <npll/irq.h>

static irqHandler_t IRQ_Handlers[IRQDEV_MAX];

bool IRQ_DisableSave(void) {
	u32 msr;
	bool ret;

	asm("mfmsr	%0" : "=r" (msr));
	ret = !!(msr & MSR_EE);

	if (ret)
		IRQ_Disable();

	/* else already disabled, no need */

	return ret;
}

void IRQ_Init(void) {
	/* mask all Flipper IRQs */
	PI_INTMR = 0;

	/* ack all Flipper IRQs */
	PI_INTSR = PI_INTSR;

	if (H_ConsoleType == CONSOLE_TYPE_WII || H_ConsoleType == CONSOLE_TYPE_WII_U) {
		/* unmask Hollywood IRQs in the Flipper PIC */
		PI_INTMR |= PI_IRQDEV_HLWD;

		/* mask and ack all Hollywood IRQs */
		HW_PPCIRQMASK = 0;
		HW_PPCIRQFLAG = HW_PPCIRQFLAG;

		/* unmask GPIO */
		HW_PPCIRQMASK |= HW_IRQDEV_GPIOB | HW_IRQDEV_GPIO;
	}

	if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		/* mask and ack all Latte IRQs */
		LT_PPC0INT1EN = 0;
		LT_PPC0INT2EN = 0;
		LT_PPC0INT1STS = LT_PPC0INT1STS;
		LT_PPC0INT2STS = LT_PPC0INT2STS;

		LT_PPC1INT1EN = 0;
		LT_PPC1INT2EN = 0;
		LT_PPC1INT1STS = LT_PPC1INT1STS;
		LT_PPC1INT2STS = LT_PPC1INT2STS;

		LT_PPC2INT1EN = 0;
		LT_PPC2INT2EN = 0;
		LT_PPC2INT1STS = LT_PPC2INT1STS;
		LT_PPC2INT2STS = LT_PPC2INT2STS;
	}

	/* clear all handlers */
	memset(IRQ_Handlers, 0, sizeof(IRQ_Handlers));
}

static void IRQ_DoHandle(enum irqDev dev) {
	if (IRQ_Handlers[dev])
		IRQ_Handlers[dev](dev);
}

void __attribute__((noreturn)) IRQ_Handle(void) {
	u32 intsr;

	puts("got irq");

	/* TODO: Handle Flipper PIC IRQs */

	/* TODO: Handle other Hollywood PIC IRQs */
	if (H_ConsoleType == CONSOLE_TYPE_WII || H_ConsoleType == CONSOLE_TYPE_WII_U) {
		intsr = HW_PPCIRQFLAG;
		if (intsr & HW_IRQDEV_GPIOB) {
			IRQ_DoHandle(IRQDEV_GPIOB);
			HW_PPCIRQFLAG = HW_IRQDEV_GPIOB;
		}
		if (intsr & HW_IRQDEV_GPIO) {
			IRQ_DoHandle(IRQDEV_GPIO);
			HW_PPCIRQFLAG = HW_IRQDEV_GPIO;
		}
	}

	/* TODO: Handle Latte PIC IRQs */

	IRQ_Return();
}

void IRQ_RegisterHandler(enum irqDev dev, irqHandler_t func) {
	assert(!IRQ_Handlers[dev]);
	IRQ_Handlers[dev] = func;
}
