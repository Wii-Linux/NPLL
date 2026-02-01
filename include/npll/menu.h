/*
 * NPLL - Menu
 *
 * Copyright (C) 2025-2026 Techflash
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
	int numEntries;
	char *header;
	char *content;
	char *footer;

	/* called during UI_Switch to this menu, if present */
	void (*init)(struct menu *menu);

	/* called during UI_Switch away from this menu, if present */
	void (*cleanup)(struct menu *menu);

	/* called during UI_UpLevel (this menu can now be fully purged), if present */
	void (*destroy)(struct menu *menu);

	struct menu *previous;
};

#define MAX_ENTRIES 24

/* magic value to tell the menu to use the controls footers */
#define FOOTER_CONTROLS (void *)('C'<<24 | 'T'<<16 | 'R'<<8 | 'L')

#define FOOTER_CONTROLS_GCN "\
Use the GameCube controller to navigate the menu.\r\n\
DPad: navigate between options; A button: select an option"
#define FOOTER_CONTROLS_WII "\
Use a GameCube controller or the front buttons to navigate the menu.\r\n\
[GameCube Controller] DPad: navigate between options; A button: select an option\r\n\
[Front Buttons] Power: Move forward an option; Reset (Press): Move backward an\r\n\
                option; Reset (Hold): Select an option; Eject: Select an option"
#define FOOTER_CONTROLS_WIIU "\
Use the Wii U GamePad to navigate the menu.\r\n\
DPad: navigate between options; A button: select an option"

extern void UI_Init(void);
extern void UI_HandleInputs(void);
extern void UI_Redraw(void);
extern void UI_Switch(struct menu *m);
extern void UI_AddEntry(struct menuEntry *e);
extern void UI_LogPutchar(char c);

/* has dummy param so that it can be used directly as a .selected() */
extern void UI_UpLevel(struct menuEntry *_dummy);

#endif /* _MENU_H */
