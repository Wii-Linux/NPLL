/*
 * NPLL - Hollywood/Latte Hardware - EHCI/OHCI instance plumbing
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "USB-HCD"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/hollywood/ehci.h>
#include <npll/hollywood/ohci.h>
#include <npll/log.h>
#include <npll/soc.h>
#include <npll/timer.h>
#include <npll/usb.h>
#include <npll/utils.h>

#define EHCI_HCSPARAMS       0x04u
#define EHCI_USBCMD_OFF      0x00u
#define EHCI_USBSTS_OFF      0x04u
#define EHCI_USBINTR_OFF     0x08u
#define EHCI_PERIODICLIST_OFF 0x14u
#define EHCI_ASYNCLIST_OFF   0x18u
#define EHCI_CONFIGFLAG_OFF  0x40u
#define EHCI_PORTSC_OFF(p)   (0x44u + ((p) * 4u))
#define EHCI_CMD_RUN         BIT(0)
#define EHCI_CMD_RESET       BIT(1)
#define EHCI_CMD_PERIODIC    BIT(4)
#define EHCI_CMD_ASYNC       BIT(5)
#define EHCI_STS_HALTED      BIT(12)
#define EHCI_STS_PERIODIC    BIT(14)
#define EHCI_STS_ASYNC       BIT(15)
#define EHCI_PORT_CONNECT    BIT(0)
#define EHCI_PORT_CSC        BIT(1)
#define EHCI_PORT_ENABLE     BIT(2)
#define EHCI_PORT_PEC        BIT(3)
#define EHCI_PORT_RESET      BIT(8)
#define EHCI_PORT_OWNER      BIT(13)
#define EHCI_PORT_LINE_MASK  (3u << 10)
#define EHCI_PORT_LINE_K     (1u << 10)
#define EHCI_PORT_CHANGE     (EHCI_PORT_CSC | EHCI_PORT_PEC | BIT(5))

#define EHCI_LINK_TERMINATE  BIT(0)
#define EHCI_LINK_QH         BIT(1)
#define EHCI_QH_SPEED_FULL   (0u << 12)
#define EHCI_QH_SPEED_LOW    (1u << 12)
#define EHCI_QH_SPEED_HIGH   (2u << 12)
#define EHCI_QH_DTC          BIT(14)
#define EHCI_QH_HEAD         BIT(15)
#define EHCI_QH_CONTROL_EP   BIT(27)
#define EHCI_QH_RL(n)        ((u32)(n) << 28)
#define EHCI_QH_MULT_ONE     (1u << 30)
#define EHCI_QH_RL_HS        4u
#define EHCI_QH_CMASK(n)     ((u32)(n) << 8)
#define EHCI_QH_HUB_ADDR(n)  ((u32)(n) << 16)
#define EHCI_QH_HUB_PORT(n)  ((u32)(n) << 23)
#define EHCI_QTD_ACTIVE      BIT(7)
#define EHCI_QTD_HALTED      BIT(6)
#define EHCI_QTD_DBE         BIT(5)
#define EHCI_QTD_BABBLE      BIT(4)
#define EHCI_QTD_XACT        BIT(3)
#define EHCI_QTD_PID_OUT     (0u << 8)
#define EHCI_QTD_PID_IN      (1u << 8)
#define EHCI_QTD_PID_SETUP   (2u << 8)
#define EHCI_QTD_CERR        (3u << 10)
#define EHCI_QTD_IOC         BIT(15)
#define EHCI_QTD_BYTES(n)    ((n) << 16)
#define EHCI_QTD_TOGGLE      BIT(31)
#define EHCI_QTD_REMAIN(t)   (((t) >> 16) & 0x7fffu)
#define EHCI_QTD_ERROR       (EHCI_QTD_HALTED | EHCI_QTD_DBE | EHCI_QTD_BABBLE | EHCI_QTD_XACT)
#define EHCI_MAX_QTDS        18u

#define OHCI_CONTROL         0x04u
#define OHCI_COMMAND_STATUS  0x08u
#define OHCI_INTR_DISABLE    0x14u
#define OHCI_HCCA            0x18u
#define OHCI_CONTROL_HEAD    0x20u
#define OHCI_BULK_HEAD       0x28u
#define OHCI_FM_INTERVAL     0x34u
#define OHCI_PERIODIC_START  0x40u
#define OHCI_RH_DESC_A       0x48u
#define OHCI_RH_PORT(p)      (0x54u + ((p) * 4u))
#define OHCI_CTRL_HCFS_MASK  (3u << 6)
#define OHCI_CTRL_OPERATIONAL (2u << 6)
#define OHCI_CTRL_PERIODIC   BIT(2)
#define OHCI_CTRL_CONTROL    BIT(4)
#define OHCI_CTRL_BULK       BIT(5)
#define OHCI_CMD_RESET       BIT(0)
#define OHCI_CMD_CONTROL_FILLED BIT(1)
#define OHCI_CMD_BULK_FILLED BIT(2)
#define OHCI_PORT_CONNECT    BIT(0)
#define OHCI_PORT_ENABLE     BIT(1)
#define OHCI_PORT_RESET      BIT(4)
#define OHCI_PORT_LOW_SPEED  BIT(9)
#define OHCI_PORT_CHANGE     (BIT(16) | BIT(17) | BIT(18) | BIT(19) | BIT(20))

#define OHCI_ED_DIR_TD       (0u << 11)
#define OHCI_ED_LOW_SPEED    BIT(13)
#define OHCI_ED_SKIP         BIT(14)
#define OHCI_ED_HEAD_HALTED  BIT(0)
#define OHCI_ED_HEAD_CARRY   BIT(1)
#define OHCI_TD_ROUNDING     BIT(18)
#define OHCI_TD_DP_SETUP     (0u << 19)
#define OHCI_TD_DP_OUT       (1u << 19)
#define OHCI_TD_DP_IN        (2u << 19)
#define OHCI_TD_NO_INTERRUPT (7u << 21)
#define OHCI_TD_TOGGLE_CARRY (0u << 24)
#define OHCI_TD_TOGGLE_0     (2u << 24)
#define OHCI_TD_TOGGLE_1     (3u << 24)
#define OHCI_TD_CC_SHIFT     28u
#define OHCI_TD_CC_MASK      (0xfu << OHCI_TD_CC_SHIFT)
#define OHCI_TD_CC_NOT_ACCESSED (0xfu << OHCI_TD_CC_SHIFT)
#define OHCI_TD_CC_STALL     4u
#define OHCI_MAX_TDS         18u

enum hcdType {
	HCD_EHCI,
	HCD_OHCI
};
/*
 * Resident interrupt-IN state.  Unlike the one-shot transfer schedules above,
 * these stay linked in the hardware periodic schedule for the lifetime of the
 * endpoint so the controller polls the device continuously without any CPU
 * involvement.  The HC-visible descriptors live in a dedicated MEM2 allocation
 * (separate cache lines from the CPU-only bookkeeping) so invalidating the qTD
 * on a poll never discards a bookkeeping write.
 */
struct ehciIntSched {
	struct ehciQh qh;
	struct ehciQtd qtd;
} __attribute__((aligned(32)));

struct ehciIntEndpoint {
	struct ehciIntSched *sched;   /* MEM2, HC-visible */
	void *buffer;                 /* MEM2, HC-visible DMA target */
	struct ehciIntEndpoint *next;
	struct usbEndpoint *ep;
	u32 length;
	u32 caps;                     /* endpointCaps incl. S-mask/C-mask */
	u32 endpoint;                 /* qH endpoint characteristics */
	u8 toggle;
};

struct ohciIntSched {
	struct ohciED ed;
	struct ohciTD td[2];
} __attribute__((aligned(32)));

struct ohciIntEndpoint {
	struct ohciIntSched *sched;   /* MEM2, HC-visible */
	void *buffer;                 /* MEM2, HC-visible DMA target */
	struct ohciIntEndpoint *next;
	struct usbEndpoint *ep;
	u32 length;
	bool input;
};

struct hcdPrivate {
	enum hcdType type;
	struct ohciHCCA *hcca;
	struct ehciSchedule *ehci;
	u32 *periodicList;
	struct ohciSchedule *ohci;
	struct ehciIntEndpoint *ehciInt;   /* resident interrupt QH chain */
	struct ohciIntEndpoint *ohciInt;   /* resident interrupt ED chain */
	bool periodicOn;
};

struct ehciSchedule {
	struct ehciQh qh;
	struct ehciQtd qtd[EHCI_MAX_QTDS];
	struct usbSetupPacket setup __attribute__((aligned(32)));
} __attribute__((aligned(32)));

struct ohciSchedule {
	struct ohciED ed;
	struct ohciTD td[OHCI_MAX_TDS];
	struct usbSetupPacket setup __attribute__((aligned(32)));
} __attribute__((aligned(32)));

_Static_assert((__builtin_offsetof(struct ehciSchedule, setup) & 31u) == 0,
	"EHCI setup packet must be 32-byte aligned");
_Static_assert((__builtin_offsetof(struct ohciSchedule, setup) & 31u) == 0,
	"OHCI setup packet must be 32-byte aligned");

static u32 ehciPhys(const void *ptr);

