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

#define MENU_LOG_LINES_MAX 12
#define MENU_LOG_LINES_MIN 5
#define MENU_ENTRY_GROWTH_MIN 8
#define MENU_ENTRY_CAPACITY_OWNED BIT(31)

static bool hasChanged = true;
static uint selected = 0;
static uint bodyScroll = 0;
static uint curFooterLines;
static struct menu *curMenu = NULL;
static bool autobootActive = false;
static bool autobootCanceled = false;
static uint autobootTimeout = 0;
static char autobootText[32];

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
	H_PlatOps->reboot();
}

static void rootMenuShutdownCB(struct menuEntry *dummy) {
	(void)dummy;
	assert(H_PlatOps->shutdown);
	H_PlatOps->shutdown();
}

static void rootMenuRetToLdrCB(struct menuEntry *dummy) {
	(void)dummy;
	assert(H_PlatOps->exit);
	H_PlatOps->exit();
}


static struct menuEntry rebootEntry = { .name = "Reboot", .selected = rootMenuRebootCB };
static struct menuEntry shutdownEntry = { .name = "Shutdown", .selected = rootMenuShutdownCB };
static struct menuEntry retToLdrEntry = { .name = "Exit to Loader", .selected = rootMenuRetToLdrCB };
static struct menuEntry sysInfoEntry = { .name = "System Information", .selected = UI_SwitchCB, .data = { (u32)&UI_SysInfoMenu } };

static struct menuEntry *rootMenuEntries[4] = {
	&sysInfoEntry,
};

static void rootMenuInit(struct menu *m) {
	uint idx = 1;

	if (H_PlatOps->reboot)
		m->entries[idx++] = &rebootEntry;

	if (H_PlatOps->shutdown)
		m->entries[idx++] = &shutdownEntry;

	if (H_PlatOps->exit)
		m->entries[idx++] = &retToLdrEntry;

	m->numEntries = idx;
}

static struct menu rootMenu = {
	.entries = rootMenuEntries,
	.numEntries = 1,
	.entryCapacity = sizeof(rootMenuEntries) / sizeof(struct menuEntry *),
	.header = "NPLL - Main Menu",
	.footer = FOOTER_CONTROLS,
	.init = rootMenuInit,
	.cleanup = NULL,
	.destroy = NULL,
	.previous = NULL
};

static void updateAutobootContent(void) {
	snprintf(autobootText, sizeof(autobootText), "Autobooting in %u second%s",
		autobootTimeout, autobootTimeout == 1 ? "" : "s");
	hasChanged = true;
}

static void cancelAutoboot(void) {
	autobootCanceled = true;
	autobootActive = false;
	hasChanged = true;
}

static void autobootEventCB(void *arg) {
	(void)arg;

	if (!autobootActive)
		return;

	if (autobootTimeout)
		autobootTimeout--;
	if (autobootTimeout) {
		updateAutobootContent();
		return;
	}

	autobootActive = false;
	curMenu->entries[selected]->selected(curMenu->entries[selected]);
}

static void uiRedrawWrapper(void *arg) {
	(void)arg;
	UI_HandleInputs();
	#ifndef DEBUG_NO_MENU
	UI_Redraw();
	#endif
}

void UI_Init(void) {
	memset(partitions, 0, sizeof(partitions));
	UI_Switch(&rootMenu);
	T_QueueRepeatingEvent(1000 * 1000, autobootEventCB, NULL);
	T_QueueRepeatingEvent(10 * 1000, uiRedrawWrapper, NULL);
}

static void outputToDevice(char c, void *arg) {
	struct outputDevice *odev = arg;

	odev->writeChar(c);
}
static void putsDev(const struct outputDevice *odev, const char *str) {
	odev->writeStr(str);
	odev->writeStr("\r\n");
}

static uint countTextLines(const char *str) {
	uint lines = 1;

	if (!str || !*str)
		return 0;

	while (*str) {
		if (*str == '\n' && str[1] != '\0')
			lines++;
		str++;
	}

	return lines;
}

static uint menuEntryCapacity(const struct menu *menu) {
	return menu->entryCapacity & ~MENU_ENTRY_CAPACITY_OWNED;
}

static bool menuOwnsEntries(const struct menu *menu) {
	return !!(menu->entryCapacity & MENU_ENTRY_CAPACITY_OWNED);
}

static uint menuContentLines(const struct menu *menu) {
	return countTextLines(menu->content);
}

static uint menuEntryBaseLine(const struct menu *menu) {
	uint contentLines = menuContentLines(menu);

	return contentLines + (contentLines ? 1 : 0);
}

