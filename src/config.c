/*
 * NPLL - Config file handling
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "cfg"
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/console.h>
#include <npll/config.h>
#include <npll/elf.h>
#include <npll/dol.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/linux.h>
#include <npll/menu.h>
#include <npll/partition.h>
#include <npll/utils.h>
#include "wii/mini_ipc.h"

/*
 * menuEntry->data[] layout:
 *   Gumboot entries:
 *     [0] partition *  [1] filesystem *  [2] path (malloc'd)  [7] kind
 *   NPLL entries:
 *     [0] struct npllEntry * (malloc'd)                       [7] kind
 */
#define DATA_IDX_GB_PART 0
#define DATA_IDX_GB_FS   1
#define DATA_IDX_GB_PATH 2
#define DATA_IDX_NPLL    0
#define DATA_IDX_KIND    7

enum entryKind {
	KIND_GUMBOOT = 1,
	KIND_NPLL    = 2
};

enum npllType {
	NPLL_TYPE_NONE = 0,
	NPLL_TYPE_LINUX,
	NPLL_TYPE_GENERIC,
	NPLL_TYPE_GENERIC_DOL,
	NPLL_TYPE_CHANNEL
};

#define NPLL_MOD_IOS               BIT(0)
#define NPLL_MOD_MINI_SD           BIT(1)
#define NPLL_MOD_DKP_CMDLINE       BIT(2)
#define NPLL_MOD_LINUX_LDR_CMDLINE BIT(3)

#define NPLL_INCLUDE_MAX_DEPTH 8
#define NPLL_CFG_VERSION       "1"

struct npllEntry {
	char *id;
	char *name;
	enum npllType type;
	u32 mods;

	char *execPath;
	char *initrdPath;
	char *dtbPath;
	char *cmdline;

	u32 ios;
	u32 titleidHi, titleidLo;

	bool seenIOSKey;

	/* `platforms` listed the current console */
	bool platformMatch;

	/* origin: needed for `self:` path resolution at load time */
	struct partition *srcPart;
	struct filesystem *srcFs;
};

/*
 * Tracks whether *any* npll.cfg has been successfully parsed across calls.
 * Once true, gumboot.lst can no longer override globals (timeout/default).
 */
static bool npllEverConsumed = false;

/*
 * Shared helpers
 */