static REGISTER_DRIVER(usbHCDDriver);
static struct hcdPrivate privateData[USB_MAX_CONTROLLERS];
static struct usbHostController hcs[USB_MAX_CONTROLLERS];
static uint numHCs;

static bool waitEHCI(struct usbHostController *hc, u32 off, u32 mask, bool set) {
	u64 start = mftb();
	while (!!(EHCI_ReadOp32(hc->mmioBase, off) & mask) != set) {
		if (T_HasElapsed(start, 1000000u))
			return false;
	}
	return true;
}

static int ehciStart(struct usbHostController *hc) {
	struct hcdPrivate *priv = hc->priv;
	u32 command, params;
	uint i;

	command = EHCI_ReadOp32(hc->mmioBase, EHCI_USBCMD_OFF) & ~EHCI_CMD_RUN;

	EHCI_WriteOp32(hc->mmioBase, EHCI_USBCMD_OFF, command);
	if (!waitEHCI(hc, EHCI_USBSTS_OFF, EHCI_STS_HALTED, true))
		return -ETIMEDOUT;

	EHCI_WriteOp32(hc->mmioBase, EHCI_USBCMD_OFF, command | EHCI_CMD_RESET);
	if (!waitEHCI(hc, EHCI_USBCMD_OFF, EHCI_CMD_RESET, false))
		return -ETIMEDOUT;

	params = EHCI_Read32(hc->mmioBase, EHCI_HCSPARAMS);
	hc->numPorts = params & 0x0fu;
	if (!hc->numPorts || hc->numPorts > 15u)
		return -ENODEV;

	/*
	 * Hollywood/Latte require every HC-visible object to be 32-byte aligned.
	 * Keep the schedules in MEM2 as well: MEM1 has additional DMA-write
	 * hazards on these non-standard controllers.
	 */
	/* HC reset above cleared PLE; keep the resident-interrupt tracking in sync */
	priv->ehciInt = NULL;
	priv->periodicOn = false;

	priv->ehci = M_PoolAlloc(POOL_MEM2, sizeof(*priv->ehci), 32);
	memset(priv->ehci, 0, sizeof(*priv->ehci));

	priv->periodicList = M_PoolAlloc(POOL_MEM2, 4096, 4096);
	for (i = 0; i < 1024; i++)
		priv->periodicList[i] = npll_cpu_to_le32(EHCI_LINK_TERMINATE);
	dcache_flush(priv->periodicList, 4096);

	EHCI_WriteOp32(hc->mmioBase, EHCI_USBINTR_OFF, 0);
	EHCI_WriteOp32(hc->mmioBase, EHCI_PERIODICLIST_OFF, ehciPhys(priv->periodicList));
	EHCI_WriteOp32(hc->mmioBase, EHCI_CONFIGFLAG_OFF, 1);

	command = EHCI_ReadOp32(hc->mmioBase, EHCI_USBCMD_OFF);
	EHCI_WriteOp32(hc->mmioBase, EHCI_USBCMD_OFF, command | EHCI_CMD_RUN);
	if (!waitEHCI(hc, EHCI_USBSTS_OFF, EHCI_STS_HALTED, false)) {
		free(priv->periodicList);
		priv->periodicList = NULL;
		free(priv->ehci);
		priv->ehci = NULL;
		return -ETIMEDOUT;
	}

	return 0;
}

static void ehciStop(struct usbHostController *hc) {
	struct hcdPrivate *priv = hc->priv;
	u32 command = EHCI_ReadOp32(hc->mmioBase, EHCI_USBCMD_OFF);

	EHCI_WriteOp32(hc->mmioBase, EHCI_USBINTR_OFF, 0);
	EHCI_WriteOp32(hc->mmioBase, EHCI_USBCMD_OFF, command & ~EHCI_CMD_RUN);
	waitEHCI(hc, EHCI_USBSTS_OFF, EHCI_STS_HALTED, true);

	if (priv->periodicList) {
		free(priv->periodicList);
		priv->periodicList = NULL;
	}

	if (priv->ehci) {
		free(priv->ehci);
		priv->ehci = NULL;
	}
}

static int ehciPortStatus(struct usbHostController *hc, uint port, struct usbRootPortStatus *status) {
	u32 value;
	if (port >= hc->numPorts)
		return -EINVAL;

	value = EHCI_ReadOp32(hc->mmioBase, EHCI_PORTSC_OFF(port));

	/*
	 * A companion-owned port remains physically connected in PORTSC, but
	 * it is no longer a device on this EHCI root hub.
	 */
	status->connected = !!(value & EHCI_PORT_CONNECT) && !(value & EHCI_PORT_OWNER);
	status->enabled = !!(value & EHCI_PORT_ENABLE);
	status->changed = !!(value & EHCI_PORT_CHANGE);

	if (value & EHCI_PORT_ENABLE)
		status->speed = USB_SPEED_HIGH;
	else if ((value & EHCI_PORT_LINE_MASK) == EHCI_PORT_LINE_K)
		status->speed = USB_SPEED_LOW;
	else
		status->speed = USB_SPEED_FULL;

	return 0;
}

static int ehciPortReset(struct usbHostController *hc, uint port, enum usbSpeed *speed) {
	u32 value;
	u64 start;

	if (port >= hc->numPorts || !speed)
		return -EINVAL;

	value = EHCI_ReadOp32(hc->mmioBase, EHCI_PORTSC_OFF(port));
	value &= ~EHCI_PORT_CHANGE;
	EHCI_WriteOp32(hc->mmioBase, EHCI_PORTSC_OFF(port), value | EHCI_PORT_RESET);
	udelay(50000u);
	EHCI_WriteOp32(hc->mmioBase, EHCI_PORTSC_OFF(port), value & ~EHCI_PORT_RESET);

	start = mftb();
	do {
		value = EHCI_ReadOp32(hc->mmioBase, EHCI_PORTSC_OFF(port));
		if (!(value & EHCI_PORT_RESET))
			break;
	} while (!T_HasElapsed(start, 100000u));

	if (value & EHCI_PORT_RESET)
		return -ETIMEDOUT;

	if (!(value & EHCI_PORT_ENABLE)) {
		EHCI_WriteOp32(hc->mmioBase, EHCI_PORTSC_OFF(port), (value & ~EHCI_PORT_CHANGE) | EHCI_PORT_OWNER);
		*speed = USB_SPEED_FULL;
		return -EAGAIN;
	}

	*speed = USB_SPEED_HIGH;
	return 0;
}

static void ehciClearChange(struct usbHostController *hc, uint port) {
	u32 value = EHCI_ReadOp32(hc->mmioBase, EHCI_PORTSC_OFF(port));
	EHCI_WriteOp32(hc->mmioBase, EHCI_PORTSC_OFF(port), value | (value & EHCI_PORT_CHANGE));
}

static int ohciStart(struct usbHostController *hc) {
	struct hcdPrivate *priv = hc->priv;
	u32 control, ports;
	u64 start;

	/* HC reset below clears PLE; keep the resident-interrupt tracking in sync */
	priv->ohciInt = NULL;
	priv->periodicOn = false;

	priv->hcca = M_PoolAlloc(POOL_MEM2, sizeof(*priv->hcca), 256);
	memset(priv->hcca, 0, sizeof(*priv->hcca));

	priv->ohci = M_PoolAlloc(POOL_MEM2, sizeof(*priv->ohci), 32);
	memset(priv->ohci, 0, sizeof(*priv->ohci));
	dcache_flush(priv->hcca, sizeof(*priv->hcca));

	OHCI_Write32(hc->mmioBase, OHCI_INTR_DISABLE, 0xffffffffu);
	OHCI_Write32(hc->mmioBase, OHCI_COMMAND_STATUS, OHCI_CMD_RESET);

	start = mftb();
	while (OHCI_Read32(hc->mmioBase, OHCI_COMMAND_STATUS) & OHCI_CMD_RESET) {
		if (T_HasElapsed(start, 1000000u)) {
			free(priv->ohci);
			priv->ohci = NULL;
			free(priv->hcca);
			priv->hcca = NULL;
			return -ETIMEDOUT;
		}
	}

	ports = OHCI_Read32(hc->mmioBase, OHCI_RH_DESC_A) & 0xffu;
	if (!ports || ports > 15u) {
		free(priv->ohci);
		priv->ohci = NULL;
		free(priv->hcca);
		priv->hcca = NULL;
		return -ENODEV;
	}

	hc->numPorts = ports;

	/* HCR resets the frame timing registers on some implementations. */
	OHCI_Write32(hc->mmioBase, OHCI_FM_INTERVAL, 0x27782edfu);
	OHCI_Write32(hc->mmioBase, OHCI_PERIODIC_START, 0x2a2fu);
	OHCI_Write32(hc->mmioBase, OHCI_HCCA, (u32)(uintptr_t)virtToPhys(priv->hcca));
	control = OHCI_Read32(hc->mmioBase, OHCI_CONTROL) & ~(OHCI_CTRL_HCFS_MASK | OHCI_CTRL_PERIODIC | OHCI_CTRL_CONTROL | OHCI_CTRL_BULK);
	OHCI_Write32(hc->mmioBase, OHCI_CONTROL, control | OHCI_CTRL_OPERATIONAL);
	udelay(10000u);
	return 0;
}

