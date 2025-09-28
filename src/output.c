/*
 * NPLL - Character output
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <npll/panic.h>
#include <npll/output.h>

#define MAX_DEV 16

int O_NumDevices = 0;
const struct outputDevice *O_Devices[MAX_DEV];

void O_AddDevice(const struct outputDevice *dev) {
	char *name = "NULL", *driver = "NULL";
	if (O_NumDevices >= MAX_DEV)
		panic("Trying to add too many output devices");

	if (dev->name)
		name = dev->name;
	if (dev->driver && dev->driver->name)
		driver = (char *)dev->driver->name;

	printf("OUT: Adding new device: %s [driver: %s]\r\n", name, driver);
	O_Devices[O_NumDevices] = dev;
	O_NumDevices++;
}
