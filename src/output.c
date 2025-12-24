/*
 * NPLL - Character output
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <npll/panic.h>
#include <npll/output.h>

#define MAX_DEV 16

int O_NumDevices = 0;
const struct outputDevice *O_Devices[MAX_DEV];
static int deviceNum = 0; /* 0 = memlog, 1 = debug, >1 = real device */

void O_AddDevice(const struct outputDevice *dev) {
	char *name = "NULL", *driver = "NULL";
	if (O_NumDevices >= MAX_DEV)
		panic("Trying to add too many output devices");

	if (dev->name)
		name = dev->name;
	if (dev->driver && dev->driver->name)
		driver = (char *)dev->driver->name;

	printf("OUT: Adding new device (cur=%d total=%d): %s [driver: %s]\r\n", O_NumDevices, deviceNum, name, driver);
	O_Devices[O_NumDevices] = dev;
	O_NumDevices++;

	if (deviceNum == 0) /* added memlog */
		deviceNum++;
	else if (deviceNum == 1) /* added debug */
		deviceNum++;
	else if (deviceNum == 2) { /* added real device */
		deviceNum++;
		puts("OUT: Cleaning out debug devices");
		O_DebugCleanup();
		O_MemlogCleanup();
	}
}

void O_RemoveDevice(const struct outputDevice *dev) {
	int i, size;
	bool found = false;

	for (i = 0; i < MAX_DEV; i++) {
		if (O_Devices[i] == dev) {
			found = true;
			break;
		}
	}
	if (!found)
		return;

	/* shift the array back */
	size = (MAX_DEV - i - 1) * sizeof(struct outputDevice *);
	memmove(&O_Devices[i], &O_Devices[i + 1], size);
	O_Devices[MAX_DEV - 1] = NULL;

	O_NumDevices--;
}
