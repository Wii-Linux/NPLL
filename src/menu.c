/*
 * NPLL - Menu
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "menu"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/config.h>
#include <npll/console.h>
#include <npll/cpu.h>
#include <npll/drivers.h>
#include <npll/fs.h>
#include <npll/input.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/menu.h>
#include <npll/output.h>
#include <npll/partition.h>
#include <npll/timer.h>
#include <npll/utils.h>
#include <npll/video.h>

#define LOG_LINES 5
/* FIXME: make this dynamic */
#define ROOT_MENU_ENTRIES_MAX 20

static bool hasChanged = true;
static uint selected = 0;
struct logLine {
	const char *start;
	uint len;
	bool done;
};

static struct logLine logLines[LOG_LINES];
static uint logLineIdx = 0;
static uint curFooterLines;
static struct menu *curMenu = NULL;

struct configPartition {
	struct menuEntry *entries;
	uint numEntries;
	struct partition *part;
};
static uint numParts = 0;
static struct configPartition partitions[MAX_PARTITIONS * MAX_BDEV];

static void rootMenuRebootCB(struct menuEntry *dummy) {
	(void)dummy;
	assert(H_PlatOps->reboot);

	/* TODO: clean up */
	H_PlatOps->reboot();
}

static void rootMenuShutdownCB(struct menuEntry *dummy) {
	(void)dummy;
	assert(H_PlatOps->shutdown);

	/* TODO: clean up */
	H_PlatOps->shutdown();
}

static void rootMenuRetToLdrCB(struct menuEntry *dummy) {
	(void)dummy;
	assert(H_PlatOps->exit);

	/* TODO: clean up */
	H_PlatOps->exit();
}


static struct menuEntry rebootEntry = { .name = "Reboot", .selected = rootMenuRebootCB };
static struct menuEntry shutdownEntry = { .name = "Shutdown", .selected = rootMenuShutdownCB };
static struct menuEntry retToLdrEntry = { .name = "Exit to Loader", .selected = rootMenuRetToLdrCB };
static struct menuEntry sysInfoEntry = { .name = "System Information", .selected = UI_SwitchCB, .data = { (u32)&UI_SysInfoMenu } };

static struct menuEntry *rootMenuEntries[ROOT_MENU_ENTRIES_MAX] = {
	&sysInfoEntry,
};

static void rootMenuInit(struct menu *m) {
	uint idx = 1;

	if (H_PlatOps->reboot)
		rootMenuEntries[idx++] = &rebootEntry;

	if (H_PlatOps->shutdown)
		rootMenuEntries[idx++] = &shutdownEntry;

	if (H_PlatOps->exit)
		rootMenuEntries[idx++] = &retToLdrEntry;

	m->numEntries = idx;
}

static struct menu rootMenu = {
	.entries = rootMenuEntries,
	.numEntries = 2,
	.header = "NPLL - Main Menu",
	.footer = FOOTER_CONTROLS,
	.init = rootMenuInit,
	.cleanup = NULL,
	.destroy = NULL,
	.previous = NULL
};

static void uiRedrawWrapper(void *arg) {
	(void)arg;
	UI_HandleInputs();
	UI_Redraw();
}

void UI_Init(void) {
	memset(logLines, 0, sizeof(logLines));
	memset(partitions, 0, sizeof(partitions));
	UI_Switch(&rootMenu);
	T_QueueRepeatingEvent(10 * 1000, uiRedrawWrapper, NULL);
}

void UI_HandleInputs(void) {
	inputEvent_t ev;

	assert(curMenu);

	ev = IN_ConsumeEvent();
	while (ev) {
		hasChanged = true;
		if (ev & INPUT_EV_DOWN) {
			if (selected < (curMenu->numEntries - 1))
				selected++;
		}
		if (ev & INPUT_EV_UP) {
			if (selected > 0)
				selected--;
		}
		if (ev & INPUT_EV_SELECT) {
			curMenu->entries[selected]->selected(curMenu->entries[selected]);
			break;
		}

		ev = IN_ConsumeEvent();
	}
}

static void outputToDevice(char c, void *arg) {
	struct outputDevice *odev = arg;

	odev->writeChar(c);
}
static void putsDev(const struct outputDevice *odev, const char *str) {
	odev->writeStr(str);
	odev->writeStr("\r\n");
}

