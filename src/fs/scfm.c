/*
 * NPLL - Wii U MLC SCFM (SLC-resident block write cache) overlay
 *
 * SCFM divides the MLC into fixed 8 MiB "regions". Each region is either
 * uncached (read straight from the MLC) or mapped to one of a small number of
 * cache slots holding the live copy. The slot map lives in the image header.
 *
 * On-disk format reverse-engineered by Techflash. The image is stored
 * unencrypted in the SLC SFFS (IOSU reaches it via a plaintext-I/O flag).
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "scfm"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/endian.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/scfm.h>
#include <npll/types.h>
#include "sffs.h"

#define SCFM_SECTOR_SIZE        0x200u
#define SCFM_REGION_SIZE        0x800000u
#define SCFM_REGION_SECTORS     (SCFM_REGION_SIZE / SCFM_SECTOR_SIZE) /* 0x4000 */
#define SCFM_REGION_MAP_OFFSET  0x30u
#define SCFM_REGION_MAP_SIZE    0x3c00u
#define SCFM_SLOT_RECORDS_OFFSET (SCFM_REGION_MAP_OFFSET + SCFM_REGION_MAP_SIZE)
#define SCFM_CACHE_LINE_SECTORS 0x100u /* 128 KiB cache line */
#define SCFM_CACHE_LINES_PER_SLOT (SCFM_REGION_SECTORS / SCFM_CACHE_LINE_SECTORS) /* 0x40 */
#define SCFM_METADATA_SIZE      0x4000u
#define SCFM_SLOT_VALID         0x80u
#define SCFM_SLOT_INDEX_MASK    0x7fu

/* All multi-byte fields are big-endian on disk. */
struct scfmDiskHeader {
	u32 metadataSize;       /* 0x00: expected 0x00004000 */
	u32 slotCount;          /* 0x04: expected 0x00000010 */
	u32 slotSize;           /* 0x08: expected 0x00800000 */
	u8  unknown0c[0x0c];    /* 0x0c */
	u32 payloadSize;        /* 0x18: expected 0x08000000 */
	u8  unknown1c[0x14];    /* 0x1c..0x2f */
	u8  regionToSlot[SCFM_REGION_MAP_SIZE]; /* 0x30 */
} __attribute__((packed));

/*
 * Per-slot record, at SCFM_SLOT_RECORDS_OFFSET. A slot holds one 8 MiB region,
 * but only the 128 KiB cache lines whose bit is set in validLines are actually
 * live — the rest must be read from the real MLC.
 */
struct scfmDiskSlotRecord {
	u8  nextSlot;           /* 0x00 */
	u8  previousSlot;       /* 0x01 */
	u16 region;             /* 0x02: big-endian */
	u32 flags;              /* 0x04: big-endian */
	u8  validLines[SCFM_CACHE_LINES_PER_SLOT / 8]; /* 0x08: 64-bit line bitmap */
} __attribute__((packed));

/*
 * Only the metadata (header + region map + slot records) is resident. The
 * 128 MiB payload stays on the SLC and is read on demand through `snap`, which
 * captures scfm.img's on-disk layout so it survives the SFFS unmount.
 */
static struct {
	bool active;
	u8 *metadata;
	u64 size;
	u32 metadataSize;
	u32 slotCount;
	u32 slotSize;
	struct sffsSnapshot snap;
} scfm;

/*
 * If a whole-device MLC sector is live in the cache, set *fileOff to its byte
 * offset within scfm.img and return true; otherwise return false (read the MLC).
 */
static bool scfmLookupSector(u32 mlcSector, u64 *fileOff) {
	const struct scfmDiskHeader *h = (const struct scfmDiskHeader *)scfm.metadata;
	const struct scfmDiskSlotRecord *records =
		(const struct scfmDiskSlotRecord *)(scfm.metadata + SCFM_SLOT_RECORDS_OFFSET);
	u32 region = mlcSector / SCFM_REGION_SECTORS;
	u32 sectorInRegion = mlcSector % SCFM_REGION_SECTORS;
	u32 slot, line;
	u8 mapping;
	u64 offset;

	if (region >= SCFM_REGION_MAP_SIZE)
		return false;
	mapping = h->regionToSlot[region];
	if (!(mapping & SCFM_SLOT_VALID))
		return false;
	slot = mapping & SCFM_SLOT_INDEX_MASK;
	if (slot >= scfm.slotCount)
		return false; /* corrupt metadata */
	/*
	 * A mapped slot only holds SOME of its region's 128 KiB cache lines live;
	 * a line whose validLines bit is clear is stale and must come from the MLC.
	 */
	line = sectorInRegion / SCFM_CACHE_LINE_SECTORS;
	if (!(records[slot].validLines[line >> 3] & (1u << (line & 7))))
		return false;
	offset = (u64)scfm.metadataSize + (u64)slot * scfm.slotSize +
		(u64)sectorInRegion * SCFM_SECTOR_SIZE;
	if (offset > scfm.size - SCFM_SECTOR_SIZE)
		return false;
	*fileOff = offset;
	return true;
}