static void ohciStop(struct usbHostController *hc) {
	struct hcdPrivate *priv = hc->priv;

	OHCI_Write32(hc->mmioBase, OHCI_INTR_DISABLE, 0xffffffffu);
	OHCI_Write32(hc->mmioBase, OHCI_CONTROL, OHCI_Read32(hc->mmioBase, OHCI_CONTROL) & ~OHCI_CTRL_HCFS_MASK);
	OHCI_Write32(hc->mmioBase, OHCI_COMMAND_STATUS, OHCI_CMD_RESET);

	if (priv->ohci) {
		free(priv->ohci);
		priv->ohci = NULL;
	}

	if (priv->hcca) {
		free(priv->hcca);
		priv->hcca = NULL;
	}
}

static int ohciPortStatus(struct usbHostController *hc, uint port, struct usbRootPortStatus *status) {
	u32 value;

	if (port >= hc->numPorts)
		return -EINVAL;

	value = OHCI_Read32(hc->mmioBase, OHCI_RH_PORT(port));
	status->connected = !!(value & OHCI_PORT_CONNECT);
	status->enabled = !!(value & OHCI_PORT_ENABLE);
	status->changed = !!(value & OHCI_PORT_CHANGE);
	status->speed = (value & OHCI_PORT_LOW_SPEED) ? USB_SPEED_LOW : USB_SPEED_FULL;

	return 0;
}

static int ohciPortReset(struct usbHostController *hc, uint port, enum usbSpeed *speed) {
	u32 value;
	u64 start;

	if (port >= hc->numPorts || !speed)
		return -EINVAL;

	OHCI_Write32(hc->mmioBase, OHCI_RH_PORT(port), OHCI_PORT_RESET);
	start = mftb();
	do {
		value = OHCI_Read32(hc->mmioBase, OHCI_RH_PORT(port));
		if (value & BIT(20))
			break;
	} while (!T_HasElapsed(start, 100000u));

	if (!(value & BIT(20)))
		return -ETIMEDOUT;

	OHCI_Write32(hc->mmioBase, OHCI_RH_PORT(port), BIT(20));
	*speed = (value & OHCI_PORT_LOW_SPEED) ? USB_SPEED_LOW : USB_SPEED_FULL;

	return 0;
}

static void ohciClearChange(struct usbHostController *hc, uint port) {
	u32 value = OHCI_Read32(hc->mmioBase, OHCI_RH_PORT(port));
	OHCI_Write32(hc->mmioBase, OHCI_RH_PORT(port), value & OHCI_PORT_CHANGE);
}

static u32 ehciPhys(const void *ptr) {
	return (u32)(uintptr_t)virtToPhys(ptr);
}

static void ehciSetBuffers(struct ehciQtd *qtd, const void *buffer, u32 length) {
	u32 address = ehciPhys(buffer);
	u32 page = address & ~0xfffu;
	uint i;
	(void)length;

	qtd->buffer[0] = npll_cpu_to_le32(address);
	for (i = 1; i < 5; i++)
		qtd->buffer[i] = npll_cpu_to_le32(page + (i * 0x1000u));
}

static u32 ehciQtdCapacity(const void *buffer, u32 remaining) {
	u32 capacity = 0x5000u - (ehciPhys(buffer) & 0xfffu);

	if (capacity > 0x7fffu)
		capacity = 0x7fffu;

	return remaining < capacity ? remaining : capacity;
}

static u32 ehciDataChunk(const void *buffer, u32 remaining, u16 maxPacketSize) {
	u32 chunk = ehciQtdCapacity(buffer, remaining);

	if (chunk < remaining)
		chunk -= chunk % maxPacketSize;

	return chunk;
}

static u32 ehciControlDmaLength(const struct usbTransfer *transfer, u32 chunk, u32 remaining, bool input) {
	if (input && transfer->device->speed != USB_SPEED_HIGH && chunk == remaining)
		return (chunk + transfer->endpoint->maxPacketSize - 1u) & ~((u32)transfer->endpoint->maxPacketSize - 1u);

	return chunk;
}

static void ehciBuildQTD(struct ehciQtd *qtd, u32 next, u32 alternate, u32 token, const void *buffer, u32 length) {
	memset(qtd, 0, sizeof(*qtd));

	qtd->next = npll_cpu_to_le32(next);
	qtd->alternate = npll_cpu_to_le32(alternate);
	qtd->token = npll_cpu_to_le32(token | EHCI_QTD_CERR | EHCI_QTD_BYTES(length) | EHCI_QTD_ACTIVE);

	if (length)
		ehciSetBuffers(qtd, buffer, length);
}

static bool ehciCanTransfer(const struct usbDevice *dev) {
	return dev->speed == USB_SPEED_HIGH ||
		(dev->ttHubAddress && dev->ttPort);
}

static u32 ehciQhEndpoint(struct usbTransfer *transfer, bool control) {
	struct usbDevice *dev = transfer->device;
	struct usbEndpoint *endpoint = transfer->endpoint;
	u32 value = (u32)dev->address |
		((u32)(endpoint->address & USB_ENDPOINT_NUMBER_MASK) << 8) |
		EHCI_QH_DTC | ((u32)endpoint->maxPacketSize << 16);
	if (dev->speed == USB_SPEED_HIGH) {
		value |= EHCI_QH_SPEED_HIGH | EHCI_QH_RL(EHCI_QH_RL_HS);
	}
	else if (dev->speed == USB_SPEED_LOW)
		value |= EHCI_QH_SPEED_LOW;
	else
		value |= EHCI_QH_SPEED_FULL;
	if (control && dev->speed != USB_SPEED_HIGH)
		value |= EHCI_QH_CONTROL_EP;
	return value;
}

static u32 ehciQhCaps(const struct usbDevice *dev) {
	u32 value = EHCI_QH_MULT_ONE;
	if (dev->speed != USB_SPEED_HIGH)
		value |= EHCI_QH_HUB_ADDR(dev->ttHubAddress) |
			EHCI_QH_HUB_PORT(dev->ttPort);
	return value;
}

static int ehciDisableAsync(struct usbHostController *hc) {
	u32 command = EHCI_ReadOp32(hc->mmioBase, EHCI_USBCMD_OFF);
	EHCI_WriteOp32(hc->mmioBase, EHCI_USBCMD_OFF, command & ~EHCI_CMD_ASYNC);
	return waitEHCI(hc, EHCI_USBSTS_OFF, EHCI_STS_ASYNC, false) ? 0 : -ETIMEDOUT;
}