static void drawLine(const struct outputDevice *odev) {
	uint i;

	/* some of our output devices can benefit from bulk transfers */
	for (i = odev->columns; i > 0; i -= 4) {
		if (i >= 4)
			odev->writeStr("====");
		else if (i == 3)
			odev->writeStr("===");
		else if (i == 2)
			odev->writeStr("==");
		else if (i == 1)
			odev->writeChar('=');
	}
}

static void drawLogs(const struct outputDevice *odev) {
	uint i, j, maxLen;
	bool cutOff;

	for (i = 0; i < LOG_LINES; i++) {
		cutOff = false;

		if (!logLines[i].done) {
			/* some of our output devices can benefit from bulk transfers */
			for (j = odev->columns; j > 0; j -= 4) {
				if (j >= 4)
					odev->writeStr("    ");
				else if (j == 3)
					odev->writeStr("   ");
				else if (j == 2)
					odev->writeStr("  ");
				else if (j == 1)
					odev->writeChar(' ');
			}
			odev->writeStr("\r\n");
			continue;
		}

		/* is it big enough to fit on 1 line? */
		if (logLines[i].len > odev->columns) {
			maxLen = odev->columns - 3;
			cutOff = true;
		}
		else
			maxLen = logLines[i].len;

		for (j = 0; j < maxLen; j++)
			odev->writeChar(logLines[i].start[j]);

		if (cutOff)
			odev->writeStr("...");

		odev->writeStr("\r\n");
	}
}


void UI_Redraw(void) {
	uint i, j;
	const struct outputDevice *odev;

	if (__likely(!hasChanged))
		return;

	for (i = 0; i < O_NumDevices; i++) {
		odev = O_Devices[i];

		odev->writeStr("\x1b[1;1H\x1b[2J");
		if (curMenu->header)
			putsDev(odev, curMenu->header);

		drawLine(odev);
		odev->writeStr("\r\n");

		if (curMenu->content) {
			putsDev(odev, curMenu->content);
			putsDev(odev, "");
		}

		for (j = 0; j < curMenu->numEntries; j++) {
			fctprintf(outputToDevice, (void *)odev,
				"%s %d: %s\r\n",
				selected == j ? "\x1b[0m\x1b[31m\x1b[47m *" : "\x1b[0m  ",
				j, curMenu->entries[j]->name
			);
		}

		odev->writeStr("\x1b[0m");

		if (curMenu->footer) {
			fctprintf(outputToDevice, (void *)odev, "\x1b[%d;1H\rDebug Logs:\r\n", odev->rows - curFooterLines - 3 - LOG_LINES);
			drawLine(odev);
			odev->writeStr("\r\n");
			drawLogs(odev);
			drawLine(odev);
			odev->writeStr("\r\n");
			odev->writeStr(curMenu->footer);
		}
		else {
			fctprintf(outputToDevice, (void *)odev, "\x1b[%d;1H\rDebug Logs:\r\n", odev->rows - 2 - LOG_LINES);
			drawLine(odev);
			odev->writeStr("\r\n");
			drawLogs(odev);
		}


		hasChanged = false;
	}
}

void UI_Switch(struct menu *m) {
	struct menu *prev;
	char *s;

	selected = 0;
	hasChanged = true;
	logLineIdx = 0;
	curFooterLines = 0;
	memset(logLines, 0, sizeof(logLines));

	if (curMenu && curMenu->cleanup)
		curMenu->cleanup(curMenu);

	prev = curMenu;
	curMenu = m;

	if (m->init)
		m->init(m);

	if (m->footer) {
		if (m->footer == FOOTER_CONTROLS) {
			if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) m->footer = FOOTER_CONTROLS_GCN;
			else if (H_ConsoleType == CONSOLE_TYPE_WII) m->footer = FOOTER_CONTROLS_WII;
			else if (H_ConsoleType == CONSOLE_TYPE_WII_U) m->footer = FOOTER_CONTROLS_WIIU;
		}

		s = m->footer;
		while (*s) {
			if (*s == '\r')
				curFooterLines++;

			s++;
		}
	}

	m->previous = prev;
}

void UI_SwitchCB(struct menuEntry *e) {
	struct menu *m = (struct menu *)e->data[0];

	UI_Switch(m);
}