bool S_SCFMActive(void) {
	return scfm.active;
}

bool S_SCFMCached(u64 off) {
	u64 fileOff;

	if (!scfm.active)
		return false;
	return scfmLookupSector((u32)(off / SCFM_SECTOR_SIZE), &fileOff);
}

int S_SCFMLoadFile(const char *path) {
	const struct scfmDiskHeader *h;
	u32 metadataSize, slotCount, slotSize;
	struct sffsSnapshot snap;
	u8 *metadata;
	ssize_t size, got;
	int fd, ret;

	fd = FS_Open(path);
	if (fd < 0)
		return fd;
	size = FS_GetSize(fd);
	if (size < (ssize_t)SCFM_METADATA_SIZE) {
		FS_Close(fd);
		return -EIO;
	}

	/* Only the metadata is resident; the payload is read on demand. */
	metadata = malloc(SCFM_METADATA_SIZE);
	if (!metadata) {
		FS_Close(fd);
		return -ENOMEM;
	}
	got = FS_Read(fd, metadata, SCFM_METADATA_SIZE);
	if (got != (ssize_t)SCFM_METADATA_SIZE) {
		log_printf("metadata read failed: %d/%u\r\n", (int)got, SCFM_METADATA_SIZE);
		free(metadata);
		FS_Close(fd);
		return -EIO;
	}

	h = (const struct scfmDiskHeader *)metadata;
	metadataSize = npll_be32_to_cpu(h->metadataSize);
	slotCount = npll_be32_to_cpu(h->slotCount);
	slotSize = npll_be32_to_cpu(h->slotSize);
	if (metadataSize != SCFM_METADATA_SIZE || slotSize != SCFM_REGION_SIZE ||
		slotCount == 0 || slotCount > SCFM_SLOT_INDEX_MASK + 1) {
		log_printf("bad header: meta=%08x slots=%u slotSize=%08x\r\n",
			metadataSize, slotCount, slotSize);
		free(metadata);
		FS_Close(fd);
		return -EINVAL;
	}
	if ((u64)size < (u64)metadataSize + (u64)slotCount * slotSize) {
		log_printf("image too small\r\n");
		free(metadata);
		FS_Close(fd);
		return -EINVAL;
	}

	/*
	 * Capture scfm.img's on-disk layout so the payload can be read straight
	 * from the SLC block device after SFFS is unmounted.
	 */
	ret = SFFS_Snapshot(fd, &snap);
	FS_Close(fd);
	if (ret) {
		log_printf("snapshot failed: %d\r\n", ret);
		free(metadata);
		return ret;
	}

	scfm.metadata = metadata;
	scfm.size = (u64)size;
	scfm.metadataSize = metadataSize;
	scfm.slotCount = slotCount;
	scfm.slotSize = slotSize;
	scfm.snap = snap;
	scfm.active = true;
	log_printf("active (lazy): %u slots x 0x%x bytes\r\n", slotCount, slotSize);
	return 0;
}

void S_SCFMClear(void) {
	if (scfm.metadata)
		free(scfm.metadata);
	SFFS_SnapshotFree(&scfm.snap);
	memset(&scfm, 0, sizeof(scfm));
}

ssize_t S_SCFMReadMLC(struct blockDevice *bdev, void *buf, u64 off, size_t len) {
	u8 *out = buf;
	u32 firstSect, numSect;

	if (!bdev || !buf || (off & (SCFM_SECTOR_SIZE - 1)) || (len & (SCFM_SECTOR_SIZE - 1)))
		return -EINVAL;
	if (!scfm.active)
		return B_ReadDevice(bdev, buf, len, off);

	firstSect = (u32)(off / SCFM_SECTOR_SIZE);
	numSect = (u32)(len / SCFM_SECTOR_SIZE);
	while (numSect) {
		u32 inLine = firstSect % SCFM_CACHE_LINE_SECTORS;
		u32 chunk = SCFM_CACHE_LINE_SECTORS - inLine; /* to end of this cache line */
		u64 fileOff;
		bool cached = scfmLookupSector(firstSect, &fileOff);
		size_t bytes;
		ssize_t r;

		if (chunk > numSect)
			chunk = numSect;
		bytes = (size_t)chunk * SCFM_SECTOR_SIZE;
		/* Cache validity and slot payload are both uniform within one line. */
		if (cached)
			r = SFFS_SnapshotRead(&scfm.snap, out, fileOff, bytes);
		else
			r = B_ReadDevice(bdev, out, bytes, (u64)firstSect * SCFM_SECTOR_SIZE);
		if (r != (ssize_t)bytes)
			return r < 0 ? r : -EIO;
		firstSect += chunk;
		numSect -= chunk;
		out += bytes;
	}
	return (ssize_t)len;
}
