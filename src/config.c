/*
 * NPLL - Config file handling
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "cfg"
#include <stdlib.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/elf.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/menu.h>
#include <npll/partition.h>

enum dataIdx {
	DATA_IDX_PART,
	DATA_IDX_FS,
	DATA_IDX_PATH
};

static void selectedCB(struct menuEntry *entry) {
	struct partition *part;
	struct filesystem *fs;
	char *path;
	int fd, ret;

	part = (struct partition *)entry->data[DATA_IDX_PART];
	fs = (struct filesystem *)entry->data[DATA_IDX_FS];
	path = (char *)entry->data[DATA_IDX_PATH];

	if (FS_MountedPartition != part || FS_Mounted != fs) {
		ret = FS_Mount(fs, part);
		if (ret != 0) {
			log_printf("FS_Mount failed: %d\r\n", ret);
			return;
		}
	}
	fd = FS_Open(path);
	if (fd < 0) {
		log_printf("FS_Open failed: %d\r\n", fd);
		return;
	}

	ret = ELF_LoadFile(fd); /* does not return on success */
	log_printf("ELF_LoadFile failed: %d\r\n", ret);
}

static void ensureEntryCapacity(struct menuEntry **entries, uint *capacity, uint numEntries, uint needed) {
	struct menuEntry *newEntries;
	uint newCapacity;

	if (*capacity >= needed)
		return;

	newCapacity = *capacity ? *capacity : 8;
	while (newCapacity < needed)
		newCapacity *= 2;

	newEntries = malloc(sizeof(struct menuEntry) * newCapacity);
	if (*entries) {
		memcpy(newEntries, *entries, sizeof(struct menuEntry) * numEntries);
		free(*entries);
	}

	*entries = newEntries;
	*capacity = newCapacity;
}

#define FINALIZE_ENTRY() \
	if (titleCur != 0 && pathCur != 0) {								\
		ensureEntryCapacity(&entries, &entryCapacity, (uint)numEntries, (uint)numEntries + 1);	\
		memset(&entries[numEntries], 0, sizeof(struct menuEntry));				\
		strcpy(entries[numEntries].name, entryTitle);						\
		entries[numEntries].selected = selectedCB;						\
		entries[numEntries].data[DATA_IDX_PART] = (u32)FS_MountedPartition;			\
		entries[numEntries].data[DATA_IDX_FS] = (u32)FS_Mounted;				\
		entries[numEntries].data[DATA_IDX_PATH] = (u32)malloc(strlen(entryPath) + 1);		\
		strcpy((char *)entries[numEntries].data[DATA_IDX_PATH], entryPath);			\
		numEntries++;										\
		memset(entryPath, 0, sizeof(entryPath));						\
		memset(entryTitle, 0, sizeof(entryTitle));						\
		titleCur = pathCur = 0;									\
	}												\
	/* else it's not an actual bootable entry */							\
	isInEntry = false;

/*
 * TODO: update to proper NPLL-specific format once
 * I make one....
 */
