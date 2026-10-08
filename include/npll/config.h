/*
 * NPLL - Config file handling
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _CONFIG_H
#define _CONFIG_H

#include <npll/menu.h>

#define CONFIG_GLOBAL_TIMEOUT BIT(0)
#define CONFIG_GLOBAL_DEFAULT BIT(1)
#define CONFIG_GLOBAL_VI_MODE BIT(2)

extern int C_Probe(struct menuEntry **entriesOut, int *timeoutOut, uint *defaultOut, uint *globalsOut);

/* Free any heap-allocated state behind a menuEntry produced by C_Probe. */
extern void C_FreeEntryData(struct menuEntry *entry);

#endif /* _CONFIG_H */
