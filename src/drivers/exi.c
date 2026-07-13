/*
 * NPLL - Flipper/Hollywood/Latte Hardware - EXI
 *
 * Copyright (C) 2025-2026 Techflash
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
#include <npll/irq.h>
#include <npll/timer.h>
#include <string.h>

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

#define EXI_SD_CMD0_RETRIES 16
#define EXI_SD_IDLE_BYTES 10
#define EXI_SD_INIT_CRC 0x95
#define EXI_SD_R1_IDLE BIT(0)
#define EXI_SD_R1_BUSY BIT(7)

struct exi_id_entry {
	u32 id;
	const char *name;
	struct exi_device_driver *driver;
	uint speed_mhz;
};

extern struct exi_device_driver usbgeckoEXIDriver, sdgeckoEXIDriver;
static struct exi_device_driver *exiRegisteredDrivers[3];

static const struct exi_id_entry exiIDTable[] = {
	{ 0xffff1698, "GameCube Mask ROM/RTC/SRAM/UART", NULL, 8 },
	{ 0xffff2843, "GameCube Mask ROM/RTC/SRAM/UART", NULL, 8 },
	{ 0xfffff308, "Wii Mask ROM/RTC/SRAM/UART", NULL, 8 },
	{ 0x00000004, "Memory Card 59", NULL, 8 },
	{ 0x00000008, "Memory Card 123", NULL, 8 },
	{ 0x00000010, "Memory Card 251", NULL, 8 },
	{ 0x00000020, "Memory Card 507", NULL, 8 },
	{ 0x00000040, "Memory Card 1019", NULL, 8 },
	{ 0x00000080, "Memory Card 2043", NULL, 8 },
	{ 0x04020200, "BroadBand Adapter (DOL-015)", NULL, 32 },
	{ 0x0a000000, "Microphone (DOL-022)", NULL, 16 },
	{ 0, NULL, NULL, 0 },
};

static struct exi_device exiDevices[3][3];
static bool exiProbeFailed[3][3];

/*
 * Hardware register values, masks, and shifts
 */
#define EXI_CSR_EXIINTMASK   BIT(0)
#define EXI_CSR_EXIINT       BIT(1)
#define EXI_CSR_TCINTMASK    BIT(2)
#define EXI_CSR_TCINT        BIT(3)
#define EXI_CSR_CLK_SHIFT    4
#define EXI_CSR_CLK          (7u << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_64MHZ    (6u << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_32MHZ    (5u << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_16MHZ    (4u << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_8MHZ     (3u << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_4MHZ     (2u << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_2MHZ     (1u << EXI_CSR_CLK_SHIFT)
#define   EXI_CSR_CLK_1MHZ     (0u << EXI_CSR_CLK_SHIFT)
#define EXI_CSR_CS_SHIFT     7
#define EXI_CSR_CS           (7u << EXI_CSR_CS_SHIFT)
#define EXI_CSR_EXTINTMASK   BIT(10)
#define EXI_CSR_EXTINT       BIT(11)
#define EXI_CSR_EXT          BIT(12)
#define EXI_CSR_ROMDIS       BIT(13)

#define EXI_CR_TSTART        BIT(0)
#define EXI_CR_DMA           BIT(1)
#define EXI_CR_RW_SHIFT      2
#define EXI_CR_RW            (3u << EXI_CR_RW_SHIFT)
#define   EXI_CR_RW_RD         (0u << EXI_CR_RW_SHIFT)
#define   EXI_CR_RW_WR         (1u << EXI_CR_RW_SHIFT)
#define   EXI_CR_RW_RDWR       (2u << EXI_CR_RW_SHIFT)
#define EXI_CR_TLEN_SHIFT    4
#define EXI_CR_TLEN          (3u << EXI_CR_TLEN_SHIFT)

static bool exiHotplugSlot(uint channel, uint cs) {
	return cs == 0 && (channel == 0 || channel == 1);
}