/* This allocator's free() panics on NULL; this wrapper makes free(NULL) a no-op. */
static void sfree(void *p) {
	if (p)
		free(p);
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

void C_FreeEntryData(struct menuEntry *entry) {
	struct npllEntry *ne;

	switch ((enum entryKind)entry->data[DATA_IDX_KIND]) {
	case KIND_GUMBOOT:
		sfree((void *)entry->data[DATA_IDX_GB_PATH]);
		break;

	case KIND_NPLL:
		ne = (struct npllEntry *)entry->data[DATA_IDX_NPLL];
		if (!ne)
			break;

		sfree(ne->id);
		sfree(ne->name);
		sfree(ne->execPath);
		sfree(ne->initrdPath);
		sfree(ne->dtbPath);
		sfree(ne->cmdline);
		sfree(ne);
		break;

	default:
		break;
	}
}

/*
 * Gumboot parser (legacy format)
 */

static void gumbootSelectedCB(struct menuEntry *entry) {
	struct partition *part;
	struct filesystem *fs;
	char *path;
	int fd, ret;

	part = (struct partition *)entry->data[DATA_IDX_GB_PART];
	fs = (struct filesystem *)entry->data[DATA_IDX_GB_FS];
	path = (char *)entry->data[DATA_IDX_GB_PATH];

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

#define FINALIZE_GB_ENTRY() \
	if (titleCur != 0 && pathCur != 0) {								\
		ensureEntryCapacity(&entries, &entryCapacity, (uint)numEntries, (uint)numEntries + 1);	\
		memset(&entries[numEntries], 0, sizeof(struct menuEntry));				\
		strcpy(entries[numEntries].name, entryTitle);						\
		entries[numEntries].selected = gumbootSelectedCB;					\
		entries[numEntries].data[DATA_IDX_GB_PART] = (u32)(uintptr_t)FS_MountedPartition;			\
		entries[numEntries].data[DATA_IDX_GB_FS] = (u32)(uintptr_t)FS_Mounted;				\
		entries[numEntries].data[DATA_IDX_GB_PATH] = (u32)(uintptr_t)malloc(strlen(entryPath) + 1);	\
		entries[numEntries].data[DATA_IDX_KIND] = KIND_GUMBOOT;					\
		strcpy((char *)entries[numEntries].data[DATA_IDX_GB_PATH], entryPath);			\
		numEntries++;										\
		memset(entryPath, 0, sizeof(entryPath));						\
		memset(entryTitle, 0, sizeof(entryTitle));						\
		titleCur = pathCur = 0;									\
	}												\
	isInEntry = false;

static int gumbootProbe(struct menuEntry **entriesOut, int *timeoutOut, uint *defaultOut) {
	int fd;
	ssize_t size, ret;
	uint lineNum = 1, titleCur = 0, pathCur = 0, numEntries = 0;
	uint entryCapacity = 0;
	char *file, *curLine, *cur, *end, entryTitle[64], entryPath[128];
	bool skipToEndOfLine = false, isInEntry = false, isInTitle = false, isInPath = false;
	struct menuEntry *entries = NULL;

	fd = FS_Open("gumboot/gumboot.lst");
	if (fd < 0)
		return 0; /* absent is fine */

	size = FS_GetSize(fd);
	if (size <= 0) {
		log_printf("FS_GetSize on gumboot/gumboot.lst failed: %d\r\n", size);
		FS_Close(fd);
		return -1;
	}

	file = malloc((size_t)size + 1);
	ret = FS_Read(fd, file, (size_t)size);
	if (ret != size) {
		log_printf("FS_Read on gumboot/gumboot.lst failed: %d\r\n", ret);
		free(file);
		FS_Close(fd);
		return -1;
	}
	file[size] = '\0';
	FS_Close(fd);

	cur = curLine = file;
	end = file + size;
	memset(entryTitle, 0, sizeof(entryTitle));
	memset(entryPath, 0, sizeof(entryPath));

	while (cur != end) {
		if (cur == curLine && *curLine != '\t' && isInEntry) {
			FINALIZE_GB_ENTRY()
		}
		if (*cur == '\r' || *cur == '\n') {
			curLine = cur + 1;
			if (*cur == '\n')
				lineNum++;
			skipToEndOfLine = false;
			isInTitle = false;
			isInPath = false;
			goto cont;
		}
		else if (skipToEndOfLine)
			goto cont;
		else if (*curLine == '#') {
			skipToEndOfLine = true;
			goto cont;
		}
		else if (isInTitle) {
			if (titleCur >= sizeof(entryTitle) - 1)
				goto cont;
			entryTitle[titleCur++] = *cur;
			goto cont;
		}
		else if (isInPath) {
			if (pathCur >= sizeof(entryPath) - 1)
				goto cont;
			entryPath[pathCur++] = *cur;
			goto cont;
		}

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
			if (timeoutOut)
				*timeoutOut = (int)strtol(curLine + 8, NULL, 10);
			skipToEndOfLine = true;
			cur += 8;
			continue;
		}
		else if (!isInEntry && !memcmp(curLine, "default ", 8)) {
			if (defaultOut)
				*defaultOut = (uint)strtoul(curLine + 8, NULL, 10);
			skipToEndOfLine = true;
			cur += 8;
			continue;
		}

		else if (!isInEntry && !memcmp(curLine, "title ", 6)) {
			isInTitle = true;
			isInEntry = true;
			titleCur = 0;
			cur += 5;
			goto cont;
		}
		else if (isInEntry && !memcmp(curLine, "\troot ", 6)) {
			cur += 6;
			if (!memcmp(cur, "(sd0,0)", 7))
				cur += 7;
			if (*cur == '\r' || *cur == '\n')
				continue;
			else if (*cur != '/') {
				log_printf("Garbage 'root' directive @ line %d:%d (offset %d)\r\n", lineNum, (uintptr_t)cur - (uintptr_t)curLine, (uintptr_t)cur - (uintptr_t)file);
				while (numEntries)
					free((char *)entries[--numEntries].data[DATA_IDX_GB_PATH]);
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
				log_printf("Garbage 'kernel' directive @ line %d:%d (offset %d)\r\n", lineNum, (uintptr_t)cur - (uintptr_t)curLine, (uintptr_t)cur - (uintptr_t)file);
				while (numEntries)
					free((char *)entries[--numEntries].data[DATA_IDX_GB_PATH]);
				if (entries)
					free(entries);
				free(file);
				return -1;
			}
			isInPath = true;
			if (!pathCur)
				cur++;
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
			log_printf("Garbage in config file @ line %d:%d (offset %d)\r\n", lineNum, (uintptr_t)cur - (uintptr_t)curLine, (uintptr_t)cur - (uintptr_t)file);
			while (numEntries)
				free((char *)entries[--numEntries].data[DATA_IDX_GB_PATH]);
			if (entries)
				free(entries);
			free(file);
			return -1;
		}

	cont:
		cur++;
	}

	if (isInEntry) {
		FINALIZE_GB_ENTRY();
	}

	*entriesOut = numEntries ? entries : NULL;
	free(file);
	if (!numEntries)
		free(entries);
	return (int)numEntries;
}

/*
 * NPLL parser
 */

static const char *curPlatformName(void) {
	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: return "gamecube";
	case CONSOLE_TYPE_WII:      return "wii";
	case CONSOLE_TYPE_WII_U:    return "wiiu";
	}

	return "unknown";
}

static bool isPlatToken(const char *s, size_t len) {
	return (len == 3 && !memcmp(s, "wii", 3))
		|| (len == 4 && !memcmp(s, "wiiu", 4))
		|| (len == 8 && !memcmp(s, "gamecube", 8));
}

/*
 * Check if `key` matches `base` or `base_<curplat>`.
 * Returns:
 *   1  : matched current platform (or base with no suffix at all if perPlat==false)
 *   0  : matched a non-current platform suffix (caller should ignore silently)
 *  -1  : did not match this base key
 */
static int matchPlatKey(const char *key, const char *base, bool perPlat) {
	size_t bl = strlen(base);
	size_t kl = strlen(key);
	const char *suf, *cp;
	size_t sl;

	if (!perPlat)
		return strcmp(key, base) == 0 ? 1 : -1;

	if (kl <= bl + 1 || memcmp(key, base, bl) != 0 || key[bl] != '_')
		return -1;

	suf = key + bl + 1;
	sl = kl - bl - 1;
	if (!isPlatToken(suf, sl))
		return -1;

	cp = curPlatformName();
	if (sl == strlen(cp) && !memcmp(suf, cp, sl))
		return 1;

	return 0;
}

/* "medium[,pN]:path", path doesn't start with '/' and is non-empty. */
static bool pathSyntaxOk(const char *p) {
	const char *colon, *comma, *q;

	if (!p || !*p)
		return false;

	colon = strchr(p, ':');
	if (!colon || colon == p || colon[1] == '\0' || colon[1] == '/')
		return false;

	comma = strchr(p, ',');
	if (comma && comma < colon) {
		if (comma == p || comma[1] != 'p')
			return false;

		q = comma + 2;
		if (q >= colon || !isdigit((u8)*q))
			return false;

		while (q < colon) {
			if (!isdigit((u8)*q))
				return false;
			q++;
		}
	}

	return true;
}

