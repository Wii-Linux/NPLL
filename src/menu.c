/*
 * NPLL - Menu
 *
 * Copyright (C) 2025 Techflash
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <npll/menu.h>
#include <npll/output.h>
#include <npll/video.h>
#include <npll/utils.h>
#include <npll/timer.h>

static bool hasChanged = true;
static int selected = 0;

static void rootMenuSelectedCB(struct menuEntry *ent) {
	printf("Root Menu Selected Callback for entry %s\r\n",  ent->name);
	V_Flush();
	udelay(500 * 1000);
}

static struct menuEntry rootMenuEntries[] = {
	{ .name = "foo", .selected = rootMenuSelectedCB, .data = { 69 } },
	{ .name = "bar", .selected = rootMenuSelectedCB, .data = { 420 } },
};

static struct menu rootMenu = {
	.entries = rootMenuEntries,
	.numEntries = 2,
	.header = "NPLL - Main Menu",
	.footer = FOOTER_CONTROLS,
	.init = NULL,
	.cleanup = NULL,
	.destroy = NULL,
	.previous = NULL
};

static struct menu *curMenu = NULL;

void UI_Init(void) {
	UI_Switch(&rootMenu);
}

void UI_Redraw(void) {
	int i, j;
	if (__likely(!hasChanged))
		return;

	printf("\x2b[1;1H\x1b[2J");
	if (curMenu->header)
		printf("%s\r\n", curMenu->header);

	puts("==========");

	for (i = 0; i < curMenu->numEntries; i++)
		printf("%s %d: %s\r\n", selected == i ? "\x1b[0m\x1b[31m\x1b[47m *" : "\x1b[0m  ", i, curMenu->entries[i].name);

	hasChanged = false;
}

void UI_Switch(struct menu *m) {
	selected = 0;
	hasChanged = true;

	if (curMenu && curMenu->cleanup)
		curMenu->cleanup(curMenu);

	curMenu = m;

	if (m->init)
		m->init(m);
}

void UI_AddEntry(struct menuEntry *e) {
	hasChanged = true;
	memcpy(&curMenu->entries[curMenu->numEntries++], e, sizeof(struct menuEntry));
}

void UI_UpLevel(void) {
	struct menu *prev;

	hasChanged = true;
	prev = curMenu->previous;

	if (curMenu->cleanup)
		curMenu->cleanup(curMenu);

	if (curMenu->destroy)
		curMenu->destroy(curMenu);

	assert(prev);
	curMenu = prev;
}