static bool exiValidSlot(uint channel, uint cs) {
	if (channel >= 3 || cs >= 3)
		return false;

	if (channel == 0)
		return true;
	if (channel == 1)
		return cs == 0;
	if (channel == 2)
		return cs == 0 && H_ConsoleType == CONSOLE_TYPE_GAMECUBE;

	return false;
}

static bool exiSlotPresent(uint channel, uint cs) {
	if (channel == 0 && cs == 1)
		return true;
	if (!exiValidSlot(channel, cs))
		return false;
	if (!exiHotplugSlot(channel, cs))
		return true;

	return H_EXIExtPresent(channel);
}

static const char *exiSlotName(uint channel, uint cs) {
	if (channel == 0 && cs == 0)
		return "Slot-A";
	if (channel == 1 && cs == 0)
		return "Slot-B";
	if (channel == 0 && cs == 1)
		return "Internal";
	if (channel == 0 && cs == 2)
		return "SP1";
	if (channel == 2 && cs == 0)
		return "SP2";
	return "unknown";
}

static const struct exi_id_entry *exiIDToEntry(u32 id) {
	const struct exi_id_entry *ent = exiIDTable;

	while (ent->name) {
		if (ent->id == id)
			return ent;
		ent++;
	}

	return NULL;
}

static bool exiDriverRegistered(struct exi_device_driver *drv) {
	uint i;

	for (i = 0; i < sizeof(exiRegisteredDrivers) / sizeof(exiRegisteredDrivers[0]); i++) {
		if (exiRegisteredDrivers[i] == drv)
			return true;
	}

	return false;
}

/*
 * Convert MHz speed to EXI speed index
 */
