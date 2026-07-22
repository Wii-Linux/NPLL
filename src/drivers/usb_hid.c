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
#define MAX_USB_DRH 2
#define KEYBOARD_POLL_US  10000u
#define KEYBOARD_TIMEOUT_US 30000u
#define KEYBOARD_ERROR_RETRIES 3u
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

struct usbDRH {
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
static struct usbDRH drhs[MAX_USB_DRH];

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

static int hidReadReport(struct usbInterface *interface, struct usbEndpoint *endpoint, void *report, u32 length, u32 *actual) {
	int ret = -EIO;
	uint attempt;

	for (attempt = 0; attempt < KEYBOARD_ERROR_RETRIES; attempt++) {
		*actual = 0;
		ret = USB_InterruptTransfer(interface->device, endpoint, report, length, actual, KEYBOARD_TIMEOUT_US);
		if (!ret || ret == -ETIMEDOUT)
			return ret;

		if (ret == -EPIPE) {
			ret = USB_ClearHalt(interface->device, endpoint);
			if (ret)
				return ret;
		}

		/*
		 * A changed HID report remains queued until ACKed.  Retry transient
		 * transaction/toggle errors while that report is still available.
		 */
		udelay(1000);
	}
	return ret;
}

static int keyboardReadReport(struct usbKeyboard *keyboard, u8 *report, u32 *actual) {
	return hidReadReport(keyboard->interface, keyboard->endpoint, report, sizeof(keyboard->report), actual);
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
static void drhParseReport(struct usbDRH *drh, const u8 *data) {
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

	pressed = (next.buttons & ~drh->state.buttons) & DRH_MENU_BUTTONS;
	button = drhFirstButton(pressed);
	if (button) {
		IN_NewEvent(drhButtonAction(button));
		drh->repeatButton = button;
		drh->repeatStarted = now;
		drh->lastRepeat = now;
	}
	else if (!(next.buttons & drh->repeatButton)) {
		drh->repeatButton = drhFirstButton(next.buttons & DRH_MENU_BUTTONS);
		drh->repeatStarted = now;
		drh->lastRepeat = now;
	}
	else if (drh->repeatButton != DRH_BTN_A &&
	         T_HasElapsed(drh->repeatStarted, KEYBOARD_REPEAT_DELAY_US) &&
	         T_HasElapsed(drh->lastRepeat, KEYBOARD_REPEAT_PERIOD_US)) {
		drh->lastRepeat = now;
		IN_NewEvent(drhButtonAction(drh->repeatButton));
	}
	drh->state = next;
}

static void drhPoll(void) {
	struct usbDRH *drh;
	u8 report[DRH_REPORT_SIZE] ALIGN(32);
	u32 actual;
	int ret;
	uint i;

	for (i = 0; i < MAX_USB_DRH; i++) {
		drh = &drhs[i];
		if (!drh->interface || !drh->interface->device->connected)
			continue;

		memset(report, 0, sizeof(report));
		actual = 0;
		ret = hidReadReport(drh->interface, drh->endpoint, report, sizeof(report), &actual);

		if (ret == -ETIMEDOUT)
			continue;

		if (ret) {
			if (!drh->errorLogged) {
				log_printf("DRH interrupt transfer failed: %d\r\n", ret);
				drh->errorLogged = true;
			}
			continue;
		}

		drh->errorLogged = false;
		if (actual == sizeof(report))
			drhParseReport(drh, report);
	}
}

static void keyboardPoll(void *data) {
	struct usbKeyboard *keyboard;
	inputEvent_t action;
	u8 report[8], key;
	u64 now;
	u32 actual;
	int ret;
	uint i;
	(void)data;
	drhPoll();

	for (i = 0; i < MAX_USB_KEYBOARDS; i++) {
		keyboard = &keyboards[i];
		if (!keyboard->interface || !keyboard->interface->device->connected)
			continue;

		memset(report, 0, sizeof(report));
		actual = 0;
		ret = keyboardReadReport(keyboard, report, &actual);
		if (ret == -ETIMEDOUT) {
			now = mftb();
			if (keyboard->repeatKey &&
			    T_HasElapsed(keyboard->repeatStarted, KEYBOARD_REPEAT_DELAY_US) &&
			    T_HasElapsed(keyboard->lastRepeat, KEYBOARD_REPEAT_PERIOD_US)) {
				keyboard->lastRepeat = now;
				IN_NewEvent(keyAction(keyboard->repeatKey));
			}
			continue;
		}
		if (ret) {
			if (!keyboard->errorLogged) {
				log_printf("keyboard interrupt transfer failed: %d\r\n", ret);
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

	memset(keyboard, 0, sizeof(*keyboard));
	interface->driverData = NULL;
}

static int drhProbe(struct usbInterface *interface, const struct usbDeviceId *id) {
	struct usbDRH *drh = NULL;
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
	for (i = 0; i < MAX_USB_DRH; i++) {
		if (!drhs[i].interface) {
			drh = &drhs[i];
			break;
		}
	}
	if (!drh)
		return -ENOSPC;
	memset(drh, 0, sizeof(*drh));
	drh->interface = interface;
	drh->endpoint = endpoint;
	interface->driverData = drh;
	log_printf("DRH bound on bus %u address %u interface %u, endpoint %02x/%u\r\n",
		interface->device->hc->bus, interface->device->address,
		interface->descriptor.interfaceNumber, endpoint->address,
		endpoint->maxPacketSize);
	return 0;
}

static void drhRemove(struct usbInterface *interface) {
	struct usbDRH *drh = interface->driverData;
	if (!drh)
		return;
	memset(drh, 0, sizeof(*drh));
	interface->driverData = NULL;
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
		.matchFlags = USB_MATCH_VENDOR_PRODUCT,
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
	memset(drhs, 0, sizeof(drhs));
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
	memset(drhs, 0, sizeof(drhs));
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
