/*
 * NPLL - Flipper/Hollywood Hardware - Video Interface
 *
 * Copyright (C) 2025-2026 Techflash
*/

#ifndef _FLIPPER_VI_H
#define _FLIPPER_VI_H

#include <npll/soc.h>

enum viMode {
	VI_MODE_640X480_NTSC_INT,
	VI_MODE_640X480_NTSC_PROG,
	VI_MODE_640X576_PAL50_INT,
	VI_MODE_640X480_PAL60_INT,
	VI_MODE_640X480_PAL60_PROG,
	VI_MODE_MAX
};

enum viModeChoiceIdx {
	/*
	 * Best guess from existing hardware state and data available at viDrvInit
	 * time
	 */
	VI_MODE_CHOICE_EARLY,
	/*
	 * Desired mode derived from Wii SFFS
	 */
	VI_MODE_CHOICE_SYS,
	/*
	 * Desired mode from config
	 */
	VI_MODE_CHOICE_CONF,

	VI_MODE_CHOICE_MAX
};
int H_VISetModeTier(enum viModeChoiceIdx tier, enum viMode mode);

extern const char *H_VIMode;

void H_VIDisable(void);

#endif /* _FLIPPER_VI_H */
