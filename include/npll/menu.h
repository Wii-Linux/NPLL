/*
 * NPLL - Menu
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _MENU_H
#define _MENU_H

#include <stdbool.h>
#include <npll/types.h>

struct menuEntry {
	char name[128];
	void (*selected)(struct menuEntry *);
	u32 data[8];
};

struct menu {
	struct menuEntry *entries;
	char *header;
	char *footer;
};

extern void M_Init(void);
extern void M_Redraw(void);
extern void M_Switch(struct menu *m);
extern void M_AddEntry(struct menuEntry *e);

#endif /* _MENU_H */
