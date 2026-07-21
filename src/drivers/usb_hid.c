/*
 * NPLL - USB HID keyboard support
 *
 * Copyright (C) 2026 Techflash
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
#define KEYBOARD_POLL_US  10000u
#define KEYBOARD_TIMEOUT_US 30000u
#define KEYBOARD_REPEAT_DELAY_US 400000u
#define KEYBOARD_REPEAT_PERIOD_US 100000u

struct usbKeyboard {
	struct usbInterface *interface;
	struct usbEndpoint *endpoint;
	u8 report[8];
	u8 repeatKey;
	u64 repeatStarted;
	u64 lastRepeat;
	bool errorLogged;
};

static REGISTER_DRIVER(usbHIDTopDriver);
static struct usbKeyboard keyboards[MAX_USB_KEYBOARDS];

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

static void keyboardPoll(void *data) {
	struct usbKeyboard *keyboard;
	inputEvent_t action;
	u8 report[8], key;
	u64 now;
	u32 actual;
	int ret;
	uint i;
	(void)data;

	for (i = 0; i < MAX_USB_KEYBOARDS; i++) {
		keyboard = &keyboards[i];
		if (!keyboard->interface || !keyboard->interface->device->connected)
			continue;

		memset(report, 0, sizeof(report));
		actual = 0;

		ret = USB_InterruptTransfer(keyboard->interface->device, keyboard->endpoint, report, sizeof(report), &actual, KEYBOARD_TIMEOUT_US);
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

static void usbHIDInit(void) {
	memset(keyboards, 0, sizeof(keyboards));
	if (USB_RegisterDriver(&keyboardDriver)) {
		usbHIDTopDriver.state = DRIVER_STATE_FAULTED;
		return;
	}
	T_QueueRepeatingEvent(KEYBOARD_POLL_US, keyboardPoll, NULL);
	usbHIDTopDriver.state = DRIVER_STATE_READY;
}

static void usbHIDCleanup(void) {
	T_CancelRepeatingEvent(keyboardPoll, NULL);
	USB_UnregisterDriver(&keyboardDriver);
	memset(keyboards, 0, sizeof(keyboards));
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
