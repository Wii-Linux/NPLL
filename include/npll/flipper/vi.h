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

extern const char *H_VIMode;

void H_VIDisable(void);

#endif /* _FLIPPER_VI_H */
