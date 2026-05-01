/*
 * NPLL - Timing
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <npll/console.h>
#include <npll/irq.h>
#include <npll/timer.h>
#include <npll/types.h>

struct timedEvent {
	u64  fireTB;
	void (*callback)(void *);
	void *cbData;
};

struct repeatingEvent {
	u32 periodUsecs;
	void (*callback)(void *);
	void *cbData;
};

#define MAX_EVENTS 32
#define DEC_IDLE 0x7fffffff

static struct timedEvent events[MAX_EVENTS];
static struct repeatingEvent repeatingEvents[MAX_EVENTS];
uint numRepeatingEvents = 0;
static bool eventsEnabled = false;

/* bus clock / 4 */
static u32 possibleTicksPerUsec[] = {
	0, /* platform 0 (invalid) */
	162 / 4, /* platform 1 (GameCube) */
	243 / 4, /* platform 2 (Wii) */
	248 / 4, /* platform 3 (Wii U) */
};
static u32 ticksPerUsec;


/* spin waiting for [ticks] ticks of the timebase */
static void spinOnTB(u64 ticks) {
	u64 start = mftb();
	while ((mftb() - start) < ticks) {
		/* TODO: would do real work here if it were more advanced */
	}
}

bool T_HasElapsed(u64 startTB, u32 usecSince) {
	u64 tb, ticks;
	tb = mftb();
	ticks = (u64)ticksPerUsec * usecSince;
	return (tb >= (startTB + ticks));
}

/* delay for [n] microseconds */
void udelay(u32 usec) {
	spinOnTB((u64)ticksPerUsec * usec);
}

void T_Init(void) {
	ticksPerUsec = possibleTicksPerUsec[H_ConsoleType];
	memset(events, 0, sizeof(events));
	eventsEnabled = false;
	mtdec(DEC_IDLE);
}

static int latestQueuedEvent(void) {
	int i;

	for (i = 0; i < MAX_EVENTS; i++) {
		if (!events[i].callback)
			break;
	}
	return i - 1;
}

static int eventIdxForTB(u64 tb) {
	int i;

	for (i = 0; i < MAX_EVENTS; i++) {
		if (!events[i].callback || events[i].fireTB > tb)
			break;
	}

	return i;
}

static void programNextDEC(u64 now) {
	u64 delta;

	if (!events[0].callback) {
		mtdec(DEC_IDLE);
		return;
	}

	if (events[0].fireTB <= now) {
		mtdec(1);
		return;
	}

	delta = events[0].fireTB - now;
	if (delta > DEC_IDLE)
		delta = DEC_IDLE;
	mtdec((u32)delta);
}

void T_QueueEvent(u32 fireInUsecs, void (*callback)(void *), void *cbData) {
	int idx, max;
	bool irqs;
	u64 fireTB = mftb() + ((u64)fireInUsecs * ticksPerUsec);

	irqs = IRQ_DisableSave();

	max = latestQueuedEvent();
	idx = eventIdxForTB(fireTB);
	assert_msg(max != MAX_EVENTS - 1, "T_QueueEvent: events overflow");

	/* shift queue forward to make room */
	if (max != -1 && idx <= max) /* don't shift nothing */
		memmove(&events[idx + 1], &events[idx], (uint)(max - idx + 1) * sizeof(struct timedEvent));

	/* insert the event */
	events[idx].fireTB = fireTB;
	events[idx].callback = callback;
	events[idx].cbData = cbData;

	/* reprogram DEC if this is the first queued event */
	if (eventsEnabled && idx == 0)
		programNextDEC(mftb());

	IRQ_Restore(irqs);
}

void T_EnableEvents(void) {
	bool irqs;

	irqs = IRQ_DisableSave();
	eventsEnabled = true;
	programNextDEC(mftb());
	IRQ_Restore(irqs);
}

static void repeatingEventCB(void *dat) {
	struct repeatingEvent *repEv;
	repEv = (struct repeatingEvent *)dat;

	repEv->callback(repEv->cbData);
	T_QueueEvent(repEv->periodUsecs, repeatingEventCB, dat);
}

void T_QueueRepeatingEvent(u32 periodUsecs, void (*callback)(void *), void *cbData) {
	assert_msg(numRepeatingEvents != MAX_EVENTS, "T_QueueRepeatingEvent: repeatingEvents overflow");
	repeatingEvents[numRepeatingEvents].periodUsecs = periodUsecs;
	repeatingEvents[numRepeatingEvents].callback = callback;
	repeatingEvents[numRepeatingEvents].cbData = cbData;
	T_QueueEvent(periodUsecs, repeatingEventCB, &repeatingEvents[numRepeatingEvents++]);
}

void T_DECHandler(void) {
	u64 tb;
	struct timedEvent ev;
	bool irqs;

	/* consume all pending events */
	while (true) {
		irqs = IRQ_DisableSave();
		tb = mftb();
		if (!eventsEnabled) {
			mtdec(DEC_IDLE);
			IRQ_Restore(irqs);
			break;
		}
		if (!events[0].callback || tb < events[0].fireTB) {
			programNextDEC(tb);
			IRQ_Restore(irqs);
			break;
		}

		/* stash the event */
		memcpy(&ev, &events[0], sizeof(struct timedEvent));

		/* shift list forward */
		memmove(&events[0], &events[1], sizeof(events) - sizeof(struct timedEvent));
		memset(&events[MAX_EVENTS - 1], 0, sizeof(struct timedEvent));

		/*
		 * If the next event was already due when we entered this pass,
		 * drain it after this callback to preserve event order.
		 */
		if (events[0].callback && events[0].fireTB <= tb)
			mtdec(DEC_IDLE);
		else
			programNextDEC(tb);
		IRQ_Enable();

		ev.callback(ev.cbData);
	}
}
