/*
 * NPLL - Character output
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "OUT"

#include <assert.h>
#include <npll/log.h>
#include <string.h>
#include <stdbool.h>
#include <npll/irq.h>
#include <npll/output.h>
#include <npll/panic.h>

#define MAX_DEV 16

int O_NumDevices = 0;
const struct outputDevice *O_Devices[MAX_DEV];
static int deviceNum = 0; /* 0 = debug, >=1 = real device */

void O_AddDevice(const struct outputDevice *dev) {
	bool irqs;
	char *name = "NULL", *driver = "NULL";

	assert_msg(O_NumDevices < MAX_DEV, "Trying to add too many output devices");

	if (dev->name)
		name = dev->name;
	if (dev->driver && dev->driver->name)
		driver = (char *)dev->driver->name;

	log_printf("Adding new device (cur=%d total=%d): %s [driver: %s]\r\n", O_NumDevices, deviceNum, name, driver);

	irqs = IRQ_DisableSave();
	O_Devices[O_NumDevices++] = dev;
	IRQ_Restore(irqs);

	if (deviceNum == 0) /* added debug */
		deviceNum++;
	else if (deviceNum == 1) { /* added real device */
		deviceNum++;
		log_puts("Cleaning out debug devices");
		O_DebugCleanup();
	}
}

void O_RemoveDevice(const struct outputDevice *dev) {
	int i, size;
	bool irqs, found = false;

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

	irqs = IRQ_DisableSave();
	memmove(&O_Devices[i], &O_Devices[i + 1], size);
	O_Devices[MAX_DEV - 1] = NULL;
	O_NumDevices--;
	IRQ_Restore(irqs);
}
