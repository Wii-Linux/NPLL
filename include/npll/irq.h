/*
 * NPLL - IRQ handling
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _IRQ_H
#define _IRQ_H

#include <stdbool.h>

enum irqDev {
	IRQDEV_GPIOB,
	IRQDEV_GPIO,
	IRQDEV_SDHCI0,
	IRQDEV_SDHCI1,
	IRQDEV_SDHCI2,
	IRQDEV_SDHCI3,
	IRQDEV_RSW,
	IRQDEV_DI,
	IRQDEV_EHCI0,
	IRQDEV_OHCI0,
	IRQDEV_OHCI1,
	IRQDEV_NAND,
	IRQDEV_AES,
	IRQDEV_SI,
	IRQDEV_SHA1,
	IRQDEV_EHCI1,
	IRQDEV_OHCI2,
	IRQDEV_EHCI2,
	IRQDEV_OHCI3,
	IRQDEV_MAX
};

typedef void (*irqHandler_t)(enum irqDev dev);

extern void IRQ_Handle(void);
extern void __attribute__((noreturn)) IRQ_Return(void);
extern void IRQ_Init(void);
extern void IRQ_Enable(void);
extern void IRQ_Disable(void);
extern void IRQ_RegisterHandler(enum irqDev dev, irqHandler_t func);
extern bool IRQ_DisableSave(void);
static inline void IRQ_Restore(bool enabled) {
	if (enabled)
		IRQ_Enable();
	/* already disabled, no need */
}
extern bool IRQ_CanWait(enum irqDev dev);
extern void IRQ_WaitLocked(enum irqDev dev, unsigned int timeoutUsecs);
extern void IRQ_Mask(enum irqDev dev);
extern void IRQ_Unmask(enum irqDev dev);

#endif /* _IRQ_H */