static uint menuBodyLines(const struct menu *menu) {
	return menuEntryBaseLine(menu) + menu->numEntries;
}

static uint headerLines(const struct menu *menu) {
	return countTextLines(menu->header);
}

static uint logHeightForRows(uint rows, uint footerLines, uint headerLineCount) {
	uint logHeight = rows / 4;
	uint reservedTop;
	uint reservedBottom;

	if (logHeight < MENU_LOG_LINES_MIN)
		logHeight = MENU_LOG_LINES_MIN;
	if (logHeight > MENU_LOG_LINES_MAX)
		logHeight = MENU_LOG_LINES_MAX;

	reservedTop = headerLineCount + 2;
	reservedBottom = (curMenu && curMenu->footer) ? (3 + footerLines + logHeight) : (2 + logHeight);

	while (logHeight > 0 && reservedTop + reservedBottom >= rows) {
		logHeight--;
		reservedBottom--;
	}

	return logHeight;
}

static uint bodyHeightForRows(uint rows, uint footerLines, uint headerLineCount) {
	uint logHeight = logHeightForRows(rows, footerLines, headerLineCount);
	uint reservedTop = headerLineCount + 2;
	uint reservedBottom = (curMenu && curMenu->footer) ? (3 + footerLines + logHeight) : (2 + logHeight);

	if (rows <= reservedTop + reservedBottom)
		return 1;

	return rows - reservedTop - reservedBottom;
}

static uint currentBodyHeightForDevice(const struct outputDevice *odev) {
	return bodyHeightForRows(odev->rows, curFooterLines, headerLines(curMenu));
}

static uint canonicalBodyHeight(void) {
	uint i, minHeight = (uint)-1;

	if (!curMenu || !O_NumDevices)
		return 1;

	for (i = 0; i < O_NumDevices; i++) {
		uint height = currentBodyHeightForDevice(O_Devices[i]);

		if (height < minHeight)
			minHeight = height;
	}

	return minHeight == (uint)-1 ? 1 : minHeight;
}

static void clampScrollToBody(uint bodyHeight) {
	uint bodyLines;
	uint maxScroll;

	assert(curMenu);

	bodyLines = menuBodyLines(curMenu);
	if (bodyLines <= bodyHeight)
		maxScroll = 0;
	else
		maxScroll = bodyLines - bodyHeight;

	if (bodyScroll > maxScroll)
		bodyScroll = maxScroll;
}

static void ensureSelectionVisible(uint bodyHeight) {
	uint selectedLine;

	assert(curMenu);

	selectedLine = menuEntryBaseLine(curMenu) + selected;
	if (selectedLine < bodyScroll)
		bodyScroll = selectedLine;
	else if (selectedLine >= bodyScroll + bodyHeight)
		bodyScroll = selectedLine - bodyHeight + 1;

	clampScrollToBody(bodyHeight);
}

static void normalizeMenuState(void) {
	assert(curMenu);
	assert(curMenu->numEntries);

	if (selected >= curMenu->numEntries)
		selected = curMenu->numEntries - 1;

	clampScrollToBody(canonicalBodyHeight());
}

static void drawTextLine(const struct outputDevice *odev, const char *text, uint targetLine) {
	uint currentLine = 0;

	while (text && *text) {
		if (currentLine == targetLine) {
			if (*text != '\r' && *text != '\n')
				odev->writeChar(*text);
		}

		if (*text == '\n') {
			if (currentLine == targetLine)
				break;

			currentLine++;
		}

		text++;
	}

	odev->writeStr("\r\n");
}

static void drawEntryLine(const struct outputDevice *odev, uint entryIdx) {
	fctprintf(outputToDevice, (void *)odev,
		"%s %d: %s\r\n",
		selected == entryIdx ? "\x1b[0m\x1b[31m\x1b[47m *" : "\x1b[0m  ",
		entryIdx, curMenu->entries[entryIdx]->name
	);
}

