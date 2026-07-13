/*
 * NPLL - Hollywood/Latte Hardware - SHA-1 Engine
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "SHA1"

#include <assert.h>
#include <errno.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/utils.h>
#include <npll/hollywood/sha1.h>

static REGISTER_DRIVER(sha1Drv);
struct sha1Regs {
	vu32 ctrl;
	vu32 src;
	vu32 h[5];
};

static volatile struct sha1Regs *const regs = (volatile struct sha1Regs *)(AHB_BASE + 0x030000);
static const u32 iv[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };

#define SHA_CTRL_EXEC BIT(31)

static int sha1Reset(void) {
	bool irqs;
	u64 tb = mftb();

	irqs = IRQ_DisableSave();

	while (regs->ctrl & SHA_CTRL_EXEC) {
		if (T_HasElapsed(tb, 100 * 1000)) {
			log_printf("timeout on SHA-1 eng to be free, ctrl=0x%08x\r\n", regs->ctrl);
			IRQ_Restore(irqs);
			return -ETIMEDOUT;
		}
	}

	/* it's ready, let's reset it */
	regs->ctrl = 0;
	while (regs->ctrl & SHA_CTRL_EXEC) {
		if (T_HasElapsed(tb, 100 * 1000)) {
			log_printf("timeout on SHA-1 eng to reset, ctrl=0x%08x\r\n", regs->ctrl);
			IRQ_Restore(irqs);
			return -ETIMEDOUT;
		}
	}

	IRQ_Restore(irqs);
	return 0;
}

int H_SHA1Process(const void *in, u32 *out, size_t size) {
	int ret;
	u64 tb;
	u32 ctrl;
	bool irqs;

	assert(H_ConsoleType != CONSOLE_TYPE_GAMECUBE);

	ctrl = SHA_CTRL_EXEC | ((u32)(size / 64) - 1);
	irqs = IRQ_DisableSave();

	if (size < 64 || size & 63) {
		log_printf("H_SHA1Process: invalid size: %u\r\n", size);
		IRQ_Restore(irqs);
		return -EINVAL;
	}
	if ((uintptr_t)in & 63 || !in) {
		log_printf("H_SHA1Process: invalid source: %08x\r\n", in);
		IRQ_Restore(irqs);
		return -EINVAL;
	}
	if ((uintptr_t)out & 3 || !out) {
		log_printf("H_SHA1Process: invalid dest: %08x\r\n", out);
		IRQ_Restore(irqs);
		return -EINVAL;
	}

	ret = sha1Reset();
	if (ret) {
		log_printf("H_SHA1Process: sha1Reset failed: %d\r\n", ret);
		IRQ_Restore(irqs);
		return ret;
	}

	barrier(); regs->h[0] = iv[0];
	barrier(); regs->h[1] = iv[1];
	barrier(); regs->h[2] = iv[2];
	barrier(); regs->h[3] = iv[3];
	barrier(); regs->h[4] = iv[4]; barrier();

	dcache_flush(in, (u32)size);

	regs->src = (uintptr_t)virtToPhys(in); barrier();
	regs->ctrl = ctrl;
	tb = mftb();
	while (regs->ctrl & SHA_CTRL_EXEC) {
		if (T_HasElapsed(tb, 100 * 1000)) {
			log_printf("H_SHA1Process: timeout on SHA-1 eng to finish, ctrl=0x%08x\r\n", regs->ctrl);
			IRQ_Restore(irqs);
			return -ETIMEDOUT;
		}
	}

	barrier(); out[0] = regs->h[0];
	barrier(); out[1] = regs->h[1];
	barrier(); out[2] = regs->h[2];
	barrier(); out[3] = regs->h[3];
	barrier(); out[4] = regs->h[4]; barrier();

	IRQ_Restore(irqs);
	return 0;
}

static void sha1Init(void) {
	int ret = sha1Reset();
	if (ret) {
		log_printf("sha1Reset failed: %d\r\n", ret);
		sha1Drv.state = DRIVER_STATE_FAULTED;
		return;
	}
	sha1Drv.state = DRIVER_STATE_READY;
}

static void sha1Cleanup(void) {
	sha1Reset();
	sha1Drv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(sha1Drv) = {
	.name = "Hollywood/Latte SHA-1 Engine",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_OTHER,
	.init = sha1Init,
	.cleanup = sha1Cleanup
};
