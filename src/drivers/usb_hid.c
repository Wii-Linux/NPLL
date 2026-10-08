/*
 * NPLL - USB HID keyboard and DRH support
 *
 * Copyright (C) 2026 Techflash
 *
 * DRH code based on the linux-wiiu DRH driver:
 * https://gitlab.com/linux-wiiu/linux-wiiu/-/blob/rewrite-6.6/drivers/hid/hid-wiiu-drc.c
 * Copyright (C) 2021 Emmanuel Gil Peyrot <linkmauve@linkmauve.fr>
 * Copyright (C) 2019 Ash Logan <ash@heyquark.com>
 * Copyright (C) 2013 Mema Hacking
 */

#define MODULE "USB-HID"

#include <errno.h>
#include <string.h>
#include <npll/drivers.h>
#include <npll/input.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/usb.h>

#define MAX_USB_KEYBOARDS 8
#define DRH_HID_INTERFACES 2
#define KEYBOARD_POLL_US  10000u
#define KEYBOARD_REPEAT_DELAY_US 400000u
#define KEYBOARD_REPEAT_PERIOD_US 100000u

#define USB_VENDOR_NINTENDO 0x057eu
#define USB_PRODUCT_WIIU_DRH 0x0341u
#define DRH_REPORT_SIZE 128u

#define DRH_BTN_DOWN   0x000100u
#define DRH_BTN_UP     0x000200u
#define DRH_BTN_RIGHT  0x000400u
#define DRH_BTN_LEFT   0x000800u
#define DRH_BTN_A      0x008000u
#define DRH_MENU_BUTTONS (DRH_BTN_DOWN | DRH_BTN_UP | DRH_BTN_RIGHT | \
	DRH_BTN_LEFT | DRH_BTN_A)

struct usbKeyboard {
	struct usbInterface *interface;
	struct usbEndpoint *endpoint;
	u8 report[8];
	u8 repeatKey;
	u64 repeatStarted;
	u64 lastRepeat;
	bool errorLogged;
};

struct usbDRHState {
	u32 buttons;
	i16 sticks[4];
	u16 touchX, touchY, touchPressure;
	i16 accel[3];
	i32 gyro[3];
	i16 magnetometer[3];
	u8 volume, battery;
	bool charging;
};

struct usbDRHInterface {
	struct usbInterface *interface;
	struct usbEndpoint *endpoint;
	struct usbDRHState state;
	u32 repeatButton;
	u64 repeatStarted;
	u64 lastRepeat;
	bool errorLogged;
};

static REGISTER_DRIVER(usbHIDTopDriver);
static struct usbKeyboard keyboards[MAX_USB_KEYBOARDS];
static struct {
	struct usbDevice *device;
	struct usbDRHInterface interfaces[DRH_HID_INTERFACES];
	struct usbEndpoint *cdcIn, *cdcOut;
	bool cdcArmed;
	bool shuttingDown;
} drh;

/* Consume asynchronous notifications while waiting for our CDC reply */
static int drhStartupCommand(struct usbDevice *dev, struct usbEndpoint *out, struct usbEndpoint *in, u16 *transaction) {
	u8 command[13] ALIGN(32) = { 0x7e, 1, 0, 0, 0, 0x20, 4, 0, 0, 0, 0, 1, 0xff };
	u8 reply[1024] ALIGN(32);
	u8 *p;
	u32 actual, offset, length;
	u16 tag = (++*transaction << 4) | 8;
	uint attempt;
	int ret;

	command[2] = (u8)(tag >> 8);
	command[3] = (u8)tag;
	length = sizeof(command);
	ret = USB_BulkTransfer(dev, out, command, length, &actual, 100000);
	if (ret || actual != length)
		return ret ? ret : -EIO;

	for (attempt = 0; attempt < 8; attempt++) {
		ret = USB_BulkTransfer(dev, in, reply, sizeof(reply), &actual, 100000);
		if (ret)
			return ret;

		for (offset = 0; offset + 12 <= actual; offset += 12 + length) {
			p = reply + offset;
			length = ((u32)p[10] << 8) | p[11];
			if (p[0] != 0x7e || p[1] != 1 || length > actual - offset - 12)
				break;
			if (p[2] != command[2] || (p[3] & 0xf0) != (command[3] & 0xf0) ||
			    p[5] != 0x05 || p[6] != 4 || p[7] != command[7])
				continue;
			if (p[8] || p[9])
				return -EIO;

			return 0;
		}
	}

	return -ETIMEDOUT;
}

