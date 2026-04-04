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

static int findDev(const struct blockDevice *bdev) {
	int i;

	for (i = 0; i < MAX_BDEV; i++) {
		if (bdev == B_Devices[i])
			return i;
	}

	return -1;
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
	int i, ret;
	struct filesystem *fs;

	assert_msg(initialized, "block: B_Register w/o B_Init");
	assert_msg(B_NumDevices < MAX_BDEV, "block: B_Devices overflow");
	assert_msg(!B_Devices[B_NumDevices], "block: B_Devices corruption");
	assert_msg(addrIsValidCached((void *)bdev), "block: invalid device passed to B_Register");
	assert_msg(findDev(bdev) == -1, "block: registering already-registered device");

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
	int idx, i, size;
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
		free(bdev->partitions[i]);
	}

	size = (MAX_BDEV - idx - 1) * sizeof(struct blockDevice *);

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
	assert_msg(part->bdev->read, "block: block device has no read op");

	if (off + len > part->size)
		return -1;

	return part->bdev->read(part->bdev, dest, len, part->offset + off);
}

ssize_t B_Write(struct partition *part, const void *src, size_t len, u64 off) {
	assert_msg(part, "block: B_Write with NULL partition");
	assert_msg(part->bdev, "block: partition has no block device");
	assert_msg(part->bdev->write, "block: block device has no write op");

	if (off + len > part->size)
		return -1;

	return part->bdev->write(part->bdev, src, len, part->offset + off);
}
