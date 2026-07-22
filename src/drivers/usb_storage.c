/*
 * NPLL - USB Mass Storage
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "USB-MSC"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <npll/block.h>
#include <npll/drivers.h>
#include <npll/endian.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/usb.h>

#define USB_MSC_MAX_DEVICES 8
#define USB_MSC_MAX_LUNS    4
#define USB_MSC_TIMEOUT_US  5000000u
#define USB_MSC_SCAN_TIMEOUT_US 30000000u
#define USB_MSC_IO_BYTES    (64u * 1024u)
#define USB_MSC_SCAN_DELAY_US 1000000u
#define USB_MSC_PHASE_DELAY_US 150u

_Static_assert(sizeof(struct usbBotCbw) == 31, "BOT CBW layout");
_Static_assert(sizeof(struct usbBotCsw) == 13, "BOT CSW layout");

struct usbMassStorage;
struct usbMassLun {
	struct blockDevice bdev;
	struct blockTransfer transfer;
	struct usbMassStorage *storage;
	u8 lun;
	bool registered;
	char name[24];
};

struct usbMassStorage {
	struct usbInterface *interface;
	struct usbEndpoint *bulkIn;
	struct usbEndpoint *bulkOut;
	u32 tag;
	u32 commandTimeoutUsecs;
	u8 numLuns;
	bool scanPending;
	struct usbMassLun luns[USB_MSC_MAX_LUNS];
};

static REGISTER_DRIVER(usbStorageTopDriver);
static struct usbMassStorage storageDevices[USB_MSC_MAX_DEVICES];

static u32 getBe32(const u8 *p) {
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u64 getBe64(const u8 *p) {
	return ((u64)getBe32(p) << 32) | getBe32(p + 4);
}

static void putBe16(u8 *p, u16 value) {
	p[0] = (u8)(value >> 8);
	p[1] = (u8)value;
}

static void putBe32(u8 *p, u32 value) {
	p[0] = (u8)(value >> 24);
	p[1] = (u8)(value >> 16);
	p[2] = (u8)(value >> 8);
	p[3] = (u8)value;
}

static void putBe64(u8 *p, u64 value) {
	putBe32(p, (u32)(value >> 32));
	putBe32(p + 4, (u32)value);
}

static int botReset(struct usbMassStorage *storage) {
	int ret;

	ret = USB_ControlTransfer(storage->interface->device,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		USB_BOT_REQ_RESET, 0, storage->interface->descriptor.interfaceNumber,
		NULL, 0, storage->commandTimeoutUsecs
	);

	if (ret < 0) {
		log_printf("BOT class reset failed: %d\r\n", ret);
		return ret;
	}
	udelay(10000);

	ret = USB_ClearHalt(storage->interface->device, storage->bulkIn);
	if (ret) {
		log_printf("BOT clear IN halt failed: %d\r\n", ret);
		return ret;
	}

	ret = USB_ClearHalt(storage->interface->device, storage->bulkOut);
	if (ret)
		log_printf("BOT clear OUT halt failed: %d\r\n", ret);

	return ret;
}

static int botCommand(struct usbMassStorage *storage, u8 lun, const u8 *cdb, u8 cdbLength, void *data, u32 dataLength, bool input, u32 *actualData) {
	struct usbBotCbw cbw ALIGN(32);
	struct usbBotCsw csw ALIGN(32);
	u32 actual, tag, residue;
	int ret;

	if (!cdb || !cdbLength || cdbLength > sizeof(cbw.cdb) || (dataLength && !data) || lun >= storage->numLuns)
		return -EINVAL;

	memset(&cbw, 0, sizeof(cbw));
	memset(&csw, 0, sizeof(csw));

	tag = ++storage->tag;
	if (!tag)
		tag = ++storage->tag;

	cbw.signature = npll_cpu_to_le32(USB_BOT_CBW_SIGNATURE);
	cbw.tag = npll_cpu_to_le32(tag);
	cbw.transferLength = npll_cpu_to_le32(dataLength);
	cbw.flags = input ? USB_DIR_IN : USB_DIR_OUT;
	cbw.lun = lun;
	cbw.cdbLength = cdbLength;
	memcpy(cbw.cdb, cdb, cdbLength);

	actual = 0;
	ret = USB_BulkTransfer(storage->interface->device, storage->bulkOut, &cbw, sizeof(cbw), &actual, storage->commandTimeoutUsecs);
	if (ret || actual != sizeof(cbw)) {
		log_printf("CBW tag %08x failed: ret %d, actual %u\r\n", tag, ret, actual);
		goto recover;
	}

	/*
	 * A number of BOT devices need a small command-to-data settling gap.
	 * We can otherwise turn the schedule around substantially faster
	 * than the host stacks these devices are commonly tested with.
	 */
	udelay(USB_MSC_PHASE_DELAY_US);

	if (dataLength) {
		actual = 0;
		ret = USB_BulkTransfer(storage->interface->device,
			input ? storage->bulkIn : storage->bulkOut,
			data, dataLength, &actual, storage->commandTimeoutUsecs);
		if (ret) {
			log_printf("data phase tag %08x failed: ret %d, actual %u/%u\r\n",
				tag, ret, actual, dataLength);
			goto recover;
		}
		if (actualData)
			*actualData = actual;
	}
	else if (actualData)
		*actualData = 0;

	udelay(USB_MSC_PHASE_DELAY_US);

	actual = 0;
	ret = USB_BulkTransfer(storage->interface->device, storage->bulkIn, &csw, sizeof(csw), &actual, storage->commandTimeoutUsecs);
	if (ret || actual != sizeof(csw)) {
		log_printf("CSW tag %08x failed: ret %d, actual %u\r\n", tag, ret, actual);
		goto recover;
	}

	if (npll_le32_to_cpu(csw.signature) != USB_BOT_CSW_SIGNATURE || npll_le32_to_cpu(csw.tag) != tag) {
		log_printf("invalid CSW for tag %08x: signature %08x, tag %08x\r\n", tag, npll_le32_to_cpu(csw.signature), npll_le32_to_cpu(csw.tag));
		goto phaseError;
	}

	residue = npll_le32_to_cpu(csw.residue);
	if (residue > dataLength) {
		log_printf("invalid CSW residue %u/%u for tag %08x\r\n", residue, dataLength, tag);
		goto phaseError;
	}

	if (actualData && dataLength && *actualData > dataLength - residue)
		*actualData = dataLength - residue;

	if (csw.status == 0)
		return 0;

	if (csw.status == 1) {
		log_printf("command tag %08x failed, residue %u/%u\r\n", tag, residue, dataLength);
		return -EIO;
	}