static int ehciControlTransfer(struct usbHostController *hc, struct usbTransfer *transfer) {
	struct hcdPrivate *priv = hc->priv;
	struct ehciSchedule *sched = priv->ehci;
	struct ehciQtd *qtd;
	u32 qhPhys, statusPhys, nextPhys, token, chunk, dmaLength, remaining, completed;
	u32 command, overlayNext, qtdToken;
	u32 failureToken = 0;
	u8 *cursor;
	uint dataFirst, statusIndex, count, i;
	bool input, toggle;
	u64 start;

	if (!sched || !transfer->setup || !transfer->endpoint || !ehciCanTransfer(transfer->device))
		return -EINVAL;
	if (transfer->length && !transfer->data)
		return -EINVAL;

	memset(sched, 0, sizeof(*sched));
	memcpy(&sched->setup, transfer->setup, sizeof(sched->setup));

	input = !!(transfer->setup->requestType & USB_DIR_IN);
	remaining = transfer->length;
	cursor = transfer->data;
	dataFirst = 1;
	count = 1;

	while (remaining) {
		if (count + 1u >= EHCI_MAX_QTDS)
			return -EMSGSIZE;

		chunk = ehciQtdCapacity(cursor, remaining);
		remaining -= chunk;
		cursor += chunk;
		count++;
	}
	statusIndex = count++;
	statusPhys = ehciPhys(&sched->qtd[statusIndex]);

	/* Status stage is always DATA1 and opposite the data-stage direction. */
	token = transfer->length && input ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN;
	ehciBuildQTD(&sched->qtd[statusIndex], EHCI_LINK_TERMINATE, EHCI_LINK_TERMINATE, token | EHCI_QTD_TOGGLE | EHCI_QTD_IOC, NULL, 0);

	remaining = transfer->length;
	cursor = transfer->data;
	toggle = true;
	for (i = dataFirst; i < statusIndex; i++) {
		chunk = ehciQtdCapacity(cursor, remaining);
		dmaLength = ehciControlDmaLength(transfer, chunk, remaining, input);
		nextPhys = i + 1u < statusIndex ? ehciPhys(&sched->qtd[i + 1u]) : statusPhys;
		token = input ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;

		if (toggle)
			token |= EHCI_QTD_TOGGLE;

		ehciBuildQTD(&sched->qtd[i], nextPhys, statusPhys, token, cursor, dmaLength);
		if (((chunk + transfer->endpoint->maxPacketSize - 1u) / transfer->endpoint->maxPacketSize) & 1u)
			toggle = !toggle;

		remaining -= chunk;
		cursor += chunk;
	}

	nextPhys = transfer->length ? ehciPhys(&sched->qtd[dataFirst]) : statusPhys;
	ehciBuildQTD(&sched->qtd[0], nextPhys, EHCI_LINK_TERMINATE, EHCI_QTD_PID_SETUP, &sched->setup, sizeof(sched->setup));

	qhPhys = ehciPhys(&sched->qh);
	sched->qh.horizontal = npll_cpu_to_le32(qhPhys | EHCI_LINK_QH);
	sched->qh.endpoint = npll_cpu_to_le32(ehciQhEndpoint(transfer, true) | EHCI_QH_HEAD);
	sched->qh.endpointCaps = npll_cpu_to_le32(ehciQhCaps(transfer->device));
	sched->qh.overlayNext = npll_cpu_to_le32(ehciPhys(&sched->qtd[0]));
	sched->qh.overlayAlternate = npll_cpu_to_le32(EHCI_LINK_TERMINATE);

	if (transfer->length) {
		if (input)
			dcache_invalidate(transfer->data, transfer->length);
		else
			dcache_flush(transfer->data, transfer->length);
	}

	dcache_flush(sched, sizeof(*sched));
	EHCI_WriteOp32(hc->mmioBase, EHCI_ASYNCLIST_OFF, qhPhys);

	command = EHCI_ReadOp32(hc->mmioBase, EHCI_USBCMD_OFF);
	EHCI_WriteOp32(hc->mmioBase, EHCI_USBCMD_OFF, command | EHCI_CMD_ASYNC);
	if (!waitEHCI(hc, EHCI_USBSTS_OFF, EHCI_STS_ASYNC, true)) {
		ehciDisableAsync(hc);
		transfer->status = USB_TRANSFER_ERROR;
		return -ETIMEDOUT;
	}

	start = mftb();
	while (true) {
		/*
		 * Once a qTD has been prefetched, its backing token is not a
		 * reliable indication of execution progress on Hollywood/Latte.
		 * In particular, split transactions may leave it looking inactive
		 * while the live qH overlay is still completing split phases.
		 */
		dcache_invalidate(&sched->qh, sizeof(sched->qh));
		qtdToken = npll_le32_to_cpu(sched->qh.overlayToken);
		overlayNext = npll_le32_to_cpu(sched->qh.overlayNext);
		if (qtdToken & EHCI_QTD_ERROR) {
			failureToken = qtdToken;
			goto finished;
		}

		/*
		 * Before the HC fetches SETUP the overlay token is also inactive;
		 * overlayNext still points at the first qTD in that state.
		 */
		if (!(qtdToken & EHCI_QTD_ACTIVE) && (overlayNext & EHCI_LINK_TERMINATE))
			break;

		if (T_HasElapsed(start, transfer->timeoutUsecs)) {
			log_printf("bus %u control timeout: qTD %08x, overlay %08x\r\n",
				hc->bus, qtdToken, npll_le32_to_cpu(sched->qh.overlayToken)
			);
			transfer->status = USB_TRANSFER_TIMEOUT;
			ehciDisableAsync(hc);
			return -ETIMEDOUT;
		}
	}

finished:
	if (ehciDisableAsync(hc)) {
		transfer->status = USB_TRANSFER_ERROR;
		return -ETIMEDOUT;
	}

	completed = 0;

	if (failureToken) {
		log_printf("bus %u control transfer failed, qTD token %08x\r\n", hc->bus, failureToken);
		transfer->actualLength = 0;
		transfer->status = (failureToken & (EHCI_QTD_DBE | EHCI_QTD_BABBLE | EHCI_QTD_XACT)) ? USB_TRANSFER_ERROR : USB_TRANSFER_STALL;
		return 0;
	}

	remaining = transfer->length;
	cursor = transfer->data;

	for (i = dataFirst; i < statusIndex; i++) {
		qtd = &sched->qtd[i];
		dcache_invalidate(qtd, sizeof(*qtd));

		qtdToken = npll_le32_to_cpu(qtd->token);
		chunk = ehciQtdCapacity(cursor, remaining);
		dmaLength = ehciControlDmaLength(transfer, chunk, remaining, input);
		if (EHCI_QTD_REMAIN(qtdToken) <= dmaLength) {
			u32 done = dmaLength - EHCI_QTD_REMAIN(qtdToken);
			completed += done < chunk ? done : chunk;
		}
		remaining -= chunk;
		cursor += chunk;

		if (qtdToken & EHCI_QTD_ERROR) {
			log_printf("bus %u control data qTD %u failed, token %08x\r\n", hc->bus, i, qtdToken);
			transfer->actualLength = completed;
			transfer->status = (qtdToken & (EHCI_QTD_DBE | EHCI_QTD_BABBLE | EHCI_QTD_XACT)) ? USB_TRANSFER_ERROR : USB_TRANSFER_STALL;
			return 0;
		}
		if (EHCI_QTD_REMAIN(qtdToken))
			break;
	}

	transfer->actualLength = completed;
	if (input && completed)
		dcache_invalidate(transfer->data, completed);

	dcache_invalidate(&sched->qtd[statusIndex], sizeof(sched->qtd[statusIndex]));

	qtdToken = npll_le32_to_cpu(sched->qtd[statusIndex].token);
	if (qtdToken & EHCI_QTD_ERROR) {
		log_printf("bus %u control status qTD failed, token %08x\r\n", hc->bus, qtdToken);
		transfer->status = (qtdToken & (EHCI_QTD_DBE | EHCI_QTD_BABBLE | EHCI_QTD_XACT)) ? USB_TRANSFER_ERROR : USB_TRANSFER_STALL;
		return 0;
	}
	transfer->status = USB_TRANSFER_COMPLETE;
	return 0;
}

static int ehciDataTransfer(struct usbHostController *hc, struct usbTransfer *transfer) {
	struct hcdPrivate *priv = hc->priv;
	struct ehciSchedule *sched = priv->ehci;
	struct usbEndpoint *endpoint = transfer->endpoint;
	struct ehciQtd *qtd;
	u32 qhPhys, nextPhys, token, chunk, remaining, qtdToken;
	u32 completed = 0;
	u8 *cursor = transfer->data;
	uint count = 0, i, packets;
	bool input, toggle, finished;
	u64 start;

	if (!sched || !endpoint || !ehciCanTransfer(transfer->device) ||
	    (endpoint->attributes & USB_ENDPOINT_XFER_MASK) != USB_ENDPOINT_XFER_BULK ||
	    !endpoint->maxPacketSize || (transfer->length && !transfer->data))
		return -EINVAL;

	input = !!(endpoint->address & USB_ENDPOINT_DIR_MASK);
	remaining = transfer->length;

	/* A zero-length bulk request is still one transaction. */
	do {
		if (count >= EHCI_MAX_QTDS)
			return -EMSGSIZE;

		chunk = remaining ? ehciDataChunk(cursor, remaining, endpoint->maxPacketSize) : 0;
		remaining -= chunk;
		cursor += chunk;
		count++;
	} while (remaining);

	memset(sched, 0, sizeof(*sched));
	remaining = transfer->length;
	cursor = transfer->data;
	toggle = !!endpoint->toggle;

	for (i = 0; i < count; i++) {
		chunk = remaining ? ehciDataChunk(cursor, remaining, endpoint->maxPacketSize) : 0;
		nextPhys = i + 1u < count ? ehciPhys(&sched->qtd[i + 1u]) : EHCI_LINK_TERMINATE;
		token = input ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;

		if (toggle)
			token |= EHCI_QTD_TOGGLE;

		if (i + 1u == count)
			token |= EHCI_QTD_IOC;

		ehciBuildQTD(&sched->qtd[i], nextPhys, input ? EHCI_LINK_TERMINATE : nextPhys, token, cursor, chunk);
		packets = chunk ? (chunk + endpoint->maxPacketSize - 1u) / endpoint->maxPacketSize : 1u;

		if (packets & 1u)
			toggle = !toggle;

		remaining -= chunk;
		cursor += chunk;
	}

	qhPhys = ehciPhys(&sched->qh);
	sched->qh.horizontal = npll_cpu_to_le32(qhPhys | EHCI_LINK_QH);
	sched->qh.endpoint = npll_cpu_to_le32(ehciQhEndpoint(transfer, false) | EHCI_QH_HEAD);
	sched->qh.endpointCaps = npll_cpu_to_le32(ehciQhCaps(transfer->device));
	sched->qh.overlayNext = npll_cpu_to_le32(ehciPhys(&sched->qtd[0]));
	sched->qh.overlayAlternate = npll_cpu_to_le32(EHCI_LINK_TERMINATE);

	if (transfer->length) {
		if (input)
			dcache_invalidate(transfer->data, transfer->length);
		else
			dcache_flush(transfer->data, transfer->length);
	}
	dcache_flush(sched, sizeof(*sched));
	EHCI_WriteOp32(hc->mmioBase, EHCI_ASYNCLIST_OFF, qhPhys);
	EHCI_WriteOp32(hc->mmioBase, EHCI_USBCMD_OFF, EHCI_ReadOp32(hc->mmioBase, EHCI_USBCMD_OFF) | EHCI_CMD_ASYNC);
	if (!waitEHCI(hc, EHCI_USBSTS_OFF, EHCI_STS_ASYNC, true)) {
		ehciDisableAsync(hc);
		transfer->status = USB_TRANSFER_ERROR;
		return -ETIMEDOUT;
	}

	start = mftb();
	while (true) {
		finished = true;
		for (i = 0; i < count; i++) {
			dcache_invalidate(&sched->qtd[i], sizeof(sched->qtd[i]));
			qtdToken = npll_le32_to_cpu(sched->qtd[i].token);

			if (qtdToken & EHCI_QTD_ERROR)
				goto dataFinished;
			if (qtdToken & EHCI_QTD_ACTIVE) {
				finished = false;
				continue;
			}

			/*
			 * A short IN packet follows the alternate terminate link;
			 * later qTDs intentionally remain active.
			 */
			if (input && EHCI_QTD_REMAIN(qtdToken)) {
				finished = true;
				break;
			}
		}
		if (finished)
			break;
		if (T_HasElapsed(start, transfer->timeoutUsecs)) {
			dcache_invalidate(&sched->qh, sizeof(sched->qh));
			log_printf(
				"bus %u bulk %s timeout: qTD %08x, overlay %08x\r\n",
				hc->bus, input ? "IN" : "OUT", qtdToken, npll_le32_to_cpu(sched->qh.overlayToken)
			);
			transfer->status = USB_TRANSFER_TIMEOUT;
			ehciDisableAsync(hc);
			return -ETIMEDOUT;
		}
	}

dataFinished:
	if (ehciDisableAsync(hc)) {
		transfer->status = USB_TRANSFER_ERROR;
		return -ETIMEDOUT;
	}

	remaining = transfer->length;
	cursor = transfer->data;
	toggle = !!endpoint->toggle;

	for (i = 0; i < count; i++) {
		qtd = &sched->qtd[i];
		dcache_invalidate(qtd, sizeof(*qtd));

		qtdToken = npll_le32_to_cpu(qtd->token);
		if (qtdToken & EHCI_QTD_ACTIVE)
			break;

		chunk = remaining ? ehciDataChunk(cursor, remaining, endpoint->maxPacketSize) : 0;
		if (EHCI_QTD_REMAIN(qtdToken) > chunk) {
			transfer->status = USB_TRANSFER_ERROR;
			return 0;
		}

		chunk -= EHCI_QTD_REMAIN(qtdToken);
		completed += chunk;
		packets = chunk ? (chunk + endpoint->maxPacketSize - 1u) / endpoint->maxPacketSize : 1u;

		if (packets & 1u)
			toggle = !toggle;

		if (qtdToken & EHCI_QTD_ERROR) {
			transfer->actualLength = completed;
			endpoint->toggle = (u8)toggle;
			transfer->status = (qtdToken & (EHCI_QTD_DBE | EHCI_QTD_BABBLE | EHCI_QTD_XACT)) ? USB_TRANSFER_ERROR : USB_TRANSFER_STALL;
			return 0;
		}

		remaining -= remaining < chunk ? remaining : chunk;
		cursor += chunk;
		if (EHCI_QTD_REMAIN(qtdToken))
			break;
	}

	transfer->actualLength = completed;
	endpoint->toggle = (u8)toggle;

	if (input && completed)
		dcache_invalidate(transfer->data, completed);

	transfer->status = USB_TRANSFER_COMPLETE;
	return 0;
}

