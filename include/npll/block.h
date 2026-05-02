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

enum blockAlignmentMode {
	/* The caller's request must already satisfy the relevant constraint. */
	BLOCK_ALIGN_REJECT = 0,

	/* The block core may use bounce buffers/read-modify-write cycles. */
	BLOCK_ALIGN_BOUNCE
};

enum blockTransferMode {
	/* Requests must be exactly this size. */
	BLOCK_TRANSFER_EXACT = 0,

	/* Requests may be any multiple of this size. */
	BLOCK_TRANSFER_MULTIPLE
};

struct blockTransfer {
	/* atomic transfer size in bytes */
	u32 size;

	/* whether size is exact or repeatable */
	enum blockTransferMode mode;

	/* required buffer alignment in bytes, or 0 for no special alignment */
	u32 dmaAlign;
};

struct blockDevice {
	/* name of the block device */
	char *name;

	/* size of the block device in bytes */
	u64 size;

	/* block size in bytes (typically 512) */
	u32 blockSize;

	/* optional driver-specific data */
	void *drvData;

	/* supported transfer units */
	const struct blockTransfer *transfers;
	uint numTransfers;

	/* how the core should handle unsupported offset/length alignment */
	enum blockAlignmentMode blockAlignMode;

	/* how the core should handle buffers that do not satisfy dmaAlign */
	enum blockAlignmentMode dmaAlignMode;

	/* block I/O operations */
	ssize_t (*read)(struct blockDevice *bdev, void *dest, size_t len, u64 off);
	ssize_t (*write)(struct blockDevice *bdev, const void *src, size_t len, u64 off);

	/* partitions on this device */
	uint numPartitions;
	struct partition *partitions[MAX_PARTITIONS];
	bool probePartitions;
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

/* read from a raw block device */
extern ssize_t B_ReadDevice(struct blockDevice *bdev, void *dest, size_t len, u64 off);

/* write to a raw block device */
extern ssize_t B_WriteDevice(struct blockDevice *bdev, const void *src, size_t len, u64 off);

/* read from a partition (offsets are relative to the partition start) */
extern ssize_t B_Read(struct partition *part, void *dest, size_t len, u64 off);

/* write to a partition (offsets are relative to the partition start) */
extern ssize_t B_Write(struct partition *part, const void *src, size_t len, u64 off);

#endif /* _BLOCK_H */
