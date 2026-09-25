/* NPLL - Wii system configuration. Copyright (C) 2026 Techflash */
#ifndef _WII_CONFIGS_H
#define _WII_CONFIGS_H

#include <npll/types.h>
#include <npll/flipper/vi.h>

#define WC_SYSCONF_PATH "/shared2/sys/SYSCONF"
#define WC_SYSCONF_MAX_SIZE 0x4000u

enum wcType {
	WC_BIG_ARRAY = 1, WC_SMALL_ARRAY, WC_BYTE, WC_SHORT,
	WC_LONG, WC_LONG_LONG, WC_BOOL
};

/* Payload borrows storage from data; multi-byte values remain big endian. */
struct wcValue {
	enum wcType type;
	const u8 *data;
	size_t length;
};

int WC_Find(const void *data, size_t length, const char *name, struct wcValue *value);
/* Decode encrypted setting.txt and copy a NUL-terminated value. */
int WC_GetSetting(const void *data, size_t length, const char *name, char *out, size_t capacity);
int WC_GetVideoMode(const void *settings, size_t settingsLength,
	const void *sysconf, size_t sysconfLength, enum viMode *mode);
/* Read from the currently mounted Wii SFFS; caller owns the returned buffer. */
int WC_ReadFile(const char *path, size_t maxSize, void **data, size_t *length);
void WC_LoadFromSFFS(void);

#endif
