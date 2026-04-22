/*
 * NPLL - Flipper/Hollywood/Latte Hardware - PI Reset SWitch (RSW) Input
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "RSW"

#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/irq.h>
#include <npll/input.h>
#include <npll/log.h>
#include <npll/regs.h>
#include <npll/timer.h>
#include <npll/types.h>

static REGISTER_DRIVER(rswDrv);

/* the reset button is bouncy, need to account for this */
#define DEBOUNCE_US (33 * 1000)
#define HOLD_US (2000 * 1000)

static bool pressed = false;
static bool selected = false;
static u8 evCounter = 0;
static u8 holdCounter = 0;

#define PACK_DATA2(x, y) (void *)((((u8)(x)) << 8) | (((u8)(y)) << 0))
#define UNPACK_DATA2(d, x, y) \
	y = (u8)(((u32)d) & 0x000000ff); \
	x = (u8)((((u32)d) & 0x0000ff00) >> 8);

static void rswHoldTimerHandler(void *data) {
	u8 prevEvCtr, prevHoldCtr;
	UNPACK_DATA2(data, prevEvCtr, prevHoldCtr);

	if (prevEvCtr != evCounter || prevHoldCtr != holdCounter)
		return; /* don't care, user didn't hold */

	selected = true;
	IN_NewEvent(INPUT_EV_SELECT);
}

static void rswTimerHandler(void *data) {
	bool prevWasPressed;
	u8 prevCtr;
	UNPACK_DATA2(data, prevCtr, prevWasPressed);

	if (prevCtr != evCounter)
		return; /* don't care, was bounced event */

	/* press debounce: still held, check if holding later */
	if (prevWasPressed && pressed)
		T_QueueEvent(HOLD_US, rswHoldTimerHandler, PACK_DATA2(evCounter, ++holdCounter));

	/* release debounce: still released, fire input event (unless already selected) */
	else if (!prevWasPressed && !pressed && !selected)
		IN_NewEvent(INPUT_EV_UP);
}

static void rswIRQHandler(enum irqDev dev) {
	bool nowPressed;
	(void)dev;

	nowPressed = !(PI_INTSR & PI_IRQDEV_RSWST);
	if (pressed == nowPressed) /* nothing to do */
		return;

	log_printf("RSW IRQ, pressed=%d, nowPressed=%d, selected=%d\r\n", pressed, nowPressed, selected);
	if (!pressed && nowPressed) {
		pressed = true;
		selected = false;
		/* only check for actual final press state (and thus generate input) in DEBOUNCE_US */
		T_QueueEvent(DEBOUNCE_US, rswTimerHandler, PACK_DATA2(++evCounter, true));
	}
	else if (pressed && !nowPressed) {
		pressed = false;
		T_QueueEvent(DEBOUNCE_US, rswTimerHandler, PACK_DATA2(++evCounter, false));
	}
}

static void rswInit(void) {
	/* register our IRQ handler */
	IRQ_RegisterHandler(IRQDEV_RSW, rswIRQHandler);
	IRQ_Unmask(IRQDEV_RSW);

	/* we're all good */
	rswDrv.state = DRIVER_STATE_READY;
}

static void rswCleanup(void) {
	rswDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(rswDrv) = {
	.name = "PI Reset Button",
	.mask = DRIVER_ALLOW_WII, /* Wii U doesn't have it, GCN has it but we don't have other face buttons */
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_OTHER,
	.init = rswInit,
	.cleanup = rswCleanup
};
