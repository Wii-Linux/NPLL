/*
 * NPLL - Block devices
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "block"

#include <assert.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/fs.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/menu.h>
#include <npll/partition.h>
#include <npll/types.h>
#include <npll/utils.h>

static int initialized = 0;

uint B_NumDevices = 0;
struct blockDevice *B_Devices[MAX_BDEV];

static struct blockTransfer defaultTransfer = {
	.size = 0,
	.mode = BLOCK_TRANSFER_MULTIPLE,
	.dmaAlign = 0
};

static int findDev(const struct blockDevice *bdev) {
	int i;

	for (i = 0; i < MAX_BDEV; i++) {
		if (bdev == B_Devices[i])
			return i;
	}

	return -1;
}

static bool ptrAligned(const void *ptr, u32 align) {
	if (!align)
		return true;

	return ((uintptr_t)ptr % align) == 0;
}

static bool transferSupports(const struct blockTransfer *xfer, size_t len, u64 off) {
	if (!xfer->size || (off % xfer->size))
		return false;

	switch (xfer->mode) {
	case BLOCK_TRANSFER_EXACT:
		return len == xfer->size;
	case BLOCK_TRANSFER_MULTIPLE:
		return (len % xfer->size) == 0;
	default:
		return false;
	}
}

static const struct blockTransfer *selectTransfer(const struct blockDevice *bdev, size_t len, u64 off, bool supported) {
	const struct blockTransfer *xfer, *best = NULL;
	uint i, n;

	xfer = bdev->transfers;
	n = bdev->numTransfers;
	if (!xfer || !n) {
		defaultTransfer.size = bdev->blockSize;
		xfer = &defaultTransfer;
		n = 1;
	}

	for (i = 0; i < n; i++) {
		if (!xfer[i].size)
			continue;

		if (supported && !transferSupports(&xfer[i], len, off))
			continue;

		if (!best || xfer[i].size > best->size)
			best = &xfer[i];
	}

	return best;
}

static inline u64 alignDownU64(u64 val, u32 align) {
	return val - (val % align);
}

static inline u64 alignUpU64(u64 val, u32 align) {
	u64 rem;

	rem = val % align;
	if (!rem)
		return val;

	return val + (align - rem);
}

static ssize_t bounceDMA(struct blockDevice *bdev, const struct blockTransfer *xfer, void *buf, size_t len, u64 off, bool write) {
	u8 *tmp;
	ssize_t ret;

	tmp = M_PoolAlloc(POOL_ANY, len, xfer->dmaAlign);
	if (!tmp)
		return -1;

	if (write) {
		memcpy(tmp, buf, len);
		ret = bdev->write(bdev, tmp, len, off);
	}
	else {
		ret = bdev->read(bdev, tmp, len, off);
		if (ret == (ssize_t)len)
			memcpy(buf, tmp, len);
	}

	if (ret != (ssize_t)len) {
		free(tmp);
		return -1;
	}

	free(tmp);
	return (ssize_t)len;
}

static ssize_t blockRW(struct blockDevice *bdev, void *buf, size_t len, u64 off, bool write) {
	const struct blockTransfer *xfer;
	u8 *tmp, *cursor;
	const u8 *writeCursor;
	u64 pos, end, chunkOff;
	size_t chunkSkip, chunkLen, done;
	ssize_t ret;

	if (!len)
		return 0;

	xfer = selectTransfer(bdev, len, off, true);
	if (xfer) {
		if (ptrAligned(buf, xfer->dmaAlign))
			return write ?
				bdev->write(bdev, buf, len, off) :
				bdev->read(bdev, buf, len, off);

		if (bdev->dmaAlignMode == BLOCK_ALIGN_BOUNCE)
			return bounceDMA(bdev, xfer, buf, len, off, write);

		return -1;
	}

	if (bdev->blockAlignMode != BLOCK_ALIGN_BOUNCE)
		return -1;

	xfer = selectTransfer(bdev, len, off, false);
	if (!xfer)
		return -1;

	if (!ptrAligned(buf, xfer->dmaAlign) && bdev->dmaAlignMode != BLOCK_ALIGN_BOUNCE)
		return -1;

	if (alignUpU64(off + len, xfer->size) > bdev->size)
		return -1;

	tmp = M_PoolAlloc(POOL_ANY, xfer->size, xfer->dmaAlign);
	if (!tmp)
		return -1;

	cursor = buf;
	writeCursor = buf;
	pos = alignDownU64(off, xfer->size);
	end = alignUpU64(off + len, xfer->size);
	done = 0;

	while (pos < end) {
		chunkOff = (pos < off) ? off - pos : 0;
		chunkSkip = (size_t)chunkOff;
		chunkLen = xfer->size - chunkSkip;
		if (chunkLen > len - done)
			chunkLen = len - done;

		if (write) {
			if (chunkSkip || chunkLen != xfer->size) {
				ret = bdev->read(bdev, tmp, xfer->size, pos);
				if (ret != (ssize_t)xfer->size) {
					free(tmp);
					return -1;
				}
			}
			memcpy(tmp + chunkSkip, writeCursor, chunkLen);
			ret = bdev->write(bdev, tmp, xfer->size, pos);
		}
		else {
			ret = bdev->read(bdev, tmp, xfer->size, pos);
			if (ret == (ssize_t)xfer->size)
				memcpy(cursor, tmp + chunkSkip, chunkLen);
		}

		if (ret != (ssize_t)xfer->size) {
			free(tmp);
			return -1;
		}

		cursor += chunkLen;
		writeCursor += chunkLen;
		done += chunkLen;
		pos += xfer->size;
	}

	free(tmp);
	return (ssize_t)len;
}

void B_Init(void) {
	if (initialized)
		return;

	B_NumDevices = 0;
	memset(B_Devices, 0, sizeof(B_Devices));

	initialized = 1;
}

void B_Shutdown(void) {
	if (!initialized)
		return;

	assert(!B_NumDevices);
	initialized = 0;
}

void B_Register(struct blockDevice *bdev) {
	bool irqs;
	int ret;
	uint i;
	struct filesystem *fs;

	assert_msg(initialized, "block: B_Register w/o B_Init");
	assert_msg(B_NumDevices < MAX_BDEV, "block: B_Devices overflow");
	assert_msg(!B_Devices[B_NumDevices], "block: B_Devices corruption");
	assert_msg(addrIsValidCached((void *)bdev), "block: invalid device passed to B_Register");
	assert_msg(findDev(bdev) == -1, "block: registering already-registered device");

	if (bdev->probePartitions)
		P_ProbePartitions(bdev);

	irqs = IRQ_DisableSave();
	B_Devices[B_NumDevices++] = bdev;
	IRQ_Restore(irqs);

	log_printf("registered %s (%llu bytes, %d partition(s))\r\n",
		   bdev->name, bdev->size, bdev->numPartitions);

	/* now probe all of its partitions for filesystems */
	ret = -1;
	for (i = 0; i < bdev->numPartitions; i++) {
		fs = FS_Probe(bdev->partitions[i]);
		if (!fs)
			continue;

		ret = FS_Mount(fs, bdev->partitions[i]);
		if (ret)
			log_printf("FS_Mount failed on %s part %d: %d\r\n", bdev->name, i, ret);
		else {
			UI_AddPart(bdev->partitions[i]);
			break; /* success! */
		}
	}
}

