/*
 * NPLL - Hollywood/Latte Hardware - AES Engine
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "AES"

#include <assert.h>
#include <errno.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/utils.h>

static REGISTER_DRIVER(aesDrv);
struct aesRegs {
	vu32 ctrl;
	vu32 src;
	vu32 dest;
	vu32 key;
	vu32 iv;
};

static volatile struct aesRegs *const regs = (volatile struct aesRegs *)0xcd020000;

#define AES_CTRL_IV   BIT(12)
#define AES_CTRL_DEC  BIT(27)
#define AES_CTRL_ENA  BIT(28)
#define AES_CTRL_ERR  BIT(29)
#define AES_CTRL_IRQ  BIT(30)
#define AES_CTRL_EXEC BIT(31)

static int aesReset(void) {
	bool irqs;
	u64 tb = mftb();

	irqs = IRQ_DisableSave();

	while (regs->ctrl & AES_CTRL_EXEC) {
		if (T_HasElapsed(tb, 100 * 1000)) {
			log_printf("timeout on AES eng to be free, ctrl=0x%08x\r\n", regs->ctrl);
			IRQ_Restore(irqs);
			return -ETIMEDOUT;
		}
	}

	/* it's ready, let's reset it */
	regs->ctrl = 0;
	while (regs->ctrl & AES_CTRL_EXEC) {
		if (T_HasElapsed(tb, 100 * 1000)) {
			log_printf("timeout on AES eng to reset, ctrl=0x%08x\r\n", regs->ctrl);
			IRQ_Restore(irqs);
			return -ETIMEDOUT;
		}
	}

	IRQ_Restore(irqs);
	return 0;
}

static int aesOp(const char *func, const void *in, void *out, u32 *iv, u32 *key, size_t size, u32 aesCtrlFlags) {
	int ret;
	u64 tb;
	u32 ctrl;
	bool irqs;

	assert(H_ConsoleType != CONSOLE_TYPE_GAMECUBE);

	ctrl = AES_CTRL_EXEC | aesCtrlFlags | ((u32)(size / 16) - 1);
	irqs = IRQ_DisableSave();

	if (size < 16 || size & 15) {
		log_printf("%s: invalid size: %u\r\n", func, size);
		IRQ_Restore(irqs);
		return -EINVAL;
	}
	if ((u32)in & 15 || !in) {
		log_printf("%s: invalid source: %08x\r\n", func, in);
		IRQ_Restore(irqs);
		return -EINVAL;
	}
	if ((u32)out & 15 || !out) {
		log_printf("%s: invalid dest: %08x\r\n", func, out);
		IRQ_Restore(irqs);
		return -EINVAL;
	}

	ret = aesReset();
	if (ret) {
		log_printf("%s: aesReset failed: %d\r\n", func, ret);
		IRQ_Restore(irqs);
		return ret;
	}

	if (iv) {
		barrier(); regs->iv = iv[0];
		barrier(); regs->iv = iv[1];
		barrier(); regs->iv = iv[2];
		barrier(); regs->iv = iv[3]; barrier();
	}
	else
		ctrl |= AES_CTRL_IV;

	if (key) {
		barrier(); regs->key = key[0];
		barrier(); regs->key = key[1];
		barrier(); regs->key = key[2];
		barrier(); regs->key = key[3]; barrier();
	}

	dcache_flush(in, (u32)size);
	dcache_invalidate(out, (u32)size);

	regs->src = (u32)virtToPhys(in);
	regs->dest = (u32)virtToPhys(out); barrier();
	regs->ctrl = ctrl;
	tb = mftb();
	while (regs->ctrl & AES_CTRL_EXEC) {
		if (T_HasElapsed(tb, 100 * 1000)) {
			log_printf("%s: timeout on AES eng to finish, ctrl=0x%08x\r\n", func, regs->ctrl);
			IRQ_Restore(irqs);
			return -ETIMEDOUT;
		}
	}

	IRQ_Restore(irqs);
	dcache_invalidate(out, (u32)size);
	return 0;
}

int H_AESEncrypt(const void *in, void *out, u32 *iv, u32 *key, size_t size) {
	return aesOp("H_AESEncrypt", in, out, iv, key, size, AES_CTRL_ENA);
}
int H_AESDecrypt(const void *in, void *out, u32 *iv, u32 *key, size_t size) {
	return aesOp("H_AESDecrypt", in, out, iv, key, size, AES_CTRL_ENA | AES_CTRL_DEC);
}
int H_AESCopy(const void *in, void *out, size_t size) {
	return aesOp("H_AESCopy", in, out, NULL, NULL, size, 0);
}

static void aesInit(void) {
	int ret = aesReset();
	if (ret) {
		log_printf("aesReset failed: %d\r\n", ret);
		aesDrv.state = DRIVER_STATE_FAULTED;
		return;
	}
	aesDrv.state = DRIVER_STATE_READY;
}

static void aesCleanup(void) {
	aesReset();
	aesDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(aesDrv) = {
	.name = "Hollywood/Latte AES Engine",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_OTHER,
	.init = aesInit,
	.cleanup = aesCleanup
};
