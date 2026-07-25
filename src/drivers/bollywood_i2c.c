/*
 * NPLL - Bollywood/Latte hardware I2C engine
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "BW-I2C"

#include <errno.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/soc.h>
#include <npll/i2c.h>
#include <npll/log.h>
#include <npll/timer.h>

static REGISTER_DRIVER(i2cEngDrv);

struct i2cMasterRegs {
	vu32 ctrl;
	vu32 wrdata;
	vu32 wren;
	vu32 rddata;
};

struct i2cEng {
	struct i2cMasterRegs *regs;
	vu32 *intStatus;
	u32 writeDone;
	u32 readDone;
};

#define I2C_CTRL_ENABLE       BIT(0)
#define I2C_CTRL_ACTIVE       BIT(1)
#define I2C_WREN_QUEUE        BIT(0)
#define I2C_WREN_READ_ENABLE  BIT(1)
#define I2C_DATA_LAST         BIT(8)
#define I2C_TIMEOUT_US        500000

static u32 i2cMCTRLnLT(uint khz) {
	u32 lowPeriod = (248000 / khz / 2) - 1;
	u32 refPeriod = (1000000 / khz / 40) / 4;

	return (lowPeriod << 16) | (refPeriod << 8) | I2C_CTRL_ENABLE;
}

/* tve.rpl programs these fields for 400 kHz operation on Latte. */
#define i2cMCTRL0LT() i2cMCTRLnLT(400)

/*
 * Engine 2 (going to the SMC) is programmed for 5KHz by IOSU,
 * and, strangely, 10KHz by Cafe2Wii.  Use 5KHz here.
 */
#define i2cMCTRL2LT() i2cMCTRLnLT(5)

static void queueByte(struct i2cMasterRegs *regs, u8 byte, bool last) {
	regs->wrdata = byte | (last ? I2C_DATA_LAST : 0);
	sync();
	regs->wren |= I2C_WREN_QUEUE;
	sync();
}

static int waitForCompletion(struct i2cEng *eng, u32 status) {
	u64 start = mftb();

	while (!(*eng->intStatus & status)) {
		if (T_HasElapsed(start, I2C_TIMEOUT_US)) {
			log_printf("I2C timeout: regs=%08x want=%08x ctrl=%08x wrdata=%08x "
				"wren=%08x rddata=%08x intsts=%08x\r\n",
				(u32)eng->regs, status, eng->regs->ctrl,
				eng->regs->wrdata, eng->regs->wren,
				eng->regs->rddata, *eng->intStatus);
			return -ETIMEDOUT;
		}
	}

	/* the completion bits are W1C */
	*eng->intStatus = status;
	sync();
	return 0;
}

static int i2cEngTransfer(struct i2cController *controller, struct i2cMsg *msgs, uint numMsg) {
	uint i, j;
	struct i2cEng *eng = controller->priv;
	struct i2cMasterRegs *regs = eng->regs;
	int ret;

	for (i = 0; i < numMsg; i++) {
		if (msgs[i].flags & I2C_MSG_READ) {
			queueByte(regs, (u8)((msgs[i].addr << 1) | 1), msgs[i].len == 0);
			for (j = 0; j < msgs[i].len; j++)
				queueByte(regs, 0, j + 1 == msgs[i].len);
			ret = waitForCompletion(eng, eng->readDone);
			if (ret)
				return ret;
			for (j = 0; j < msgs[i].len; j++)
				msgs[i].buf[j] = (u8)regs->rddata;
		}
		else {
			queueByte(regs, (u8)(msgs[i].addr << 1), msgs[i].len == 0);
			for (j = 0; j < msgs[i].len; j++)
				queueByte(regs, msgs[i].buf[j], j + 1 == msgs[i].len);
			ret = waitForCompletion(eng, eng->writeDone);
			if (ret)
				return ret;
		}
	}

	return (int)numMsg;
}

