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
	IRQDEV_MAX
};

typedef void (*irqHandler_t)(enum irqDev dev);

extern void __attribute__((noreturn)) IRQ_Handle(void);
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

#endif /* _IRQ_H */
