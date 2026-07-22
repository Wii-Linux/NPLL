/*
 * NPLL - synchronous USB host core
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "USB"

#include <errno.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/endian.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/usb.h>

static struct usbHostController *controllers;
static struct usbDriver *drivers;
static struct usbDevice devices[USB_MAX_DEVICES];
static u8 configurationData[USB_MAX_DEVICES][USB_MAX_CONFIG_LENGTH] ALIGN(32);
static bool initialized, started;

static void bindInterface(struct usbInterface *intf);

static void pollEvent(void *data) {
	(void)data;
	USB_Poll();
}

static int transferError(enum usbTransferStatus status) {
	switch (status) {
	case USB_TRANSFER_COMPLETE: return 0;
	case USB_TRANSFER_STALL: return -EPIPE;
	case USB_TRANSFER_TIMEOUT: return -ETIMEDOUT;
	case USB_TRANSFER_DISCONNECTED: return -ENODEV;
	default: return -EIO;
	}
}

static bool usbDMAInMEM1(const void *data) {
	return (u32)(uintptr_t)virtToPhys(data) < MEM2_PHYS_BASE;
}

static bool usbNeedsBounce(const void *data, u32 length) {
	if (!length)
		return false;

	/*
	 * Hollywood/Latte DMA buffers must start on a 32-byte boundary.  Keep
	 * both directions out of MEM1: IN has a known partial-word write loss,
	 * while direct OUT from a packed object can share a cache line with live
	 * stack data.  A dedicated MEM2 allocation avoids both hazards.
	 */
	return !ptrAligned(data, 32) || usbDMAInMEM1(data);
}

static struct usbDevice *allocateDevice(struct usbHostController *hc, uint port,
	enum usbSpeed speed, struct usbDevice *parent) {
	uint i;

	for (i = 0; i < USB_MAX_DEVICES; i++) {
		if (devices[i].connected)
			continue;

		memset(&devices[i], 0, sizeof(devices[i]));
		devices[i].hc = hc;
		devices[i].port = (u8)port;
		devices[i].parent = parent;
		devices[i].depth = parent ? (u8)(parent->depth + 1u) : 0;
		devices[i].speed = speed;
		if (parent && parent->speed == USB_SPEED_HIGH && speed != USB_SPEED_HIGH) {
			devices[i].ttHubAddress = parent->address;
			devices[i].ttPort = (u8)(port + 1u);
		}
		else if (parent && parent->ttHubAddress) {
			devices[i].ttHubAddress = parent->ttHubAddress;
			devices[i].ttPort = parent->ttPort;
		}
		devices[i].connected = true;
		devices[i].descriptor.maxPacketSize0 = 8;
		return &devices[i];
	}
	return NULL;
}

static u8 allocateAddress(struct usbHostController *hc) {
	uint address, i;
	bool used;

	for (address = 1; address < 128; address++) {
		used = false;
		for (i = 0; i < USB_MAX_DEVICES; i++) {
			if (devices[i].connected && devices[i].hc == hc && devices[i].address == address) {
				used = true;
				break;
			}
		}
		if (!used)
			return (u8)address;
	}
	return 0;
}

static void disconnectDevice(struct usbDevice *dev) {
	uint i;

	if (!dev || !dev->connected)
		return;

	dev->connected = false;
	for (i = 0; i < USB_MAX_DEVICES; i++)
		if (devices[i].connected && devices[i].parent == dev)
			disconnectDevice(&devices[i]);
	for (i = 0; i < dev->numInterfaces; i++) {
		if (dev->interfaces[i].driver) {
			dev->interfaces[i].driver->remove(&dev->interfaces[i]);
			dev->interfaces[i].driver = NULL;
		}
	}
}

void USB_DisconnectDevice(struct usbDevice *dev) {
	disconnectDevice(dev);
}