phaseError:
	ret = -EIO;
recover:
	if (botReset(storage))
		log_puts("BOT reset recovery failed");

	return ret ? ret : -EIO;
}

static int scsiRequestSense(struct usbMassStorage *storage, u8 lun,
	u8 *sense, u32 senseLength) {
	u8 cdb[6] = { SCSI_REQUEST_SENSE, 0, 0, 0, 0, 0 };
	u32 actual = 0;

	cdb[4] = (u8)senseLength;
	return botCommand(storage, lun, cdb, sizeof(cdb), sense, senseLength, true, &actual);
}

static int scsiReady(struct usbMassStorage *storage, u8 lun) {
	u8 cdb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
	u8 sense[32] ALIGN(32);
	int ret = -EIO;
	uint attempt;

	for (attempt = 0; attempt < 15; attempt++) {
		ret = botCommand(storage, lun, cdb, sizeof(cdb), NULL, 0, false, NULL);
		if (!ret)
			return 0;

		memset(sense, 0, sizeof(sense));
		if (scsiRequestSense(storage, lun, sense, 18))
			break;

		log_printf("LUN %u readiness sense %x/%02x/%02x\r\n", lun, sense[2] & 0x0fu, sense[12], sense[13]);

		/*
		 * NOT READY / becoming ready and the one-shot UNIT ATTENTION
		 * following media initialization are both transient.
		 */
		if ((sense[2] & 0x0fu) != 2u && (sense[2] & 0x0fu) != 6u)
			break;

		udelay(500000);
	}
	return ret;
}