/*
 * Resident interrupt-IN endpoints (EHCI).
 *
 * The qH stays linked in the periodic frame list and PLE stays enabled for the
 * endpoint's lifetime, so the controller polls the device every frame with no
 * CPU involvement.  A poll is just a cache-invalidate of the qTD plus a token
 * read; the busy-wait that used to burn a full timeout per idle poll is gone.
 */
static u32 ehciIntCaps(struct usbTransfer *tmp) {
	struct usbDevice *dev = tmp->device;
	struct usbEndpoint *ep = tmp->endpoint;
	u32 caps = ehciQhCaps(dev);
	uint intervalMicroframes, i, smask;

	if (dev->speed == USB_SPEED_HIGH) {
		uint interval = ep->interval ? ep->interval : 1u;
		if (interval > 16u)
			interval = 16u;
		intervalMicroframes = 1u << (interval - 1u);
		if (intervalMicroframes <= 8u) {
			smask = 0;
			for (i = 0; i < 8u; i += intervalMicroframes)
				smask |= BIT(i);
		}
		else
			smask = BIT(0);
		caps |= smask;
	}
	else {
		/* one start-split and three complete-split opportunities */
		caps |= BIT(0) | EHCI_QH_CMASK(0x1cu);
	}
	return caps;
}

static void ehciIntArmQtd(struct ehciIntEndpoint *ie) {
	struct ehciIntSched *sched = ie->sched;
	u32 token = EHCI_QTD_PID_IN | (ie->toggle ? EHCI_QTD_TOGGLE : 0);

	ehciBuildQTD(&sched->qtd, EHCI_LINK_TERMINATE, EHCI_LINK_TERMINATE,
		token | EHCI_QTD_IOC, ie->buffer, ie->length);
	dcache_flush(&sched->qtd, sizeof(sched->qtd));

	/*
	 * Re-point the (still linked, live) qH overlay at the fresh qTD.  The qTD
	 * is flushed above before overlayNext is written, and overlayNext is a
	 * single aligned store written last, so the controller only ever follows
	 * it to a fully-written, already-visible qTD.  The overlay Active bit stays
	 * clear here (Active lives in the qTD), so a torn read of the other overlay
	 * words cannot make the HC execute a partial descriptor.
	 */
	sched->qh.current = 0;
	sched->qh.overlayToken = 0;
	sched->qh.overlayAlternate = npll_cpu_to_le32(EHCI_LINK_TERMINATE);
	sched->qh.overlayNext = npll_cpu_to_le32(ehciPhys(&sched->qtd));
	dcache_flush(&sched->qh, sizeof(sched->qh));
}

static void ehciIntRebuild(struct usbHostController *hc) {
	struct hcdPrivate *priv = hc->priv;
	struct ehciIntEndpoint *ie;
	u32 head = EHCI_LINK_TERMINATE;
	uint i;

	/* chain every resident interrupt qH: head -> ... -> terminate */
	for (ie = priv->ehciInt; ie; ie = ie->next) {
		u32 link = ie->next ? (ehciPhys(&ie->next->sched->qh) | EHCI_LINK_QH)
				    : EHCI_LINK_TERMINATE;
		ie->sched->qh.horizontal = npll_cpu_to_le32(link);
		dcache_flush(&ie->sched->qh, sizeof(ie->sched->qh));
	}
	if (priv->ehciInt)
		head = ehciPhys(&priv->ehciInt->sched->qh) | EHCI_LINK_QH;

	/*
	 * Anchor the chain at every frame.  We deliberately ignore bInterval for
	 * placement: over-polling only spends bus bandwidth (the controller does
	 * the work, not the CPU), and each qH's S-mask still gates the microframe.
	 * This keeps multi-endpoint periodic scheduling trivial.
	 */
	for (i = 0; i < 1024; i++)
		priv->periodicList[i] = npll_cpu_to_le32(head);
	dcache_flush(priv->periodicList, 4096);
}

static int ehciIntEnsurePeriodic(struct usbHostController *hc, bool on) {
	struct hcdPrivate *priv = hc->priv;
	u32 command;

	if (on == priv->periodicOn)
		return 0;

	command = EHCI_ReadOp32(hc->mmioBase, EHCI_USBCMD_OFF);
	if (on) {
		EHCI_WriteOp32(hc->mmioBase, EHCI_PERIODICLIST_OFF, ehciPhys(priv->periodicList));
		EHCI_WriteOp32(hc->mmioBase, EHCI_USBCMD_OFF, command | EHCI_CMD_PERIODIC);
		if (!waitEHCI(hc, EHCI_USBSTS_OFF, EHCI_STS_PERIODIC, true))
			return -ETIMEDOUT;
	}
	else {
		EHCI_WriteOp32(hc->mmioBase, EHCI_USBCMD_OFF, command & ~EHCI_CMD_PERIODIC);
		if (!waitEHCI(hc, EHCI_USBSTS_OFF, EHCI_STS_PERIODIC, false))
			return -ETIMEDOUT;
	}
	priv->periodicOn = on;
	return 0;
}

static void ehciInterruptStop(struct usbHostController *hc, struct usbEndpoint *ep) {
	struct hcdPrivate *priv = hc->priv;
	struct ehciIntEndpoint *ie = ep->hcData;
	struct ehciIntEndpoint **link;

	if (!ie)
		return;

	for (link = &priv->ehciInt; *link; link = &(*link)->next) {
		if (*link == ie) {
			*link = ie->next;
			break;
		}
	}
	ep->hcData = NULL;

	ehciIntRebuild(hc);
	if (!priv->ehciInt)
		ehciIntEnsurePeriodic(hc, false);

	/* let the controller finish any in-flight frame before freeing */
	udelay(2000);

	free(ie->buffer);
	free(ie->sched);
	free(ie);
}

