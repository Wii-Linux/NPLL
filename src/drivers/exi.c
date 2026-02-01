/*
 * NPLL - Flipper/Hollywood/Latte Hardware - EXI
 *
 * Copyright (C) 2025 Techflash
 *
 * Derived in part from the Linux spi-exi driver:
 * Copyright (C) 2025 Techflash
 */

#define MODULE "EXI"

#include <npll/types.h>
#include <npll/log.h>
#include <npll/regs.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/drivers/exi.h>

/* WARNING: if the USB Gecko driver is enabled this will cause recursion:
 * - usbgecko driver triggers EXI transactions
 *  - EXI transactions trigger logs
 *    - logs print to all output devices
 *      - output to USB Gecko triggers EXI transactions
 *        - goto step 1
 */
#if 0
#define dbg(...) log_printf(__VA_ARGS__)
#else
#define dbg(...) (void)0
#endif

/*
 * Hardware registers
 */
struct exi_channel_regs {
	vu32 csr;    /* Channel Status Register */
	vu32 mar;    /* DMA Memory Address Register */
	vu32 length; /* DMA Transfer Length */
	vu32 cr;     /* Control Register */
	vu32 data;   /* Immediate data */
};

struct exi_regs {
	volatile struct exi_channel_regs channels[3]; /* EXI has 3 channels, each with the above registers */
};

static volatile struct exi_regs *regs;

/*
 * Hardware register values, masks, and shifts
 */
#define EXI_CSR_CLK_SHIFT    4
#define EXI_CSR_CLK          (7 << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_64MHZ    (6 << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_32MHZ    (5 << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_16MHZ    (4 << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_8MHZ     (3 << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_4MHZ     (2 << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_2MHZ     (1 << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_1MHZ     (0 << EXI_CSR_CLK_SHIFT)
#define EXI_CSR_CS_SHIFT     7
#define EXI_CSR_CS           (7 << EXI_CSR_CS_SHIFT)
#define EXI_CSR_EXT          (1 << 12)

#define EXI_CR_TSTART        (1 << 0)
#define EXI_CR_DMA           (1 << 1)
#define EXI_CR_RW_SHIFT      2
#define EXI_CR_RW            (3 << EXI_CR_RW_SHIFT)
#define   EXI_CR_RW_RD         (0 << EXI_CR_RW_SHIFT)
#define   EXI_CR_RW_WR         (1 << EXI_CR_RW_SHIFT)
#define   EXI_CR_RW_RDWR       (2 << EXI_CR_RW_SHIFT)
#define EXI_CR_TLEN_SHIFT    4
#define EXI_CR_TLEN          (3 << EXI_CR_TLEN_SHIFT)

/*
 * Convert MHz speed to EXI speed index
 */
static unsigned int exiSpeedFromMhz(unsigned int mhz)
{
	if (mhz > 32)
		return EXI_CSR_CLK_64MHZ;
	else if (mhz > 16)
		return EXI_CSR_CLK_32MHZ;
	else if (mhz > 8)
		return EXI_CSR_CLK_16MHZ;
	else if (mhz > 4)
		return EXI_CSR_CLK_8MHZ;
	else if (mhz > 2)
		return EXI_CSR_CLK_4MHZ;
	else if (mhz > 1)
		return EXI_CSR_CLK_2MHZ;

	return EXI_CSR_CLK_1MHZ;
}

/*
 * Selects the desired device (CS line) on the given
 * EXI channel, and sets the desired clock speed.
 */
void H_EXISelect(unsigned int channel, unsigned int cs, unsigned int clkMhz) {
	u32 csr;

	dbg("Channel %d, selecting CS %d at clock %dMHz\r\n", channel, cs, clkMhz);

	csr = 0;
	csr |= (1 << (EXI_CSR_CS_SHIFT + cs)); /* set the appropriate CS bit */
	csr |= exiSpeedFromMhz(clkMhz);         /* set the appropriate CLK bits */
	dbg("Writing CSR=0x%08x\r\n", csr);
	regs->channels[channel].csr = csr;     /* write CSR back */
}

/*
 * Deselects any selected device (CS line) on the given
 * EXI channel.
 */
void H_EXIDeselect(unsigned int channel) {
	dbg("Writing CSR=0x00000000\r\n");
	regs->channels[channel].csr = 0;
}