#define TOK_EQ(tok, tl, s) ((tl) == sizeof(s) - 1 && !memcmp((tok), (s), (tl)))

/* Parse type_X value into entry. Returns 0 on success, -1 on error. */
static int parseTypeValue(struct npllEntry *e, const char *val) {
	const char *p = val, *tok;
	size_t tl;
	bool primarySet = false;

	while (*p) {
		tok = p;
		while (*p && *p != ',') p++;
		tl = (size_t)(p - tok);
		if (*p == ',') p++;
		if (!tl)
			continue;

		if (TOK_EQ(tok, tl, "linux")) {
			if (primarySet) goto multi;
			e->type = NPLL_TYPE_LINUX;
			primarySet = true;
		}
		else if (TOK_EQ(tok, tl, "generic")) {
			if (primarySet) goto multi;
			e->type = NPLL_TYPE_GENERIC;
			primarySet = true;
		}
		else if (TOK_EQ(tok, tl, "generic_dol")) {
			if (primarySet) goto multi;
			e->type = NPLL_TYPE_GENERIC_DOL;
			primarySet = true;
		}
		else if (TOK_EQ(tok, tl, "channel")) {
			if (primarySet) goto multi;
			e->type = NPLL_TYPE_CHANNEL;
			e->mods |= NPLL_MOD_IOS;	/* channel implies ios */
			primarySet = true;
		}
		else if (TOK_EQ(tok, tl, "ios"))
			e->mods |= NPLL_MOD_IOS;
		else if (TOK_EQ(tok, tl, "minisd"))
			e->mods |= NPLL_MOD_MINI_SD;
		else if (TOK_EQ(tok, tl, "dkp_cmdline"))
			e->mods |= NPLL_MOD_DKP_CMDLINE;
		else if (TOK_EQ(tok, tl, "linux_loader_cmdline"))
			e->mods |= NPLL_MOD_LINUX_LDR_CMDLINE;
		else {
			log_printf("warn: unknown type token in '%s'\r\n", val);
			return -1;
		}
	}

	return primarySet ? 0 : -1;

multi:
	log_printf("warn: multiple primary types in '%s'\r\n", val);
	return -1;
}

/*
 * Parse `platforms` value; set entry->platformMatch if current platform appears.
 * Returns true if at least one valid token was present.
 */
static bool parsePlatformsValue(struct npllEntry *e, const char *val) {
	const char *p = val, *tok;
	size_t tl;
	bool anyValid = false;
	const char *cp = curPlatformName();
	size_t cpl = strlen(cp);

	while (*p) {
		tok = p;
		while (*p && *p != ',') p++;
		tl = (size_t)(p - tok);
		if (*p == ',') p++;
		if (!tl)
			continue;

		if (isPlatToken(tok, tl)) {
			anyValid = true;
			if (tl == cpl && !memcmp(tok, cp, tl))
				e->platformMatch = true;
		}
		else
			log_printf("warn: unknown platform token in '%s'\r\n", val);
	}

	return anyValid;
}

/* Parse "00000001,00000002" titleid form. */
static int parseTitleid(struct npllEntry *e, const char *val) {
	const char *comma = strchr(val, ',');
	char *endp;

	if (!comma)
		return -1;

	e->titleidHi = (u32)strtoul(val, &endp, 16);
	if (endp != comma)
		return -1;

	e->titleidLo = (u32)strtoul(comma + 1, &endp, 16);
	if (*endp != '\0')
		return -1;

	return 0;
}

static void freeNPLLEntry(struct npllEntry *e) {
	if (!e)
		return;

	sfree(e->id);
	sfree(e->name);
	sfree(e->execPath);
	sfree(e->initrdPath);
	sfree(e->dtbPath);
	sfree(e->cmdline);
	free(e);
}

static bool idExists(struct npllEntry **arr, uint n, const char *id) {
	uint i;

	for (i = 0; i < n; i++) {
		if (arr[i]->id && !strcmp(arr[i]->id, id))
			return true;
	}

	return false;
}