static int ehciInterruptArm(struct usbHostController *hc, struct usbDevice *dev, struct usbEndpoint *ep, u32 length) {
	struct hcdPrivate *priv = hc->priv;
	struct ehciIntEndpoint *ie = ep->hcData;
	struct usbTransfer tmp;

	if (!priv->periodicList || !length || length > 0x400u || !ehciCanTransfer(dev))
		return -EINVAL;

	if (ie) {
		/* already armed: reload the qTD (e.g. after a cleared halt) */
		ie->toggle = 0;
		ehciIntArmQtd(ie);
		return 0;
	}

	ie = malloc(sizeof(*ie));
	if (!ie)
		return -ENOMEM;

	memset(ie, 0, sizeof(*ie));
	ie->sched = M_PoolAlloc(POOL_MEM2, sizeof(*ie->sched), 32);
	ie->buffer = M_PoolAlloc(POOL_MEM2, alignUpU32(length, 32), 32);
	ie->ep = ep;
	ie->length = length;

	memset(&tmp, 0, sizeof(tmp));
	tmp.device = dev;
	tmp.endpoint = ep;

	memset(ie->sched, 0, sizeof(*ie->sched));
	ie->sched->qh.endpoint = npll_cpu_to_le32(ehciQhEndpoint(&tmp, false));
	ie->sched->qh.endpointCaps = npll_cpu_to_le32(ehciIntCaps(&tmp));
	ehciIntArmQtd(ie);

	ie->next = priv->ehciInt;
	priv->ehciInt = ie;
	ep->hcData = ie;

	ehciIntRebuild(hc);
	if (ehciIntEnsurePeriodic(hc, true)) {
		ehciInterruptStop(hc, ep);
		return -ETIMEDOUT;
	}
	return 0;
}

static int ehciInterruptPoll(struct usbHostController *hc, struct usbEndpoint *ep, void *data, u32 length, u32 *actual) {
	struct ehciIntEndpoint *ie = ep->hcData;
	struct ehciIntSched *sched;
	u32 token, remaining, got;
	(void)hc;

	if (!ie)
		return -EINVAL;

	sched = ie->sched;
	dcache_invalidate(&sched->qtd, sizeof(sched->qtd));
	token = npll_le32_to_cpu(sched->qtd.token);

	if (token & EHCI_QTD_ACTIVE)
		return 0;   /* still waiting on the device; instant return */

	if (token & EHCI_QTD_ERROR) {
		if (!(token & (EHCI_QTD_DBE | EHCI_QTD_BABBLE | EHCI_QTD_XACT)))
			return -EPIPE;   /* halted: caller clears halt then re-arms */
		/* transient bus error: reload and report nothing this round */
		ehciIntArmQtd(ie);
		return 0;
	}

	remaining = EHCI_QTD_REMAIN(token);
	got = remaining > ie->length ? 0 : ie->length - remaining;
	if (got > length)
		got = length;
	if (got) {
		dcache_invalidate(ie->buffer, got);
		memcpy(data, ie->buffer, got);
	}

	ie->toggle ^= 1u;
	ep->toggle = ie->toggle;
	*actual = got;

	ehciIntArmQtd(ie);   /* reload for the next report */
	return 1;
}

static u32 ohciPhys(const void *ptr) {
	return (u32)(uintptr_t)virtToPhys(ptr);
}

static u32 ohciTDCapacity(const void *buffer, u32 remaining) {
	u32 capacity = 0x2000u - (ohciPhys(buffer) & 0xfffu);
	return remaining < capacity ? remaining : capacity;
}

static u32 ohciDataChunk(const void *buffer, u32 remaining, u16 maxPacketSize) {
	u32 chunk = ohciTDCapacity(buffer, remaining);
	if (chunk < remaining)
		chunk -= chunk % maxPacketSize;
	return chunk;
}

static void ohciBuildTD(struct ohciTD *td, u32 next, u32 flags,
	const void *buffer, u32 length) {
	u32 address = length ? ohciPhys(buffer) : 0;

	memset(td, 0, sizeof(*td));
	td->control = npll_cpu_to_le32(flags | OHCI_TD_NO_INTERRUPT | OHCI_TD_CC_NOT_ACCESSED);
	td->currentBuffer = npll_cpu_to_le32(address);
	td->next = npll_cpu_to_le32(next);
	td->bufferEnd = npll_cpu_to_le32(length ? address + length - 1u : 0);
}

static void ohciBuildED(struct ohciED *ed, struct usbTransfer *transfer,
	u32 head, u32 tail) {
	u32 control = (u32)transfer->device->address |
		((u32)(transfer->endpoint->address & USB_ENDPOINT_NUMBER_MASK) << 7) |
		OHCI_ED_DIR_TD | ((u32)transfer->endpoint->maxPacketSize << 16);

	if (transfer->device->speed == USB_SPEED_LOW)
		control |= OHCI_ED_LOW_SPEED;

	ed->control = npll_cpu_to_le32(control);
	ed->tail = npll_cpu_to_le32(tail);
	/* For non-control endpoints the TD asks hardware to use this carry bit
	 * as the next DATA0/DATA1 value, then OHCI updates it on completion. */
	ed->head = npll_cpu_to_le32(head |
		(transfer->endpoint->toggle ? OHCI_ED_HEAD_CARRY : 0));
	ed->next = 0;
}

enum ohciListType {
	OHCI_LIST_CONTROL,
	OHCI_LIST_BULK
};

/* Busy-wait for the HCCA frame counter to advance by `frames` (each ~1 ms). */
static void ohciWaitFrames(struct usbHostController *hc, uint frames) {
	struct hcdPrivate *priv = hc->priv;
	u64 deadline = mftb();
	u16 start;

	dcache_invalidate(priv->hcca, sizeof(*priv->hcca));
	start = npll_le16_to_cpu(priv->hcca->frameNumber);

	do {
		dcache_invalidate(priv->hcca, sizeof(*priv->hcca));
		if ((u16)(npll_le16_to_cpu(priv->hcca->frameNumber) - start) >= frames)
			return;
	} while (!T_HasElapsed(deadline, 1000u * (frames + 1u)));
}

static int ohciRunList(struct usbHostController *hc, struct ohciSchedule *sched, uint numTDs, enum ohciListType list, u32 timeoutUsecs, u32 *conditionCode) {
	u32 control, tdControl, cc = 0xfu;
	u64 start;
	uint i;
	bool complete;

	dcache_flush(sched, sizeof(*sched));
	control = OHCI_Read32(hc->mmioBase, OHCI_CONTROL);
	if (list == OHCI_LIST_CONTROL) {
		OHCI_Write32(hc->mmioBase, OHCI_CONTROL_HEAD, ohciPhys(&sched->ed));
		OHCI_Write32(hc->mmioBase, OHCI_CONTROL, control | OHCI_CTRL_CONTROL);
		OHCI_Write32(hc->mmioBase, OHCI_COMMAND_STATUS, OHCI_CMD_CONTROL_FILLED);
	}
	else {
		OHCI_Write32(hc->mmioBase, OHCI_BULK_HEAD, ohciPhys(&sched->ed));
		OHCI_Write32(hc->mmioBase, OHCI_CONTROL, control | OHCI_CTRL_BULK);
		OHCI_Write32(hc->mmioBase, OHCI_COMMAND_STATUS, OHCI_CMD_BULK_FILLED);
	}

	start = mftb();
	while (true) {
		complete = true;
		for (i = 0; i < numTDs; i++) {
			dcache_invalidate(&sched->td[i], sizeof(sched->td[i]));
			tdControl = npll_le32_to_cpu(sched->td[i].control);
			cc = (tdControl & OHCI_TD_CC_MASK) >> OHCI_TD_CC_SHIFT;
			if (cc == 0xfu) {
				complete = false;
				continue;
			}
			if (cc)
				goto ohciDone;
		}
		if (complete)
			break;
		if (T_HasElapsed(start, timeoutUsecs)) {
			cc = 0xfu;
			break;
		}
	}

ohciDone:
	control = OHCI_Read32(hc->mmioBase, OHCI_CONTROL);
	if (list == OHCI_LIST_CONTROL) {
		OHCI_Write32(hc->mmioBase, OHCI_CONTROL, control & ~OHCI_CTRL_CONTROL);
		OHCI_Write32(hc->mmioBase, OHCI_CONTROL_HEAD, 0);
	}
	else {
		OHCI_Write32(hc->mmioBase, OHCI_CONTROL, control & ~OHCI_CTRL_BULK);
		OHCI_Write32(hc->mmioBase, OHCI_BULK_HEAD, 0);
	}

	/*
	 * Let the HC drain the ED it may still be walking before the shared
	 * schedule is reused.  One frame boundary is sufficient and returns as soon
	 * as the counter ticks, versus the old unconditional 2 ms sleep.
	 */
	ohciWaitFrames(hc, 1);

	/*
	 * A TD can complete right at the polling deadline, just after we stopped
	 * the list.  Reconcile that late completion; otherwise the device advances
	 * its data toggle while the software endpoint keeps the stale one.
	 */
	if (cc == 0xfu) {
		complete = true;
		for (i = 0; i < numTDs; i++) {
			dcache_invalidate(&sched->td[i], sizeof(sched->td[i]));
			tdControl = npll_le32_to_cpu(sched->td[i].control);
			cc = (tdControl & OHCI_TD_CC_MASK) >> OHCI_TD_CC_SHIFT;
			if (cc == 0xfu) {
				complete = false;
				break;
			}
			if (cc)
				break;
		}
		if (complete && i == numTDs)
			cc = 0;
	}
	if (conditionCode)
		*conditionCode = cc;

	return cc == 0xfu ? -ETIMEDOUT : 0;
}

