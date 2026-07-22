/*
 * NPLL - USB hub class driver
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "USB-HUB"

#include <errno.h>
#include <string.h>
#include <npll/drivers.h>
#include <npll/endian.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/usb.h>

#define USB_MAX_HUBS 8
#define USB_HUB_MAX_PORTS 15
#define USB_HUB_POLL_US 20000u
#define USB_HUB_TIMEOUT_US 100000u

struct usbHub {
	struct usbInterface *interface;
	struct usbEndpoint *interrupt;
	struct usbDevice *children[USB_HUB_MAX_PORTS];
	u8 numPorts;
	bool errorLogged;
};

static REGISTER_DRIVER(usbHubTopDriver);
static struct usbHub hubs[USB_MAX_HUBS];

static int hubControl(struct usbHub *hub, u8 type, u8 request, u16 value, u16 index, void *data, u16 length) {
	return USB_ControlTransfer(hub->interface->device, type, request, value, index, data, length, USB_DEFAULT_TIMEOUT_US);
}

static int hubPortStatus(struct usbHub *hub, uint port, struct usbHubPortStatus *status) {
	int ret;

	memset(status, 0, sizeof(*status));

	ret = hubControl(hub, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER, USB_REQ_GET_STATUS, 0, (u16)(port + 1u), status, sizeof(*status));
	if (ret != sizeof(*status))
		return ret < 0 ? ret : -EIO;

	status->status = npll_le16_to_cpu(status->status);
	status->change = npll_le16_to_cpu(status->change);

	return 0;
}

static int hubPortFeature(struct usbHub *hub, uint port, u16 feature, bool set) {
	int ret = hubControl(hub, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER,
		set ? USB_REQ_SET_FEATURE : USB_REQ_CLEAR_FEATURE, feature,
		(u16)(port + 1u), NULL, 0
	);

	return ret < 0 ? ret : 0;
}

static int hubResetPort(struct usbHub *hub, uint port, enum usbSpeed *speed) {
	struct usbHubPortStatus status;
	u64 start;
	int ret;

	ret = hubPortFeature(hub, port, USB_FEATURE_PORT_RESET, true);
	if (ret)
		return ret;

	start = mftb();
	do {
		udelay(10000);
		ret = hubPortStatus(hub, port, &status);
		if (ret)
			return ret;

		if ((status.status & USB_PORT_STAT_ENABLE) && !(status.status & USB_PORT_STAT_RESET))
			break;
	} while (!T_HasElapsed(start, 500000));

	if (!(status.status & USB_PORT_STAT_ENABLE))
		return -ETIMEDOUT;

	hubPortFeature(hub, port, USB_FEATURE_C_PORT_RESET, false);
	if (status.status & USB_PORT_STAT_HIGH_SPEED)
		*speed = USB_SPEED_HIGH;
	else if (status.status & USB_PORT_STAT_LOW_SPEED)
		*speed = USB_SPEED_LOW;
	else
		*speed = USB_SPEED_FULL;

	return 0;
}

static void hubDetachPort(struct usbHub *hub, uint port) {
	if (!hub->children[port])
		return;

	USB_DisconnectDevice(hub->children[port]);
	hub->children[port] = NULL;
	log_printf("bus %u hub %u port %u disconnected\r\n", hub->interface->device->hc->bus, hub->interface->device->address, port + 1u);
}

static void hubHandlePort(struct usbHub *hub, uint port) {
	struct usbHubPortStatus status;
	enum usbSpeed speed;
	int ret;

	ret = hubPortStatus(hub, port, &status);
	if (ret)
		return;

	if ((status.change & USB_PORT_STAT_C_CONNECTION) ||((status.status & USB_PORT_STAT_CONNECTION) && !hub->children[port])) {
		if (status.change & USB_PORT_STAT_C_CONNECTION)
			hubPortFeature(hub, port, USB_FEATURE_C_PORT_CONNECTION, false);

		if (!(status.status & USB_PORT_STAT_CONNECTION)) {
			hubDetachPort(hub, port);
			return;
		}
		if (hub->children[port])
			hubDetachPort(hub, port);

		udelay(100000);
		ret = hubResetPort(hub, port, &speed);
		if (ret) {
			log_printf("bus %u hub %u port %u reset failed: %d\r\n", hub->interface->device->hc->bus, hub->interface->device->address, port + 1, ret);
			return;
		}
		udelay(10000);

		ret = USB_EnumerateChild(hub->interface->device, port, speed, &hub->children[port]);
		if (ret)
			log_printf("bus %u hub %u port %u enumeration failed: %d\r\n", hub->interface->device->hc->bus, hub->interface->device->address, port + 1, ret);
	}

	if (status.change & USB_PORT_STAT_C_ENABLE)
		(void)hubPortFeature(hub, port, USB_FEATURE_C_PORT_ENABLE, false);
	if (status.change & USB_PORT_STAT_C_RESET)
		(void)hubPortFeature(hub, port, USB_FEATURE_C_PORT_RESET, false);
}

static void hubPoll(void *data) {
	u8 bitmap[4] ALIGN(32);
	struct usbHub *hub;
	u32 actual;
	uint h, port;
	int ret;

	(void)data;
	for (h = 0; h < USB_MAX_HUBS; h++) {
		hub = &hubs[h];
		if (!hub->interface || !hub->interface->device->connected)
			continue;

		memset(bitmap, 0, sizeof(bitmap));
		actual = 0;
		ret = USB_InterruptTransfer(hub->interface->device, hub->interrupt, bitmap, (hub->numPorts + 8u) / 8u, &actual, USB_HUB_TIMEOUT_US);
		if (ret == -ETIMEDOUT)
			continue;

		if (ret) {
			if (!hub->errorLogged)
				log_printf("hub interrupt transfer failed: %d\r\n", ret);
			hub->errorLogged = true;
			continue;
		}

		hub->errorLogged = false;
		for (port = 0; port < hub->numPorts; port++) {
			if (bitmap[(port + 1u) / 8u] & BIT((port + 1u) & 7u))
				hubHandlePort(hub, port);
		}
	}
}

static int hubProbe(struct usbInterface *interface, const struct usbDeviceId *id) {
	struct usbHubDescriptor descriptor;
	struct usbHub *hub = NULL;
	struct usbEndpoint *interrupt = NULL;
	uint i;
	int ret;
	(void)id;

	for (i = 0; i < interface->numEndpoints; i++) {
		if ((interface->endpoints[i].attributes & USB_ENDPOINT_XFER_MASK) == USB_ENDPOINT_XFER_INT && (interface->endpoints[i].address & USB_ENDPOINT_DIR_MASK)) {
			interrupt = &interface->endpoints[i];
			break;
		}
	}

	if (!interrupt)
		return -ENODEV;

	for (i = 0; i < USB_MAX_HUBS; i++) {
		if (!hubs[i].interface) {
			hub = &hubs[i];
			break;
		}
	}

	if (!hub)
		return -ENOSPC;

	memset(&descriptor, 0, sizeof(descriptor));
	memset(hub, 0, sizeof(*hub));
	hub->interface = interface;
	hub->interrupt = interrupt;

	ret = hubControl(hub, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE,
		USB_REQ_GET_DESCRIPTOR, USB_DT_HUB << 8, 0, &descriptor,
		sizeof(descriptor)
	);
	if (ret != sizeof(descriptor) || descriptor.descriptorType != USB_DT_HUB || !descriptor.numPorts || descriptor.numPorts > USB_HUB_MAX_PORTS) {
		memset(hub, 0, sizeof(*hub));
		return ret < 0 ? ret : -EIO;
	}

	hub->numPorts = descriptor.numPorts;
	for (i = 0; i < hub->numPorts; i++) {
		ret = hubPortFeature(hub, i, USB_FEATURE_PORT_POWER, true);
		if (ret) {
			memset(hub, 0, sizeof(*hub));
			return ret;
		}
	}
	udelay((u32)descriptor.powerOnToPowerGood * 2000u);
	interface->driverData = hub;
	log_printf("hub bound on bus %u address %u, %u ports\r\n", interface->device->hc->bus, interface->device->address, hub->numPorts);

	/*
	 * A child may have been attached before this hub was configured, in
	 * which case relying only on the interrupt change bitmap can miss it.
	 */

	for (i = 0; i < hub->numPorts; i++)
		hubHandlePort(hub, i);

	return 0;
}