/*
 * Immediate transaction to channel.
 * Both the read and write will be of the same size if using both.
 * Assumes desired device is already selected.
 */
int H_EXIXferImm(unsigned int channel,
		 unsigned int len,
		 unsigned int mode,
		 const void *in, void *out)
{
	u32 cr, data;

	if (len > 4 ||
	    !len    ||
	    channel > 2) {
		log_printf("H_EXIXferImm with invalid params: chan=%d, len=%d, mode=%d\r\n", channel, len, mode);
		return -1;
	}

	/*
	 * Decide whether to poll or to use interrupts.
	 * If clock < 32MHz, it's worth it to use interrupts,
	 * since we'll be polling for a while.  However if clock
	 * is 32MHz, the cost of the interrupt is more than the
	 * cost of just spinning until the transfer is done.
	 *
	 * TODO: Do this
	 */

	/* read CR */
	cr = regs->channels[channel].cr;

	/*
	 * transfer should not already be in progress or else
	 * we've done something very wrong
	 */
	if (cr & EXI_CR_TSTART) {
		log_puts("H_EXIXferImm while transfer in progress?!");
		while (regs->channels[channel].cr & EXI_CR_TSTART);
	}


	dbg("Channel %d, doing xfer with len=%d mode=%c%c\r\n",
			channel, len, (mode & EXI_MODE_READ) ? 'R' : '-',
			(mode & EXI_MODE_WRITE) ? 'W' : '-');

	/* get the desired data */
	if (mode & EXI_MODE_WRITE) {
		switch (len) {
		case 1:
			data = *(u8 *)in << 24;
			break;
		case 2:
			data = *(u16 *)in << 16;
			break;
		case 3:
			data = (*(u32 *)in & 0x00ffffff) << 8;
			break;
		case 4:
			data = *(u32 *)in;
			break;
		default:
			return -1;
		}
		dbg("Outgoing data from buffer, data=0x%08x\r\n", data);
	}
	else {
		data = 0;
		dbg("Outgoing data static, data=0x%08x\r\n", data);
	}

	/* give the EXI hardware our data */
	regs->channels[channel].data = data;

	cr = 0;

	/* mode */
	if (mode == (EXI_MODE_READ | EXI_MODE_WRITE))
		cr |= EXI_CR_RW_RDWR;
	else if (mode == EXI_MODE_READ)
		cr |= EXI_CR_RW_RD;
	else if (mode == EXI_MODE_WRITE)
		cr |= EXI_CR_RW_WR;
	else
		return -1;

	cr |= ((len - 1) << EXI_CR_TLEN_SHIFT); /* length */
	cr |= EXI_CR_TSTART;              /* start the transfer */
	dbg("Writing CR=0x%08x\r\n", cr);
	regs->channels[channel].cr = cr; /* do it */

	/* spin until transfer done */
	while (regs->channels[channel].cr & EXI_CR_TSTART);

	/* transfer done, read our data, if any */
	if (mode & EXI_MODE_READ) {
		data = regs->channels[channel].data;
		dbg("Incoming data=0x%08x\r\n", data);

		/* write it back */
		switch (len) {
		case 1:
			*(u8 *)out = (data & 0xff000000) >> 24;
			break;
		case 2:
			*(u16 *)out = (data & 0xffff0000) >> 16;
			break;
		case 3:
			*(u32 *)out = (data & 0xffffff00) >> 8;
			break;
		case 4:
			*(u32 *)out = data;
			break;
		default:
			return -1;
		}
	}
	return 0;
}

static void exiCallback(void) {

}

static void exiInit(void) {
	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: {
		regs = (struct exi_regs*)0xcc006800;
		break;
	}
	case CONSOLE_TYPE_WII:
	case CONSOLE_TYPE_WII_U: {
		regs = (struct exi_regs *)0xcd806800;
		HW_AIPROT |= (1 << 0);
		break;
	}
	default:
		break;
	}

	/* register our callback */
	D_AddCallback(exiCallback);

	/* we're all good */
	exiDrv.state = DRIVER_STATE_READY;
}

static void exiCleanup(void) {
	D_RemoveCallback(exiCallback);
	exiDrv.state = DRIVER_STATE_NOT_READY;
}

REGISTER_DRIVER(exiDrv) = {
	.name = "EXI",
	.mask = DRIVER_ALLOW_ALL,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_CRITICAL,
	.init = exiInit,
	.cleanup = exiCleanup
};