/* Validate a fully-parsed entry. Returns true if it should be kept. */
static bool validateNPLLEntry(struct npllEntry *e) {
	bool needsExec = false;

	if (!e->id || !e->name) {
		log_printf("warn: entry missing id or name; dropping\r\n");
		return false;
	}

	/* silently skip entries not for this platform */
	if (!e->platformMatch)
		return false;

	if (e->type == NPLL_TYPE_NONE) {
		log_printf("warn: entry '%s' has no usable type_%s; dropping\r\n", e->id, curPlatformName());
		return false;
	}

	/* modifier-platform restrictions */
	if ((e->mods & NPLL_MOD_IOS) && H_ConsoleType != CONSOLE_TYPE_WII && e->type != NPLL_TYPE_CHANNEL) {
		log_printf("warn: entry '%s' uses 'ios' modifier outside Wii; dropping\r\n", e->id);
		return false;
	}
	if ((e->mods & NPLL_MOD_MINI_SD) && H_ConsoleType != CONSOLE_TYPE_WII) {
		log_printf("warn: entry '%s' uses 'minisd' modifier outside Wii; dropping\r\n", e->id);
		return false;
	}

	/* reloading IOS replaces MINI, so 'minisd' (which asks MINI for things) cannot coexist with 'ios' */
	if ((e->mods & NPLL_MOD_IOS) && (e->mods & NPLL_MOD_MINI_SD)) {
		log_printf("warn: entry '%s' uses both 'ios' and 'minisd' modifiers (mutually exclusive); dropping\r\n", e->id);
		return false;
	}

	switch (e->type) {
	case NPLL_TYPE_LINUX:
	case NPLL_TYPE_GENERIC:
	case NPLL_TYPE_GENERIC_DOL:
		needsExec = true;
		break;

	case NPLL_TYPE_CHANNEL:
		if (!e->titleidHi && !e->titleidLo) {
			log_printf("warn: channel entry '%s' missing titleid; dropping\r\n", e->id);
			return false;
		}
		break;

	default:
		return false;
	}

	if (needsExec) {
		if (!e->execPath) {
			log_printf("warn: entry '%s' missing exec_%s; dropping\r\n", e->id, curPlatformName());
			return false;
		}
		if (!pathSyntaxOk(e->execPath)) {
			log_printf("warn: entry '%s' has bad exec path '%s'; dropping\r\n", e->id, e->execPath);
			return false;
		}
	}

	if (e->type == NPLL_TYPE_LINUX) {
		if (e->initrdPath && !pathSyntaxOk(e->initrdPath)) {
			log_printf("warn: entry '%s' has bad initrd path; dropping\r\n", e->id);
			return false;
		}
		if (e->dtbPath && !pathSyntaxOk(e->dtbPath)) {
			log_printf("warn: entry '%s' has bad dtb path; dropping\r\n", e->id);
			return false;
		}
		if (e->cmdline && !e->dtbPath && !(e->mods & (NPLL_MOD_DKP_CMDLINE | NPLL_MOD_LINUX_LDR_CMDLINE))) {
			log_printf("warn: entry '%s' has cmdline but no dtb and no cmdline modifier; dropping\r\n", e->id);
			return false;
		}
	}

	if (e->mods & NPLL_MOD_IOS) {
		if (!e->seenIOSKey && e->type != NPLL_TYPE_CHANNEL) {
			log_printf("warn: entry '%s' uses 'ios' modifier but missing 'ios' key; dropping\r\n", e->id);
			return false;
		}
	}
	return true;
}

/* Parsing context, shared across recursive include calls. */
struct npllCtx {
	struct npllEntry **entries;
	uint numEntries;
	uint entryCap;

	bool sawCfgVersion;
	bool aborted;

	int timeout;
	bool hasTimeout;
	char *defaultId;

	struct npllEntry *cur;

	/* origin info (for `self:` snapshots) */
	struct partition *srcPart;
	struct filesystem *srcFs;

	int depth;
	const char *includeStack[NPLL_INCLUDE_MAX_DEPTH];
};

static void ensureNPLLCapacity(struct npllCtx *ctx, uint needed) {
	struct npllEntry **na;
	uint nc;

	if (ctx->entryCap >= needed)
		return;

	nc = ctx->entryCap ? ctx->entryCap : 4;
	while (nc < needed)
		nc *= 2;

	na = malloc(sizeof(*na) * nc);
	if (ctx->entries) {
		memcpy(na, ctx->entries, sizeof(*na) * ctx->numEntries);
		free(ctx->entries);
	}

	ctx->entries = na;
	ctx->entryCap = nc;
}

static void finalizeCurEntry(struct npllCtx *ctx) {
	if (!ctx->cur)
		return;

	if (idExists(ctx->entries, ctx->numEntries, ctx->cur->id)) {
		log_printf("warn: duplicate entry id '%s'; keeping first\r\n", ctx->cur->id);
		freeNPLLEntry(ctx->cur);
	}
	else if (validateNPLLEntry(ctx->cur)) {
		ensureNPLLCapacity(ctx, ctx->numEntries + 1);
		ctx->entries[ctx->numEntries++] = ctx->cur;
	}
	else
		freeNPLLEntry(ctx->cur);

	ctx->cur = NULL;
}

static void assignStr(char **slot, const char *val) {
	sfree(*slot);
	*slot = strdup(val);
}

static void handleEntryKey(struct npllCtx *ctx, char *key, char *val) {
	struct npllEntry *e = ctx->cur;
	int r;

	if (!e) {
		log_printf("warn: indented key '%s' outside an entry; ignoring\r\n", key);
		return;
	}

	if (!strcmp(key, "name")) {
		assignStr(&e->name, val);
		return;
	}
	if (!strcmp(key, "platforms")) {
		if (!parsePlatformsValue(e, val))
			log_printf("warn: entry '%s' has no valid platform tokens\r\n", e->id ? e->id : "?");
		return;
	}
	if (!strcmp(key, "titleid")) {
		if (parseTitleid(e, val) != 0)
			log_printf("warn: bad titleid '%s'\r\n", val);
		return;
	}
	if (!strcmp(key, "ios")) {
		e->ios = (u32)strtoul(val, NULL, 10);
		e->seenIOSKey = true;
		return;
	}

	/* per-platform keys */
	r = matchPlatKey(key, "type", true);
	if (r == 1) {
		if (parseTypeValue(e, val) != 0)
			e->type = NPLL_TYPE_NONE;
		return;
	}
	if (r == 0) return;

	r = matchPlatKey(key, "exec", true);
	if (r == 1) { assignStr(&e->execPath, val); return; }
	if (r == 0) return;

	r = matchPlatKey(key, "initrd", true);
	if (r == 1) { assignStr(&e->initrdPath, val); return; }
	if (r == 0) return;

	r = matchPlatKey(key, "dtb", true);
	if (r == 1) { assignStr(&e->dtbPath, val); return; }
	if (r == 0) return;

	r = matchPlatKey(key, "cmdline", true);
	if (r == 1) { assignStr(&e->cmdline, val); return; }
	if (r == 0) return;

	log_printf("warn: unknown entry key '%s'; ignoring\r\n", key);
}