static void hubRemove(struct usbInterface *interface) {
	struct usbHub *hub = interface->driverData;
	uint port;

	if (!hub)
		return;
	for (port = 0; port < hub->numPorts; port++)
		hubDetachPort(hub, port);

	memset(hub, 0, sizeof(*hub));
	interface->driverData = NULL;
}

static const struct usbDeviceId hubIds[] = {{
	.interfaceClass = USB_CLASS_HUB,
	.interfaceSubclass = 0,
	.interfaceProtocol = 1,
	.matchFlags = USB_MATCH_INTERFACE,
}, { 0 }};

static struct usbDriver hubDriver = {
	.name = "USB Hub",
	.ids = hubIds,
	.probe = hubProbe,
	.remove = hubRemove,
};

static void usbHubInit(void) {
	memset(hubs, 0, sizeof(hubs));
	if (USB_RegisterDriver(&hubDriver)) {
		usbHubTopDriver.state = DRIVER_STATE_FAULTED;
		return;
	}
	T_QueueRepeatingEvent(USB_HUB_POLL_US, hubPoll, NULL);
	usbHubTopDriver.state = DRIVER_STATE_READY;
}

static void usbHubCleanup(void) {
	T_CancelRepeatingEvent(hubPoll, NULL);
	USB_UnregisterDriver(&hubDriver);
	memset(hubs, 0, sizeof(hubs));
	usbHubTopDriver.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(usbHubTopDriver) = {
	.name = "USB hubs",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_OTHER,
	.init = usbHubInit,
	.cleanup = usbHubCleanup,
};