static int scsiCapacity(struct usbMassStorage *storage, u8 lun, u64 *lastLba, u32 *blockSize) {
	u8 cdb[16], data[32] ALIGN(32);
	u32 actual = 0;
	int ret;

	memset(cdb, 0, sizeof(cdb));
	memset(data, 0, sizeof(data));

	cdb[0] = SCSI_READ_CAPACITY_10;
	ret = botCommand(storage, lun, cdb, 10, data, 8, true, &actual);
	if (ret || actual != 8)
		return ret ? ret : -EIO;

	*lastLba = getBe32(data);
	*blockSize = getBe32(data + 4);
	if (*lastLba != 0xffffffffull)
		return *blockSize ? 0 : -EIO;

	memset(cdb, 0, sizeof(cdb));
	memset(data, 0, sizeof(data));
	cdb[0] = SCSI_SERVICE_ACTION_IN_16;
	cdb[1] = 0x10u; /* READ CAPACITY(16) */
	putBe32(cdb + 10, sizeof(data));
	actual = 0;

	ret = botCommand(storage, lun, cdb, sizeof(cdb), data, sizeof(data), true, &actual);
	if (ret || actual < 12)
		return ret ? ret : -EIO;

	*lastLba = getBe64(data);
	*blockSize = getBe32(data + 8);
	return *blockSize ? 0 : -EIO;
}

static int scsiRw(struct usbMassLun *lun, void *buffer, u32 blocks, u64 lba, bool write) {
	u8 cdb[16];
	u32 actual = 0, length = blocks * lun->bdev.blockSize;
	int ret;

	memset(cdb, 0, sizeof(cdb));
	if (lba <= 0xffffffffull && blocks <= 0xffffu) {
		cdb[0] = write ? SCSI_WRITE_10 : SCSI_READ_10;
		putBe32(cdb + 2, (u32)lba);
		putBe16(cdb + 7, (u16)blocks);
		ret = botCommand(lun->storage, lun->lun, cdb, 10, buffer, length, !write, &actual);
	}
	else {
		cdb[0] = write ? SCSI_WRITE_16 : SCSI_READ_16;
		putBe64(cdb + 2, lba);
		putBe32(cdb + 10, blocks);
		ret = botCommand(lun->storage, lun->lun, cdb, 16, buffer, length, !write, &actual);
	}
	return ret ? ret : (actual == length ? 0 : -EIO);
}

static ssize_t massRead(struct blockDevice *bdev, void *dest, size_t len, u64 off) {
	struct usbMassLun *lun = bdev->drvData;
	u8 *cursor = dest;
	u32 blocks, blockSize = bdev->blockSize, maxBlocks = USB_MSC_IO_BYTES / blockSize;
	u64 lba = off / blockSize;
	size_t remaining = len;

	if (!lun || !lun->storage->interface->device->connected || (off % blockSize) || (len % blockSize) || !maxBlocks)
		return -1;

	while (remaining) {
		blocks = (u32)(remaining / blockSize);
		if (blocks > maxBlocks)
			blocks = maxBlocks;

		if (scsiRw(lun, cursor, blocks, lba, false))
			return -1;

		cursor += blocks * blockSize;
		remaining -= blocks * blockSize;
		lba += blocks;
	}

	return (ssize_t)len;
}

static ssize_t massWrite(struct blockDevice *bdev, const void *src, size_t len, u64 off) {
	struct usbMassLun *lun = bdev->drvData;
	const u8 *cursor = src;
	u32 blocks, blockSize = bdev->blockSize, maxBlocks = USB_MSC_IO_BYTES / blockSize;
	u64 lba = off / blockSize;
	size_t remaining = len;

	if (!lun || !lun->storage->interface->device->connected || (off % blockSize) || (len % blockSize) || !maxBlocks)
		return -1;

	while (remaining) {
		blocks = (u32)(remaining / blockSize);
		if (blocks > maxBlocks)
			blocks = maxBlocks;
		if (scsiRw(lun, (void *)cursor, blocks, lba, true))
			return -1;

		cursor += blocks * blockSize;
		remaining -= blocks * blockSize;
		lba += blocks;
	}

	return (ssize_t)len;
}