static void drhInitializeMode(void) {
	struct usbInterface *interface;
	struct usbDevice *dev = drh.device;
	struct usbEndpoint *candidateOut = NULL, *candidateIn = NULL, *out = NULL, *in = NULL, *ep;
	u16 transaction = 0x100;
	uint i, j;
	int ret;

	for (i = 0; i < dev->numInterfaces; i++) {
		interface = &dev->interfaces[i];
		candidateOut = candidateIn = NULL;
		if (interface->descriptor.interfaceClass != 0x0a)
			continue;

		for (j = 0; j < interface->numEndpoints; j++) {
			ep = &interface->endpoints[j];
			if ((ep->attributes & USB_ENDPOINT_XFER_MASK) != USB_ENDPOINT_XFER_BULK)
				continue;

			if (ep->address & USB_ENDPOINT_DIR_MASK)
				candidateIn = ep;
			else
				candidateOut = ep;
		}
		if (candidateOut && candidateIn) {
			out = candidateOut;
			in = candidateIn;
			break;
		}
	}
	drh.cdcIn = in;
	drh.cdcOut = out;
	if (!out)
		return;

	ret = drhStartupCommand(dev, out, in, &transaction);
	if (ret)
		log_printf("DRH normal-mode setup failed: %d\r\n", ret);
}

static int drhShutdownStationCount(struct usbDevice *dev, struct usbEndpoint *out, struct usbEndpoint *in, u16 *transaction, u32 timeout, u8 *count) {
	u8 command[12] ALIGN(32) = { 0x7e, 1, 0, 0, 0, 0x20, 2, 2, 0, 0, 0, 0 };
	u8 reply[1024] ALIGN(32);
	u16 tag;
	int ret;
	u32 actual = 0, remaining;
	uint offset, length;
	u64 start = mftb();
	const u8 *p;

	*transaction = (*transaction % 0x0fff) + 1;
	tag = (u16)(*transaction << 4) | 8u;
	command[2] = (u8)(tag >> 8);
	command[3] = (u8)tag;
	ret = USB_BulkTransfer(dev, out, command, sizeof(command), &actual, timeout);
	if (ret || actual != sizeof(command)) {
		log_printf("DRH station query OUT: USB=%d bytes=%u\r\n", ret, actual);
		return ret ? ret : -EIO;
	}

	while (T_ElapsedUsecs(start) < timeout) {
		remaining = timeout - T_ElapsedUsecs(start);
		if (!remaining || remaining > timeout)
			break;

		ret = USB_BulkTransfer(dev, in, reply, sizeof(reply), &actual, remaining);
		if (ret) {
			log_printf("DRH station query IN: USB=%d bytes=%u\r\n", ret, actual);
			return ret;
		}

		for (offset = 0; offset + 12 <= actual;) {
			p = reply + offset;
			length = ((uint)p[10] << 8) | p[11];
			if (p[0] != 0x7e || p[1] != 1 || length > actual - offset - 12) {
				log_printf("DRH station query framing: bytes=%u offset=%u length=%u\r\n", actual, offset, length);
				return -EIO;
			}

			if (p[2] == command[2] && (p[3] & 0xf0) == (command[3] & 0xf0) &&
			    p[5] == 5 && p[6] == 2 && p[7] == 2) {
				if (!p[8] && p[9] == 0x0f && !length)
					return -EBUSY;

				if (p[8] || p[9] || length != 55 || p[12] > 2) {
					log_printf("DRH station query rejected: error=%02x%02x length=%u\r\n", p[8], p[9], length);
					return -EIO;
				}

				*count = p[12];
				return 0;
			}
			offset += 12 + length;
		}
	}

	return -ETIMEDOUT;
}

