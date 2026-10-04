/*
 * NPLL - Timing
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <npll/console.h>
#include <npll/cpu.h>
#include <npll/irq.h>
#include <npll/thread.h>
#include <npll/timer.h>
#include <npll/types.h>

#define MAX_EVENTS 64
#define DEC_IDLE 0x7fffffff
#define THREAD_STACK_SIZE (64 * 1024)

enum threadState {
	FREE,
	READY,
	RUNNING,
	WAITING
};

struct thread {
	u32 *sp;
	void *stack;
	enum threadState state;
	const void *channel;
	u64 deadline;
	void (*callback)(void *);
	void *data;
	u32 period, generation;
	bool active, cancelled;
};

static struct thread threads[MAX_EVENTS + 1];
static struct thread *current;
static uint cursor, interruptDepth;
static bool eventsEnabled, stopping;
extern void TH_Switch(u32 **oldSP, u32 **newSP);
static void programNextDEC(u64 now);
static void schedule(void);

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
	while ((mftb() - start) < ticks);
}

bool T_HasElapsed(u64 startTB, u32 usecSince) {
	u64 tb, ticks;
	tb = mftb();
	ticks = (u64)ticksPerUsec * usecSince;
	return (tb >= (startTB + ticks));
}

u32 T_ElapsedUsecs(u64 startTB) {
	u64 delta;
	u64 tb = mftb();

	if (tb <= startTB || !ticksPerUsec)
		return 0;

	/* Narrow before dividing: a u64 divide would pull in __udivdi3, and
	 * the image has no room to spare for it. */
	delta = tb - startTB;
	if (delta > 0xffffffffull)
		return 0xffffffffu;

	return (u32)delta / ticksPerUsec;
}

/* delay for [n] microseconds */
/*
 * FIXME: udelay should really only be a spin loop, but there's too many
 * callers to fix right now.  Eventually make an msleep/sleep() and migrate
 * udelay callers with large delays off.
 */
void udelay(u32 usec) {
	u32 msr;

	asm volatile("mfmsr %0" : "=r"(msr));
	/* keep spins for short delays */
	if (TH_CanBlock() && (msr & MSR_EE) && usec > 1000)
		TH_Sleep(usec);
	else
		spinOnTB((u64)ticksPerUsec * usec);
}

void T_Init(void) {
	ticksPerUsec = possibleTicksPerUsec[H_ConsoleType];
	eventsEnabled = false;
	mtdec(DEC_IDLE);
}

void TH_Init(void) {
	memset(threads, 0, sizeof(threads));
	current = &threads[0];
	current->state = RUNNING;
	current->active = true;
	stopping = false;
	cursor = interruptDepth = 0;
}

void TH_BootComplete(void) {
	bool enabled = IRQ_DisableSave();
	threads[0].active = false;
	TH_Wake(&threads[0]);
	IRQ_Restore(enabled);
}

void TH_InterruptEnter(void) {
	interruptDepth++;
}

void TH_InterruptLeave(void) {
	assert(interruptDepth);
	interruptDepth--;
}

bool TH_CanBlock(void) {
	return current && eventsEnabled && !interruptDepth;
}

static void programNextDEC(u64 now) {
	u64 next = now + DEC_IDLE, delta;
	uint i;

	if (eventsEnabled) {
		for (i = 0; i <= MAX_EVENTS; i++) {
			if (threads[i].state == WAITING && threads[i].deadline < next)
				next = threads[i].deadline;
		}
	}

	delta = next > now ? next - now : 1;
	mtdec((u32)delta);
}

static void threadEntry(void) {
	while (true) {
		IRQ_Enable();
		current->active = true;
		current->callback(current->data);
		IRQ_Disable();
		current->active = false;
		TH_Wake(current);

		if (current->period && !current->cancelled) {
			current->deadline = mftb() + (u64)current->period * ticksPerUsec;
			current->state = WAITING;
		}
		else
			current->state = FREE;

		schedule();
	}
}

static void prepareThread(struct thread *t) {
	u32 r13;

	/* FIXME specify per thread */
	if (!t->stack)
		t->stack = malloc(THREAD_STACK_SIZE);

	t->sp = (u32 *)((char *)t->stack + THREAD_STACK_SIZE - 112);
	memset(t->sp, 0, 112);
	*(u32 *)t->stack = 0xdeaddead;
	t->sp[1] = (u32)(uintptr_t)threadEntry;
	asm volatile("mr %0,13" : "=r"(r13));
	t->sp[5] = r13;
}

static void schedule(void) {
	struct thread *old = current, *next;
	uint n, idx;

	assert_msg(!old->stack || *(u32 *)old->stack == 0xdeaddead, "thread stack overflow");

	while (true) {
		for (n = 1; n <= MAX_EVENTS + 1; n++) {
			idx = (cursor + n) % (MAX_EVENTS + 1);
			next = &threads[idx];
			if (next->state != READY || (stopping && idx && !next->active))
				continue;

			if (next != old && !next->sp)
				prepareThread(next);

			cursor = idx;
			current = next;
			next->state = RUNNING;
			programNextDEC(mftb());
			if (next != old)
				TH_Switch(&old->sp, &next->sp);

			return;
		}

		programNextDEC(mftb());
		/* nothing to do */
		CPU_Idle();
	}
}

