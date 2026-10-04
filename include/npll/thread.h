/*
 * NPLL - Threading
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _THREAD_H
#define _THREAD_H
#include <stdbool.h>
#include <npll/allocator.h>
#include <npll/types.h>

struct threadMutex {
	void *owner;
	uint depth;
};

void TH_Lock(struct threadMutex *mutex);
void TH_Unlock(struct threadMutex *mutex);

/* Pause starting new callbacks and join other active callbacks before teardown */
void TH_Quiesce(void);
void TH_Resume(void);
/* Join other callbacks, then enter a new thread with IRQs and switching disabled.
 * The callback must not return. The calling thread is abandoned.
 */
void TH_Handoff(enum pool_idx pool, void (*callback)(void *), void *data)
	__attribute__((noreturn));
void TH_Init(void);
void TH_BootComplete(void);
bool TH_CanBlock(void);
void TH_Yield(void);
void TH_Sleep(u32 usecs);
/*
 * Caller holds IRQs disabled across testing its condition and this call.
 * Returns with IRQs still disabled. A timeout is a wakeup, not completion.
 */
void TH_WaitLocked(const void *channel, u32 timeoutUsecs);
void TH_Wake(const void *channel);
void TH_InterruptEnter(void);
void TH_InterruptLeave(void);

#endif /* _THREAD_H */