static u32 ohciTransferred(const struct ohciTD *td, const void *buffer, u32 length) {
	u32 current = npll_le32_to_cpu(td->currentBuffer);
	u32 start = length ? ohciPhys(buffer) : 0;

	if (!length || !current)
		return length;
	if (current < start || current - start > length)
		return 0;

	return current - start;
}

static void ohciSetStatus(struct usbTransfer *transfer, u32 cc) {
	if (!cc)
		transfer->status = USB_TRANSFER_COMPLETE;
	else if (cc == OHCI_TD_CC_STALL)
		transfer->status = USB_TRANSFER_STALL;
	else
		transfer->status = USB_TRANSFER_ERROR;
}

static int ohciControlTransfer(struct usbHostController *hc, struct usbTransfer *transfer) {
	struct hcdPrivate *priv = hc->priv;
	struct ohciSchedule *sched = priv->ohci;
	u32 dataFlags, statusFlags, dataLength, cc;
	uint statusIndex, dummyIndex, numTDs;
	bool input;
	int ret;

	if (!sched || !transfer->setup || !transfer->endpoint || !transfer->endpoint->maxPacketSize || (transfer->length && !transfer->data))
		return -EINVAL;
	if (transfer->device->speed == USB_SPEED_HIGH)
		return -EINVAL;
	if (transfer->length && ohciTDCapacity(transfer->data, transfer->length) < transfer->length)
		return -EMSGSIZE;

	memset(sched, 0, sizeof(*sched));
	memcpy(&sched->setup, transfer->setup, sizeof(sched->setup));

	input = !!(transfer->setup->requestType & USB_DIR_IN);
	statusIndex = transfer->length ? 2u : 1u;
	dummyIndex = statusIndex + 1u;
	numTDs = statusIndex + 1u;

	ohciBuildTD(&sched->td[0], ohciPhys(&sched->td[1]), OHCI_TD_DP_SETUP | OHCI_TD_TOGGLE_0, &sched->setup, sizeof(sched->setup));
	if (transfer->length) {
		dataFlags = (input ? OHCI_TD_DP_IN | OHCI_TD_ROUNDING : OHCI_TD_DP_OUT) | OHCI_TD_TOGGLE_1;
		ohciBuildTD(&sched->td[1], ohciPhys(&sched->td[statusIndex]), dataFlags, transfer->data, transfer->length);
	}
	statusFlags = (transfer->length && input ? OHCI_TD_DP_OUT : OHCI_TD_DP_IN) | OHCI_TD_TOGGLE_1;
	ohciBuildTD(&sched->td[statusIndex], ohciPhys(&sched->td[dummyIndex]), statusFlags, NULL, 0);
	ohciBuildTD(&sched->td[dummyIndex], 0, 0, NULL, 0);
	ohciBuildED(&sched->ed, transfer, ohciPhys(&sched->td[0]), ohciPhys(&sched->td[dummyIndex]));
	if (transfer->length) {
		if (input)
			dcache_invalidate(transfer->data, transfer->length);
		else
			dcache_flush(transfer->data, transfer->length);
	}

	ret = ohciRunList(hc, sched, numTDs, OHCI_LIST_CONTROL, transfer->timeoutUsecs, &cc);
	if (ret) {
		transfer->status = USB_TRANSFER_TIMEOUT;
		return ret;
	}

	dataLength = transfer->length ? ohciTransferred(&sched->td[1], transfer->data, transfer->length) : 0;
	transfer->actualLength = dataLength;
	if (input && dataLength)
		dcache_invalidate(transfer->data, dataLength);

	ohciSetStatus(transfer, cc);
	return 0;
}

static int ohciOneDataTransfer(struct usbHostController *hc, struct usbTransfer *transfer, void *buffer, u32 length, u32 *actual) {
	struct hcdPrivate *priv = hc->priv;
	struct ohciSchedule *sched = priv->ohci;
	struct usbEndpoint *endpoint = transfer->endpoint;
	u32 flags, cc, transferred;
	bool input = !!(endpoint->address & USB_ENDPOINT_DIR_MASK);
	int ret;

	memset(sched, 0, sizeof(*sched));
	flags = (input ? OHCI_TD_DP_IN | OHCI_TD_ROUNDING : OHCI_TD_DP_OUT) | OHCI_TD_TOGGLE_CARRY;

	ohciBuildTD(&sched->td[0], ohciPhys(&sched->td[1]), flags, buffer, length);
	ohciBuildTD(&sched->td[1], 0, 0, NULL, 0);
	ohciBuildED(&sched->ed, transfer, ohciPhys(&sched->td[0]), ohciPhys(&sched->td[1]));

	if (length) {
		if (input)
			dcache_invalidate(buffer, length);
		else
			dcache_flush(buffer, length);
	}

	ret = ohciRunList(hc, sched, 1, OHCI_LIST_BULK, transfer->timeoutUsecs, &cc);
	if (ret) {
		transfer->status = USB_TRANSFER_TIMEOUT;
		return ret;
	}

	transferred = ohciTransferred(&sched->td[0], buffer, length);
	*actual = transferred;
	dcache_invalidate(&sched->ed, sizeof(sched->ed));
	endpoint->toggle = (u8)!!(npll_le32_to_cpu(sched->ed.head) & OHCI_ED_HEAD_CARRY);
	if (cc) {
		log_printf("bus %u OHCI endpoint %02x condition code %u\r\n",
			hc->bus, endpoint->address, cc);
		ohciSetStatus(transfer, cc);
		return 0;
	}
	if (input && transferred)
		dcache_invalidate(buffer, transferred);

	transfer->status = USB_TRANSFER_COMPLETE;
	return 0;
}

static int ohciDataTransfer(struct usbHostController *hc, struct usbTransfer *transfer) {
	u8 *cursor = transfer->data;
	u32 remaining = transfer->length, chunk, actual, total = 0;
	int ret;

	if (!transfer->endpoint || !transfer->endpoint->maxPacketSize || transfer->device->speed == USB_SPEED_HIGH || (transfer->length && !transfer->data))
		return -EINVAL;

	do {
		chunk = remaining ? ohciDataChunk(cursor, remaining, transfer->endpoint->maxPacketSize) : 0;
		ret = ohciOneDataTransfer(hc, transfer, cursor, chunk, &actual);
		total += actual;

		if (ret || transfer->status != USB_TRANSFER_COMPLETE || actual < chunk)
			break;

		remaining -= chunk;
		cursor += chunk;
	} while (remaining);

	transfer->actualLength = total;
	return ret;
}

/*
 * Resident interrupt-IN endpoints (OHCI).
 *
 * The ED stays linked in the HCCA interrupt table with one live TD plus a dummy
 * tail; PLE stays on for the endpoint's lifetime.  Re-arming pauses the ED with
 * its SKIP bit (the HC ignores a skipped ED at the next frame boundary) so the
 * TD queue can be rewritten safely while linked.
 */
static void ohciIntArmTD(struct ohciIntEndpoint *ie) {
	struct ohciIntSched *sched = ie->sched;
	u32 flags = OHCI_TD_DP_IN | OHCI_TD_ROUNDING | OHCI_TD_TOGGLE_CARRY;

	ohciBuildTD(&sched->td[0], ohciPhys(&sched->td[1]), flags, ie->buffer, ie->length);
	ohciBuildTD(&sched->td[1], 0, 0, NULL, 0);

	/* head -> live TD (preserve the HC's toggle carry); tail -> dummy */
	sched->ed.head = npll_cpu_to_le32(ohciPhys(&sched->td[0]) |
		(ie->ep && ie->ep->toggle ? OHCI_ED_HEAD_CARRY : 0));
	sched->ed.tail = npll_cpu_to_le32(ohciPhys(&sched->td[1]));
	dcache_flush(sched, sizeof(*sched));
}

static void ohciIntRearm(struct ohciIntEndpoint *ie) {
	struct ohciIntSched *sched = ie->sched;
	u32 control = npll_le32_to_cpu(sched->ed.control);

	sched->ed.control = npll_cpu_to_le32(control | OHCI_ED_SKIP);
	dcache_flush(&sched->ed, sizeof(sched->ed));

	ohciIntArmTD(ie);

	sched->ed.control = npll_cpu_to_le32(control & ~OHCI_ED_SKIP);
	dcache_flush(&sched->ed, sizeof(sched->ed));
}

static void ohciIntRebuild(struct usbHostController *hc) {
	struct hcdPrivate *priv = hc->priv;
	struct ohciIntEndpoint *ie;
	u32 head = 0;
	uint i;

	for (ie = priv->ohciInt; ie; ie = ie->next) {
		ie->sched->ed.next = npll_cpu_to_le32(ie->next ? ohciPhys(&ie->next->sched->ed) : 0);
		dcache_flush(&ie->sched->ed, sizeof(ie->sched->ed));
	}
	if (priv->ohciInt)
		head = ohciPhys(&priv->ohciInt->sched->ed);

	for (i = 0; i < 32; i++)
		priv->hcca->interruptTable[i] = npll_cpu_to_le32(head);
	dcache_flush(priv->hcca->interruptTable, sizeof(priv->hcca->interruptTable));
}

