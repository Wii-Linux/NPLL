/*
 * NPLL - Block devices
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _BLOCK_H
#define _BLOCK_H

#include <npll/partition.h>
#include <npll/types.h>

#define MAX_BDEV 16
#define MAX_PARTITIONS 32

struct blockDevice {
	/* name of the block device */
	char *name;

	/* size of the block device in bytes */
	u64 size;

	/* block size in bytes (typically 512) */
	u32 blockSize;

	/* optional driver-specific data */
	void *drvData;

	/* block I/O operations */
	ssize_t (*read)(struct blockDevice *bdev, void *dest, size_t len, u64 off);
	ssize_t (*write)(struct blockDevice *bdev, const void *src, size_t len, u64 off);

	/* partitions on this device */
	uint numPartitions;
	struct partition *partitions[MAX_PARTITIONS];
};

/* registered block devices */
extern uint B_NumDevices;
extern struct blockDevice *B_Devices[];

/* initialize the block core */
extern void B_Init(void);

/* shut down the block core */
extern void B_Shutdown(void);

/*
 * Register a block device and probe its partition table.
 * If no recognized partition table is found, a single pseudo-partition
 * spanning the entire device is created.
 */
extern void B_Register(struct blockDevice *bdev);

/* unregister a block device */
extern void B_Unregister(const struct blockDevice *bdev);

/* read from a partition (offsets are relative to the partition start) */
extern ssize_t B_Read(struct partition *part, void *dest, size_t len, u64 off);

/* write to a partition (offsets are relative to the partition start) */
extern ssize_t B_Write(struct partition *part, const void *src, size_t len, u64 off);

#endif /* _BLOCK_H */