void B_Unregister(const struct blockDevice *bdev) {
	int idx;
	uint i, size;
	bool irqs;

	assert_msg(initialized, "block: B_Unregister w/o B_Init");
	assert_msg(B_NumDevices > 0, "block: B_Devices underflow");
	assert_msg(addrIsValidCached((void *)bdev), "block: invalid device passed to B_Unregister");
	idx = findDev(bdev);
	assert_msg(idx > -1, "block: unregistering non-existent device");

	for (i = 0; i < bdev->numPartitions; i++) {
		if (bdev->partitions[i] == FS_MountedPartition)
			FS_Unmount();

		UI_DelPart(bdev->partitions[i]);
		if (bdev->probePartitions)
			free(bdev->partitions[i]);
	}

	size = (uint)(MAX_BDEV - idx - 1) * sizeof(struct blockDevice *);

	irqs = IRQ_DisableSave();
	memmove(&B_Devices[idx], &B_Devices[idx + 1], size);
	B_Devices[MAX_BDEV - 1] = NULL;
	B_NumDevices--;
	IRQ_Restore(irqs);

	log_printf("unregistered %s\r\n", bdev->name);
}

ssize_t B_Read(struct partition *part, void *dest, size_t len, u64 off) {
	assert_msg(part, "block: B_Read with NULL partition");
	assert_msg(part->bdev, "block: partition has no block device");

	if (off > part->size || len > part->size - off)
		return -1;

	return B_ReadDevice(part->bdev, dest, len, part->offset + off);
}

ssize_t B_Write(struct partition *part, const void *src, size_t len, u64 off) {
	assert_msg(part, "block: B_Write with NULL partition");
	assert_msg(part->bdev, "block: partition has no block device");

	if (off > part->size || len > part->size - off)
		return -1;

	return B_WriteDevice(part->bdev, src, len, part->offset + off);
}

ssize_t B_ReadDevice(struct blockDevice *bdev, void *dest, size_t len, u64 off) {
	assert_msg(bdev, "block: B_ReadDevice with NULL block device");
	assert_msg(bdev->read, "block: block device has no read op");

	if (off > bdev->size || len > bdev->size - off)
		return -1;

	return blockRW(bdev, dest, len, off, false);
}

ssize_t B_WriteDevice(struct blockDevice *bdev, const void *src, size_t len, u64 off) {
	assert_msg(bdev, "block: B_WriteDevice with NULL block device");
	assert_msg(bdev->write, "block: block device has no write op");

	if (off > bdev->size || len > bdev->size - off)
		return -1;

	return blockRW(bdev, (void *)src, len, off, true);
}