static void handleGlobalKey(struct npllCtx *ctx, char *key, char *val) {
	/* cfg_version may legally appear in an included file; just ignored here. */
	if (!strcmp(key, "cfg_version"))
		return;

	if (!strcmp(key, "default")) {
		if (!ctx->defaultId)
			ctx->defaultId = strdup(val);
		return;
	}

	if (!strcmp(key, "timeout")) {
		ctx->timeout = (int)strtol(val, NULL, 10);
		ctx->hasTimeout = true;
		return;
	}

	log_printf("warn: unknown global key '%s'; ignoring\r\n", key);
}

/* Forward decl: parseFile may include other files. */
static void parseFile(struct npllCtx *ctx, char *buf, struct partition *part, struct filesystem *fs, const char *displayPath);

/*
 * Path resolution: aliases and block-device lookup
 */

struct mediumAlias {
	const char *alias;
	const char *target;
	u32 consoleMask;	/* bitmask of (1 << consoleType) */
};

static const struct mediumAlias mediumAliases[] = {
	{ "dvd",  "di",     BIT(CONSOLE_TYPE_GAMECUBE) | BIT(CONSOLE_TYPE_WII) },
	{ "emmc", "sdhci2", BIT(CONSOLE_TYPE_WII_U) },
	{ "sd",   "sdhci0", BIT(CONSOLE_TYPE_WII)      | BIT(CONSOLE_TYPE_WII_U) },
};

/*
 * Resolve a medium alias to a real block device name, honoring the current
 * console type.  Returns NULL if the input is not an alias.
 */
static const char *resolveMediumAlias(const char *medium, size_t medLen) {
	uint i;

	/* `nand` is special: maps to a different bdev per console */
	if (medLen == 4 && !memcmp(medium, "nand", 4)) {
		if (H_ConsoleType == CONSOLE_TYPE_WII)
			return "nand0";
		if (H_ConsoleType == CONSOLE_TYPE_WII_U)
			return "nand1";
		return NULL;
	}

	for (i = 0; i < sizeof(mediumAliases) / sizeof(mediumAliases[0]); i++) {
		const struct mediumAlias *a = &mediumAliases[i];

		if (strlen(a->alias) != medLen)
			continue;
		if (memcmp(a->alias, medium, medLen) != 0)
			continue;
		if (!(a->consoleMask & BIT(H_ConsoleType)))
			continue;
		return a->target;
	}

	return NULL;
}

static struct blockDevice *findBdevByName(const char *name, size_t nameLen) {
	uint i;

	for (i = 0; i < B_NumDevices; i++) {
		struct blockDevice *bd = B_Devices[i];

		if (!bd)
			continue;
		if (strlen(bd->name) != nameLen)
			continue;
		if (memcmp(bd->name, name, nameLen) != 0)
			continue;
		return bd;
	}

	return NULL;
}

static int resolvePath(const char *spec, struct partition *originPart, struct filesystem *originFs, struct partition **partOut, struct filesystem **fsOut, const char **subPathOut) {
	const char *colon, *comma, *bdName;
	size_t medLen, bdLen;
	struct blockDevice *bdev;
	struct partition *part;
	struct filesystem *fs;
	char *endp;
	uint partIdx;

	if (!spec || !*spec || !partOut || !fsOut || !subPathOut)
		return -1;

	colon = strchr(spec, ':');
	if (!colon || colon == spec || colon[1] == '\0' || colon[1] == '/')
		return -1;

	comma = strchr(spec, ',');
	if (comma && comma > colon)
		comma = NULL;

	medLen = (size_t)((comma ? comma : colon) - spec);

	/* `self`: no partition number permitted; falls back to caller-provided origin */
	if (medLen == 4 && !memcmp(spec, "self", 4)) {
		if (comma)
			return -1;
		if (!originPart || !originFs)
			return -1;

		*partOut = originPart;
		*fsOut = originFs;
		*subPathOut = colon + 1;
		return 0;
	}

	/* try an alias first; fall back to treating the medium as a literal bdev name */
	bdName = resolveMediumAlias(spec, medLen);
	if (bdName)
		bdLen = strlen(bdName);
	else {
		bdName = spec;
		bdLen = medLen;
	}

	bdev = findBdevByName(bdName, bdLen);
	if (!bdev)
		return -1;

	/* partition index: 1-indexed in spec, 0-indexed in storage */
	if (comma) {
		if (comma[1] != 'p')
			return -1;

		partIdx = (uint)strtoul(comma + 2, &endp, 10);
		if (endp != colon || partIdx == 0 || partIdx > bdev->numPartitions)
			return -1;

		part = bdev->partitions[partIdx - 1];
	}
	else {
		/* no partition number: per spec, "full disk" -- in this impl, partitions[0] */
		if (bdev->numPartitions == 0)
			return -1;
		part = bdev->partitions[0];
	}

	if (!part)
		return -1;

	fs = FS_Probe(part);
	if (!fs)
		return -1;

	*partOut = part;
	*fsOut = fs;
	*subPathOut = colon + 1;
	return 0;
}

/*
 * Resolve an `@include` target.  Only includes whose path lands on the
 * currently mounted fs/partition are supported; cross-mount includes return
 * NULL and the caller should warn+skip.
 */
static const char *resolveSameDevIncludePath(const char *spec, struct partition *part, struct filesystem *fs) {
	struct partition *rp;
	struct filesystem *rfs;
	const char *sub;

	if (resolvePath(spec, part, fs, &rp, &rfs, &sub) != 0)
		return NULL;

	if (rp != FS_MountedPartition || rfs != FS_Mounted)
		return NULL;

	return sub;
}