void USBHID_PowerOffDRC(void) {
	static u16 transaction;
	u8 command[12] ALIGN(32) = { 0x7e, 1, 0, 0, 0, 0, 4, 0x1a, 0, 0, 0, 0 };
	struct usbDevice *dev;
	struct usbEndpoint *out = drh.cdcOut, *in = drh.cdcIn;
	uint target;
	bool sent = false;
	u64 tb;
	u32 actual, elapsed;
	u16 tag;
	u8 stationCount = 255;
	int stationResult, ret;
	drh.shuttingDown = true;

	if (drh.cdcArmed) {
		USB_ResidentInStop(drh.device, in);
		drh.cdcArmed = false;
	}

	USB_LockTopology();
	dev = drh.device;
	if (!dev || !dev->connected)
		goto unlock;
	if (!out || !in) {
		log_puts("DRH shutdown: CDC bulk endpoints missing");
		goto unlock;
	}
	stationResult = drhShutdownStationCount(dev, out, in, &transaction, 100000, &stationCount);

	for (target = stationResult || stationCount > 1 ? 3u : 2u; target >= 2; target--) {
		actual = 0;
		transaction = (transaction % 0x0fff) + 1;
		tag = (u16)(transaction << 4) | 8u;
		command[2] = (u8)(tag >> 8);
		command[3] = (u8)tag;
		command[5] = (u8)((target << 5) | 2);
		ret = USB_BulkTransfer(dev, out, command, sizeof(command), &actual, 100000);
		if (ret || actual != sizeof(command))
			log_printf("DRH shutdown target %u: USB error %d, sent %u/12 bytes\r\n", target, ret, actual);
		else
			sent = true;
	}

	if (sent) {
		tb = mftb();
		stationCount = 255;
		do {
			elapsed = T_ElapsedUsecs(tb);
			if (elapsed >= 40000)
				break;
			stationResult = drhShutdownStationCount(dev, out, in, &transaction, 40000 - elapsed, &stationCount);
			if (stationResult != -EBUSY && (stationResult || !stationCount))
				break;

			udelay(1000);
		} while (!T_HasElapsed(tb, 40000));
		elapsed = T_ElapsedUsecs(tb);
		if (elapsed < 40000 && (stationResult || stationCount))
			udelay(40000 - elapsed);
	}

	if (!dev->parent && dev->hc->ops->rootPortDisable) {
		ret = dev->hc->ops->rootPortDisable(dev->hc, dev->port);
		if (ret)
			log_printf("DRH USB disable failed: %d\r\n", ret);
	}
	else
		log_puts("DRH USB disable: unsupported topology/controller");
unlock:
	USB_UnlockTopology();
}

static inputEvent_t keyAction(u8 key) {
	switch (key) {
	case 0x4fu: return INPUT_EV_RIGHT;
	case 0x50u: return INPUT_EV_LEFT;
	case 0x51u: return INPUT_EV_DOWN;
	case 0x52u: return INPUT_EV_UP;
	case 0x28u: /* Enter */
	case 0x2cu: /* Space */
		return INPUT_EV_SELECT;
	case 0x45u: /* F12 */
		return INPUT_EV_SCREENSHOT;
	default:
		return 0;
	}
}

static u8 reportActionKey(const u8 *report) {
	uint i;

	for (i = 2; i < 8; i++) {
		if (keyAction(report[i]))
			return report[i];
	}

	return 0;
}

/*
 * Non-blocking peek at the resident interrupt endpoint.  Returns 1 when a new
 * report was copied out (count in *actual), 0 when nothing is queued, or a
 * negative errno on a fatal error.  A halted endpoint is recovered and re-armed
 * transparently, reported as "nothing this round".
 */
