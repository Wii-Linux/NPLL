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
#include <npll/video.h>
#include <npll/utils.h>

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

void UI_Init(void) {
	memset(entries, 0, MAX_ENTRIES * sizeof(struct menuEntry));
}

static u64 lastTb = 0;
void UI_Redraw(void) {
	int i;
	u64 tb = mftb();
	if (__unlikely(T_HasElapsed(lastTb, 1000 * 1000))) {
		puts("a");
		lastTb = tb;
		return;
	}
	if (__likely(!hasChanged))
		return;

	puts("==== MENU ====");
	for (i = 0; i < lastEntry; i++)
		printf(" %c %d: %s\r\n", selected == i ? '*' : ' ', i, entries[i].name);

	hasChanged = false;
}

void UI_Switch(struct menu *m) {
	curMenu.header = m->header;
	curMenu.footer = m->footer;
	lastEntry = 0;
	selected = 0;
	hasChanged = false;
	memset(entries, 0, MAX_ENTRIES * sizeof(struct menuEntry));
}

void UI_AddEntry(struct menuEntry *e) {
	hasChanged = true;
	memcpy(&entries[lastEntry], e, sizeof(struct menuEntry));
	lastEntry++;
}
