/*
 * NPLL - Block device partitions
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _PARTITION_H
#define _PARTITION_H

#include <npll/types.h>

struct partition {
	/* block device that contains this partition */
	struct blockDevice *bdev;

	/* byte offset of the partition within the block device */
	u64 offset;

	/* size of the partition in bytes */
	u64 size;

	/* partition type (e.g. MBR type byte) */
	u8 type;

	/* partition index (0-based) */
	int index;
};

/* MBR partition table entry (on-disk layout) */
struct __attribute__((packed)) mbrEntry {
	u8 status;
	u8 chsFirst[3];
	u8 type;
	u8 chsLast[3];
	u32 lbaStart;
	u32 sectors;
};

void P_ProbePartitions(struct blockDevice *bdev);

#endif /* _PARTITION_H */