static int hidReadReport(struct usbDevice *dev, struct usbEndpoint *endpoint, void *report, u32 length, u32 *actual) {
	int ret = USB_ResidentInPoll(dev, endpoint, report, length, actual);

	if (ret == -EPIPE) {
		ret = USB_ClearHalt(dev, endpoint);
		if (ret < 0)
			return ret;
		ret = USB_ResidentInArm(dev, endpoint, length);
		if (ret < 0)
			return ret;
		return 0;
	}
	return ret;
}

static int keyboardReadReport(struct usbKeyboard *keyboard, u8 *report, u32 *actual) {
	return hidReadReport(keyboard->interface->device, keyboard->endpoint, report, sizeof(keyboard->report), actual);
}

static i16 drhLE16(const u8 *data) {
	return (i16)((u16)data[0] | ((u16)data[1] << 8));
}

static i32 drhLE24(const u8 *data) {
	u32 value = (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16);
	if (value & 0x00800000u)
		value |= 0xff000000u;
	return (i32)value;
}

static inputEvent_t drhButtonAction(u32 button) {
	switch (button) {
	case DRH_BTN_UP: return INPUT_EV_UP;
	case DRH_BTN_DOWN: return INPUT_EV_DOWN;
	case DRH_BTN_LEFT: return INPUT_EV_LEFT;
	case DRH_BTN_RIGHT: return INPUT_EV_RIGHT;
	case DRH_BTN_A: return INPUT_EV_SELECT;
	default: return 0;
	}
}

static u32 drhFirstButton(u32 buttons) {
	static const u32 order[] = { DRH_BTN_UP, DRH_BTN_DOWN, DRH_BTN_LEFT, DRH_BTN_RIGHT, DRH_BTN_A };
	uint i;

	for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		if (buttons & order[i])
			return order[i];
	}
	return 0;
}

/* FIXME: this is really ugly, should probably use a struct, but the linux-wiiu driver basically did the same thing */
static void drhParseReport(struct usbDRHInterface *pad, const u8 *data) {
	struct usbDRHState next;
	u32 pressed, button, base;
	u64 now = mftb();
	uint i;
	u32 x = 0, y = 0, pressure = 0;

	memset(&next, 0, sizeof(next));
	next.buttons = ((u32)data[80] << 16) | ((u32)data[2] << 8) | data[3];
	for (i = 0; i < 4; i++)
		next.sticks[i] = drhLE16(data + 6 + (i * 2));
	next.volume = data[14];
	next.accel[0] = drhLE16(data + 15);
	next.accel[1] = drhLE16(data + 17);
	next.accel[2] = drhLE16(data + 19);
	next.gyro[0] = drhLE24(data + 21);
	next.gyro[1] = drhLE24(data + 24);
	next.gyro[2] = drhLE24(data + 27);
	next.magnetometer[0] = drhLE16(data + 30);
	next.magnetometer[1] = drhLE16(data + 32);
	next.magnetometer[2] = drhLE16(data + 34);
	for (i = 0; i < 10; i++) {
		base = 36 + (i * 4);
		x += ((u32)(data[base + 1] & 0x0fu) << 8) | data[base];
		y += ((u32)(data[base + 3] & 0x0fu) << 8) | data[base + 2];
	}
	next.touchX = (u16)(x / 10u);
	next.touchY = (u16)(y / 10u);
	pressure |= ((data[37] >> 4) & 7u) << 0;
	pressure |= ((data[39] >> 4) & 7u) << 3;
	pressure |= ((data[41] >> 4) & 7u) << 6;
	pressure |= ((data[43] >> 4) & 7u) << 9;
	next.touchPressure = (u16)pressure;
	next.battery = data[5];
	next.charging = !!(data[4] & 0x40u);

	pressed = (next.buttons & ~pad->state.buttons) & DRH_MENU_BUTTONS;
	button = drhFirstButton(pressed);
	if (button) {
		IN_NewEvent(drhButtonAction(button));
		pad->repeatButton = button;
		pad->repeatStarted = now;
		pad->lastRepeat = now;
	}
	else if (!(next.buttons & pad->repeatButton)) {
		pad->repeatButton = drhFirstButton(next.buttons & DRH_MENU_BUTTONS);
		pad->repeatStarted = now;
		pad->lastRepeat = now;
	}
	else if (pad->repeatButton != DRH_BTN_A &&
	         T_HasElapsed(pad->repeatStarted, KEYBOARD_REPEAT_DELAY_US) &&
	         T_HasElapsed(pad->lastRepeat, KEYBOARD_REPEAT_PERIOD_US)) {
		pad->lastRepeat = now;
		IN_NewEvent(drhButtonAction(pad->repeatButton));
	}
	pad->state = next;
}