static int parseConfiguration(struct usbDevice *dev, u16 totalLength) {
	struct usbConfigurationDescriptor *config;
	struct usbDescriptorHeader *header;
	struct usbInterfaceDescriptor *interfaceDesc;
	struct usbEndpointDescriptor *endpointDesc;
	struct usbInterface *current = NULL;
	u16 offset, next;

	if (!dev || !dev->configurationData || totalLength < sizeof(*config))
		return -EINVAL;

	config = (struct usbConfigurationDescriptor *)dev->configurationData;
	if (config->length < sizeof(*config) || config->descriptorType != USB_DT_CONFIGURATION ||
	    npll_le16_to_cpu(config->totalLength) != totalLength || !config->configurationValue) {
		log_printf("bus %u address %u: bad config header %u/%u/%u/%u expected %u\r\n",
			dev->hc->bus, dev->address, config->length, config->descriptorType,
			npll_le16_to_cpu(config->totalLength), config->configurationValue,
			totalLength);
		return -EIO;
	}

	dev->numInterfaces = 0;
	offset = config->length;
	while (offset < totalLength) {
		if ((u16)(totalLength - offset) < sizeof(*header)) {
			log_printf("bus %u address %u: descriptor tail at %u/%u\r\n",
				dev->hc->bus, dev->address, offset, totalLength);
			return -EIO;
		}

		header = (struct usbDescriptorHeader *)(dev->configurationData + offset);
		if (header->length < sizeof(*header) || header->length > (u16)(totalLength - offset)) {
			log_printf("bus %u address %u: bad descriptor at %u: %u/%u, remain %u\r\n",
				dev->hc->bus, dev->address, offset, header->length,
				header->descriptorType, totalLength - offset);
			return -EIO;
		}

		next = (u16)(offset + header->length);

		if (header->descriptorType == USB_DT_INTERFACE) {
			if (header->length < sizeof(*interfaceDesc)) {
				log_printf("bus %u address %u: short interface at %u: %u\r\n",
					dev->hc->bus, dev->address, offset, header->length);
				return -EIO;
			}

			if (current) {
				current->extraLength = (u16)(offset - (u16)(current->extra - dev->configurationData));
			}
			interfaceDesc = (struct usbInterfaceDescriptor *)header;
			current = NULL;

			/*
			 * Device drivers bind the default alternate setting.  Other
			 * settings remain available in the retained descriptor blob.
			 */
			if (!interfaceDesc->alternateSetting) {
				if (dev->numInterfaces >= USB_MAX_INTERFACES) {
					log_printf("bus %u address %u: more than %u interfaces\r\n",
						dev->hc->bus, dev->address, USB_MAX_INTERFACES);
					return -ENOSPC;
				}

				current = &dev->interfaces[dev->numInterfaces++];
				memset(current, 0, sizeof(*current));
				current->device = dev;
				memcpy(&current->descriptor, interfaceDesc, sizeof(*interfaceDesc));
				current->extra = dev->configurationData + next;
			}
		}
		else if (header->descriptorType == USB_DT_ENDPOINT && current) {
			if (header->length < sizeof(*endpointDesc)) {
				log_printf("bus %u address %u: short endpoint at %u: %u\r\n",
					dev->hc->bus, dev->address, offset, header->length);
				return -EIO;
			}

			if (current->numEndpoints >= USB_MAX_ENDPOINTS) {
				log_printf("bus %u address %u interface %u: more than %u endpoints\r\n",
					dev->hc->bus, dev->address,
					current->descriptor.interfaceNumber, USB_MAX_ENDPOINTS);
				return -ENOSPC;
			}

			endpointDesc = (struct usbEndpointDescriptor *)header;

			/*
			 * Endpoint counts and entries are not trusted for allocation.
			 * Ignore unusable entries while retaining the raw blob for a
			 * device-specific driver which may understand the quirk.
			 */
			if (!(endpointDesc->endpointAddress & USB_ENDPOINT_NUMBER_MASK)) {
				offset = next;
				continue;
			}

			current->endpoints[current->numEndpoints].address = endpointDesc->endpointAddress;
			current->endpoints[current->numEndpoints].attributes = endpointDesc->attributes;
			current->endpoints[current->numEndpoints].maxPacketSize = npll_le16_to_cpu(endpointDesc->maxPacketSize) & 0x7ffu;
			current->endpoints[current->numEndpoints].interval = endpointDesc->interval;
			if (!current->endpoints[current->numEndpoints].maxPacketSize) {
				offset = next;
				continue;
			}
			current->numEndpoints++;
		}
		offset = next;
	}
	if (current)
		current->extraLength = (u16)(totalLength - (u16)(current->extra - dev->configurationData));

	if (!dev->numInterfaces)
		return -ENODEV;

	dev->configuration = config->configurationValue;
	dev->configurationLength = totalLength;
	return 0;
}

