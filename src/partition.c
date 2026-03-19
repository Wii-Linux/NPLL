/*
 * NPLL - Block device partitions
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "part"

#include <string.h>
#include <npll/block.h>
#include <npll/log.h>
#include <npll/partition.h>
#include <npll/types.h>
#include <npll/utils.h>

/*
 * Try to parse an MBR partition table from the first sector of the device.
 * Returns the number of valid partitions found, or 0 if no MBR was found.
 */
static int probeMBR(struct blockDevice *bdev) {
	u8 ALIGN(32) mbr[512];
	struct mbrEntry *entries;
	int count = 0;
	int i;
	ssize_t ret;

	if (!bdev->read) {
		log_puts("bdev has no read functionality????");
		return 0;
	}

	ret = bdev->read(bdev, mbr, 512, 0);
	if (ret != 512) {
		log_printf("read failed trying to probe for MBR: %d\r\n", ret);
		return 0;
	}

	/* check MBR signature */
	if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
		log_printf("no MBR signature, got %02x %02x\r\n", mbr[510], mbr[511]);
		return 0;
	}

	entries = (struct mbrEntry *)&mbr[446];

	for (i = 0; i < 4 && count < MAX_PARTITIONS; i++) {
		if (entries[i].type == 0 || entries[i].sectors == 0)
			continue;

		bdev->partitions[count].bdev = bdev;
		bdev->partitions[count].offset = (u64)entries[i].lbaStart * bdev->blockSize;
		bdev->partitions[count].size = (u64)entries[i].sectors * bdev->blockSize;
		bdev->partitions[count].type = entries[i].type;
		bdev->partitions[count].index = count;
		count++;
	}

	/* TODO: extended partitions */

	return count;
}

void P_ProbePartitions(struct blockDevice *bdev) {
	int count;

	bdev->numPartitions = 0;
	memset(bdev->partitions, 0, sizeof(bdev->partitions));

	count = probeMBR(bdev);
	if (count > 0) {
		bdev->numPartitions = count;
		log_printf("found %d MBR partition(s) on %s\r\n", count, bdev->name);
		return;
	}
	#if 0
	count = probeGPT(bdev);
	if (count > 0) {
		bdev->numPartitions = count;
		log_printf("found %d GPT partition(s) on %s\r\n", count, bdev->name);
		return;
	}
	#endif

	/* no recognized partition table; create a pseudo-partition spanning the entire device */
	bdev->partitions[0].bdev = bdev;
	bdev->partitions[0].offset = 0;
	bdev->partitions[0].size = bdev->size;
	bdev->partitions[0].type = 0;
	bdev->partitions[0].index = 0;
	bdev->numPartitions = 1;
	log_printf("no partition table on %s, using whole device\r\n", bdev->name);
}