static void drawLine(const struct outputDevice *odev) {
	int i;

	/* some of our output devices can benefit from bulk transfers */
	for (i = (int)odev->columns; i > 0; i -= 4) {
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

static void drawBodySeparator(const struct outputDevice *odev, uint bodyHeight) {
	char indicator[32];
	uint bodyLines, hiddenAbove, hiddenBelow, indicatorLen, lineLen;
	int i;

	bodyLines = menuBodyLines(curMenu);
	hiddenAbove = bodyScroll;
	if (bodyScroll + bodyHeight >= bodyLines)
		hiddenBelow = 0;
	else
		hiddenBelow = bodyLines - (bodyScroll + bodyHeight);

	if (!hiddenAbove && !hiddenBelow) {
		drawLine(odev);
		return;
	}

	indicatorLen = (uint)snprintf(indicator, sizeof(indicator), " ^ %u lines, v %u lines ", hiddenAbove, hiddenBelow);
	if (indicatorLen >= odev->columns) {
		drawLine(odev);
		return;
	}

	lineLen = odev->columns - indicatorLen;
	for (i = 0; i < (int)(lineLen / 2); i++)
		odev->writeChar('=');
	odev->writeStr(indicator);
	for (i = 0; i < (int)(lineLen - (lineLen / 2)); i++)
		odev->writeChar('=');
}

static void drawLogs(const struct outputDevice *odev, uint logHeight) {
	const char *memlogStart, *memlogEnd, *lineStart, *lineEnd, *cur, *lineStarts[MENU_LOG_LINES_MAX];
	uint lineLens[MENU_LOG_LINES_MAX], curLine = 0, totalLines, firstLine, idx, i, maxLen;
	int j;
	bool cutOff;

	assert(logHeight <= MENU_LOG_LINES_MAX);

	L_GetMemlogBounds(&memlogStart, &memlogEnd);
	memset(lineStarts, 0, sizeof(lineStarts));
	memset(lineLens, 0, sizeof(lineLens));

	lineStart = memlogStart;
	cur = memlogStart;
	while (cur < memlogEnd) {
		if (*cur == '\n') {
			lineEnd = cur;
			idx = curLine % MENU_LOG_LINES_MAX;

			if (lineEnd > lineStart && lineEnd[-1] == '\r')
				lineEnd--;

			lineStarts[idx] = lineStart;
			lineLens[idx] = (uint)(lineEnd - lineStart);
			curLine++;
			lineStart = cur + 1;
		}
		cur++;
	}

	totalLines = curLine;
	if (totalLines > logHeight)
		firstLine = totalLines - logHeight;
	else
		firstLine = 0;

	for (i = 0; i < logHeight; i++) {
		if (firstLine + i >= totalLines) {
			/* some of our output devices can benefit from bulk transfers */
			for (j = (int)odev->columns; j > 0; j -= 4) {
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
		cutOff = false;
		maxLen = lineLens[(firstLine + i) % MENU_LOG_LINES_MAX];
		if (maxLen > odev->columns) {
			maxLen = odev->columns > 3 ? odev->columns - 3 : 0;
			cutOff = true;
		}

		for (j = 0; (uint)j < maxLen; j++)
			odev->writeChar(lineStarts[(firstLine + i) % MENU_LOG_LINES_MAX][j]);

		if (cutOff)
			odev->writeStr("...");

		odev->writeStr("\r\n");
	}
}

static void drawAutoboot(const struct outputDevice *odev) {
	uint len, col;
	const char *color = "\x1b[0m";

	if (!autobootActive)
		return;

	if (autobootTimeout <= 1)
		color = "\x1b[1m\x1b[31m";
	else if (autobootTimeout <= 3)
		color = "\x1b[1m\x1b[33m";

	len = (uint)strlen(autobootText);
	if (len >= odev->columns)
		col = 1;
	else
		col = odev->columns - len + 1;

	fctprintf(outputToDevice, (void *)odev, "\x1b[1;%uH%s%s\x1b[0m", col, color, autobootText);
}

static void drawBody(const struct outputDevice *odev, uint bodyHeight) {
	uint i;
	uint contentLines = menuContentLines(curMenu);
	uint entryBase = menuEntryBaseLine(curMenu);
	uint bodyLines = menuBodyLines(curMenu);

	for (i = 0; i < bodyHeight; i++) {
		uint bodyLine = bodyScroll + i;

		if (bodyLine >= bodyLines) {
			odev->writeStr("\r\n");
			continue;
		}

		if (bodyLine < contentLines) {
			drawTextLine(odev, curMenu->content, bodyLine);
			continue;
		}

		if (contentLines && bodyLine == contentLines) {
			odev->writeStr("\r\n");
			continue;
		}

		drawEntryLine(odev, bodyLine - entryBase);
	}
}

static void ensureMenuEntryCapacity(struct menu *menu, uint needed) {
	struct menuEntry **entries;
	uint newCapacity;

	if (menuEntryCapacity(menu) >= needed)
		return;

	newCapacity = menuEntryCapacity(menu) ? menuEntryCapacity(menu) : MENU_ENTRY_GROWTH_MIN;
	while (newCapacity < needed)
		newCapacity *= 2;

	entries = malloc(sizeof(struct menuEntry *) * newCapacity);
	memcpy(entries, menu->entries, sizeof(struct menuEntry *) * menu->numEntries);
	if (menuOwnsEntries(menu))
		free(menu->entries);

	menu->entries = entries;
	menu->entryCapacity = newCapacity | MENU_ENTRY_CAPACITY_OWNED;
}

void UI_Invalidate(void) {
	hasChanged = true;
}

void UI_HandleInputs(void) {
	inputEvent_t ev;
	uint bodyHeight;
	uint firstEntryLine;

	assert(curMenu);

	ev = IN_ConsumeEvent();
	while (ev) {
		cancelAutoboot();
		hasChanged = true;
		bodyHeight = canonicalBodyHeight();
		firstEntryLine = menuEntryBaseLine(curMenu) + selected;

		if (ev & INPUT_EV_DOWN) {
			if (firstEntryLine >= bodyScroll + bodyHeight) {
				bodyScroll++;
				clampScrollToBody(bodyHeight);
			}
			else if (selected < (curMenu->numEntries - 1)) {
				selected++;
				ensureSelectionVisible(bodyHeight);
			}
		}
		if (ev & INPUT_EV_UP) {
			if (selected == 0 && bodyScroll > 0) {
				bodyScroll--;
			}
			else if (selected > 0) {
				selected--;
				ensureSelectionVisible(bodyHeight);
			}
		}
		if (ev & INPUT_EV_SELECT) {
			curMenu->entries[selected]->selected(curMenu->entries[selected]);
			break;
		}

		ev = IN_ConsumeEvent();
	}
}


void UI_Redraw(void) {
	uint i;
	const struct outputDevice *odev;
	uint bodyHeight;
	uint logHeight;
	uint headerLineCount;
	uint logRow;

	if (__likely(!hasChanged))
		return;

	normalizeMenuState();
	headerLineCount = headerLines(curMenu);

	for (i = 0; i < O_NumDevices; i++) {
		odev = O_Devices[i];
		bodyHeight = currentBodyHeightForDevice(odev);
		logHeight = logHeightForRows(odev->rows, curFooterLines, headerLineCount);

		odev->writeStr("\x1b[1;1H\x1b[2J");
		if (curMenu->header)
			putsDev(odev, curMenu->header);

		drawBodySeparator(odev, bodyHeight);
		odev->writeStr("\r\n");
		drawBody(odev, bodyHeight);

		odev->writeStr("\x1b[0m");
		logRow = odev->rows - (curMenu->footer ? (3 + curFooterLines + logHeight) : (2 + logHeight));

		if (curMenu->footer) {
			fctprintf(outputToDevice, (void *)odev, "\x1b[%d;1H\rDebug Logs:\r\n", logRow);
			drawLine(odev);
			odev->writeStr("\r\n");
			drawLogs(odev, logHeight);
			drawLine(odev);
			odev->writeStr("\r\n");
			odev->writeStr(curMenu->footer);
		}
		else {
			fctprintf(outputToDevice, (void *)odev, "\x1b[%d;1H\rDebug Logs:\r\n", logRow);
			drawLine(odev);
			odev->writeStr("\r\n");
			drawLogs(odev, logHeight);
		}

		drawAutoboot(odev);
	}

	hasChanged = false;
}

void UI_Switch(struct menu *m) {
	struct menu *prev;
	char *s;

	selected = 0;
	bodyScroll = 0;
	hasChanged = true;
	curFooterLines = 0;

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
	ensureMenuEntryCapacity(menu, menu->numEntries + 1);
	menu->entries[menu->numEntries++] = e;
}

void UI_PrependEntry(struct menu *menu, struct menuEntry *e) {
	hasChanged = true;
	ensureMenuEntryCapacity(menu, menu->numEntries + 1);
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
	if (menu == curMenu && menu->numEntries)
		normalizeMenuState();
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
	normalizeMenuState();
}

void UI_AddPart(struct partition *part) {
	bool irqs, bootNow = false;
	int i, num, timeout;
	uint defaultEntry;
	struct menuEntry *entries;

	num = C_Probe(&entries, &timeout, &defaultEntry);
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
		if (!autobootCanceled && !IN_HasReceivedInput() && timeout >= 0 &&
			defaultEntry < (uint)num && rootMenu.entries[defaultEntry] != &sysInfoEntry) {
			selected = defaultEntry;
			autobootTimeout = (uint)timeout;
			autobootActive = true;
			updateAutobootContent();
			if (!autobootTimeout)
				bootNow = true;
		}
		IRQ_Restore(irqs);
		if (bootNow)
			autobootEventCB(NULL);
	}
}

void UI_DelPart(struct partition *part) {
	uint i, partToDel = (uint)-1;
	bool irqs = IRQ_DisableSave();

	cancelAutoboot();

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
