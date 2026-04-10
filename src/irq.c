/*
 * NPLL - IRQ handling
 *
 * Copyright (C) 2026 Techflash
 */

#include <assert.h>
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
	if (H_ConsoleType != CONSOLE_TYPE_WII_U) {
		/* mask all Flipper IRQs */
		PI_INTMR = 0;

		/* ack all Flipper IRQs */
		PI_INTSR = PI_INTSR;
	}

	if (H_ConsoleType == CONSOLE_TYPE_WII) {
		/* unmask the Reset Switch as well as Hollywood IRQs in the Flipper PIC */
		PI_INTMR |= PI_IRQDEV_RSW | PI_IRQDEV_HLWD;

		/* mask and ack all Hollywood IRQs */
		HW_PPCIRQMASK = 0;
		HW_PPCIRQFLAG = HW_PPCIRQFLAG;

		/* unmask GPIO and SDHCI0 */
		HW_PPCIRQMASK |= HW_IRQDEV_GPIOB | HW_IRQDEV_GPIO | HW_IRQDEV_SDHCI0;
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

		/* mask and ack all IRQs in the Latte PI */
		LATTE_PI_INTMR = 0;
		LATTE_PI_INTMR0 = 0;
		LATTE_PI_INTMR1 = 0;
		LATTE_PI_INTMR2 = 0;
		LATTE_PI_INTSR = LATTE_PI_INTSR;
		LATTE_PI_INTSR0 = LATTE_PI_INTSR0;
		LATTE_PI_INTSR1 = LATTE_PI_INTSR1;
		LATTE_PI_INTSR2 = LATTE_PI_INTSR2;

		/* unmask GPIO and SDHCI0 */
		LT_PPC0INT1EN |= HW_IRQDEV_GPIOB | HW_IRQDEV_GPIO | HW_IRQDEV_SDHCI0;

		/* unmask Latte IRQs in the Latte PI */
		LATTE_PI_INTMR |= PI_IRQDEV_LATTE;
		LATTE_PI_INTMR0 |= PI_IRQDEV_LATTE;
	}

	/* clear all handlers */
	memset(IRQ_Handlers, 0, sizeof(IRQ_Handlers));
}

static void IRQ_DoHandle(enum irqDev dev) {
	if (IRQ_Handlers[dev])
		IRQ_Handlers[dev](dev);
}

void __attribute__((noreturn)) IRQ_Handle(void) {
	u32 intsr, ppcirqflag, ppc0int1sts;

	if (H_ConsoleType != CONSOLE_TYPE_WII_U)
		intsr = PI_INTSR;

	if (H_ConsoleType == CONSOLE_TYPE_WII && intsr & PI_IRQDEV_RSW) {
		PI_INTSR = PI_IRQDEV_RSW;
		IRQ_DoHandle(IRQDEV_RSW);
	}
	if (H_ConsoleType == CONSOLE_TYPE_WII && intsr & PI_IRQDEV_HLWD) {
		ppcirqflag = HW_PPCIRQFLAG;
		PI_INTSR = PI_IRQDEV_HLWD;
		if (ppcirqflag & HW_IRQDEV_GPIOB) {
			HW_PPCIRQFLAG = HW_IRQDEV_GPIOB;
			IRQ_DoHandle(IRQDEV_GPIOB);
		}
		if (ppcirqflag & HW_IRQDEV_GPIO) {
			HW_PPCIRQFLAG = HW_IRQDEV_GPIO;
			IRQ_DoHandle(IRQDEV_GPIO);
		}
		if (ppcirqflag & HW_IRQDEV_SDHCI0) {
			HW_PPCIRQFLAG = HW_IRQDEV_SDHCI0;
			IRQ_DoHandle(IRQDEV_SDHCI0);
		}
	}

	if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		intsr = LATTE_PI_INTSR;
		if (!(intsr & PI_IRQDEV_LATTE))
			IRQ_Return();

		ppc0int1sts = LT_PPC0INT1STS;
		PI_INTSR = PI_IRQDEV_LATTE;
		if (ppc0int1sts & HW_IRQDEV_GPIOB) {
			LT_PPC0INT1STS = HW_IRQDEV_GPIOB;
			IRQ_DoHandle(IRQDEV_GPIOB);
		}
		if (ppc0int1sts & HW_IRQDEV_GPIO) {
			LT_PPC0INT1STS = HW_IRQDEV_GPIO;
			IRQ_DoHandle(IRQDEV_GPIO);
		}
		if (ppc0int1sts & HW_IRQDEV_SDHCI0) {
			LT_PPC0INT1STS = HW_IRQDEV_SDHCI0;
			IRQ_DoHandle(IRQDEV_SDHCI0);
		}

		LATTE_PI_INTSR = PI_IRQDEV_LATTE;
	}

	IRQ_Return();
}

void IRQ_RegisterHandler(enum irqDev dev, irqHandler_t func) {
	assert(!IRQ_Handlers[dev]);
	IRQ_Handlers[dev] = func;
}