static uint exiSpeedFromMhz(uint mhz) {
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
void H_EXISelect(uint channel, uint cs, uint clkMhz) {
	u32 csr;

	dbg("Channel %d, selecting CS %d at clock %dMHz\r\n", channel, cs, clkMhz);

	csr = regs->channels[channel].csr;
	csr &= (EXI_CSR_EXIINTMASK | EXI_CSR_TCINTMASK | EXI_CSR_EXTINTMASK);
	csr |= BIT((EXI_CSR_CS_SHIFT + cs)); /* set the appropriate CS bit */
	csr |= exiSpeedFromMhz(clkMhz);         /* set the appropriate CLK bits */
	dbg("Writing CSR=0x%08x\r\n", csr);
	regs->channels[channel].csr = csr;     /* write CSR back */
}

/*
 * Selects an SD card clock mode without asserting a CS line.
 */
void H_EXISelectSD(uint channel, uint clkMhz) {
	u32 csr;

	dbg("Channel %d, selecting SD clock %dMHz\r\n", channel, clkMhz);

	csr = regs->channels[channel].csr;
	csr &= (EXI_CSR_EXIINTMASK | EXI_CSR_TCINTMASK | EXI_CSR_EXTINTMASK);
	csr |= exiSpeedFromMhz(clkMhz);
	regs->channels[channel].csr = csr;
}

/*
 * Deselects any selected device (CS line) on the given
 * EXI channel.
 */
void H_EXIDeselect(uint channel) {
	u32 csr;

	csr = regs->channels[channel].csr;
	csr &= (EXI_CSR_EXIINTMASK | EXI_CSR_TCINTMASK | EXI_CSR_EXTINTMASK);
	dbg("Writing CSR=0x%08x\r\n", csr);
	regs->channels[channel].csr = csr;
}

/*
 * Returns whether a channel's EXT line is asserted.
 */
bool H_EXIExtPresent(uint channel) {
	if (channel > 2)
		return false;

	return !!(regs->channels[channel].csr & EXI_CSR_EXT);
}

/*
 * Clears a channel's latched external-device edge.
 */
void H_EXIClearExt(uint channel) {
	u32 csr;

	if (channel > 2)
		return;

	csr = regs->channels[channel].csr;
	csr &= (EXI_CSR_EXTINTMASK | EXI_CSR_TCINTMASK | EXI_CSR_EXTINTMASK);
	csr |= EXI_CSR_EXTINT;
	regs->channels[channel].csr = csr;
}

/*
 * Immediate transaction to channel.
 * Both the read and write will be of the same size if using both.
 * Assumes desired device is already selected.
 */
int H_EXIXferImm(uint channel, uint len, uint mode, const void *in, void *out) {
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
			data = (u32)*(u8 *)in << 24;
			break;
		case 2:
			data = (u32)*(u16 *)in << 16;
			break;
		case 3:
			data = ((u32)((u8 *)in)[0] << 24);
			data |= ((u32)((u8 *)in)[1] << 16);
			data |= ((u32)((u8 *)in)[2] << 8);
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
			*(u8 *)out = (u8)((data & 0xff000000) >> 24);
			break;
		case 2:
			*(u16 *)out = (u16)((data & 0xffff0000) >> 16);
			break;
		case 3:
			((u8 *)out)[0] = (u8)((data & 0xff000000) >> 24);
			((u8 *)out)[1] = (u8)((data & 0x00ff0000) >> 16);
			((u8 *)out)[2] = (u8)((data & 0x0000ff00) >> 8);
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

uint H_EXIReadID(uint channel, uint cs) {
	u16 cmd = 0x0000;
	u32 id = 0;
	bool irqs;

	if (channel >= 3 || cs >= 3)
		return 0;

	irqs = IRQ_DisableSave();
	H_EXISelect(channel, cs, 8);
	(void)H_EXIWriteImm(channel, 2, &cmd);
	(void)H_EXIReadImm(channel, 4, &id);
	H_EXIDeselect(channel);
	IRQ_Restore(irqs);

	return id;
}

static int exiSDIdleClocks(uint channel, uint clkMhz) {
	u8 tx = 0xff, rx;
	uint i;
	int ret = 0;

	H_EXIDeselect(channel);
	H_EXISelectSD(channel, clkMhz);
	for (i = 0; i < EXI_SD_IDLE_BYTES; i++) {
		ret = H_EXIRdWrImm(channel, 1, &tx, &rx);
		if (ret)
			break;
	}
	H_EXIDeselect(channel);

	return ret;
}

static int exiSDSendCMD0(uint channel, uint cs, uint clkMhz, u8 *r1) {
	u8 cmd[6] = {
		0x40, 0, 0, 0, 0, EXI_SD_INIT_CRC,
	};
	u8 tx = 0xff;
	uint i;
	int ret = 0;

	H_EXISelect(channel, cs, clkMhz);
	for (i = 0; i < sizeof(cmd); i++) {
		ret = H_EXIWriteImm(channel, 1, &cmd[i]);
		if (ret)
			goto out;
	}

	for (i = 0; i < EXI_SD_CMD0_RETRIES; i++) {
		ret = H_EXIRdWrImm(channel, 1, &tx, r1);
		if (ret)
			goto out;
		if (!(*r1 & EXI_SD_R1_BUSY))
			break;
	}

	if (i == EXI_SD_CMD0_RETRIES)
		ret = -1;

out:
	H_EXIDeselect(channel);
	return ret;
}

static bool exiProbeSD(uint channel, uint cs) {
	static const uint initClks[] = { 1, 2, 4 };
	u8 r1 = 0xff;
	uint i;
	bool irqs;

	irqs = IRQ_DisableSave();
	for (i = 0; i < sizeof(initClks) / sizeof(initClks[0]); i++) {
		if (exiSDIdleClocks(channel, initClks[i]))
			continue;
		if (exiSDSendCMD0(channel, cs, initClks[i], &r1))
			continue;
		if (r1 == EXI_SD_R1_IDLE)
			break;
	}
	IRQ_Restore(irqs);

	return r1 == EXI_SD_R1_IDLE;
}

static bool exiProbeNoID(uint channel, uint cs, struct exi_device_driver **driver, const char **name, uint *speedMhz) {
	u16 cmd = 0x9000;
	u16 resp = 0xffff;
	bool irqs;

	irqs = IRQ_DisableSave();
	H_EXISelect(channel, cs, 8);
	(void)H_EXIRdWrImm(channel, 2, &cmd, &resp);
	H_EXIDeselect(channel);
	IRQ_Restore(irqs);

	if (resp == 0x0470) {
		*driver = &usbgeckoEXIDriver;
		*name = "USB Gecko";
		*speedMhz = 32;
		return true;
	}

	if (exiProbeSD(channel, cs)) {
		*driver = &sdgeckoEXIDriver;
		*name = "SDGecko";
		*speedMhz = 16;
		return true;
	}

	return false;
}

static int exiBind(struct exi_device *dev) {
	struct exi_device_driver *drv;
	int ret;

	if (dev->drv_data)
		return 0;
	if (!dev->driver)
		return 0;

	drv = dev->driver;
	if (!drv || !exiDriverRegistered(drv)) {
		log_printf("[%u:%u] no EXI child driver for %s yet\r\n",
			   dev->channel, dev->cs, dev->name);
		return 0;
	}
	if (!drv->probe)
		return 0;

	ret = drv->probe(dev);
	if (ret) {
		log_printf("[%u:%u] EXI child %s probe failed: %d\r\n",
			   dev->channel, dev->cs, drv->name, ret);
		dev->driver = NULL;
		return ret;
	}

	return 0;
}

static int exiBindDriver(struct exi_device_driver *drv) {
	uint channel, cs;
	int ret = 0;

	for (channel = 0; channel < 3; channel++) {
		for (cs = 0; cs < 3; cs++) {
			struct exi_device *dev = &exiDevices[channel][cs];

			if (dev->driver == drv && !dev->drv_data) {
				ret = exiBind(dev);
				if (ret)
					return ret;
			}
		}
	}

	return ret;
}

static void exiRemove(uint channel, uint cs) {
	struct exi_device *dev;
	struct exi_device_driver *drv;

	if (channel >= 3 || cs >= 3)
		return;

	dev = &exiDevices[channel][cs];
	if (!dev->driver)
		return;

	drv = dev->driver;
	if (drv && drv->remove && dev->drv_data)
		drv->remove(dev);

	memset(dev, 0, sizeof(*dev));
	exiProbeFailed[channel][cs] = false;
}

static int exiProbe(uint channel, uint cs) {
	struct exi_device *dev;
	const struct exi_id_entry *ent;
	const char *name = "Unknown";
	struct exi_device_driver *driver = NULL;
	uint speedMhz = 8;
	u32 id;

	if (!exiValidSlot(channel, cs))
		return 0;
	if (exiDevices[channel][cs].driver)
		return 0;
	if (exiProbeFailed[channel][cs])
		return 0;
	if (!exiSlotPresent(channel, cs))
		return 0;
	if (exiHotplugSlot(channel, cs)) {
		udelay(250000);
		if (!exiSlotPresent(channel, cs))
			return 0;
	}

	id = H_EXIReadID(channel, cs);
	ent = exiIDToEntry(id);
	if (ent) {
		name = ent->name;
		driver = ent->driver;
		speedMhz = ent->speed_mhz;
		log_printf("[%u:%u] EXI ID 0x%08x: %s\r\n", channel, cs, id, name);
	} else {
		log_printf("[%u:%u] bogus EXI ID 0x%08x, probing no-ID devices\r\n", channel, cs, id);
	}

	if (channel == 0 && cs == 1) {
		name = "GameCube Mask ROM/RTC/SRAM/UART";
		driver = NULL;
		speedMhz = 8;
	}

	if (!driver && !exiProbeNoID(channel, cs, &driver, &name, &speedMhz)) {
		log_printf("[%u:%u] no supported EXI device detected\r\n", channel, cs);
		exiProbeFailed[channel][cs] = true;
		return 0;
	}

	if (!driver) {
		log_printf("[%u:%u] no NPLL driver exists for %s\r\n", channel, cs, name);
		exiProbeFailed[channel][cs] = true;
		return 0;
	}

	dev = &exiDevices[channel][cs];
	memset(dev, 0, sizeof(*dev));
	dev->channel = channel;
	dev->cs = cs;
	dev->name = name;
	dev->max_clk_mhz = speedMhz;
	dev->hotplug = exiHotplugSlot(channel, cs);
	dev->driver = driver;

	log_printf("[%u:%u] new EXI device: %s on %s\r\n",
		   channel, cs, name, exiSlotName(channel, cs));
	return exiBind(dev);
}

static void exiRescanSlot(uint channel, uint cs) {
	if (!exiValidSlot(channel, cs))
		return;

	if (!exiSlotPresent(channel, cs)) {
		H_EXIClearExt(channel);
		exiProbeFailed[channel][cs] = false;
		if (exiDevices[channel][cs].driver) {
			log_printf("[%u:%u] removed EXI device\r\n", channel, cs);
			exiRemove(channel, cs);
		}
		return;
	}

	H_EXIClearExt(channel);
	if (!exiDevices[channel][cs].driver)
		(void)exiProbe(channel, cs);
}

static void exiRescanChannel(uint channel) {
	uint cs;

	if (channel == 0) {
		for (cs = 0; cs < 3; cs++)
			exiRescanSlot(channel, cs);
	} else {
		exiRescanSlot(channel, 0);
	}
}

static void exiHotplug(void *dummy) {
	(void)dummy;

	exiRescanSlot(0, 0);
	exiRescanSlot(1, 0);
}

int H_EXIRegisterDriver(struct exi_device_driver *drv) {
	uint i;

	if (!drv)
		return -1;

	for (i = 0; i < sizeof(exiRegisteredDrivers) / sizeof(exiRegisteredDrivers[0]); i++) {
		if (exiRegisteredDrivers[i] == drv)
			return exiBindDriver(drv);
	}

	for (i = 0; i < sizeof(exiRegisteredDrivers) / sizeof(exiRegisteredDrivers[0]); i++) {
		if (!exiRegisteredDrivers[i]) {
			exiRegisteredDrivers[i] = drv;
			return exiBindDriver(drv);
		}
	}

	return -1;
}

void H_EXIUnregisterDriver(struct exi_device_driver *drv) {
	uint i, channel, cs;
	struct exi_device *dev;

	if (!drv)
		return;

	for (channel = 0; channel < 3; channel++) {
		for (cs = 0; cs < 3; cs++) {
			dev = &exiDevices[channel][cs];

			if (dev->driver == drv && dev->drv_data && drv->remove)
				drv->remove(dev);
		}
	}

	for (i = 0; i < sizeof(exiRegisteredDrivers) / sizeof(exiRegisteredDrivers[0]); i++) {
		if (exiRegisteredDrivers[i] == drv)
			exiRegisteredDrivers[i] = NULL;
	}
}

static void exiInit(void) {
	uint i;

	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: {
		regs = (struct exi_regs *)FLIPPER_EXI_BASE;
		break;
	}
	case CONSOLE_TYPE_WII:
	case CONSOLE_TYPE_WII_U: {
		regs = (struct exi_regs *)HOLLYWOOD_EXI_BASE;
		HW_AIPROT |= BIT(0);
		break;
	}
	default:
		break;
	}

	regs->channels[0].csr = EXI_CSR_EXTINT | EXI_CSR_ROMDIS;
	regs->channels[1].csr = EXI_CSR_EXTINT;
	regs->channels[2].csr = 0;
	memset(exiDevices, 0, sizeof(exiDevices));
	memset(exiProbeFailed, 0, sizeof(exiProbeFailed));
	memset(exiRegisteredDrivers, 0, sizeof(exiRegisteredDrivers));

	/* we're all good */
	exiDrv.state = DRIVER_STATE_READY;

	(void)exiProbe(0, 1);
	for (i = 0; i < 3; i++)
		exiRescanChannel(i);
	T_QueueRepeatingEvent(500 * 1000, exiHotplug, NULL);
}

static void exiCleanup(void) {
	uint channel, cs;

	for (channel = 0; channel < 3; channel++) {
		for (cs = 0; cs < 3; cs++)
			exiRemove(channel, cs);
	}
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