static void doInclude(struct npllCtx *ctx, const char *spec) {
	const char *path;
	char *buf;
	int fd, i;
	ssize_t got, size;

	if (ctx->depth >= NPLL_INCLUDE_MAX_DEPTH) {
		log_printf("warn: @include depth limit reached at '%s'; skipping\r\n", spec);
		return;
	}

	for (i = 0; i < ctx->depth; i++) {
		if (ctx->includeStack[i] && !strcmp(ctx->includeStack[i], spec)) {
			log_printf("warn: recursive @include of '%s'; skipping\r\n", spec);
			return;
		}
	}

	if (!pathSyntaxOk(spec)) {
		log_printf("warn: bad @include path '%s'; skipping\r\n", spec);
		return;
	}

	path = resolveSameDevIncludePath(spec, ctx->srcPart, ctx->srcFs);
	if (!path) {
		log_printf("warn: @include '%s' targets a different device; only same-device includes are currently supported, skipping\r\n", spec);
		return;
	}

	fd = FS_Open(path);
	if (fd < 0) {
		log_printf("warn: @include '%s' could not be opened (%d); skipping\r\n", spec, fd);
		return;
	}

	size = FS_GetSize(fd);
	if (size <= 0) {
		log_printf("warn: @include '%s' has bad size %d; skipping\r\n", spec, size);
		FS_Close(fd);
		return;
	}

	buf = malloc((size_t)size + 1);
	got = FS_Read(fd, buf, (size_t)size);
	FS_Close(fd);

	if (got != size) {
		log_printf("warn: @include '%s' short read (%d); skipping\r\n", spec, (int)got);
		free(buf);
		return;
	}
	buf[size] = '\0';

	ctx->includeStack[ctx->depth++] = spec;
	parseFile(ctx, buf, ctx->srcPart, ctx->srcFs, spec);
	ctx->depth--;
	ctx->includeStack[ctx->depth] = NULL;

	free(buf);
}

static void parseFile(struct npllCtx *ctx, char *buf, struct partition *part, struct filesystem *fs, const char *displayPath) {
	char *line, *eol, *eq, *rb, *p = buf;
	uint lineNum = 0;
	bool indented, firstMeaningful = (ctx->depth == 0) && !ctx->sawCfgVersion;
	size_t llen;

	(void)part;
	(void)fs;

	while (*p && !ctx->aborted) {
		line = p;
		eol = p;
		while (*eol && *eol != '\n') eol++;
		if (*eol == '\n') {
			*eol = '\0';
			p = eol + 1;
		}
		else
			p = eol;
		lineNum++;

		llen = strlen(line);
		if (llen && line[llen - 1] == '\r') {
			line[llen - 1] = '\0';
			llen--;
		}

		/* empty lines */
		if (llen == 0)
			continue;

		/* comments (with optional leading indent) */
		if (line[0] == '#' || (line[0] == '\t' && line[1] == '#'))
			continue;

		/* cfg_version must be very first meaningful line of top-level file */
		if (firstMeaningful) {
			firstMeaningful = false;
			if (memcmp(line, "cfg_version=", 12) != 0) {
				log_printf("%s: missing cfg_version on first line; aborting\r\n", displayPath);
				ctx->aborted = true;
				return;
			}
			if (strcmp(line + 12, NPLL_CFG_VERSION) != 0) {
				log_printf("%s: cfg_version '%s' unsupported (want %s); aborting\r\n", displayPath, line + 12, NPLL_CFG_VERSION);
				ctx->aborted = true;
				return;
			}
			ctx->sawCfgVersion = true;
			continue;
		}

		/* @include */
		if (!memcmp(line, "@include ", 9)) {
			doInclude(ctx, line + 9);
			continue;
		}

		/* entry header: [id] */
		if (line[0] == '[') {
			rb = strchr(line, ']');
			if (!rb || rb[1] != '\0' || rb == line + 1) {
				log_printf("%s:%u: malformed entry header '%s'; ignoring\r\n", displayPath, lineNum, line);
				continue;
			}

			finalizeCurEntry(ctx);
			ctx->cur = malloc(sizeof(*ctx->cur));
			memset(ctx->cur, 0, sizeof(*ctx->cur));

			*rb = '\0';
			ctx->cur->id = strdup(line + 1);
			ctx->cur->srcPart = ctx->srcPart;
			ctx->cur->srcFs = ctx->srcFs;
			continue;
		}

		indented = (line[0] == '\t');
		if (indented) {
			line++;
			llen--;
		}

		eq = strchr(line, '=');
		if (!eq || eq == line) {
			log_printf("%s:%u: line has no key=value form; ignoring\r\n", displayPath, lineNum);
			continue;
		}

		*eq = '\0';
		if (indented)
			handleEntryKey(ctx, line, eq + 1);
		else
			handleGlobalKey(ctx, line, eq + 1);
	}
}

static int npllEnsureFS(struct npllEntry *ne, char *pathspec, const char **pathOut) {
	struct partition *part;
	struct filesystem *fs;
	int ret;

	ret = resolvePath(pathspec, ne->srcPart, ne->srcFs, &part, &fs, pathOut);
	if (ret != 0) {
		log_printf("npllEsnureFS: resolvePath returned %d\r\n", ret);
		return ret;
	}

	if (FS_MountedPartition != part || FS_Mounted != fs) {
		ret = FS_Mount(fs, part);
		if (ret != 0) {
			log_printf("FS_Mount failed: %d\r\n", ret);
			return ret;
		}
	}

	return 0;
}

/*
 * Modifier-driven pre-entry actions.  Only meaningful on Wii; the hook itself
 * is only registered when at least one Wii-only modifier is set.
 */
static u32 preEntryIOSVer;
static bool preEntryMINISD;

static void preEntryWiiHook(void) {
	enum MINI_Err err;

	if (preEntryIOSVer)
		H_WiiReloadIOS(preEntryIOSVer);
	if (preEntryMINISD) {
		err = MINI_IPCPost(IPC_MINI_CODE_SDHC_DISCOVER, 0, 0);
		if (err != MINI_OK)
			log_printf("MINI SDHC Discover failed: %d\r\n", err); /* can't really recover */
	}
}