static void drhPollCDC(void) {
	u8 data[1024] ALIGN(32);
	u32 actual;
	if (drh.cdcArmed)
		hidReadReport(drh.device, drh.cdcIn, data, sizeof(data), &actual);
}

static void drhPoll(void) {
	struct usbDRHInterface *pad;
	u8 report[DRH_REPORT_SIZE] ALIGN(32);
	u32 actual;
	int ret;
	uint i;

	if (drh.shuttingDown || !drh.device || !drh.device->connected)
		return;
	drhPollCDC();
	for (i = 0; i < DRH_HID_INTERFACES; i++) {
		pad = &drh.interfaces[i];
		if (!pad->interface || pad->interface->driverData != pad || !pad->interface->device->connected)
			continue;

		memset(report, 0, sizeof(report));
		actual = 0;
		ret = hidReadReport(pad->interface->device, pad->endpoint, report, sizeof(report), &actual);

		if (ret == 0)
			continue;   /* no new report queued */

		if (ret < 0) {
			if (!pad->errorLogged) {
				log_printf("DRH interrupt poll failed: %d\r\n", ret);
				pad->errorLogged = true;
			}
			continue;
		}

		pad->errorLogged = false;
		if (actual == sizeof(report))
			drhParseReport(pad, report);
	}
}

static void keyboardPollLocked(void) {
	struct usbKeyboard *keyboard;
	inputEvent_t action;
	u8 report[8], key;
	u64 now;
	u32 actual;
	int ret;
	uint i;
	drhPoll();

	for (i = 0; i < MAX_USB_KEYBOARDS; i++) {
		keyboard = &keyboards[i];
		if (!keyboard->interface || !keyboard->interface->device->connected)
			continue;

		memset(report, 0, sizeof(report));
		actual = 0;
		ret = keyboardReadReport(keyboard, report, &actual);
		if (ret == 0) {
			/* no new report: drive key auto-repeat off the held key */
			now = mftb();
			if (keyboard->repeatKey &&
			    T_HasElapsed(keyboard->repeatStarted, KEYBOARD_REPEAT_DELAY_US) &&
			    T_HasElapsed(keyboard->lastRepeat, KEYBOARD_REPEAT_PERIOD_US)) {
				keyboard->lastRepeat = now;
				IN_NewEvent(keyAction(keyboard->repeatKey));
			}
			continue;
		}
		if (ret < 0) {
			if (!keyboard->errorLogged) {
				log_printf("keyboard interrupt poll failed: %d\r\n", ret);
				keyboard->errorLogged = true;
			}
			continue;
		}
		keyboard->errorLogged = false;
		if (actual != sizeof(report))
			continue;

		/* Usage 0x01 is ErrorRollOver; ignore the whole report */
		if (report[2] == 0x01u)
			continue;

		memcpy(keyboard->report, report, sizeof(report));
		key = reportActionKey(report);
		action = keyAction(key);
		now = mftb();
		if (!key) {
			keyboard->repeatKey = 0;
			continue;
		}
		if (key != keyboard->repeatKey) {
			keyboard->repeatKey = key;
			keyboard->repeatStarted = now;
			keyboard->lastRepeat = now;
			IN_NewEvent(action);
		}
		else if (T_HasElapsed(keyboard->repeatStarted, KEYBOARD_REPEAT_DELAY_US) &&
		         T_HasElapsed(keyboard->lastRepeat, KEYBOARD_REPEAT_PERIOD_US)) {
			keyboard->lastRepeat = now;
			IN_NewEvent(action);
		}
	}

}