static void ohciIntEnsurePeriodic(struct usbHostController *hc, bool on) {
	struct hcdPrivate *priv = hc->priv;
	u32 control;

	if (on == priv->periodicOn)
		return;

	control = OHCI_Read32(hc->mmioBase, OHCI_CONTROL);
	if (on)
		control |= OHCI_CTRL_PERIODIC;
	else
		control &= ~OHCI_CTRL_PERIODIC;
	OHCI_Write32(hc->mmioBase, OHCI_CONTROL, control);
	priv->periodicOn = on;
}

static void ohciInterruptStop(struct usbHostController *hc, struct usbEndpoint *ep) {
	struct hcdPrivate *priv = hc->priv;
	struct ohciIntEndpoint *ie = ep->hcData;
	struct ohciIntEndpoint **link;
	u32 control;

	if (!ie)
		return;

	/* skip the ED so the HC stops touching it before we unlink and free */
	control = npll_le32_to_cpu(ie->sched->ed.control);
	ie->sched->ed.control = npll_cpu_to_le32(control | OHCI_ED_SKIP);
	dcache_flush(&ie->sched->ed, sizeof(ie->sched->ed));

	for (link = &priv->ohciInt; *link; link = &(*link)->next) {
		if (*link == ie) {
			*link = ie->next;
			break;
		}
	}
	ep->hcData = NULL;

	ohciIntRebuild(hc);
	if (!priv->ohciInt)
		ohciIntEnsurePeriodic(hc, false);

	udelay(2000);

	free(ie->buffer);
	free(ie->sched);
	free(ie);
}

static int ohciInterruptArm(struct usbHostController *hc, struct usbDevice *dev, struct usbEndpoint *ep, u32 length) {
	struct hcdPrivate *priv = hc->priv;
	struct ohciIntEndpoint *ie = ep->hcData;
	struct usbTransfer tmp;

	if (!priv->hcca || !length || length > 0x400u || dev->speed == USB_SPEED_HIGH)
		return -EINVAL;

	if (ie) {
		ep->toggle = 0;
		ohciIntRearm(ie);
		return 0;
	}

	ie = malloc(sizeof(*ie));
	if (!ie)
		return -ENOMEM;

	memset(ie, 0, sizeof(*ie));
	ie->sched = M_PoolAlloc(POOL_MEM2, sizeof(*ie->sched), 32);
	ie->buffer = M_PoolAlloc(POOL_MEM2, alignUpU32(length, 32), 32);
	ie->ep = ep;
	ie->length = length;
	ie->input = true;

	memset(ie->sched, 0, sizeof(*ie->sched));
	memset(&tmp, 0, sizeof(tmp));
	tmp.device = dev;
	tmp.endpoint = ep;

	/* build ED control; head/tail are overwritten by ohciIntArmTD */
	ohciBuildED(&ie->sched->ed, &tmp, 0, 0);
	ohciIntArmTD(ie);

	ie->next = priv->ohciInt;
	priv->ohciInt = ie;
	ep->hcData = ie;

	ohciIntRebuild(hc);
	ohciIntEnsurePeriodic(hc, true);
	return 0;
}

static int ohciInterruptPoll(struct usbHostController *hc, struct usbEndpoint *ep, void *data, u32 length, u32 *actual) {
	struct ohciIntEndpoint *ie = ep->hcData;
	struct ohciIntSched *sched;
	u32 tdControl, cc, got;
	(void)hc;

	if (!ie)
		return -EINVAL;

	sched = ie->sched;
	dcache_invalidate(sched, sizeof(*sched));
	tdControl = npll_le32_to_cpu(sched->td[0].control);
	cc = (tdControl & OHCI_TD_CC_MASK) >> OHCI_TD_CC_SHIFT;

	if (cc == 0xfu)
		return 0;   /* not accessed yet */

	/* the HC maintains the data toggle carry in the ED across reloads */
	ep->toggle = (u8)!!(npll_le32_to_cpu(sched->ed.head) & OHCI_ED_HEAD_CARRY);

	if (cc) {
		if (cc == OHCI_TD_CC_STALL)
			return -EPIPE;
		ohciIntRearm(ie);
		return 0;
	}

	got = ohciTransferred(&sched->td[0], ie->buffer, ie->length);
	if (got > length)
		got = length;
	if (got) {
		dcache_invalidate(ie->buffer, got);
		memcpy(data, ie->buffer, got);
	}
	*actual = got;

	ohciIntRearm(ie);
	return 1;
}

static int hcdTransfer(struct usbHostController *hc, struct usbTransfer *transfer) {
	struct hcdPrivate *priv = hc->priv;

	if (priv->type == HCD_EHCI && (transfer->endpoint->attributes & USB_ENDPOINT_XFER_MASK) == USB_ENDPOINT_XFER_CONTROL)
		return ehciControlTransfer(hc, transfer);

	if (priv->type == HCD_EHCI && (transfer->endpoint->attributes & USB_ENDPOINT_XFER_MASK) == USB_ENDPOINT_XFER_BULK)
		return ehciDataTransfer(hc, transfer);

	if (priv->type == HCD_OHCI && (transfer->endpoint->attributes & USB_ENDPOINT_XFER_MASK) == USB_ENDPOINT_XFER_CONTROL)
		return ohciControlTransfer(hc, transfer);

	if (priv->type == HCD_OHCI && (transfer->endpoint->attributes & USB_ENDPOINT_XFER_MASK) == USB_ENDPOINT_XFER_BULK)
		return ohciDataTransfer(hc, transfer);

	/*
	 * Interrupt endpoints are driven by the resident-schedule arm/poll ops, not
	 * one-shot submissions; reaching here with one is a caller bug.
	 */
	transfer->status = USB_TRANSFER_ERROR;
	return -ENOSYS;
}

static const struct usbHostControllerOps ehciOps = {
	.start = ehciStart,
	.stop = ehciStop,
	.poll = NULL,
	.transfer = hcdTransfer,
	.cancel = NULL,
	.interruptArm = ehciInterruptArm,
	.interruptPoll = ehciInterruptPoll,
	.interruptStop = ehciInterruptStop,
	.rootPortStatus = ehciPortStatus,
	.rootPortReset = ehciPortReset,
	.rootPortClearChange = ehciClearChange,
};
static const struct usbHostControllerOps ohciOps = {
	.start = ohciStart,
	.stop = ohciStop,
	.poll = NULL,
	.transfer = hcdTransfer,
	.cancel = NULL,
	.interruptArm = ohciInterruptArm,
	.interruptPoll = ohciInterruptPoll,
	.interruptStop = ohciInterruptStop,
	.rootPortStatus = ohciPortStatus,
	.rootPortReset = ohciPortReset,
	.rootPortClearChange = ohciClearChange,
};

static int addHC(const char *name, uint bus, uintptr_t base, enum hcdType type) {
	struct usbHostController *hc;

	if (numHCs >= USB_MAX_CONTROLLERS)
		return -ENOSPC;

	hc = &hcs[numHCs];
	memset(hc, 0, sizeof(*hc));
	privateData[numHCs].type = type;

	hc->name = name;
	hc->bus = bus;
	hc->numPorts = 1; /* replaced by capability discovery in start() */
	hc->mmioBase = base;
	hc->ops = type == HCD_EHCI ? &ehciOps : &ohciOps;
	hc->priv = &privateData[numHCs];

	if (USB_RegisterHostController(hc))
		return -EIO;

	numHCs++;
	return 0;
}

static void usbHCDInit(void) {
	int ret = 0;

	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
		usbHCDDriver.state = DRIVER_STATE_NO_HARDWARE;
		return;
	}

	numHCs = 0;
	ret |= addHC("Hollywood EHCI0", 0, HOLLYWOOD_EHCI0_BASE, HCD_EHCI);
	ret |= addHC("Hollywood OHCI0", 1, HOLLYWOOD_OHCI0_BASE, HCD_OHCI);
	ret |= addHC("Hollywood OHCI1", 2, HOLLYWOOD_OHCI1_BASE, HCD_OHCI);
	if (H_ConsoleType == CONSOLE_TYPE_WII_U) {
		ret |= addHC("Latte EHCI1", 3, LATTE_EHCI1_BASE, HCD_EHCI);
		ret |= addHC("Latte OHCI2", 4, LATTE_OHCI2_BASE, HCD_OHCI);
		ret |= addHC("Latte EHCI2", 5, LATTE_EHCI2_BASE, HCD_EHCI);
		ret |= addHC("Latte OHCI3", 6, LATTE_OHCI3_BASE, HCD_OHCI);
	}
	if (ret) {
		while (numHCs) {
			numHCs--;
			USB_UnregisterHostController(&hcs[numHCs]);
		}
		usbHCDDriver.state = DRIVER_STATE_FAULTED;
		return;
	}
	usbHCDDriver.state = DRIVER_STATE_READY;
}

static void usbHCDCleanup(void) {
	while (numHCs) {
		numHCs--;
		USB_UnregisterHostController(&hcs[numHCs]);
	}
	usbHCDDriver.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(usbHCDDriver) = {
	.name = "Hollywood/Latte USB host controllers",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_CRITICAL,
	.init = usbHCDInit,
	.cleanup = usbHCDCleanup,
};