int C_Probe(struct menuEntry **entriesOut, int *timeoutOut, uint *defaultOut) {
	int fd, ret, size;
	uint lineNum = 1, titleCur = 0, pathCur = 0, numEntries = 0;
	uint entryCapacity = 0;
	char *file, *curLine, *cur, *end, entryTitle[64], entryPath[128];
	bool skipToEndOfLine = false, isInEntry = false, isInTitle = false, isInPath = false;
	struct menuEntry *entries = NULL;

	*timeoutOut = -1;
	*defaultOut = 0;

	fd = FS_Open("gumboot/gumboot.lst");
	if (fd < 0) {
		log_printf("FS_Open on gumboot/gumboot.lst failed: %d\r\n", fd);
		return -1;
	}

	size = FS_GetSize(fd);
	if (size <= 0) {
		log_printf("FS_GetSize on gumboot/gumboot.lst failed: %d\r\n", size);
		return -1;
	}

	file = malloc((size_t)size);
	ret = FS_Read(fd, file, (size_t)size);
	if (ret != size) {
		log_printf("FS_Read on gumboot/gumboot.lst failed: %d\r\n", ret);
		free(file);
		FS_Close(fd);
		return -1;
	}
	FS_Close(fd);

	cur = curLine = file;
	end = file + size;
	memset(entryTitle, 0, sizeof(entryTitle));
	memset(entryPath, 0, sizeof(entryPath));

	while (cur != end) {
		if (cur == curLine && *curLine != '\t' && isInEntry) { /* end entry on first un-intended line */
			FINALIZE_ENTRY()
		}
		if (*cur == '\r' || *cur == '\n') { /* note the start of the new line */
			curLine = cur + 1;
			if (*cur == '\n')
				lineNum++;
			skipToEndOfLine = false;
			isInTitle = false;
			isInPath = false;
			goto cont;
		}
		else if (skipToEndOfLine) /* just keep going if we're skipping the rest of this line */
			goto cont;
		else if (*curLine == '#') { /* skip comments */
			skipToEndOfLine = true;
			goto cont;
		}
		else if (isInTitle) { /* consume title */
			if (titleCur >= sizeof(entryTitle))
				goto cont;
			entryTitle[titleCur++] = *cur;
			goto cont;
		}
		else if (isInPath) { /* consume path */
			if (pathCur >= sizeof(entryPath))
				goto cont;
			entryPath[pathCur++] = *cur;
			goto cont;
		}


		/* skip a bunch of unsupported directives */
		else if (!isInEntry && !memcmp(curLine, "video ", 6)) {
			log_puts("skipping unsupported directive \"video\"");
			skipToEndOfLine = true;
			cur += 6;
			continue;
		}
		else if (!isInEntry && !memcmp(curLine, "color ", 6)) {
			log_puts("skipping unsupported directive \"color\"");
			skipToEndOfLine = true;
			cur += 6;
			continue;
		}
		else if (!isInEntry && !memcmp(curLine, "timeout ", 8)) {
			*timeoutOut = (int)strtol(curLine + 8, NULL, 10);
			skipToEndOfLine = true;
			cur += 8;
			continue;
		}
		else if (!isInEntry && !memcmp(curLine, "default ", 8)) {
			*defaultOut = (uint)strtoul(curLine + 8, NULL, 10);
			skipToEndOfLine = true;
			cur += 8;
			continue;
		}

		/* the start of a boot entry */
		else if (!isInEntry && !memcmp(curLine, "title ", 6)) {
			isInTitle = true;
			isInEntry = true;
			titleCur = 0;
			cur += 5;
			goto cont;
		}
		else if (isInEntry && !memcmp(curLine, "\troot ", 6)) {
			cur += 6;
			if (!memcmp(cur, "(sd0,0)", 7)) /* skip '(sd0,0)' */
				cur += 7;
			if (*cur == '\r' || *cur == '\n') /* if there's nothing else left in the entry then leave */
				continue;
			else if (*cur != '/') {
				log_printf("Garbage 'root' directive @ line %d:%d (offset %d)\r\n", lineNum, (u32)cur - (u32)curLine, (u32)cur - (u32)file);
				while (numEntries)
					free((char *)entries[--numEntries].data[DATA_IDX_PATH]);
				if (entries)
					free(entries);
				free(file);
				return -1;
			}
			isInPath = true;
			goto cont;
		}
		else if (isInEntry && !memcmp(curLine, "\tkernel ", 8)) {
			cur += 8;
			if (*cur != '/') {
				log_printf("Garbage 'kernel' directive @ line %d:%d (offset %d)\r\n", lineNum, (u32)cur - (u32)curLine, (u32)cur - (u32)file);
				while (numEntries)
					free((char *)entries[--numEntries].data[DATA_IDX_PATH]);
				if (entries)
					free(entries);
				free(file);
				return -1;
			}
			isInPath = true;
			if (!pathCur)
				cur++; /* skip the '/' if we're at the start */
			continue;
		}
		else if (isInEntry && !memcmp(curLine, "\tbrowse", 7)) {
			log_puts("skipping unsupported directive \"browse\"");
			skipToEndOfLine = true;
			cur += 7;
			continue;
		}
		else if (isInEntry && !memcmp(curLine, "\treboot", 7)) {
			log_puts("skipping unsupported directive \"reboot\"");
			skipToEndOfLine = true;
			cur += 7;
			continue;
		}
		else if (isInEntry && !memcmp(curLine, "\tpoweroff", 9)) {
			log_puts("skipping unsupported directive \"poweroff\"");
			skipToEndOfLine = true;
			cur += 9;
			continue;
		}
		else {
			log_printf("Garbage in config file @ line %d:%d (offset %d)\r\n", lineNum, (u32)cur - (u32)curLine, (u32)cur - (u32)file);
			while (numEntries)
				free((char *)entries[--numEntries].data[DATA_IDX_PATH]);
			if (entries)
				free(entries);
			free(file);
			return -1;
		}

	cont:
		cur++;
	}

	/* file ended w/o an unindented line */
	if (isInEntry) {
		FINALIZE_ENTRY();
	}

	if (numEntries)
		*entriesOut = entries;
	else
		*entriesOut = NULL;

	free(file);
	return numEntries;
}
