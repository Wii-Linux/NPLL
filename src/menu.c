/*
 * NPLL - Menu
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <npll/menu.h>
#include <npll/timer.h>

#define MAX_ENTRIES 24
static bool hasChanged = false;
static int lastEntry = 0;
static int selected = 0;
static struct menuEntry entries[MAX_ENTRIES];

static struct menu curMenu = {
	.header = "Menu Header",
	.footer = "Menu Footer",
	.entries = entries
};

void M_Init(void) {
	memset(entries, 0, MAX_ENTRIES * sizeof(struct menuEntry));
}

static u64 lastTb = 0;
void M_Redraw(void) {
	int i;
	u64 tb = mftb();
	if (T_HasElapsed(lastTb, 1000 * 500)) {
		lastTb = tb;
		puts("a");
	}
	if (!hasChanged)
		return;

	puts("==== MENU ====");
	for (i = 0; i < lastEntry; i++)
		printf(" %c %d: %s\r\n", selected == i ? '*' : ' ', i, entries[i].name);

	hasChanged = false;
}

void M_Switch(struct menu *m) {
	curMenu.header = m->header;
	curMenu.footer = m->footer;
	lastEntry = 0;
	selected = 0;
	hasChanged = false;
	memset(entries, 0, MAX_ENTRIES * sizeof(struct menuEntry));
}

void M_AddEntry(struct menuEntry *e) {
	hasChanged = true;
	memcpy(&entries[lastEntry], e, sizeof(struct menuEntry));
	lastEntry++;
}
