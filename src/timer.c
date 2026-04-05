/*
 * NPLL - Timing
 *
 * Copyright (C) 2025-2026 Techflash
 */

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
	bool active;
};

#define MAX_EVENTS 32

static struct timedEvent events[MAX_EVENTS];
static struct repeatingEvent repeatingEvents[MAX_EVENTS];
uint numRepeatingEvents = 0;

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

void T_QueueEvent(u32 fireInUsecs, void (*callback)(void *), void *cbData) {
	int idx, max;
	bool irqs;
	u64 fireTB = mftb() + (fireInUsecs * ticksPerUsec);

	irqs = IRQ_DisableSave();

	max = latestQueuedEvent();
	idx = eventIdxForTB(fireTB);

	/* shift queue forward to make room */
	if (max != -1) /* don't shift nothing */
		memmove(&events[idx + 1], &events[idx], (uint)(max - idx + 1) * sizeof(struct timedEvent));

	/* insert the event */
	events[idx].fireTB = fireTB;
	events[idx].callback = callback;
	events[idx].cbData = cbData;

	/* reprogram DEC if this is the first queued event */
	if (idx == 0) {
		u64 now = mftb();
		if (events[0].fireTB <= now)
			mtdec(0);
		else
			mtdec((u32)(events[0].fireTB - now));
	}

	IRQ_Restore(irqs);
}

static void repeatingEventCB(void *dat) {
	struct repeatingEvent *repEv;
	repEv = (struct repeatingEvent *)dat;

	T_QueueEvent(repEv->periodUsecs, repeatingEventCB, dat);

	if (repEv->active)
		return;

	repEv->active = true;
	repEv->callback(repEv->cbData);
	repEv->active = false;
}

void T_QueueRepeatingEvent(u32 periodUsecs, void (*callback)(void *), void *cbData) {
	repeatingEvents[numRepeatingEvents].periodUsecs = periodUsecs;
	repeatingEvents[numRepeatingEvents].callback = callback;
	repeatingEvents[numRepeatingEvents].cbData = cbData;
	T_QueueEvent(periodUsecs, repeatingEventCB, &repeatingEvents[numRepeatingEvents++]);
}

void T_DECHandler(void) {
	u64 tb;
	struct timedEvent ev;

	/* consume all pending events */
	while (true) {
		tb = mftb();
		if (tb < events[0].fireTB || !events[0].callback)
			break;

		/* stash the event */
		memcpy(&ev, &events[0], sizeof(struct timedEvent));

		/* shift list forward */
		memmove(&events[0], &events[1], sizeof(events) - sizeof(struct timedEvent));

		/*
		 * set DEC to the next callback if we have something else in the queue,
		 * else set it to the max possible value (do this now instead of later
		 * so that if the just-popped-out event takes too long we can still take
		 * the DEC exception for the next one)
		 */
		if (events[0].callback) {
			tb = mftb();
			if (events[0].fireTB <= tb)
				mtdec(0);
			else
				mtdec((u32)(events[0].fireTB - tb));
		} else
			mtdec(0xffffffff);

		ev.callback(ev.cbData);
	}

	if (events[0].callback) {
		tb = mftb();
		if (events[0].fireTB <= tb)
			mtdec(0);
		else
			mtdec((u32)(events[0].fireTB - tb));
	} else
		mtdec(0xffffffff);
}