static int configureDevice(struct usbDevice *dev) {
	u8 headerBuffer[32] ALIGN(32);
	struct usbConfigurationDescriptor *header;
	u16 totalLength;
	uint deviceIndex, i;
	int ret;

	memset(headerBuffer, 0, sizeof(headerBuffer));
	header = (struct usbConfigurationDescriptor *)headerBuffer;
	ret = USB_ControlTransfer(dev, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
		USB_REQ_GET_DESCRIPTOR, (u16)(USB_DT_CONFIGURATION << 8), 0,
		header, sizeof(*header), USB_DEFAULT_TIMEOUT_US
	);
	if (ret != (int)sizeof(*header) || header->length < sizeof(*header) || header->descriptorType != USB_DT_CONFIGURATION)
		return ret < 0 ? ret : -EIO;

	totalLength = npll_le16_to_cpu(header->totalLength);
	if (totalLength < sizeof(*header) || totalLength > USB_MAX_CONFIG_LENGTH)
		return -EMSGSIZE;

	deviceIndex = (uint)(dev - devices);
	if (deviceIndex >= USB_MAX_DEVICES)
		return -EINVAL;

	dev->configurationData = configurationData[deviceIndex];
	memset(dev->configurationData, 0, USB_MAX_CONFIG_LENGTH);

	ret = USB_ControlTransfer(dev, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
		USB_REQ_GET_DESCRIPTOR, (u16)(USB_DT_CONFIGURATION << 8), 0,
		dev->configurationData, totalLength, USB_DEFAULT_TIMEOUT_US
	);
	if (ret != totalLength)
		return ret < 0 ? ret : -EIO;

	ret = parseConfiguration(dev, totalLength);
	if (ret) {
		log_printf("bus %u address %u: configuration descriptor rejected: %d\r\n", dev->hc->bus, dev->address, ret);
		return ret;
	}

	ret = USB_ControlTransfer(dev, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
		USB_REQ_SET_CONFIGURATION, dev->configuration, 0, NULL, 0,
		USB_DEFAULT_TIMEOUT_US
	);
	if (ret < 0) {
		log_printf("bus %u address %u: SET_CONFIGURATION %u failed: %d\r\n", dev->hc->bus, dev->address, dev->configuration, ret);
		return ret;
	}
	for (i = 0; i < dev->numInterfaces; i++)
		bindInterface(&dev->interfaces[i]);

	return 0;
}

