/*
 * NPLL - Hollywood GPIO bit-banged I2C controller
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "GPIO-I2C"

#include <errno.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/hollywood/gpio.h>
#include <npll/i2c.h>
#include <npll/timer.h>

static REGISTER_DRIVER(gpioI2CDrv);

static inline void setSCL(bool high) {
	u32 out = HW_GPIOB_OUT & ~GPIO_AVE_SCL;
	HW_GPIOB_DIR |= GPIO_AVE_SCL;
	if (high)
		out |= GPIO_AVE_SCL;
	HW_GPIOB_OUT = out;
}

/* SDA is open drain: drive low or release it for the external pull-up */
static inline void setSDA(bool high) {
	if (high)
		HW_GPIOB_DIR &= ~GPIO_AVE_SDA;
	else {
		HW_GPIOB_OUT &= ~GPIO_AVE_SDA;
		HW_GPIOB_DIR |= GPIO_AVE_SDA;
	}
}

static inline bool getSDA(void) {
	return !!(HW_GPIOB_IN & GPIO_AVE_SDA);
}

static void i2cStart(void) {
	setSDA(true);
	setSCL(true);
	udelay(2);
	setSDA(false);
	udelay(2);
	setSCL(false);
}

static void i2cStop(void) {
	setSDA(false);
	udelay(2);
	setSCL(true);
	udelay(2);
	setSDA(true);
	udelay(2);
}

static bool writeByte(u8 value) {
	uint bit;

	for (bit = 0; bit < 8; bit++) {
		setSDA(!!(value & 0x80));
		udelay(2);
		setSCL(true);
		udelay(2);
		setSCL(false);
		value <<= 1;
	}

	setSDA(true);
	udelay(2);
	setSCL(true);
	udelay(2);
	if (getSDA()) {
		setSCL(false);
		return false;
	}
	setSCL(false);
	return true;
}

static u8 readByte(bool ack) {
	u8 value = 0;
	uint bit;

	setSDA(true);
	for (bit = 0; bit < 8; bit++) {
		value <<= 1;
		setSCL(true);
		udelay(2);
		if (getSDA())
			value |= 1;
		setSCL(false);
		udelay(2);
	}

	setSDA(!ack);
	udelay(2);
	setSCL(true);
	udelay(2);
	setSCL(false);
	setSDA(true);
	return value;
}

static int gpioI2CTransfer(struct i2cController *controller, struct i2cMsg *msgs, uint numMsg) {
	uint i, j;
	(void)controller;

	for (i = 0; i < numMsg; i++) {
		i2cStart();
		if (!writeByte((u8)((msgs[i].addr << 1) | !!(msgs[i].flags & I2C_MSG_READ))))
			goto no_ack;

		if (msgs[i].flags & I2C_MSG_READ) {
			for (j = 0; j < msgs[i].len; j++)
				msgs[i].buf[j] = readByte(j + 1 < msgs[i].len);
		}
		else {
			for (j = 0; j < msgs[i].len; j++) {
				if (!writeByte(msgs[i].buf[j]))
					goto no_ack;
			}
		}
	}

	i2cStop();
	return (int)numMsg;

no_ack:
	i2cStop();
	return -ENXIO;
}

static struct i2cController gpioI2CController = {
	.name = "Hollywood I2C over GPIO",
	.bus = I2C_BUS_DEFAULT,
	.priority = 0,
	.transfer = gpioI2CTransfer,
};

static void gpioI2CInit(void) {
	/* TODO: enable once I write that driver */
	#if 0
	/* Bollywood and Latte should use the hardware I2C engine instead */
	if (H_ConsoleType == CONSOLE_TYPE_WII_U || H_WiiRev == HW_VERSION_PROD_BOLLYWOOD) {
		gpioI2CDrv.state = DRIVER_STATE_NO_HARDWARE;
		return;
	}
	#endif

	setSDA(true);
	setSCL(true);
	if (I2C_RegisterController(&gpioI2CController)) {
		gpioI2CDrv.state = DRIVER_STATE_FAULTED;
		return;
	}
	gpioI2CDrv.state = DRIVER_STATE_READY;
}

static void gpioI2CCleanup(void) {
	I2C_UnregisterController(&gpioI2CController);
	setSDA(true);
	setSCL(true);
	gpioI2CDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(gpioI2CDrv) = {
	.name = "Hollywood I2C over GPIO",
	.mask = DRIVER_ALLOW_WII,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_CRITICAL,
	.init = gpioI2CInit,
	.cleanup = gpioI2CCleanup,
};