static struct i2cEng i2cEng0 = {
	.regs = (void *)BOLLYWOOD_I2C_ENG0_BASE,
	.intStatus = (void *)(HOLLYWOOD_REGS_BASE + 0x6c),
	.writeDone = BIT(6),
	.readDone = BIT(5),
};

static struct i2cController i2cEngController0 = {
	.name = "Bollywood/Latte I2C Engine 0",
	.bus = I2C_BUS_AVE,
	.priority = 1,
	.transfer = i2cEngTransfer,
	.priv = &i2cEng0
};

#if 0
static struct i2cController i2cEngController1 = {
	.name = "Bollywood/Latte I2C Engine 1",
	.bus = I2C_BUS_AVE2,
	.priority = 1,
	.transfer = i2cEngTransfer,
	.priv = (void *)LATTE_I2C_ENG1_BASE
};
#endif

static struct i2cEng i2cEng2 = {
	.regs = (void *)LATTE_I2C_ENG2_BASE,
	.intStatus = (void *)(LATTE_I2C_ENG2_BASE + 0x14),
	.writeDone = BIT(1),
	.readDone = BIT(0),
};

static struct i2cController i2cEngController2 = {
	.name = "Bollywood/Latte I2C Engine 2",
	.bus = I2C_BUS_SMC,
	.priority = 1,
	.transfer = i2cEngTransfer,
	.priv = &i2cEng2
};

static void initEngine(struct i2cEng *eng, bool configureTiming) {
	eng->regs->wren &= ~I2C_WREN_READ_ENABLE;
	*eng->intStatus |= eng->writeDone | eng->readDone;
	if (configureTiming && eng == &i2cEng0)
		eng->regs->ctrl = (eng->regs->ctrl & 0xff) | i2cMCTRL0LT();
	else if (configureTiming && eng == &i2cEng2)
		eng->regs->ctrl = (eng->regs->ctrl & 0xff) | i2cMCTRL2LT();
	else
		eng->regs->ctrl |= I2C_CTRL_ENABLE;
	sync();
}

static void i2cEngInit(void) {
	/*
	 * Bollywood exposes this register block and reports transactions as
	 * complete, but its I2C pins do not appear to be wired to AVE-RVL.
	 * All Wii models therefore use the GPIO controller.
	 */
	if (H_ConsoleType != CONSOLE_TYPE_WII_U) {
		i2cEngDrv.state = DRIVER_STATE_NO_HARDWARE;
		return;
	}

	initEngine(&i2cEng0, true);
	if (I2C_RegisterController(&i2cEngController0)) {
		i2cEngDrv.state = DRIVER_STATE_FAULTED;
		return;
	}
	if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		/*
		 * The SMC runs at 5 kHz under IOS (10 kHz under Cafe2Wii).
		 * Its timing formula is not known to match the AV engine, so retain
		 * the timing established by the firmware and only enable the engine.
		 */
		initEngine(&i2cEng2, true);
		if (I2C_RegisterController(&i2cEngController2)) {
			I2C_UnregisterController(&i2cEngController0);
			i2cEng0.regs->ctrl &= ~I2C_CTRL_ENABLE;
			i2cEngDrv.state = DRIVER_STATE_FAULTED;
			return;
		}
	}
	i2cEngDrv.state = DRIVER_STATE_READY;
}

static void i2cEngCleanup(void) {
	I2C_UnregisterController(&i2cEngController0);
	i2cEng0.regs->ctrl &= ~(I2C_CTRL_ENABLE | I2C_CTRL_ACTIVE);
	if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		I2C_UnregisterController(&i2cEngController2);
		i2cEng2.regs->ctrl &= ~(I2C_CTRL_ENABLE | I2C_CTRL_ACTIVE);
	}
	sync();

	i2cEngDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(i2cEngDrv) = {
	.name = "Bollywood/Latte I2C engine",
	.mask = DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_CRITICAL,
	.init = i2cEngInit,
	.cleanup = i2cEngCleanup,
};
