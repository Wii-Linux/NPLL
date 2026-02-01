/*
 * NPLL - Character output
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _OUTPUT_H
#define _OUTPUT_H

#include <npll/drivers.h>
#include <stdbool.h>

typedef void (*outputCallback_t)(void);

struct outputDevice {
	char *name;
	bool isGraphical;
	int rows;
	int columns;
	struct driver *driver;

	void (*writeChar)(const char c);
	void (*writeStr)(const char *str);
};

extern int O_NumDevices;
extern const struct outputDevice *O_Devices[];
extern void O_AddDevice(const struct outputDevice *dev);
extern void O_RemoveDevice(const struct outputDevice *dev);
extern void O_DebugInit(void);
extern void O_DebugCleanup(void);

#endif /* _OUTPUT_H */