void UI_AppendEntry(struct menu *menu, struct menuEntry *e) {
	hasChanged = true;
	assert(menu->numEntries < ROOT_MENU_ENTRIES_MAX);
	menu->entries[menu->numEntries++] = e;
}

void UI_PrependEntry(struct menu *menu, struct menuEntry *e) {
	hasChanged = true;
	assert(menu->numEntries < ROOT_MENU_ENTRIES_MAX);
	memmove(&menu->entries[1], &menu->entries[0], menu->numEntries * sizeof(struct menuEntry *));
	menu->entries[0] = e;
	menu->numEntries++;
}

void UI_DelEntry(struct menu *menu, struct menuEntry *e) {
	uint i;

	for (i = 0; i < menu->numEntries; i++) {
		if (menu->entries[i] == e)
			break;
	}

	if (i == menu->numEntries)
		return; /* not found */

	if (i != menu->numEntries - 1)
		memmove(&menu->entries[i], &menu->entries[i + 1], sizeof(struct menuEntry *) * (menu->numEntries - i - 1));

	menu->numEntries--;
}

void UI_UpLevel(struct menuEntry *_dummy) {
	struct menu *prev;

	(void)_dummy;

	hasChanged = true;
	prev = curMenu->previous;

	if (curMenu->cleanup)
		curMenu->cleanup(curMenu);

	if (curMenu->destroy)
		curMenu->destroy(curMenu);

	assert(prev);
	curMenu = prev;
}

void UI_LogPutchar(char *cptr) {
	if (*cptr == '\r')
		return; /* we'll get the \n next */

	if (*cptr == '\n' && !logLines[logLineIdx].done && logLines[logLineIdx].start) {
		if (logLineIdx >= LOG_LINES) {
			printf("bogus logLineIdx: %u\r\n", logLineIdx);
			panic("bogus logLineIdx");
		}

		logLines[logLineIdx++].done = true;
		hasChanged = true;
		return;
	}

	if (logLineIdx >= LOG_LINES) {
		memmove(&logLines[0], &logLines[1], (LOG_LINES - 1) * sizeof(struct logLine));
		memset(&logLines[LOG_LINES - 1], 0, sizeof(struct logLine));
		logLineIdx--;
		hasChanged = true;
	}
	if (logLineIdx >= LOG_LINES) {
		printf("bogus logLineIdx: %u\r\n", logLineIdx);
		panic("bogus logLineIdx");
	}

	if (!logLines[logLineIdx].start)
		logLines[logLineIdx].start = cptr;

	logLines[logLineIdx].len++;
}

void UI_AddPart(struct partition *part) {
	bool irqs;
	int i, num;
	struct menuEntry *entries;

	num = C_Probe(&entries);
	if (num == -1)
		log_printf("C_Probe failed for partition %u of %s (%s)\r\n", part->index, part->bdev->name, FS_Mounted->name);
	else if (num == 0)
		return; /* no entries */
	else if (num >= 1) {
		irqs = IRQ_DisableSave();
		assert(numParts < (MAX_BDEV * MAX_PARTITIONS) - 1);
		partitions[numParts].part = part;
		partitions[numParts].numEntries = (uint)num;
		partitions[numParts].entries = entries;
		for (i = num - 1; i >= 0; i--)
			UI_PrependEntry(&rootMenu, &entries[i]);
		numParts++;
		IRQ_Restore(irqs);
	}
}

void UI_DelPart(struct partition *part) {
	uint i, partToDel = (uint)-1;
	bool irqs = IRQ_DisableSave();

	for (i = 0; i < numParts; i++) {
		if (partitions[i].part == part) {
			partToDel = i;
			break;
		}
	}

	if (partToDel == (uint)-1) {
		IRQ_Restore(irqs);
		return;
	}

	for (i = 0; i < partitions[partToDel].numEntries; i++) {
		log_printf("deleting entry %u of part %u\r\n", i, partToDel);
		UI_DelEntry(&rootMenu, &partitions[partToDel].entries[i]);
	}

	free(partitions[partToDel].entries);
	memmove(&partitions[partToDel], &partitions[partToDel + 1], (numParts - partToDel - 1) * sizeof(struct configPartition));
	numParts--;
	IRQ_Restore(irqs);
}
