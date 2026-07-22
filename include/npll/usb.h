/*
 * NPLL - synchronous USB host core
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _NPLL_USB_H
#define _NPLL_USB_H

#include <npll/types.h>
#include <npll/usb/protocol.h>

#define USB_MAX_CONTROLLERS 7
#define USB_MAX_DEVICES     32
#define USB_MAX_INTERFACES  16
#define USB_MAX_ENDPOINTS   16
#define USB_MAX_DEPTH       5
#define USB_MAX_CONFIG_LENGTH 2048
#define USB_DEFAULT_TIMEOUT_US 5000000u
#define USB_POLL_PERIOD_US  10000u

enum usbSpeed {
	USB_SPEED_LOW,
	USB_SPEED_FULL,
	USB_SPEED_HIGH
};
enum usbTransferType {
	USB_TRANSFER_CONTROL,
	USB_TRANSFER_ISOCHRONOUS,
	USB_TRANSFER_BULK,
	USB_TRANSFER_INTERRUPT
};
enum usbTransferStatus {
	USB_TRANSFER_PENDING,
	USB_TRANSFER_COMPLETE,
	USB_TRANSFER_STALL,
	USB_TRANSFER_TIMEOUT,
	USB_TRANSFER_DISCONNECTED,
	USB_TRANSFER_ERROR
};

struct usbHostController;
struct usbDevice;
struct usbInterface;
struct usbDriver;

struct usbEndpoint {
	u8 address;
	u8 attributes;
	u16 maxPacketSize;
	u8 interval;
	u8 toggle;
	void *hcData;
};

struct usbTransfer {
	struct usbDevice *device;
	struct usbEndpoint *endpoint;
	const struct usbSetupPacket *setup;
	void *data;
	u32 length;
	u32 actualLength;
	u32 timeoutUsecs;
	enum usbTransferStatus status;
	void *hcData;
};

struct usbInterface {
	struct usbDevice *device;
	struct usbInterfaceDescriptor descriptor;
	uint numEndpoints;
	struct usbEndpoint endpoints[USB_MAX_ENDPOINTS];
	const u8 *extra;
	u16 extraLength;
	struct usbDriver *driver;
	void *driverData;
};

struct usbDevice {
	struct usbHostController *hc;
	struct usbDevice *parent;
	struct usbDevice *next;
	u8 address, port, depth, configuration;
	u8 ttHubAddress, ttPort;
	enum usbSpeed speed;
	bool connected;
	struct usbDeviceDescriptor descriptor;
	u8 *configurationData;
	u16 configurationLength;
	uint numInterfaces;
	struct usbInterface interfaces[USB_MAX_INTERFACES];
};

struct usbRootPortStatus {
	bool connected, enabled, changed;
	enum usbSpeed speed;
};

struct usbHostControllerOps {
	int (*start)(struct usbHostController *hc);
	void (*stop)(struct usbHostController *hc);
	void (*poll)(struct usbHostController *hc);
	int (*transfer)(struct usbHostController *hc, struct usbTransfer *transfer);
	int (*cancel)(struct usbHostController *hc, struct usbTransfer *transfer);
	/*
	 * Resident interrupt-IN endpoints.  interruptArm links a QH/ED into the
	 * hardware periodic schedule and returns immediately; the controller then
	 * polls the device on its own.  interruptPoll is a non-blocking peek at the
	 * DMA result (see USB_InterruptPoll for return values).  interruptStop
	 * unlinks and frees the endpoint's controller state.
	 */
	int (*interruptArm)(struct usbHostController *hc, struct usbDevice *dev, struct usbEndpoint *ep, u32 length);
	int (*interruptPoll)(struct usbHostController *hc, struct usbEndpoint *ep, void *data, u32 length, u32 *actual);
	void (*interruptStop)(struct usbHostController *hc, struct usbEndpoint *ep);
	int (*rootPortStatus)(struct usbHostController *hc, uint port, struct usbRootPortStatus *status);
	int (*rootPortReset)(struct usbHostController *hc, uint port, enum usbSpeed *speed);
	void (*rootPortClearChange)(struct usbHostController *hc, uint port);
};

struct usbHostController {
	const char *name;
	uint bus, numPorts;
	uintptr_t mmioBase;
	const struct usbHostControllerOps *ops;
	void *priv;
	bool running;
	u16 knownPorts;
	u16 connectedPorts;
	struct usbDevice *rootDevices[15];
	struct usbHostController *next;
};

#define USB_MATCH_VENDOR_PRODUCT (1u << 0)
#define USB_MATCH_INTERFACE      (1u << 1)
struct usbDeviceId {
	u16 vendor, product;
	u8 interfaceClass, interfaceSubclass, interfaceProtocol;
	u8 matchFlags;
};

struct usbDriver {
	const char *name;
	const struct usbDeviceId *ids;
	int (*probe)(struct usbInterface *interface, const struct usbDeviceId *id);
	void (*remove)(struct usbInterface *interface);
	struct usbDriver *next;
};

extern void USB_Init(void);
extern void USB_Start(void);
extern void USB_Shutdown(void);
extern void USB_Poll(void);
extern int USB_RegisterHostController(struct usbHostController *hc);
extern void USB_UnregisterHostController(struct usbHostController *hc);
extern int USB_RegisterDriver(struct usbDriver *driver);
extern void USB_UnregisterDriver(struct usbDriver *driver);
extern int USB_SubmitTransfer(struct usbTransfer *transfer);
extern int USB_ControlTransfer(struct usbDevice *dev, u8 type, u8 request, u16 value, u16 index, void *data, u16 length, u32 timeoutUsecs);
extern int USB_BulkTransfer(struct usbDevice *dev, struct usbEndpoint *ep, void *data, u32 length, u32 *actual, u32 timeoutUsecs);
/*
 * Non-blocking interrupt-IN polling.  Arm once (typically at probe), then call
 * USB_InterruptPoll from a timed-event callback: the CPU never busy-waits on the
 * bus, so an idle keyboard or hub costs almost nothing.  Call USB_InterruptStop
 * from the driver's remove().
 *
 * USB_InterruptPoll returns:
 *    1  a new report arrived; up to `length` bytes copied to `data`, count in
 *       *actual.  The endpoint is automatically re-armed for the next report.
 *    0  armed, nothing new yet (the common idle case).
 *  -EPIPE  endpoint halted; caller should USB_ClearHalt then USB_InterruptArm.
 *  <0  other fatal error.
 */
extern int USB_InterruptArm(struct usbDevice *dev, struct usbEndpoint *ep, u32 length);
extern int USB_InterruptPoll(struct usbDevice *dev, struct usbEndpoint *ep, void *data, u32 length, u32 *actual);
extern void USB_InterruptStop(struct usbDevice *dev, struct usbEndpoint *ep);
extern int USB_ClearHalt(struct usbDevice *dev, struct usbEndpoint *ep);
extern int USB_EnumerateChild(struct usbDevice *parent, uint port,
	enum usbSpeed speed, struct usbDevice **child);
extern void USB_DisconnectDevice(struct usbDevice *dev);

#endif /* _NPLL_USB_H */