static int enumerateDevice(struct usbHostController *hc, struct usbDevice *parent, uint port, enum usbSpeed speed, struct usbDevice **result) {
	struct usbDevice *dev;
	u8 address, descriptorBuffer[32] ALIGN(32);
	int ret;
	uint attempt, i;

	if (parent && parent->depth + 1u >= USB_MAX_DEPTH)
		return -ELOOP;

	dev = allocateDevice(hc, port, speed, parent);
	if (!dev)
		return -ENOSPC;

	if (result)
		*result = dev;

	ret = -EIO;
	for (attempt = 0; attempt < 3; attempt++) {
		memset(descriptorBuffer, 0, sizeof(descriptorBuffer));
		ret = USB_ControlTransfer(dev,
			USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
			USB_REQ_GET_DESCRIPTOR, (u16)(USB_DT_DEVICE << 8), 0,
			descriptorBuffer, 8, USB_DEFAULT_TIMEOUT_US
		);

		if (ret == 8)
			break;

		/*
		 * Keep reset-race recovery local and bounded so one device cannot
		 * hold up polling of the other buses.
		 */
		udelay(10000u);
	}
	if (ret == 8)
		memcpy(&dev->descriptor, descriptorBuffer, 8);

	if (ret != 8 || dev->descriptor.length < sizeof(dev->descriptor) ||
	    dev->descriptor.descriptorType != USB_DT_DEVICE ||
	    (dev->descriptor.maxPacketSize0 != 8 && dev->descriptor.maxPacketSize0 != 16 &&
	     dev->descriptor.maxPacketSize0 != 32 && dev->descriptor.maxPacketSize0 != 64)) {
		log_printf("bus %u port %u: descriptor-8 failed: %d (%u/%u/%u)\r\n",
			hc->bus, port + 1, ret, dev->descriptor.length,
			dev->descriptor.descriptorType, dev->descriptor.maxPacketSize0);

		ret = ret < 0 ? ret : -EIO;
		goto fail;
	}

	address = allocateAddress(hc);
	if (!address) {
		ret = -ENOSPC;
		goto fail;
	}

	ret = USB_ControlTransfer(dev, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE, USB_REQ_SET_ADDRESS, address, 0, NULL, 0, USB_DEFAULT_TIMEOUT_US);
	if (ret < 0) {
		log_printf("bus %u port %u: SET_ADDRESS %u failed: %d\r\n", hc->bus, port + 1, address, ret);
		goto fail;
	}

	dev->address = address;
	udelay(2000u);

	memset(descriptorBuffer, 0, sizeof(descriptorBuffer));
	ret = USB_ControlTransfer(dev, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
		USB_REQ_GET_DESCRIPTOR, (u16)(USB_DT_DEVICE << 8), 0,
		descriptorBuffer, sizeof(dev->descriptor), USB_DEFAULT_TIMEOUT_US
	);

	if (ret == (int)sizeof(dev->descriptor))
		memcpy(&dev->descriptor, descriptorBuffer, sizeof(dev->descriptor));

	if (ret != (int)sizeof(dev->descriptor) ||
	    dev->descriptor.length != sizeof(dev->descriptor) ||
	    dev->descriptor.descriptorType != USB_DT_DEVICE) {
		log_printf("bus %u address %u: full device descriptor failed: ret %d, length %u, type %u\r\n",
			hc->bus, dev->address, ret, dev->descriptor.length, dev->descriptor.descriptorType
		);
		ret = ret < 0 ? ret : -EIO;
		goto fail;
	}

	dev->descriptor.usbVersion = npll_le16_to_cpu(dev->descriptor.usbVersion);
	dev->descriptor.vendorId = npll_le16_to_cpu(dev->descriptor.vendorId);
	dev->descriptor.productId = npll_le16_to_cpu(dev->descriptor.productId);
	dev->descriptor.deviceVersion = npll_le16_to_cpu(dev->descriptor.deviceVersion);
	log_printf("bus %u address %u: device %04x:%04x, USB %x.%02x\r\n",
		hc->bus, dev->address, dev->descriptor.vendorId, dev->descriptor.productId,
		dev->descriptor.usbVersion >> 8, dev->descriptor.usbVersion & 0xffu
	);
	ret = configureDevice(dev);
	if (ret)
		goto fail;

	log_printf("bus %u address %u: configuration %u, %u interfaces\r\n",
		hc->bus, dev->address, dev->configuration, dev->numInterfaces
	);

	for (i = 0; i < dev->numInterfaces; i++) {
		log_printf("bus %u address %u interface %u: class %02x/%02x/%02x, %u endpoints\r\n",
			hc->bus, dev->address, dev->interfaces[i].descriptor.interfaceNumber,
			dev->interfaces[i].descriptor.interfaceClass,
			dev->interfaces[i].descriptor.interfaceSubclass,
			dev->interfaces[i].descriptor.interfaceProtocol,
			dev->interfaces[i].numEndpoints
		);

		if (!dev->interfaces[i].driver) {
			log_printf("bus %u address %u interface %u: no matching driver, ignored\r\n",
				hc->bus, dev->address, dev->interfaces[i].descriptor.interfaceNumber
			);
		}
	}
	return 0;

fail:
	if (result)
		*result = NULL;
	disconnectDevice(dev);
	return ret;
}

int USB_EnumerateChild(struct usbDevice *parent, uint port,
	enum usbSpeed speed, struct usbDevice **child) {
	if (!parent || !parent->connected || !child || port >= 255u)
		return -EINVAL;
	return enumerateDevice(parent->hc, parent, port, speed, child);
}

void USB_Init(void) {
	controllers = NULL;
	drivers = NULL;
	memset(devices, 0, sizeof(devices));
	memset(configurationData, 0, sizeof(configurationData));
	initialized = true;
	started = false;
}

