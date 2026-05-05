/*
 * NPLL - Filesystems - ISO9660
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "iso9660"
#include <errno.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/endian.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/partition.h>
#include <npll/utils.h>

#define MAX_FILES 16
#define FILENAME_MAX 64
#define SECTOR_SIZE 2048
#define readSects(part, dest, num, off) B_Read(part, dest, (num) * SECTOR_SIZE, (off) * SECTOR_SIZE)
#define ISO9660_PVD_IDENT "CD001"

#define FLAGS_HIDDEN    BIT(0)
#define FLAGS_DIR       BIT(1)
#define FLAGS_ASSOC     BIT(2)
#define FLAGS_XATTR     BIT(3)
#define FLAGS_OWNER_GRP BIT(4)
#define FLAGS_FINAL_REC BIT(7)

struct iso9660VolDesc;
static struct iso9660VolDesc pvd ALIGN(32);

static u32 rootSz;
static void *root;

static struct partition *mountedPart;

struct iso9660FdInfo {
	bool open;
	uint loc;
	uint size;
	uint pos;
};
static struct iso9660FdInfo files[MAX_FILES];
#define VALIDATE_FD(ret) if (fd < 0 || fd >= MAX_FILES || !files[fd].open) { return ret; }
static int allocateFd(void) {
	int i;

	for (i = 0; i < MAX_FILES; i++) {
		if (!files[i].open)
			return i;
	}

	return -1;
}

typedef struct {
	u16 le;
	u16 be;
} __attribute__((packed)) u16_LEBE;
typedef struct {
	u32 le;
	u32 be;
} __attribute__((packed)) u32_LEBE;

typedef struct {
	char year[4]; /* strD */
	char month[2]; /* strD */
	char day[2]; /* strD */
	char hour[2]; /* strD */
	char minute[2]; /* strD */
	char second[2]; /* strD */
	char hundredthSeconds[2]; /* strD */
	/* 15 min intervals; 0 = GMT-12, 100 = GMT+13 */
	u8 gmtOffset;
} __attribute__((packed)) iso9660Timestamp;

enum iso9660VolDescTypes {
	VOLDESC_TYPE_BOOT_REC = 0,
	VOLDESC_TYPE_PVD = 1,
	VOLDESC_TYPE_SVD = 2,
	VOLDESC_TYPE_VPD = 3,
	VOLDESC_TYPE_VDS_TERMINATOR = 255
};

/*
 * Volume Descriptors
 */

struct iso9660BootRecData {
	char bootSystemIdent[32]; /* strA */
	char bootIdent[32]; /* strA */
	u8 undefined[1977];
} __attribute__((packed));

/* Directory Record */
struct iso9660DirRecordHdr {
	u8 length;
	u8 extAttrRecordLen;
	u32_LEBE locationOfExtent;
	u32_LEBE dataLen;
	u8 recordingTimestamp[7];
	u8 flags;
	u8 fileUnitSizeInterleaved;
	u8 interleaveGapSize;
	u16_LEBE volumeSequenceNum;
	u8 fileNameLen;
	/* followed by fileNameLen bytes of file name, possibly a padding byte, and possibly more data */
} __attribute__((packed));

struct iso9660PVDData {
	u8 unused_07;
	char systemIdent[32]; /* strA */
	char volumeIdent[32]; /* strA */
	u8 unused_48[8];
	u32_LEBE volumeSpaceSize;
	u8 unused_58[32];
	u16_LEBE volumeSetSize;
	u16_LEBE volumeSeqNum;
	u16_LEBE logicalBlockSize;
	u32_LEBE pathTableSize;
	u32 typeLPathTableLocation__LE;
	u32 optionalTypeLPathTableLocation__LE;
	u32 typeMPathTableLocation__BE;
	u32 optionalTypeMPathTableLocation__BE;
	struct iso9660DirRecordHdr rootDirHdr;
	u8 rootDirRecordIdent;
	u8 rootDirRecordPadding;
	char volumeSetIdent[128]; /* strD */
	char publisherIdent[128]; /* strA */
	char dataPreparerIdent[128]; /* strA */
	char applicationIdent[128]; /* strA */
	char copyrightFileIdent[37]; /* strD */
	char abstractFileIdent[37]; /* strD */
	char bibliographicFileIdent[37]; /* strD */
	iso9660Timestamp volumeCreationTimestamp;
	iso9660Timestamp volumeModificationTimestamp;
	iso9660Timestamp volumeExpirationTimestamp;
	iso9660Timestamp volumeEffectiveTimestamp;
	u8 fileStructureVersion;
	u8 unused_372;
	u8 appReserved[512];
	u8 iso9660Reserved[653];
} __attribute__((packed));