static int setupLun(struct usbMassStorage *storage, u8 lunNumber) {
	struct usbMassLun *lun = &storage->luns[lunNumber];
	u8 inquiry[64] ALIGN(32), sense[32] ALIGN(32), cdb[6] = { SCSI_INQUIRY, 0, 0, 0, 36, 0 };
	u32 actual = 0, blockSize;
	u64 lastLba;
	int ret;
	uint attempt;

	ret = -EIO;
	for (attempt = 0; attempt < 10; attempt++) {
		memset(inquiry, 0, sizeof(inquiry));
		actual = 0;

		ret = botCommand(storage, lunNumber, cdb, sizeof(cdb), inquiry, 36, true, &actual);
		if (!ret)
			break;

		memset(sense, 0, sizeof(sense));
		if (!scsiRequestSense(storage, lunNumber, sense, 18)) {
			u8 key = sense[2] & 0x0fu;
			log_printf("LUN %u INQUIRY sense %x/%02x/%02x\r\n", lunNumber, key, sense[12], sense[13]);

			/*
			 * NOT READY and UNIT ATTENTION are normal while removable
			 * flash media completes initialization.
			 */
			if (key != 2u && key != 6u)
				break;
		}
		udelay(1000000);
	}

	if (ret || actual < 5 || (inquiry[0] & 0x1fu) == 0x1fu)
		return ret ? ret : -ENODEV;

	ret = scsiReady(storage, lunNumber);
	if (ret)
		return ret;

	ret = scsiCapacity(storage, lunNumber, &lastLba, &blockSize);
	if (ret || blockSize < 512u || (blockSize & (blockSize - 1u)) || lastLba == 0xffffffffffffffffull)
		return ret ? ret : -EIO;

	memset(lun, 0, sizeof(*lun));
	lun->storage = storage;
	lun->lun = lunNumber;
	snprintf(lun->name, sizeof(lun->name), "usb%u-%u.%u", storage->interface->device->hc->bus, storage->interface->device->address, lunNumber);
	lun->transfer.size = blockSize;
	lun->transfer.mode = BLOCK_TRANSFER_MULTIPLE;
	lun->transfer.dmaAlign = 32;
	lun->bdev.name = lun->name;
	lun->bdev.size = (lastLba + 1u) * blockSize;
	lun->bdev.blockSize = blockSize;
	lun->bdev.drvData = lun;
	lun->bdev.transfers = &lun->transfer;
	lun->bdev.numTransfers = 1;
	lun->bdev.blockAlignMode = BLOCK_ALIGN_BOUNCE;
	lun->bdev.dmaAlignMode = BLOCK_ALIGN_BOUNCE;
	lun->bdev.read = massRead;
	lun->bdev.write = massWrite;
	lun->bdev.probePartitions = true;
	lun->bdev.flags = BLOCK_FLAG_STANDARD;
	B_Register(&lun->bdev);
	lun->registered = true;
	log_printf("LUN %u: %llu bytes, %u-byte blocks\r\n", lunNumber, lun->bdev.size, blockSize);
	return 0;
}

static void massScan(void *data) {
	struct usbMassStorage *storage = data;
	uint i, usable = 0;
	int ret;

	storage->scanPending = false;
	if (!storage->interface || !storage->interface->device->connected) {
		memset(storage, 0, sizeof(*storage));
		return;
	}

	/*
	 * Linux leaves discovery commands pending under the SCSI command
	 * timeout.  Some flash controllers start their backend only after the
	 * first INQUIRY CBW and NAK bulk-IN for several seconds meanwhile.
	 */
	storage->commandTimeoutUsecs = USB_MSC_SCAN_TIMEOUT_US;
	for (i = 0; i < storage->numLuns; i++) {
		ret = setupLun(storage, (u8)i);
		if (ret)
			log_printf("LUN %u probe failed: %d\r\n", i, ret);
		else
			usable++;
	}

	storage->commandTimeoutUsecs = USB_MSC_TIMEOUT_US;
	log_printf("BOT bus %u address %u: %u/%u usable LUN(s)\r\n", storage->interface->device->hc->bus, storage->interface->device->address, usable, storage->numLuns);
}