void TH_Yield(void) {
	bool irqs;

	if (!TH_CanBlock())
		return;

	irqs = IRQ_DisableSave();
	assert_msg(irqs, "yield with interrupts disabled");
	current->state = READY;
	schedule();
	IRQ_Restore(irqs);
}

void TH_WaitLocked(const void *channel, u32 timeoutUsecs) {
	assert(TH_CanBlock());
	current->channel = channel;
	current->deadline = mftb() + (u64)timeoutUsecs * ticksPerUsec;
	current->state = WAITING;
	schedule();
	current->channel = NULL;
}

void TH_Sleep(u32 usecs) {
	bool irqs = IRQ_DisableSave();

	assert_msg(irqs, "sleep with interrupts disabled");
	TH_WaitLocked(NULL, usecs);
	IRQ_Restore(irqs);
}

void TH_Wake(const void *channel) {
	uint i;
	bool irqs = IRQ_DisableSave();

	assert(channel);

	for (i = 0; i <= MAX_EVENTS; i++) {
		if (threads[i].state == WAITING && threads[i].channel == channel)
			threads[i].state = READY;
	}

	programNextDEC(mftb());
	IRQ_Restore(irqs);
}

static void queueEvent(u32 delay, u32 period, void (*callback)(void *), void *data) {
	uint i;
	bool irqs = IRQ_DisableSave();

	assert(callback);

	for (i = 1; i <= MAX_EVENTS; i++) {
		if (threads[i].state == FREE)
			break;
	}

	assert_msg(i <= MAX_EVENTS, "timer threads overflow");

	threads[i].generation++;
	threads[i].sp = NULL;
	threads[i].callback = callback;
	threads[i].data = data;
	threads[i].period = period;
	threads[i].channel = NULL;
	threads[i].cancelled = threads[i].active = false;
	threads[i].deadline = mftb() + (u64)delay * ticksPerUsec;
	threads[i].state = WAITING;

	programNextDEC(mftb());
	IRQ_Restore(irqs);
}

void T_QueueEvent(u32 delay, void (*callback)(void *), void *data) {
	queueEvent(delay, 0, callback, data);
}

void T_QueueRepeatingEvent(u32 period, void (*callback)(void *), void *data) {
	assert(period);
	queueEvent(period, period, callback, data);
}

void T_CancelEvent(void (*callback)(void *), void *data) {
	struct thread *t;
	u32 generation;
	uint i;
	bool irqs = IRQ_DisableSave();
	for (i = 1; i <= MAX_EVENTS; i++) {
		t = &threads[i];
		generation = t->generation;
		if (t->state == FREE || t->callback != callback || t->data != data)
			continue;

		t->cancelled = true;

		if (!t->active)
			t->state = FREE;
		else if (t != current && irqs && TH_CanBlock()) {
			while (t->generation == generation && t->active)
				TH_WaitLocked(t, 0xffffffffu);
		}
	}

	programNextDEC(mftb());
	IRQ_Restore(irqs);
}

void T_CancelRepeatingEvent(void (*callback)(void *), void *data) {
	T_CancelEvent(callback, data);
}

void TH_Quiesce(void) {
	struct thread *t;
	uint i;
	bool enabled = IRQ_DisableSave();

	stopping = true;
	for (i = 0; i <= MAX_EVENTS; i++) {
		t = &threads[i];
		while (t != current && t->active) {
			assert(enabled && TH_CanBlock());
			TH_WaitLocked(t, 0xffffffffu);
		}
	}

	IRQ_Restore(enabled);
}

void TH_Resume(void) {
	bool enabled = IRQ_DisableSave();

	stopping = false;
	programNextDEC(mftb());
	IRQ_Restore(enabled);
}

void T_EnableEvents(void) {
	bool irqs = IRQ_DisableSave();

	eventsEnabled = true;
	programNextDEC(mftb());
	IRQ_Restore(irqs);
}

void T_DECHandler(void) {
	uint i;
	u64 now = mftb();

	if (eventsEnabled) {
		for (i = 0; i <= MAX_EVENTS; i++) {
			if (threads[i].state == WAITING && threads[i].deadline <= now)
				threads[i].state = READY;
		}
	}
	programNextDEC(now);
}

void TH_Lock(struct threadMutex *mutex) {
	bool enabled = IRQ_DisableSave();

	while (mutex->owner && mutex->owner != current) {
		assert_msg(enabled && TH_CanBlock(), "contended mutex in atomic context");
		TH_WaitLocked(mutex, 0xffffffffu);
	}

	mutex->owner = current;
	mutex->depth++;
	IRQ_Restore(enabled);
}

void TH_Unlock(struct threadMutex *mutex) {
	bool enabled = IRQ_DisableSave();

	assert(mutex->owner == current && mutex->depth);
	if (!--mutex->depth) {
		mutex->owner = NULL;
		TH_Wake(mutex);
	}

	IRQ_Restore(enabled);
}
