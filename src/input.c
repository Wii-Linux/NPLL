/*
 * NPLL - Top-level input handling
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <assert.h>
#include <string.h>
#include <npll/input.h>
#include <npll/irq.h>

#define MAX_INPUT_EVENT 16

static inputEvent_t queue[MAX_INPUT_EVENT];
static int queuePos = 0;

void IN_Init(void) {
	memset(queue, 0, sizeof(queue));
}

inputEvent_t IN_ConsumeEvent(void) {
	inputEvent_t ev;
	bool irqs;

	/* anything to get? */
	if (!queue[0])
		return 0;

	/* get the event */
	ev = queue[0];

	/* remove event from queue */
	irqs = IRQ_DisableSave();
	memmove(&queue[0], &queue[1], sizeof(inputEvent_t) * (MAX_INPUT_EVENT - 1));
	queuePos--;
	IRQ_Restore(irqs);

	return ev;
}

void IN_NewEvent(inputEvent_t ev) {
	bool irqs;

	assert(queuePos < MAX_INPUT_EVENT); /* somebody's producing too fast, or somebody's consuming too slow */

	irqs = IRQ_DisableSave();
	queue[queuePos++] = ev;
	IRQ_Restore(irqs);
}