static void installPreEntryHook(struct npllEntry *ne) {
	preEntryIOSVer = 0;
	preEntryMINISD = false;
	H_PreEntryHook = NULL;

	if (H_ConsoleType != CONSOLE_TYPE_WII)
		return;

	if (ne->mods & NPLL_MOD_IOS)
		preEntryIOSVer = ne->ios;
	if (ne->mods & NPLL_MOD_MINI_SD)
		preEntryMINISD = true;

	if (preEntryIOSVer || preEntryMINISD)
		H_PreEntryHook = preEntryWiiHook;
}

static void npllBootLinux(struct npllEntry *ne) {
	int fd, ret;
	const char *path;
	struct linuxBootFiles files;
	struct memRange reserved[64];
	void *temporaryDtb = NULL;
	size_t reservedCount = 0;
	u32 dtbExtra, cmdlineFlags = 0;

	memset(&files, 0, sizeof(files));

	if (ne->dtbPath) {
		if (npllEnsureFS(ne, ne->dtbPath, &path))
			goto fail;
		fd = FS_Open(path);
		dtbExtra = 1024u + (ne->cmdline ? (u32)strlen(ne->cmdline) + 1u : 0u);
		if (fd < 0 || L_LoadAuxFile(fd, POOL_ANY, &temporaryDtb, 0, &files.dtbSize)) {
			log_printf("npllBootLinux: failed to load device tree: %d\r\n", fd);
			goto fail;
		}
		ret = L_CollectReserved(temporaryDtb, reserved, 64, &reservedCount);
		if (ret) {
			log_printf("npllBootLinux: failed to collect DT reservations: %d\r\n", ret);
			goto fail;
		}
		files.dtb = M_PoolAllocAvoid(POOL_MEM1, files.dtbSize + dtbExtra, 64,
					    reserved, reservedCount);
		if (!files.dtb) {
			log_puts("npllBootLinux: no non-reserved space available for DTB");
			goto fail;
		}
		memcpy(files.dtb, temporaryDtb, files.dtbSize);
		free(temporaryDtb);
		temporaryDtb = NULL;
	}

	if (ne->initrdPath) {
		if (npllEnsureFS(ne, ne->initrdPath, &path))
			goto fail;
		fd = FS_Open(path);
		if (fd < 0 || L_LoadAuxFileAvoid(fd, POOL_ANY, &files.initrd, 0,
						 &files.initrdSize, reserved, reservedCount)) {
			log_printf("npllBootLinux: failed to load initrd: %d\r\n", fd);
			goto fail;
		}
	}

	if (files.dtb) {
		if (L_PrepareDTB(&files, ne->cmdline))
			goto fail;
	}

	if (npllEnsureFS(ne, ne->execPath, &path))
		goto fail;

	fd = FS_Open(path);
	if (fd < 0) {
		log_printf("npllBootLinux: FS_Open returned %d\r\n", fd);
		goto fail;
	}

	if (ne->mods & NPLL_MOD_DKP_CMDLINE)
		cmdlineFlags |= ELF_LINUX_CMDLINE_DKP;
	if (ne->mods & NPLL_MOD_LINUX_LDR_CMDLINE)
		cmdlineFlags |= ELF_LINUX_CMDLINE_LNXLDR;

	installPreEntryHook(ne);
	ret = ELF_LoadLinuxFile(fd, files.dtb, files.initrd, files.initrdSize,
				ne->cmdline, cmdlineFlags);
	log_printf("npllBootLinux: ELF_LoadLinuxFile returned %d\r\n", ret);

fail:
	sfree(temporaryDtb);
	sfree(files.dtb);
	sfree(files.initrd);
}

static void npllBootELF(struct npllEntry *ne) {
	int fd, ret;
	const char *path;

	if (npllEnsureFS(ne, ne->execPath, &path))
		return;

	fd = FS_Open(path);
	if (fd < 0) {
		log_printf("npllBootELF: FS_Open returned %d\r\n", fd);
		return;
	}

	installPreEntryHook(ne);
	ret = ELF_LoadFile(fd);
	log_printf("npllBootELF: ELF_LoadFile returned %d\r\n", ret);
}

static void npllBootDOL(struct npllEntry *ne) {
	int fd, ret;
	const char *path;

	if (npllEnsureFS(ne, ne->execPath, &path))
		return;

	fd = FS_Open(path);
	if (fd < 0) {
		log_printf("npllBootDOL: FS_Open returned %d\r\n", fd);
		return;
	}

	installPreEntryHook(ne);
	ret = DOL_LoadFile(fd);
	log_printf("npllBootDOL: DOL_LoadFile returned %d\r\n", ret);
}

static void npllBootChannel(struct npllEntry *ne) {
	if (H_ConsoleType != CONSOLE_TYPE_WII) {
		log_puts("npllBootChannel: channel boot is Wii-only");
		return;
	}

	log_printf("Booting channel %08x-%08x\r\n", ne->titleidHi, ne->titleidLo);

	H_PrepareForExecEntry();
	H_WiiBootChannel(ne->titleidHi, ne->titleidLo);
	/* does not return */
}

static void npllSelectedCB(struct menuEntry *entry) {
	struct npllEntry *ne = (struct npllEntry *)entry->data[DATA_IDX_NPLL];

	assert(ne);

	switch (ne->type) {
	case NPLL_TYPE_LINUX: {
		npllBootLinux(ne);
		break;
	}
	case NPLL_TYPE_GENERIC: {
		npllBootELF(ne);
		break;
	}
	case NPLL_TYPE_GENERIC_DOL: {
		npllBootDOL(ne);
		break;
	}
	case NPLL_TYPE_CHANNEL: {
		npllBootChannel(ne);
		break;
	}
	default:
		break;
	}
}