static int massProbe(struct usbInterface *interface, const struct usbDeviceId *id) {
	struct usbMassStorage *storage = NULL;
	u8 maxLun = 0;
	uint i;
	int ret;
	(void)id;

	for (i = 0; i < USB_MSC_MAX_DEVICES; i++) {
		if (!storageDevices[i].interface && !storageDevices[i].scanPending) {
			storage = &storageDevices[i];
			break;
		}
	}

	if (!storage)
		return -ENOSPC;

	memset(storage, 0, sizeof(*storage));
	storage->interface = interface;
	storage->commandTimeoutUsecs = USB_MSC_TIMEOUT_US;

	for (i = 0; i < interface->numEndpoints; i++) {
		if ((interface->endpoints[i].attributes & USB_ENDPOINT_XFER_MASK) != USB_ENDPOINT_XFER_BULK)
			continue;

		if (interface->endpoints[i].address & USB_ENDPOINT_DIR_MASK)
			storage->bulkIn = &interface->endpoints[i];
		else
			storage->bulkOut = &interface->endpoints[i];
	}

	if (!storage->bulkIn || !storage->bulkOut) {
		memset(storage, 0, sizeof(*storage));
		return -ENODEV;
	}

	log_printf("BOT endpoints: OUT %02x/%u, IN %02x/%u\r\n", storage->bulkOut->address, storage->bulkOut->maxPacketSize, storage->bulkIn->address, storage->bulkIn->maxPacketSize);

	ret = USB_ControlTransfer(interface->device,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		USB_BOT_REQ_GET_MAX_LUN, 0, interface->descriptor.interfaceNumber,
		&maxLun, 1, USB_MSC_TIMEOUT_US
	);

	if (ret == -EPIPE)
		maxLun = 0;
	else if (ret != 1) {
		memset(storage, 0, sizeof(*storage));
		return ret < 0 ? ret : -EIO;
	}

	storage->numLuns = (u8)(maxLun + 1u);
	if (storage->numLuns > USB_MSC_MAX_LUNS)
		storage->numLuns = USB_MSC_MAX_LUNS;

	interface->driverData = storage;
	storage->scanPending = true;
	T_QueueEvent(USB_MSC_SCAN_DELAY_US, massScan, storage);
	log_printf("BOT bus %u address %u: scanning %u LUN(s) in %u ms\r\n",
		interface->device->hc->bus, interface->device->address,
		storage->numLuns, USB_MSC_SCAN_DELAY_US / 1000u
	);
	return 0;
}

static void massRemove(struct usbInterface *interface) {
	struct usbMassStorage *storage = interface->driverData;
	uint i;

	if (!storage)
		return;
	for (i = 0; i < storage->numLuns; i++) {
		if (storage->luns[i].registered) {
			B_Unregister(&storage->luns[i].bdev);
			storage->luns[i].registered = false;
		}
	}
	if (storage->scanPending) {
		/*
		 * The queued one-shot cannot be cancelled.  Reserve this slot until
		 * it fires; massScan will observe the missing interface and clear it.
		 */
		storage->interface = NULL;
	}
	else
		memset(storage, 0, sizeof(*storage));

	interface->driverData = NULL;
}

static const struct usbDeviceId massIds[] = {
	{
		.interfaceClass = USB_CLASS_MASS_STORAGE,
		.interfaceSubclass = USB_SUBCLASS_SCSI,
		.interfaceProtocol = USB_PROTOCOL_BOT,
		.matchFlags = USB_MATCH_INTERFACE,
	},
	{ 0 }
};

static struct usbDriver massDriver = {
	.name = "USB Mass Storage",
	.ids = massIds,
	.probe = massProbe,
	.remove = massRemove,
};

static void usbStorageInit(void) {
	memset(storageDevices, 0, sizeof(storageDevices));
	if (USB_RegisterDriver(&massDriver)) {
		usbStorageTopDriver.state = DRIVER_STATE_FAULTED;
		return;
	}
	usbStorageTopDriver.state = DRIVER_STATE_READY;
}

static void usbStorageCleanup(void) {
	USB_UnregisterDriver(&massDriver);
	memset(storageDevices, 0, sizeof(storageDevices));
	usbStorageTopDriver.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(usbStorageTopDriver) = {
	.name = "USB Mass Storage",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_BLOCK,
	.init = usbStorageInit,
	.cleanup = usbStorageCleanup,
};