static void keyboardPoll(void *data) {
	(void)data;
	USB_LockTopology();
	keyboardPollLocked();
	USB_UnlockTopology();
}

static int keyboardProbe(struct usbInterface *interface, const struct usbDeviceId *id) {
	struct usbKeyboard *keyboard = NULL;
	struct usbEndpoint *endpoint = NULL;
	uint i;
	int ret;
	(void)id;

	for (i = 0; i < interface->numEndpoints; i++) {
		if ((interface->endpoints[i].attributes & USB_ENDPOINT_XFER_MASK) == USB_ENDPOINT_XFER_INT && (interface->endpoints[i].address & USB_ENDPOINT_DIR_MASK)) {
			endpoint = &interface->endpoints[i];
			break;
		}
	}
	if (!endpoint)
		return -ENODEV;

	for (i = 0; i < MAX_USB_KEYBOARDS; i++) {
		if (!keyboards[i].interface) {
			keyboard = &keyboards[i];
			break;
		}
	}

	if (!keyboard)
		return -ENOSPC;

	ret = USB_ControlTransfer(interface->device,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		USB_HID_REQ_SET_PROTOCOL, USB_HID_PROTOCOL_BOOT,
		interface->descriptor.interfaceNumber, NULL, 0, USB_DEFAULT_TIMEOUT_US
	);

	if (ret < 0)
		return ret;

	/* infinite idle means reports are sent only when state changes */
	ret = USB_ControlTransfer(interface->device,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		USB_HID_REQ_SET_IDLE, 0, interface->descriptor.interfaceNumber,
		NULL, 0, USB_DEFAULT_TIMEOUT_US
	);
	if (ret < 0 && ret != -EPIPE)
		return ret;

	memset(keyboard, 0, sizeof(*keyboard));
	keyboard->interface = interface;
	keyboard->endpoint = endpoint;

	ret = USB_ResidentInArm(interface->device, endpoint, sizeof(keyboard->report));
	if (ret) {
		memset(keyboard, 0, sizeof(*keyboard));
		return ret;
	}
	interface->driverData = keyboard;
	log_printf("HID boot keyboard bound on bus %u address %u interface %u\r\n",
		interface->device->hc->bus, interface->device->address,
		interface->descriptor.interfaceNumber);

	return 0;
}

static void keyboardRemove(struct usbInterface *interface) {
	struct usbKeyboard *keyboard = interface->driverData;

	if (!keyboard)
		return;

	USB_ResidentInStop(interface->device, keyboard->endpoint);
	memset(keyboard, 0, sizeof(*keyboard));
	interface->driverData = NULL;
}

static int drhProbe(struct usbInterface *interface, const struct usbDeviceId *id) {
	struct usbDRHInterface *pad = NULL;
	struct usbEndpoint *endpoint = NULL;
	uint i;
	(void)id;
	if (interface->descriptor.interfaceClass != USB_CLASS_HID)
		return -ENODEV;

	for (i = 0; i < interface->numEndpoints; i++) {
		if ((interface->endpoints[i].attributes & USB_ENDPOINT_XFER_MASK) ==
		    USB_ENDPOINT_XFER_INT &&
		    (interface->endpoints[i].address & USB_ENDPOINT_DIR_MASK) &&
		    interface->endpoints[i].maxPacketSize >= DRH_REPORT_SIZE) {
			endpoint = &interface->endpoints[i];
			break;
		}
	}
	if (!endpoint)
		return -ENODEV;
	if (drh.device && drh.device != interface->device)
		return -ENODEV;
	for (i = 0; i < DRH_HID_INTERFACES; i++) {
		if (!drh.interfaces[i].interface) {
			pad = &drh.interfaces[i];
			break;
		}
	}
	if (!pad)
		return -ENOSPC;
	memset(pad, 0, sizeof(*pad));
	pad->interface = interface;
	pad->endpoint = endpoint;
	if (USB_ResidentInArm(interface->device, endpoint, DRH_REPORT_SIZE)) {
		memset(pad, 0, sizeof(*pad));
		return -EIO;
	}
	if (!drh.device) {
		drh.device = interface->device;
		drhInitializeMode();
	}
	if (drh.cdcIn && !drh.cdcArmed) {
		drh.cdcArmed = !USB_ResidentInArm(interface->device, drh.cdcIn, 1024);
		if (!drh.cdcArmed)
			log_puts("DRH CDC receive setup failed");
	}

	interface->driverData = pad;
	log_printf("DRH bound on bus %u address %u interface %u, endpoint %02x/%u\r\n",
		interface->device->hc->bus, interface->device->address,
		interface->descriptor.interfaceNumber, endpoint->address,
		endpoint->maxPacketSize);
	return 0;
}

