/*
 * NPLL - I2C controller abstraction
 *
 * Copyright (C) 2026 Techflash
 */

#include <errno.h>
#include <npll/i2c.h>

static  struct i2cController *controllers;

int I2C_RegisterController( struct i2cController *controller) {
	struct i2cController *cur;

	if (!controller || !controller->transfer)
		return -EINVAL;

	for (cur = controllers; cur; cur = cur->next) {
		if (cur == controller)
			return -EBUSY;
	}

	controller->next = controllers;
	controllers = controller;
	return 0;
}

void I2C_UnregisterController( struct i2cController *controller) {
	 struct i2cController **cur;

	for (cur = &controllers; *cur; cur = &(*cur)->next) {
		if (*cur == controller) {
			*cur = controller->next;
			controller->next = NULL;
			return;
		}
	}
}

int I2C_Transfer(uint bus,  struct i2cMsg *msgs, uint numMsg) {
	struct i2cController *controller, *best = NULL;
	uint i;

	if (!msgs || !numMsg)
		return -EINVAL;

	for (i = 0; i < numMsg; i++) {
		if (msgs[i].addr > 0x7f || (msgs[i].flags & ~I2C_MSG_READ) || (!msgs[i].buf && msgs[i].len))
			return -EINVAL;
	}

	for (controller = controllers; controller; controller = controller->next) {
		if (controller->bus == bus && (!best || controller->priority > best->priority))
			best = controller;
	}

	if (!best)
		return -ENODEV;

	return best->transfer(best, msgs, numMsg);
}

int I2C_Write(uint bus, u16 addr, const void *buf, uint len) {
	int ret;
	struct i2cMsg msg = {
		.addr = addr,
		.flags = 0,
		.buf = (u8 *)buf,
		.len = len,
	};
	ret = I2C_Transfer(bus, &msg, 1);

	return ret < 0 ? ret : 0;
}

int I2C_Read(uint bus, u16 addr, void *buf, uint len) {
	int ret;
	struct i2cMsg msg = {
		.addr = addr,
		.flags = I2C_MSG_READ,
		.buf = buf,
		.len = len,
	};
	ret = I2C_Transfer(bus, &msg, 1);

	return ret < 0 ? ret : 0;
}