int USB_RegisterHostController(struct usbHostController *hc) {
	struct usbHostController *cur;
	uint count = 0;

	if (!initialized || !hc || !hc->name || !hc->ops || !hc->ops->start ||
	    !hc->ops->stop || !hc->ops->transfer || !hc->ops->rootPortStatus ||
	    !hc->ops->rootPortReset || !hc->numPorts)
		return -EINVAL;

	for (cur = controllers; cur; cur = cur->next) {
		count++;
		if (cur == hc || cur->bus == hc->bus)
			return -EBUSY;
	}

	if (count >= USB_MAX_CONTROLLERS)
		return -ENOSPC;

	hc->running = false;
	hc->knownPorts = 0;
	hc->connectedPorts = 0;
	memset(hc->rootDevices, 0, sizeof(hc->rootDevices));
	hc->next = controllers;
	controllers = hc;

	return 0;
}

void USB_UnregisterHostController(struct usbHostController *hc) {
	struct usbHostController **cur;

	if (!hc)
		return;

	if (hc->running) {
		hc->ops->stop(hc);
		hc->running = false;
	}

	for (cur = &controllers; *cur; cur = &(*cur)->next) {
		if (*cur == hc) {
			*cur = hc->next;
			hc->next = NULL;
			return;
		}
	}
}

static int matchScore(const struct usbDeviceId *id, struct usbInterface *intf) {
	int score = 0;

	if ((id->matchFlags & USB_MATCH_VENDOR_PRODUCT) &&
	    (id->vendor != intf->device->descriptor.vendorId ||
	     id->product != intf->device->descriptor.productId))
		return -1;

	if (id->matchFlags & USB_MATCH_VENDOR_PRODUCT)
		score += 8;

	if (id->matchFlags & USB_MATCH_INTERFACE) {
		if (id->interfaceClass != intf->descriptor.interfaceClass ||
		    id->interfaceSubclass != intf->descriptor.interfaceSubclass ||
		    id->interfaceProtocol != intf->descriptor.interfaceProtocol)
			return -1;
		score += 4;
	}

	return score;
}

static void bindInterface(struct usbInterface *intf) {
	struct usbDriver *drv, *best = NULL;
	const struct usbDeviceId *id, *bestId = NULL;
	int score, bestScore = -1;

	if (intf->driver)
		return;

	for (drv = drivers; drv; drv = drv->next) {
		for (id = drv->ids; id && id->matchFlags; id++) {
			score = matchScore(id, intf);
			if (score > bestScore) {
				bestScore = score;
				best = drv;
				bestId = id;
			}
		}
	}

	if (best && !best->probe(intf, bestId))
		intf->driver = best;
}

int USB_RegisterDriver(struct usbDriver *driver) {
	struct usbDriver *cur;
	uint i, j;

	if (!initialized || !driver || !driver->name || !driver->ids || !driver->probe || !driver->remove)
		return -EINVAL;

	for (cur = drivers; cur; cur = cur->next)
		if (cur == driver)
			return -EBUSY;

	driver->next = drivers;
	drivers = driver;

	for (i = 0; i < USB_MAX_DEVICES; i++) {
		if (devices[i].connected) {
			for (j = 0; j < devices[i].numInterfaces; j++)
				bindInterface(&devices[i].interfaces[j]);
		}
	}

	return 0;
}

void USB_UnregisterDriver(struct usbDriver *driver) {
	struct usbDriver **cur;
	uint i, j;

	for (i = 0; i < USB_MAX_DEVICES; i++) {
		for (j = 0; j < devices[i].numInterfaces; j++) {
			if (devices[i].interfaces[j].driver == driver) {
				driver->remove(&devices[i].interfaces[j]);
				devices[i].interfaces[j].driver = NULL;
			}
		}
	}

	for (cur = &drivers; *cur; cur = &(*cur)->next) {
		if (*cur == driver) {
			*cur = driver->next;
			driver->next = NULL;
			return;
		}
	}
}

int USB_SubmitTransfer(struct usbTransfer *transfer) {
	int ret;

	if (!started || !transfer || !transfer->device || !transfer->device->connected ||
	    !transfer->device->hc || !transfer->endpoint)
		return -ENODEV;

	if ((transfer->endpoint->attributes & USB_ENDPOINT_XFER_MASK) == USB_ENDPOINT_XFER_ISOC)
		return -EOPNOTSUPP;

	if (!transfer->timeoutUsecs)
		transfer->timeoutUsecs = USB_DEFAULT_TIMEOUT_US;

	transfer->actualLength = 0;
	transfer->status = USB_TRANSFER_PENDING;
	ret = transfer->device->hc->ops->transfer(transfer->device->hc, transfer);

	if (ret < 0)
		return ret;

	return transferError(transfer->status);
}