static void drhRemove(struct usbInterface *interface) {
	struct usbDRHInterface *pad = interface->driverData;
	uint i;

	if (!pad)
		return;
	USB_ResidentInStop(interface->device, pad->endpoint);
	memset(pad, 0, sizeof(*pad));
	interface->driverData = NULL;

	for (i = 0; i < DRH_HID_INTERFACES; i++)
		if (drh.interfaces[i].interface)
			return;

	if (drh.cdcArmed)
		USB_ResidentInStop(interface->device, drh.cdcIn);
	memset(&drh, 0, sizeof(drh));
}

static const struct usbDeviceId keyboardIds[] = {
	{
		.interfaceClass = USB_CLASS_HID,
		.interfaceSubclass = 1,
		.interfaceProtocol = USB_PROTOCOL_HID_KEYBOARD,
		.matchFlags = USB_MATCH_INTERFACE,
	},
	{ 0 }
};

static struct usbDriver keyboardDriver = {
	.name = "USB HID keyboard",
	.ids = keyboardIds,
	.probe = keyboardProbe,
	.remove = keyboardRemove,
};

static const struct usbDeviceId drhIds[] = {
	{
		.vendor = USB_VENDOR_NINTENDO,
		.product = USB_PRODUCT_WIIU_DRH,
		.interfaceClass = USB_CLASS_HID,
		.interfaceSubclass = 0,
		.interfaceProtocol = 0,
		.matchFlags = USB_MATCH_VENDOR_PRODUCT | USB_MATCH_INTERFACE,
	},
	{ 0 }
};

static struct usbDriver drhDriver = {
	.name = "Nintendo Wii U DRH",
	.ids = drhIds,
	.probe = drhProbe,
	.remove = drhRemove,
};

static void usbHIDInit(void) {
	memset(keyboards, 0, sizeof(keyboards));
	memset(&drh, 0, sizeof(drh));
	if (USB_RegisterDriver(&drhDriver)) {
		usbHIDTopDriver.state = DRIVER_STATE_FAULTED;
		return;
	}
	if (USB_RegisterDriver(&keyboardDriver)) {
		USB_UnregisterDriver(&drhDriver);
		usbHIDTopDriver.state = DRIVER_STATE_FAULTED;
		return;
	}
	T_QueueRepeatingEvent(KEYBOARD_POLL_US, keyboardPoll, NULL);
	usbHIDTopDriver.state = DRIVER_STATE_READY;
}

static void usbHIDCleanup(void) {
	T_CancelRepeatingEvent(keyboardPoll, NULL);
	USB_UnregisterDriver(&keyboardDriver);
	USB_UnregisterDriver(&drhDriver);
	memset(keyboards, 0, sizeof(keyboards));
	memset(&drh, 0, sizeof(drh));
	usbHIDTopDriver.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(usbHIDTopDriver) = {
	.name = "USB HID",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_INPUT,
	.init = usbHIDInit,
	.cleanup = usbHIDCleanup,
};
