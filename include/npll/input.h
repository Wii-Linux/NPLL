/*
 * NPLL - Top-level input handling
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _INPUT_H
#define _INPUT_H

#include <npll/types.h>
#include <npll/utils.h>

typedef u32 inputEvent_t;
#define INPUT_EV_UP     BIT(0)
#define INPUT_EV_DOWN   BIT(1)
#define INPUT_EV_LEFT   BIT(2)
#define INPUT_EV_RIGHT  BIT(3)
#define INPUT_EV_SELECT BIT(4)

extern void IN_Init(void);
extern inputEvent_t IN_ConsumeEvent(void);
extern void IN_NewEvent(inputEvent_t ev);

#endif /* _INPUT_H */