int USB_ControlTransfer(struct usbDevice *dev, u8 type, u8 request, u16 value, u16 index, void *data, u16 length, u32 timeoutUsecs) {
	struct usbSetupPacket setup;
	struct usbEndpoint ep;
	struct usbTransfer transfer;
	void *dmaData = data, *bounce = NULL;
	int ret;

	if (!dev)
		return -EINVAL;

	memset(&ep, 0, sizeof(ep));
	ep.maxPacketSize = dev->descriptor.maxPacketSize0 ? dev->descriptor.maxPacketSize0 : 8;
	ep.attributes = USB_ENDPOINT_XFER_CONTROL;

	setup.requestType = type;
	setup.request = request;
	setup.value = npll_cpu_to_le16(value);
	setup.index = npll_cpu_to_le16(index);
	setup.length = npll_cpu_to_le16(length);

	if (usbNeedsBounce(data, length) || (length && (type & USB_DIR_IN) && dev->speed != USB_SPEED_HIGH && dev->ttHubAddress)) {
		bounce = M_PoolAlloc(POOL_MEM2, alignUpU32(length, 32), 32);
		memset(bounce, 0, alignUpU32(length, 32));

		if (!(type & USB_DIR_IN))
			memcpy(bounce, data, length);

		dmaData = bounce;
	}

	memset(&transfer, 0, sizeof(transfer));
	transfer.device = dev;
	transfer.endpoint = &ep;
	transfer.setup = &setup;
	transfer.data = dmaData;
	transfer.length = length;
	transfer.timeoutUsecs = timeoutUsecs;
	ret = USB_SubmitTransfer(&transfer);

	if (bounce) {
		if ((type & USB_DIR_IN) && ret >= 0 && transfer.actualLength)
			memcpy(data, bounce, transfer.actualLength);

		free(bounce);
	}

	return ret < 0 ? ret : (int)transfer.actualLength;
}

static int dataTransfer(struct usbDevice *dev, struct usbEndpoint *ep, void *data, u32 length, u32 *actual, u32 timeout, u8 kind) {
	struct usbTransfer transfer;
	void *dmaData = data, *bounce = NULL;
	int ret;

	if (!ep || (ep->attributes & USB_ENDPOINT_XFER_MASK) != kind)
		return -EINVAL;

	memset(&transfer, 0, sizeof(transfer));

	if (usbNeedsBounce(data, length)) {
		bounce = M_PoolAlloc(POOL_MEM2, alignUpU32(length, 32), 32);
		memset(bounce, 0, alignUpU32(length, 32));

		if (!(ep->address & USB_ENDPOINT_DIR_MASK))
			memcpy(bounce, data, length);

		dmaData = bounce;
	}

	transfer.device = dev;
	transfer.endpoint = ep;
	transfer.data = dmaData;
	transfer.length = length;
	transfer.timeoutUsecs = timeout;
	ret = USB_SubmitTransfer(&transfer);

	if (bounce) {
		if ((ep->address & USB_ENDPOINT_DIR_MASK) && ret >= 0 && transfer.actualLength)
			memcpy(data, bounce, transfer.actualLength);

		free(bounce);
	}

	if (actual)
		*actual = transfer.actualLength;

	return ret;
}

int USB_BulkTransfer(struct usbDevice *dev, struct usbEndpoint *ep, void *data, u32 length, u32 *actual, u32 timeout) {
	return dataTransfer(dev, ep, data, length, actual, timeout, USB_ENDPOINT_XFER_BULK);
}

int USB_InterruptTransfer(struct usbDevice *dev, struct usbEndpoint *ep, void *data, u32 length, u32 *actual, u32 timeout) {
	return dataTransfer(dev, ep, data, length, actual, timeout, USB_ENDPOINT_XFER_INT);
}

int USB_ClearHalt(struct usbDevice *dev, struct usbEndpoint *ep) {
	int ret;

	if (!ep)
		return -EINVAL;

	ret = USB_ControlTransfer(dev, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT, USB_REQ_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT, ep->address, NULL, 0, 0);
	if (ret >= 0)
		ep->toggle = 0;

	return ret < 0 ? ret : 0;
}