struct iso9660VolDesc {
	u8 type;
	char ident[5]; /* strA */
	u8 version;
	union {
		struct iso9660BootRecData bootRecord;
		struct iso9660PVDData pvd;
		u8 u8[2041];
	} data;
};

static int iso9660LookupAt(const char *name, struct iso9660DirRecordHdr *from, struct iso9660DirRecordHdr *out) {
	int ret;
	uint off = 0;
	uint dirLen = from->dataLen.be;
	char fileName[FILENAME_MAX], *semicolon;
	void *buf;
	struct iso9660DirRecordHdr *rec;

	if (from == &pvd.data.pvd.rootDirHdr)
		buf = root; /* use cached root dir */
	else {
		buf = M_PoolAlloc(POOL_ANY, dirLen, 32);
		if (!buf)
			return -ENOMEM;

		ret = B_Read(mountedPart, buf, dirLen, from->locationOfExtent.be * SECTOR_SIZE);
		if (ret != (int)dirLen) {
			log_printf("iso9660LookupAt: B_Read failed: %d\r\n", ret);
			free(buf);
			return ret;
		}
	}

	while (off < dirLen) {
		rec = (struct iso9660DirRecordHdr *)(buf + off);

		if (rec->length == 0) {
			off = (off + SECTOR_SIZE) & ~((uint)SECTOR_SIZE - 1);
			continue;
		}

		if (off + sizeof(*rec) > dirLen || off + rec->length > dirLen) {
			log_printf("iso9660LookupAt: bogus record at off %u, len %u\r\n", off, rec->length);
			break;
		}

		/* can we even fit it? */
		if (rec->fileNameLen >= FILENAME_MAX || sizeof(*rec) + rec->fileNameLen > rec->length) {
			log_printf("file name too big (%u), skipping\r\n", rec->fileNameLen);
			goto cont;
		}

		/* stash the name */
		if (rec->fileNameLen == 1 && *(u8 *)((u32)rec + sizeof(*rec)) == 0)
			strcpy(fileName, ".");
		else if (rec->fileNameLen == 1 && *(u8 *)((u32)rec + sizeof(*rec)) == 1)
			strcpy(fileName, "..");
		else {
			memcpy(fileName, (void *)((u32)rec + sizeof(*rec)), rec->fileNameLen);
			fileName[rec->fileNameLen] = '\0';
		}

		/* trim trailing ';#' */
		semicolon = strchr(fileName, ';');
		if (semicolon)
			*semicolon = '\0';

		/*log_printf("Found a file: %s\r\n", fileName);*/
		/* is it the one we're looking for? */
		if (!strcmp(fileName, name)) {
			/*log_puts("Found it!!");*/
			memcpy(out, rec, sizeof(struct iso9660DirRecordHdr));
			if (from != &pvd.data.pvd.rootDirHdr)
				free(buf);
			return 0;
		}

cont:
		/* move on */
		off += rec->length;
	}

	if (from != &pvd.data.pvd.rootDirHdr)
		free(buf);
	return -ENOENT;
}

static int iso9660Lookup(const char *path, struct iso9660DirRecordHdr *out) {
	struct iso9660DirRecordHdr next, *cur, curScratch;
	const char *sep, *endOfComp;
	char comp[FILENAME_MAX];
	uint compLen;
	int ret;
	bool last = false;

	cur = &pvd.data.pvd.rootDirHdr;

	if (strlen(path) > FILENAME_MAX)
		return -EINVAL;

	while (true) {
		/* strip leading /'s */
		while (*path == '/')
			path++;

		sep = strchr(path, '/');
		if (!sep) {
			last = true;
			endOfComp = path + strlen(path);
		}
		else
			endOfComp = sep - 1;

		compLen = (uint)((endOfComp - path) + 1);

		memcpy(comp, path, compLen);
		comp[compLen] = '\0';

		/*log_printf("iso9660Lookup: Looking up %s\r\n", comp);*/

		ret = iso9660LookupAt(comp, cur, &next);
		if (ret)
			return ret;

		if (last)
			break;

		path = endOfComp + 1;
		cur = &curScratch;
		memcpy(&curScratch, &next, sizeof(next));
	}

	memcpy(out, &next, sizeof(struct iso9660DirRecordHdr));
	return 0;
}

