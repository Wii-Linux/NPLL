/*
 * NPLL - Menu
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <npll/cpu.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/input.h>
#include <npll/menu.h>
#include <npll/output.h>
#include <npll/video.h>
#include <npll/utils.h>
#include <npll/timer.h>

#define LOG_LINES 5

static bool hasChanged = true;
static int selected = 0;
struct logLine {
	const char *start;
	int len;
	bool done;
};

static struct logLine logLines[LOG_LINES];
static int logLineIdx = 0;
static int curFooterLines;

static void rootMenuSelectedCB(struct menuEntry *ent) {
	printf("Root Menu Selected Callback for entry %s\r\n",  ent->name);
	V_Flush();
	udelay(1000 * 1000);
}

static struct menuEntry rootMenuEntries[] = {
	{ .name = "foo", .selected = rootMenuSelectedCB, .data = { 69 } },
	{ .name = "System Information", .selected = UI_SwitchCB, .data = { (u32)&UI_SysInfoMenu } },
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
	memset(logLines, 0, sizeof(logLines));
	UI_Switch(&rootMenu);
}

void UI_HandleInputs(void) {
	inputEvent_t ev;

	assert(curMenu);

	mtspr(DABR, 0);
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
			curMenu->entries[selected].selected(&curMenu->entries[selected]);
			break;
		}

		ev = IN_ConsumeEvent();
	}
	mtspr(DABR, (u32)&selected | BIT(2) | BIT(1));
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
	int i;

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
	int i, j, maxLen;
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
	int i, j;
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
				j, curMenu->entries[j].name
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

void UI_AddEntry(struct menuEntry *e) {
	hasChanged = true;
	memcpy(&curMenu->entries[curMenu->numEntries++], e, sizeof(struct menuEntry));
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
		if (!(logLineIdx >= 0 && logLineIdx < LOG_LINES)) {
			printf("bogus logLineIdx: %d\r\n", logLineIdx);
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
	if (!(logLineIdx >= 0 && logLineIdx < LOG_LINES)) {
		printf("bogus logLineIdx: %d\r\n", logLineIdx);
		panic("bogus logLineIdx");
	}

	if (!logLines[logLineIdx].start)
		logLines[logLineIdx].start = cptr;

	logLines[logLineIdx].len++;
}