void USB_Start(void) {
	struct usbHostController *hc;

	if (!initialized || started)
		return;

	started = true;
	for (hc = controllers; hc; hc = hc->next) {
		if (!hc->ops->start(hc)) {
			hc->running = true;
			log_printf("started bus %u (%s), %u root ports\r\n", hc->bus, hc->name, hc->numPorts);
		}
		else
			log_printf("failed to start bus %u (%s)\r\n", hc->bus, hc->name);
	}

	T_QueueRepeatingEvent(USB_POLL_PERIOD_US, pollEvent, NULL);
}

void USB_Poll(void) {
	struct usbHostController *hc;
	struct usbRootPortStatus status;
	enum usbSpeed speed;
	u16 portBit;
	int ret;
	uint port;

	if (!started)
		return;

	for (hc = controllers; hc; hc = hc->next) {
		if (hc->running && hc->ops->poll)
			hc->ops->poll(hc);

		if (!hc->running)
			continue;

		for (port = 0; port < hc->numPorts; port++) {
			portBit = (u16)(1u << port);
			memset(&status, 0, sizeof(status));
			if (hc->ops->rootPortStatus(hc, port, &status))
				continue;

			/*
			 * Change bits can be stale after firmware/controller reset.  Use
			 * the live connect state for topology and only use them as events
			 * which need acknowledging.
			 */
			if (!(hc->knownPorts & portBit)) {
				hc->knownPorts |= portBit;
				if (!status.connected)
					goto acknowledge;
			}
			else if (status.connected == !!(hc->connectedPorts & portBit))
				goto acknowledge;

			if (!status.connected) {
				hc->connectedPorts &= (u16)~portBit;
				disconnectDevice(hc->rootDevices[port]);
				hc->rootDevices[port] = NULL;
				log_printf("bus %u port %u disconnected\r\n", hc->bus, port + 1);
				goto acknowledge;
			}

			/*
			 * USB 2.0 requires at least 100 ms of stable attachment before
			 * reset.  Re-read afterwards so a bounced plug is ignored.
			 */
			udelay(100000u);
			memset(&status, 0, sizeof(status));
			if (hc->ops->rootPortStatus(hc, port, &status) || !status.connected)
				goto acknowledge;

			ret = hc->ops->rootPortReset(hc, port, &speed);
			if (ret == -EAGAIN) {
				/*
				 * EHCI transferred a full/low-speed port to a companion.
				 * The OHCI instance will report and own the connection.
				 */
				goto acknowledge;
			}
			if (ret) {
				log_printf("bus %u port %u reset failed: %d\r\n", hc->bus, port + 1, ret);
				goto acknowledge;
			}

			/* USB 2.0 reset recovery (TRSTRCY) before address-zero traffic. */
			udelay(10000u);
			hc->connectedPorts |= portBit;
			log_printf("bus %u port %u connected (%s speed)\r\n", hc->bus,
				port + 1, speed == USB_SPEED_HIGH ? "high" :
				speed == USB_SPEED_LOW ? "low" : "full"
			);

			ret = enumerateDevice(hc, NULL, port, speed, &hc->rootDevices[port]);
			if (ret)
				log_printf("bus %u port %u enumeration failed: %d\r\n",
					hc->bus, port + 1, ret);

acknowledge:
			if (hc->ops->rootPortClearChange)
				hc->ops->rootPortClearChange(hc, port);
		}
	}
}

void USB_Shutdown(void) {
	struct usbHostController *hc;
	struct usbHostController *stopOrder[USB_MAX_CONTROLLERS];
	uint numControllers = 0;
	uint i, j;

	started = false;
	T_CancelRepeatingEvent(pollEvent, NULL);

	for (i = 0; i < USB_MAX_DEVICES; i++) {
		if (!devices[i].connected)
			continue;

		devices[i].connected = false;
		for (j = 0; j < devices[i].numInterfaces; j++) {
			if (devices[i].interfaces[j].driver)
				devices[i].interfaces[j].driver->remove(&devices[i].interfaces[j]);
		}
	}
	for (hc = controllers; hc && numControllers < USB_MAX_CONTROLLERS; hc = hc->next)
		stopOrder[numControllers++] = hc;

	while (numControllers) {
		hc = stopOrder[--numControllers];
		if (!hc->running)
			continue;

		hc->ops->stop(hc);
		hc->running = false;
	}
}
