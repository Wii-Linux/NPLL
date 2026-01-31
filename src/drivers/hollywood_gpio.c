/*
 * NPLL - Hollywood/Latte Hardware - GPIO
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <stdio.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/irq.h>
#include <npll/input.h>
#include <npll/regs.h>
#include <npll/types.h>
#include <npll/hollywood/gpio.h>

static REGISTER_DRIVER(gpioDrv);

static u32 prevIn = 0;

static void gpioIRQHandler(enum irqDev dev) {
	u32 in, mask, set, clearred;
	u8 i, numSet, numClearred, setIdx[32], clearredIdx[32];
	inputEvent_t ev;
	(void)dev;

	in = HW_GPIOB_IN;
	HW_GPIOB_INTFLAG = HW_GPIOB_INTFLAG;
	HW_GPIOB_INTLVL = ~in;
	printf("GPIO IRQ for device %d; prevIn = 0x%08x, curIn = 0x%08x", dev, in, prevIn);

	set = in & ~prevIn;
	clearred = prevIn & ~in;
	numSet = numClearred = 0;
	ev = 0;

	if (set & GPIO_POWER) {
		puts("GPIO: Power button pressed");
		ev |= INPUT_EV_DOWN;
	}
	if (set & GPIO_EJECT_BTN) {
		puts("GPIO: Eject button pressed");
		ev |= INPUT_EV_SELECT;
	}
	if (set & GPIO_SLOT_IN)
		puts("GPIO: Disc inserted");
	else if (clearred & GPIO_SLOT_IN)
		puts("GPIO: Disc removed");

	if (in != prevIn) {
		for (i = 0; i < 32; i++) {
			mask = 1u << i;
			if (set & mask)      setIdx[numSet++] = i;
			if (clearred & mask) clearredIdx[numClearred++] = i;
		}

		if (numSet) {
			printf("GPIO: Now high:");
			for (i = 0; i < numSet; i++)
				printf(" %d", setIdx[i]);
			puts("");
		}
		if (numClearred) {
			printf("GPIO: Now low:");
			for (i = 0; i < numClearred; i++)
				printf(" %d", clearredIdx[i]);
			puts("");
		}
	}

	if (ev)
		IN_NewEvent(ev);

	prevIn = in;
}

static void gpioInit(void) {
	u32 dir, out;
#if 0
	printf(
"=== GPIO REGISTER DUMP ===\r\n\
HW_GPIO_OWNER:  0x%08x\r\n\
HW_GPIO_ENABLE: 0x%08x\r\n\
HW_GPIOB_DIR:   0x%08x\r\n\
HW_GPIOB_OUT:   0x%08x\r\n\
HW_GPIOB_IN:    0x%08x\r\n\
HW_GPIO_DIR:    0x%08x\r\n\
HW_GPIO_OUT:    0x%08x\r\n\
HW_GPIO_IN:     0x%08x\r\n",
HW_GPIO_OWNER, HW_GPIO_ENABLE, HW_GPIOB_DIR, HW_GPIOB_OUT,
HW_GPIOB_IN, HW_GPIO_DIR, HW_GPIO_OUT, HW_GPIO_IN);
#endif

	/* Broadway owns all */
	HW_GPIO_OWNER = 0xffffffff;

	/* all enabled */
	HW_GPIO_ENABLE = 0xffffffff;

	/* set up outputs properly */
	out = GPIO_DC_DC | GPIO_FAN | GPIO_SENSOR_BAR | GPIO_DI_SPIN;
	if (H_ConsoleType == CONSOLE_TYPE_WII_U || H_WiiIsvWii)
		out |= GPIO_GAMEPAD_EN | GPIO_PADPD;
	HW_GPIOB_OUT = out;
	HW_GPIO_OUT  = out;

	/* set proper directions */
	dir = ~(GPIO_POWER | GPIO_EJECT_BTN | GPIO_SLOT_IN | GPIO_EEP_MISO | GPIO_AVE_SDA);
	if (H_ConsoleType == CONSOLE_TYPE_WII_U || H_WiiIsvWii)
		dir |= GPIO_GAMEPAD_EN | GPIO_PADPD;

	HW_GPIOB_DIR = dir;
	HW_GPIO_DIR  = dir;

	/* register the current state of the inputs */
	prevIn = HW_GPIOB_IN;

	/* register our IRQ handler */
	IRQ_Disable();
	IRQ_RegisterHandler(IRQDEV_GPIOB, gpioIRQHandler);
	IRQ_RegisterHandler(IRQDEV_GPIO, gpioIRQHandler);

	/* set up the interrupts properly */
	HW_GPIOB_INTLVL = ~prevIn;
	HW_GPIOB_INTMASK = 0xffffffff;

	/* ack all existing interrupts */
	HW_GPIOB_INTFLAG = HW_GPIOB_INTFLAG;
	IRQ_Enable();

	/* we're all good */
	gpioDrv.state = DRIVER_STATE_READY;
}

static void gpioCleanup(void) {
	gpioDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(gpioDrv) = {
	.name = "Hollywood/Latte GPIO",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_OTHER,
	.init = gpioInit,
	.cleanup = gpioCleanup
};
