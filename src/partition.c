/*
 * NPLL - Block device partitions
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "part"

#include <string.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/endian.h>
#include <npll/log.h>
#include <npll/partition.h>
#include <npll/types.h>
#include <npll/utils.h>

/*
 * Try to parse an MBR partition table from the first sector of the device.
 * Returns the number of valid partitions found, or 0 if no MBR was found.
 */
static uint probeMBR(struct blockDevice *bdev) {
	u8 ALIGN(32) _mbr[512];
	struct mbr *mbr;
	struct mbrEntry *entries;
	uint i, count = 0;
	int extIdx;
	ssize_t ret;
	u64 firstExtOff = 0, extendedOff = 0;

	mbr = (struct mbr *)_mbr;

	ret = B_ReadDevice(bdev, _mbr, 512, 0);
	if (ret != 512) {
		log_printf("read failed trying to probe for MBR: %d\r\n", ret);
		return 0;
	}

	/* check MBR signature */
	if (mbr->sig[0] != 0x55 || mbr->sig[1] != 0xAA) {
		log_printf("no MBR signature, got %02x %02x\r\n", mbr->sig[0], mbr->sig[1]);
		return 0;
	}

	/* valid MBR */
	entries = mbr->entries;
	for (i = 0; i < 4 && count < MAX_PARTITIONS; i++) {
		if (entries[i].type == 0 || entries[i].sectors == 0)
			continue;

		if (entries[i].lbaStart == 0) {
			log_printf("part w/ valid type (0x%02x) & size (0x%04x sect) but 0 size, ignoring\r\n", entries[i].type, npll_le32_to_cpu(entries[i].sectors));
			continue;
		}

		/* stash an extended partition */
		if (entries[i].type == MBR_TYPE_EXTENDED_CHS) {
			log_puts("ignoring CHS-based extended partition");
			continue;
		}
		if (entries[i].type == MBR_TYPE_EXTENDED_LBA) {
			if (extendedOff) {
				log_puts("ignoring additional extended partition");
				continue;
			}
			firstExtOff = extendedOff = (u64)npll_le32_to_cpu(entries[i].lbaStart) * bdev->blockSize;
			continue;
		}

		bdev->partitions[count] = malloc(sizeof(struct partition));
		memset(bdev->partitions[count], 0, sizeof(struct partition));
		bdev->partitions[count]->bdev = bdev;
		bdev->partitions[count]->offset = (u64)npll_le32_to_cpu(entries[i].lbaStart) * bdev->blockSize;
		bdev->partitions[count]->size = (u64)npll_le32_to_cpu(entries[i].sectors) * bdev->blockSize;
		bdev->partitions[count]->type = entries[i].type;
		bdev->partitions[count]->index = count;
		count++;
	}

	/* consume all extended partitions */
	while (extendedOff && count < MAX_PARTITIONS) {
		log_printf("consuming extended partition @ 0x%llx, count=%d\r\n", extendedOff, count);

		ret = B_ReadDevice(bdev, _mbr, 512, extendedOff);
		if (ret != 512) {
			log_printf("read failed trying to probe for extended partitions: %d\r\n", ret);
			return count; /* we at least got something */
		}

		if (mbr->sig[0] != 0x55 || mbr->sig[1] != 0xaa) {
			log_printf("no EBR signature, got %02x %02x\r\n", mbr->sig[0], mbr->sig[1]);
			return count;
		}

		/* consume the given partitions */
		extIdx = -1;
		for (i = 0; i < 4 && count < MAX_PARTITIONS; i++) {
			if (entries[i].type == 0 || entries[i].sectors == 0)
				continue;

			/*
			 * We don't support CHS extended partitions, but some disks have the CHS
			 * type but LBA fields populated.  Try to support that here.
			 */
			if (entries[i].type == MBR_TYPE_EXTENDED_CHS || entries[i].type == MBR_TYPE_EXTENDED_LBA) {
				if (extIdx != -1) {
					log_puts("ignoring additional extended partition in EBR");
					continue;
				}
				extIdx = (int)i;
				continue;
			}

			if (entries[i].lbaStart == 0) {
				log_printf("part w/ valid type (0x%02x) & size (0x%04x sect) but 0 size, ignoring\r\n", entries[i].type, npll_le32_to_cpu(entries[i].sectors));
				continue;
			}

			/* consume the logical partition */
			bdev->partitions[count] = malloc(sizeof(struct partition));
			memset(bdev->partitions[count], 0, sizeof(struct partition));
			bdev->partitions[count]->bdev = bdev;
			/* LBA Start is relative to the current EBR */
			bdev->partitions[count]->offset = ((u64)npll_le32_to_cpu(entries[i].lbaStart) * bdev->blockSize) + extendedOff;
			bdev->partitions[count]->size = (u64)npll_le32_to_cpu(entries[i].sectors) * bdev->blockSize;
			bdev->partitions[count]->type = entries[i].type;
			bdev->partitions[count]->index = count;
			count++;
		}

		/* are we there yet? */
		if (extIdx == -1) {
			log_puts("end of extended partitions");
			break;
		}
		else {
			if (entries[extIdx].lbaStart == 0) {
				log_puts("extended partition with lbaStart==0, cannot continue");
				break;
			}
			/* LBA Start of the next EBR is relative to the 1st EBR (why would they do that....) */
			extendedOff = firstExtOff + ((u64)npll_le32_to_cpu(entries[extIdx].lbaStart) * bdev->blockSize);
		}
	}

	return count;
}

void P_ProbePartitions(struct blockDevice *bdev) {
	uint count;

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
	bdev->partitions[0] = malloc(sizeof(struct partition));
	bdev->partitions[0]->bdev = bdev;
	bdev->partitions[0]->offset = 0;
	bdev->partitions[0]->size = bdev->size;
	bdev->partitions[0]->type = 0;
	bdev->partitions[0]->index = 0;
	bdev->numPartitions = 1;
	log_printf("no partition table on %s, using whole device\r\n", bdev->name);
}