/*
 * Probe the current partition for npll.cfg, parse it, and emit menu entries.
 * On success returns the number of entries (may be 0). On hard parse failure
 * returns -1. Absent file returns 0 without complaint.
 */
static int npllProbe(struct menuEntry **entriesOut, int *timeoutOut, uint *defaultOut, bool allowGlobals) {
	struct npllCtx ctx;
	struct menuEntry *menuEntries = NULL;
	char *buf;
	int fd;
	ssize_t got, size;
	uint menuCap = 0, i;

	fd = FS_Open("npll.cfg");
	if (fd < 0)
		return 0;

	size = FS_GetSize(fd);
	if (size <= 0) {
		log_printf("FS_GetSize on npll.cfg failed: %d\r\n", size);
		FS_Close(fd);
		return -1;
	}

	buf = malloc((size_t)size + 1);
	got = FS_Read(fd, buf, (size_t)size);
	FS_Close(fd);

	if (got != size) {
		log_printf("FS_Read on npll.cfg failed: %d\r\n", (int)got);
		free(buf);
		return -1;
	}
	buf[size] = '\0';

	memset(&ctx, 0, sizeof(ctx));
	ctx.timeout = -1;
	ctx.srcPart = FS_MountedPartition;
	ctx.srcFs = FS_Mounted;

	parseFile(&ctx, buf, ctx.srcPart, ctx.srcFs, "npll.cfg");
	finalizeCurEntry(&ctx);
	free(buf);

	if (ctx.aborted) {
		for (i = 0; i < ctx.numEntries; i++)
			freeNPLLEntry(ctx.entries[i]);
		sfree(ctx.entries);
		sfree(ctx.defaultId);
		return -1;
	}

	if (allowGlobals) {
		if (ctx.hasTimeout)
			*timeoutOut = ctx.timeout;

		if (ctx.defaultId) {
			for (i = 0; i < ctx.numEntries; i++) {
				if (!strcmp(ctx.entries[i]->id, ctx.defaultId)) {
					*defaultOut = i;
					break;
				}
			}
			if (i == ctx.numEntries)
				log_printf("warn: default entry '%s' not found\r\n", ctx.defaultId);
		}
	}
	sfree(ctx.defaultId);

	if (ctx.numEntries == 0) {
		sfree(ctx.entries);
		*entriesOut = NULL;
		npllEverConsumed = true;
		return 0;
	}

	/* materialize menu entries pointing at the per-entry npll structs */
	ensureEntryCapacity(&menuEntries, &menuCap, 0, ctx.numEntries);
	for (i = 0; i < ctx.numEntries; i++) {
		struct npllEntry *ne = ctx.entries[i];
		size_t nl;

		memset(&menuEntries[i], 0, sizeof(menuEntries[i]));

		nl = strlen(ne->name);
		if (nl >= sizeof(menuEntries[i].name))
			nl = sizeof(menuEntries[i].name) - 1;
		memcpy(menuEntries[i].name, ne->name, nl);
		menuEntries[i].name[nl] = '\0';

		menuEntries[i].selected = npllSelectedCB;
		menuEntries[i].data[DATA_IDX_NPLL] = (u32)(uintptr_t)ne;
		menuEntries[i].data[DATA_IDX_KIND] = KIND_NPLL;
	}
	free(ctx.entries);

	*entriesOut = menuEntries;
	npllEverConsumed = true;
	return (int)ctx.numEntries;
}

/*
 * Orchestrator: try both parsers, merge results.
 */
int C_Probe(struct menuEntry **entriesOut, int *timeoutOut, uint *defaultOut) {
	struct menuEntry *npllEnts = NULL, *gbEnts = NULL, *merged;
	int npllN, gbN, npllTimeout = -1, gbTimeout = -1;
	uint i, npllDefault = 0, gbDefault = 0;
	bool firstNPLLEver, npllGlobalsApplied = false;

	*timeoutOut = -1;
	*defaultOut = 0;
	*entriesOut = NULL;

	/* only the *first* npll.cfg overall is permitted to set globals */
	firstNPLLEver = !npllEverConsumed;
	npllN = npllProbe(&npllEnts, &npllTimeout, &npllDefault, firstNPLLEver);
	if (npllN < 0)
		return -1;

	if (firstNPLLEver && npllEverConsumed)
		npllGlobalsApplied = true;

	gbN = gumbootProbe(&gbEnts, &gbTimeout, &gbDefault);
	if (gbN < 0) {
		for (i = 0; i < (uint)npllN; i++)
			C_FreeEntryData(&npllEnts[i]);
		sfree(npllEnts);
		return -1;
	}

	if (npllGlobalsApplied) {
		*timeoutOut = npllTimeout;
		*defaultOut = npllDefault;
	}
	else if (!npllEverConsumed) {
		*timeoutOut = gbTimeout;
		*defaultOut = gbDefault;
	}
	/*
	 * else: an earlier partition already locked globals via npll.cfg;
	 * leave the (-1, 0) defaults so the caller knows we have nothing
	 * new to contribute here.
	 */

	if (npllN + gbN == 0) {
		sfree(npllEnts);
		sfree(gbEnts);
		return 0;
	}

	merged = malloc(sizeof(*merged) * (uint)(npllN + gbN));
	if (npllN)
		memcpy(merged, npllEnts, sizeof(*merged) * (uint)npllN);
	if (gbN)
		memcpy(merged + npllN, gbEnts, sizeof(*merged) * (uint)gbN);

	sfree(npllEnts);
	sfree(gbEnts);

	*entriesOut = merged;
	return npllN + gbN;
}