static bool iso9660Probe(struct filesystem *fs, struct partition *part) {
	int ret;
	(void)fs;

	/* that's where the PVD lives */
	ret = readSects(part, &pvd, 1, 16);
	if (ret != SECTOR_SIZE) {
		log_printf("iso9660Probe: readSects failed: %d\r\n", ret);
		return false;
	}

	if (memcmp(pvd.ident, ISO9660_PVD_IDENT, sizeof(pvd.ident))) {
		log_puts("iso9660Probe: bad PVD ident");
		return false;
	}

	log_puts("iso9660Probe: Found valid PVD ident");

	return true;
}

static int iso9660Mount(struct filesystem *fs, struct partition *part) {
	int ret;
	(void)fs;

	rootSz = pvd.data.pvd.rootDirHdr.dataLen.be;
	if (rootSz > 512 * 1024) {
		log_printf("iso9660Mount: Bogus root dir size: %u\r\n", rootSz);
		return -ERANGE;
	}

	/* 32B aligned for DMA */
	root = M_PoolAlloc(POOL_ANY, rootSz, 32);
	ret = B_Read(part, root, rootSz, pvd.data.pvd.rootDirHdr.locationOfExtent.be * SECTOR_SIZE);
	if (ret != (int)rootSz) {
		free(root);
		log_printf("iso9660Mount: B_Read failed: %d\r\n", ret);
		return ret;
	}

	mountedPart = part;
	memset(files, 0, sizeof(files));

	return 0;
}

static void iso9660Unmount(struct filesystem *fs) {
	(void)fs;
	if (root) {
		free(root);
		root = NULL;
	}
	rootSz = 0;
	mountedPart = NULL;
	memset(files, 0, sizeof(files));
}

static int iso9660Open(struct filesystem *fs, const char *path) {
	int ret, fd;
	struct iso9660DirRecordHdr outDirHdr;
	(void)fs;

	fd = allocateFd();
	if (fd < 0)
		return -EMFILE;

	ret = iso9660Lookup(path, &outDirHdr);
	if (ret < 0)
		return ret;

	if (outDirHdr.flags & FLAGS_DIR)
		return -EISDIR;

	files[fd].open = true;
	files[fd].loc = outDirHdr.locationOfExtent.be;
	files[fd].size = outDirHdr.dataLen.be;
	files[fd].pos = 0;
	return fd;
}

static void iso9660Close(struct filesystem *fs, int fd) {
	(void)fs;
	VALIDATE_FD();
	files[fd].open = false;
}

static ssize_t iso9660Read(struct filesystem *fs, int fd, void *dest, size_t len) {
	struct iso9660FdInfo *file;
	(void)fs;
	VALIDATE_FD(-EBADF);

	file = &files[fd];

	/* clamp at EOF */
	if (file->pos >= file->size)
		return 0;
	if (len > (size_t)(file->size - file->pos))
		len = file->size - file->pos;

	return B_Read(mountedPart, dest, len, (file->loc * SECTOR_SIZE) + file->pos);
}

static ssize_t iso9660Seek(struct filesystem *fs, int fd, ssize_t off) {
	(void)fs;
	(void)off;
	VALIDATE_FD(-EBADF);

	if (off < 0 || (u64)off > (u64)files[fd].size)
		return (ssize_t)-1;

	files[fd].pos = (u32)off;
	return off;
}

static ssize_t iso9660GetSize(struct filesystem *fs, int fd) {
	(void)fs;
	VALIDATE_FD(-EBADF);
	return (ssize_t)files[fd].size;
}


struct filesystem FS_ISO9660 = {
	.name = "ISO9660",
	.drvData = NULL,
	.probe = iso9660Probe,
	.mount = iso9660Mount,
	.unmount = iso9660Unmount,
	.open = iso9660Open,
	.close = iso9660Close,
	.read = iso9660Read,
	.seek = iso9660Seek,
	.getSize = iso9660GetSize,
	.flagMask = BLOCK_FLAG_OPTICAL
};
