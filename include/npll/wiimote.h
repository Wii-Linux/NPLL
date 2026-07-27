/*
 * NPLL - Wii Remote pairing cache
 * Copyright (C) 2026 Techflash
 */

#ifndef _NPLL_WIIMOTE_H
#define _NPLL_WIIMOTE_H

#include <npll/types.h>

#define WIIMOTE_MAX_PAIRINGS 10u
#define WIIMOTE_NAME_LENGTH  64u

struct wiimotePairing {
	u8 bdaddr[6];
	char name[WIIMOTE_NAME_LENGTH + 1u];
};

extern void WM_ClearPairings(void);
extern int WM_ParsePairings(const void *sysconf, size_t length);
extern int WM_LoadPairingsFromSFFS(void);
extern const struct wiimotePairing *WM_GetPairings(uint *count);

#endif /* _NPLL_WIIMOTE_H */
