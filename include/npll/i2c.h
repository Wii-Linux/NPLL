/*
 * NPLL - I2C controller abstraction
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _I2C_H
#define _I2C_H

#include <npll/types.h>

#define I2C_BUS_DEFAULT 0

#define I2C_MSG_READ (1u << 0)

struct i2cMsg {
	u16 addr;       /* 7-bit slave address */
	u16 flags;
	u8 *buf;
	uint len;
};

struct i2cController {
	const char *name;
	uint bus;
	int priority;
	int (*transfer)(struct i2cController *controller, struct i2cMsg *msgs, uint numMsg);
	void *priv;
	struct i2cController *next;
};

/*
 * Controllers sharing a bus may be registered.  The highest-priority one is
 * selected, allowing a hardware controller to supersede a fallback driver.
 */
extern int I2C_RegisterController(struct i2cController *controller);
extern void I2C_UnregisterController(struct i2cController *controller);

/* Returns the number of messages transferred, or a negative errno. */
extern int I2C_Transfer(uint bus, struct i2cMsg *msgs, uint num_msgs);

extern int I2C_Write(uint bus, u16 addr, const void *buf, uint len);
extern int I2C_Read(uint bus, u16 addr, void *buf, uint len);

#endif /* _I2C_H */
