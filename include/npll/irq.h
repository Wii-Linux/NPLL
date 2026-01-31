/*
 * NPLL - IRQ handling
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _IRQ_H
#define _IRQ_H


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

#endif /* _IRQ_H */
