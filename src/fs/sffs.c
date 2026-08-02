/*
 * NPLL - Filesystems - SFFS
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "sffs"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <npll/block.h>
#include <npll/console.h>
#include <npll/endian.h>
#include <npll/fs.h>
#include <npll/hollywood/aes.h>
#include <npll/hollywood/otp.h>
#include <npll/log.h>
#include <npll/partition.h>
#include <npll/utils.h>
#include "sffs.h"

#define MAX_FILES 16

/* relative to start of SFFS partition, in pages */
#define SFFS_SUPERBLOCK_PAGES (sizeof(struct sffs_superblock) / NAND_PAGE_SIZE)
#define NAND_PAGE_SIZE 2048
#define SFFS_PAGES_PER_CLUSTER 8u
#define SFFS_CLUSTER_SIZE (NAND_PAGE_SIZE * SFFS_PAGES_PER_CLUSTER)
#define SFFS_FIRST_DATA_CLUSTER 0x40u

/* lengths of the FAT and FST arrays embedded in the superblock */
#define SFFS_FAT_LEN 0x8000u
#define SFFS_FST_LEN 0x17ffu

/* NAND DMA requires at least 32-byte-aligned destinations. */
#define MIN_READ_ALIGN 32u

#define nandReadPage(part, buf, len, off) B_Read(part, buf, len, (off) * NAND_PAGE_SIZE)

#define MODE_IS_FILE(mode)     (((mode) & 3) == 1)
#define MODE_IS_DIR(mode)      (((mode) & 3) == 2)
#define MODE_OWNER_PERMS(mode) ((mode) >> 6)
#define MODE_GROUP_PERMS(mode) (((mode) & 0x30) >> 4)
#define MODE_OTHER_PERMS(mode) (((mode) & 0x0c) >> 2)

#define CLUSTER_END_OF_CHAIN 0xfffbu
#define CLUSTER_RESERVED     0xfffcu
#define CLUSTER_BAD          0xfffdu
#define CLUSTER_FREE         0xfffeu
#define CLUSTER_END          0xffffu

enum sffsModeFormat {
	SFFS_MODE_FORMAT_WII,
	SFFS_MODE_FORMAT_WII_U
};

enum sffsKeySource {
	SFFS_KEY_WII_NAND,
	SFFS_KEY_WII_U_SLC
};

struct sffsLayout {
	char magic[4];
	u32 superblockOffset;
	/* number of rotating superblock generations to scan (Wii 16, Wii U 64) */
	u32 superCount;
	/*
	 * First on-disk cluster the partition maps to page 0. The Wii SFFS
	 * partition begins 0x40 clusters in (its offset skips the boot area), so
	 * cluster N sits at partition page (N - 0x40) * 8; the Wii U SLC partition
	 * begins at cluster 0, so no subtraction.
	 */
	u32 firstDataCluster;
	enum sffsModeFormat modeFormat;
	enum sffsKeySource keySource;
};

struct sffs_fst_entry {
	char fileName[12];
	u8 mode;
	u8 attributes;
	u16 sub;
	u16 sib;
	u32 size;
	u32 uid;
	u16 gid;
	u32 unk;
} __attribute__((packed));

struct sffs_superblock {
	char magic[4];
	u32 genNum;
	u32 unk;
	u16 fat[SFFS_FAT_LEN];
	struct sffs_fst_entry fst[SFFS_FST_LEN]; /* is that right? */
	u8 pad[20];
} __attribute__((packed));

static struct partition *mountedPart = NULL;
static const struct sffsLayout *mountedLayout = NULL;
static struct sffs_superblock *sb ALIGN(32);
struct sffsFdInfo {
	bool open;
	bool raw;	/* skip AES decryption (plaintext file, e.g. Wii U scfm.img) */
	u16 firstClust;
	u32 size;
	u32 pos;
	/* Cached FAT-chain position so sequential reads walk forward, not from
	 * the start every page (walkClust is the cluster at index walkIdx). */
	u32 walkIdx;
	u16 walkClust;
};
static struct sffsFdInfo files[MAX_FILES];
#define VALIDATE_FD(ret) if (fd < 0 || fd >= MAX_FILES || !files[fd].open) { return ret; }

static const struct sffsLayout layouts[] = {
	{
		.magic = { 'S', 'F', 'F', 'S' },
		.superblockOffset = 0x3f600u,
		.superCount = 16,
		.firstDataCluster = 0x40,
		.modeFormat = SFFS_MODE_FORMAT_WII,
		.keySource = SFFS_KEY_WII_NAND
	},
	{
		.magic = { 'S', 'F', 'S', '!' },
		.superblockOffset = 0x3e000u,
		.superCount = 64,
		.firstDataCluster = 0,
		.modeFormat = SFFS_MODE_FORMAT_WII_U,
		.keySource = SFFS_KEY_WII_U_SLC
	}
};

static int allocateFd(void) {
	int i;

	for (i = 0; i < MAX_FILES; i++) {
		if (!files[i].open)
			return i;
	}

	return -1;
}

static bool sffsModeIsFile(const struct sffs_fst_entry *entry) {
	if (mountedLayout && mountedLayout->modeFormat == SFFS_MODE_FORMAT_WII_U)
		return (entry->mode & 1) == 1;

	return MODE_IS_FILE(entry->mode);
}

static bool sffsModeIsDir(const struct sffs_fst_entry *entry) {
	if (mountedLayout && mountedLayout->modeFormat == SFFS_MODE_FORMAT_WII_U)
		return (entry->mode & 1) == 0;

	return MODE_IS_DIR(entry->mode);
}

static void sffsGetKey(u32 key[4]) {
	if (mountedLayout && mountedLayout->keySource == SFFS_KEY_WII_U_SLC)
		memcpy(key, H_OTPContents.wiiu.bank2.slcNANDKey, sizeof(H_OTPContents.wiiu.bank2.slcNANDKey));
	else
		memcpy(key, H_OTPContents.wii.nandKey, sizeof(H_OTPContents.wii.nandKey));
}

static int findNewestSuperblock(struct partition *part, const struct sffsLayout *layout, uint *off) {
	uint offset, bestOffset = 0, bestGen = 0, i;
	bool found = false;
	ssize_t ret;
	u8 buf[NAND_PAGE_SIZE] ALIGN(32);
	struct sffs_superblock *_sb = (struct sffs_superblock *)buf;

	for (i = 0; i < layout->superCount; i++) {
		offset = layout->superblockOffset + i * SFFS_SUPERBLOCK_PAGES;

		ret = nandReadPage(part, _sb, NAND_PAGE_SIZE, offset);
		if (ret != NAND_PAGE_SIZE) {
			log_printf("findNewestSuperblock: nandReadPage failed: %d\r\n", ret);
			return -EIO;
		}

		if (memcmp(_sb->magic, layout->magic, 4) != 0)
			continue;

		if (!found || _sb->genNum > bestGen) {
			found = true;
			bestGen = _sb->genNum;
			bestOffset = offset;
		}
	}

	if (!found)
		return -ENODEV;

	*off = bestOffset;
	sb = malloc(sizeof(*sb));
	memcpy(sb, _sb, sizeof(buf));
	return 0;
}

static const struct sffsLayout *findLayout(struct partition *part, uint *off) {
	uint i;
	int ret;

	if (!(part->bdev->flags & BLOCK_FLAG_HLWD_NAND))
		return NULL;

	for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
		ret = findNewestSuperblock(part, &layouts[i], off);
		if (ret < 0 && ret != -ENODEV)
			return NULL;

		if (ret == 0)
			return &layouts[i];
	}

	return NULL;
}

/*
 * Resolve the cluster at index `clusterIdx` of an open file, reusing the FD's
 * cached position to walk forward instead of restarting from firstClust. The
 * FAT is immutable (read-only FS), so the cache is a pure accelerator: forward
 * seeks step from it, backward seeks fall back to a full walk. Updates the
 * cache to the resolved position.
 */
static u16 sffsWalkCached(struct sffsFdInfo *file, u32 clusterIdx) {
	u16 cluster;
	u32 idx;

	if (file->walkClust < SFFS_FAT_LEN && clusterIdx >= file->walkIdx) {
		cluster = file->walkClust;
		idx = file->walkIdx;
	}
	else {
		cluster = file->firstClust;
		idx = 0;
	}
	while (idx < clusterIdx) {
		if (cluster >= SFFS_FAT_LEN) {
			cluster = CLUSTER_END;
			break;
		}
		cluster = sb->fat[cluster];
		idx++;
	}
	if (cluster >= SFFS_FAT_LEN)
		cluster = CLUSTER_END;
	file->walkIdx = clusterIdx;
	file->walkClust = cluster;
	return cluster;
}

static int sffsLookupEntry(const char *path, struct sffs_fst_entry **out) {
	struct sffs_fst_entry *current = &sb->fst[0];
	const char *p = path;
	char component[13], tmp[13];
	uint i;

	if (p == NULL)
		return -EINVAL;

	/* skip leading slashes, / alone returns the root entry */
	while (*p == '/')
		p++;

	while (*p) {
		memcpy(tmp, current->fileName, 12);
		tmp[12] = 0;
		/* current must be a dir if we're going to descend into it */
		if (!sffsModeIsDir(current))
			return -ENOTDIR;
		if (current->sub >= SFFS_FST_LEN)
			return -ENOENT; /* CLUSTER_END (empty dir) or corrupt index */

		/* extract the next path component */
		memset(component, 0, sizeof(component));
		i = 0;
		while (*p && *p != '/') {
			if (i >= 12)
				return -ENAMETOOLONG;
			component[i++] = *p++;
		}
		component[i] = '\0';

		/* skip the separator(s) before the next component */
		while (*p == '/')
			p++;

		/* search the children of `current` via the sibling list. */
		current = &sb->fst[current->sub];
		while (true) {
			if (!memcmp(current->fileName, component, 12))
				break;
			if (current->sib >= SFFS_FST_LEN)
				return -ENOENT;
			current = &sb->fst[current->sib];
		}
	}

	*out = current;
	return 0;
}

static bool sffsProbe(struct filesystem *fs, struct partition *part) {
	uint off;
	(void)fs;

	return findLayout(part, &off) != NULL;
}

static int sffsMount(struct filesystem *fs, struct partition *part) {
	const struct sffsLayout *layout;
	uint off;
	int ret;
	uint i;
	(void)fs;

	layout = findLayout(part, &off);
	if (!layout)
		return -ENODEV;

	log_printf("sffsMount: magic=%.4s off=%u, sb->genNum=%u\r\n", layout->magic, off, sb->genNum);
	/* read the rest of it */
	for (i = 0; i < 128; i++) {
		ret = nandReadPage(part, ((void *)sb) + (i * NAND_PAGE_SIZE), NAND_PAGE_SIZE, off + i);
		if (ret != NAND_PAGE_SIZE) {
			log_printf("sffsMount: nandReadPage failed: %d\r\n", ret);
			return -EIO;
		}
	}

	memset(files, 0, sizeof(files));
	mountedPart = part;
	mountedLayout = layout;

	return 0;
}

static void sffsUnmount(struct filesystem *fs) {
	(void)fs;
	memset(files, 0, sizeof(files));
	mountedPart = NULL;
	mountedLayout = NULL;
}

static int sffsOpen(struct filesystem *fs, const char *path) {
	struct sffs_fst_entry *entry;
	const char *base;
	int ret, fd;
	(void)fs;

	fd = allocateFd();
	if (fd < 0)
		return -EMFILE;

	ret = sffsLookupEntry(path, &entry);
	if (ret < 0)
		return ret;

	if (!sffsModeIsFile(entry))
		return -EISDIR;

	files[fd].open = true;
	/*
	 * The Wii U SCFM image (scfm.img at the SLC root) is stored unencrypted —
	 * IOSU accesses it with a plaintext-I/O flag, and nothing in SFFS marks it
	 * special — so read it raw rather than AES-decrypting it into garbage.
	 */
	base = path;
	while (*base == '/')
		base++;

	files[fd].raw = (H_ConsoleType == CONSOLE_TYPE_WII_U && !strcmp(base, "scfm.img"));
	files[fd].firstClust = entry->sub;
	files[fd].size = entry->size;
	files[fd].pos = 0;
	files[fd].walkIdx = 0;
	files[fd].walkClust = entry->sub;
	return fd;
}

static void sffsClose(struct filesystem *fs, int fd) {
	(void)fs;
	VALIDATE_FD();
	files[fd].open = false;
}


static ssize_t sffsRead(struct filesystem *fs, int fd, void *dest, size_t len) {
	u32 clusterIdx, clusterOff, pageNum, pageOff, toCopy, page;
	u16 cluster;
	size_t total;
	u8 encBuf[2048] ALIGN(32);
	u8 decBuf[2048] ALIGN(32);
	struct sffsFdInfo *file;
	u32 key[4];
	u32 iv[4] = {0, 0, 0, 0};
	bool cbcCont = false;
	int ret;

	(void)fs;

	VALIDATE_FD(-EBADF);

	total = 0;

	file = &files[fd];

	/* clamp at EOF */
	if (file->pos >= file->size)
		return 0;
	if (len > (size_t)(file->size - file->pos))
		len = file->size - file->pos;

	sffsGetKey(key);
	memset(iv, 0, sizeof(iv));

	while (total < len) {
		clusterIdx = file->pos / SFFS_CLUSTER_SIZE;
		clusterOff = file->pos % SFFS_CLUSTER_SIZE;
		pageNum = clusterOff / NAND_PAGE_SIZE;
		pageOff = clusterOff % NAND_PAGE_SIZE;
		toCopy = NAND_PAGE_SIZE - pageOff;

		if (toCopy > len - total)
			toCopy = (u32)(len - total);

		cluster = sffsWalkCached(file, clusterIdx);
		if (cluster == CLUSTER_END)
			break;

		/*
		 * Cluster N maps directly to partition page (N - firstDataCluster) * 8
		 * (the reference does a raw cluster * CLUSTER_PAGES; NPLL's per-layout
		 * firstDataCluster accounts for where the partition itself starts).
		 */
		page = (cluster - mountedLayout->firstDataCluster) * SFFS_PAGES_PER_CLUSTER + pageNum;

		/*
		 * Plaintext (raw) reads of a whole page can land straight in the
		 * caller's buffer, skipping the encBuf bounce. NAND is read one page at
		 * a time (per-page ECC), so this stays page-granular either way.
		 */
		if (file->raw && pageOff == 0 && toCopy == NAND_PAGE_SIZE &&
		    (((uintptr_t)dest + total) & (MIN_READ_ALIGN - 1)) == 0) {
			if (nandReadPage(mountedPart, (u8 *)dest + total, NAND_PAGE_SIZE, page) != NAND_PAGE_SIZE) {
				log_printf("sffsRead: page %u read failed (cluster 0x%04x idx %u)\r\n", page, cluster, clusterIdx);
				return total > 0 ? (ssize_t)total : -EIO;
			}
			total += toCopy;
			file->pos += toCopy;
			continue;
		}

		if (nandReadPage(mountedPart, encBuf, NAND_PAGE_SIZE, page) != NAND_PAGE_SIZE) {
			log_printf("sffsRead: page %u read failed (cluster 0x%04x idx %u)\r\n", page, cluster, clusterIdx);
			return total > 0 ? (ssize_t)total : -EIO;
		}

		if (file->raw) {
			/* Plaintext file: copy the NAND data verbatim, no decryption. */
			memcpy((u8 *)dest + total, encBuf + pageOff, toCopy);
		}
		else {
			if (!pageNum) {
				memset(iv, 0, sizeof(iv));
				ret = H_AESDecrypt(encBuf, decBuf, iv, key, NAND_PAGE_SIZE);
			}
			else if (cbcCont)
				ret = H_AESDecrypt(encBuf, decBuf, NULL, NULL, NAND_PAGE_SIZE);
			else {
				if (nandReadPage(mountedPart, decBuf, NAND_PAGE_SIZE, page - 1) != NAND_PAGE_SIZE) {
					log_printf("sffsRead: IV page %u read failed\r\n", page - 1);
					return total > 0 ? (ssize_t)total : -EIO;
				}
				memcpy(iv, decBuf + NAND_PAGE_SIZE - sizeof(iv), sizeof(iv));
				ret = H_AESDecrypt(encBuf, decBuf, iv, key, NAND_PAGE_SIZE);
			}

			if (ret)
				return total > 0 ? (ssize_t)total : ret;

			cbcCont = true;
			memcpy((u8 *)dest + total, decBuf + pageOff, toCopy);
		}
		total += toCopy;
		file->pos += toCopy;
	}

	return (ssize_t)total;
}

static ssize_t sffsSeek(struct filesystem *fs, int fd, ssize_t off) {
	(void)fs;
	(void)off;
	VALIDATE_FD(-EBADF);

	if (off < 0 || (u64)off > (u64)files[fd].size)
		return (ssize_t)-1;

	files[fd].pos = (u32)off;
	return off;
}

static ssize_t sffsGetSize(struct filesystem *fs, int fd) {
	(void)fs;
	VALIDATE_FD(-EBADF);

	return (ssize_t)files[fd].size;
}

int SFFS_Snapshot(int fd, struct sffsSnapshot *out) {
	struct sffsFdInfo *file;
	u16 *chain;
	u16 cluster;
	u32 count, i;

	if (fd < 0 || fd >= MAX_FILES || !files[fd].open)
		return -EBADF;
	if (!mountedPart || !mountedLayout || !out)
		return -ENODEV;

	file = &files[fd];
	count = (file->size + SFFS_CLUSTER_SIZE - 1) / SFFS_CLUSTER_SIZE;
	chain = count ? malloc(count * sizeof(*chain)) : NULL;
	if (count && !chain)
		return -ENOMEM;

	cluster = file->firstClust;
	for (i = 0; i < count; i++) {
		/* chain ends before the file size */
		if (cluster >= SFFS_FAT_LEN) {
			free(chain);
			return -EIO;
		}
		chain[i] = cluster;
		cluster = sb->fat[cluster];
	}

	out->part = mountedPart;
	out->firstDataCluster = mountedLayout->firstDataCluster;
	out->size = file->size;
	out->clusterCount = count;
	out->clusters = chain;
	return 0;
}

ssize_t SFFS_SnapshotRead(const struct sffsSnapshot *snap, void *dst, u64 off, size_t len) {
	u8 buf[NAND_PAGE_SIZE] ALIGN(32);
	u8 *out = dst;
	u16 cluster;
	u32 fileOff, clusterIdx, clusterOff, pageNum, pageOff, toCopy, page;
	size_t total = 0;

	if (!snap || !snap->clusters || !dst)
		return -EINVAL;
	if (off >= snap->size)
		return 0;
	if (len > snap->size - off)
		len = (size_t)(snap->size - off);

	while (total < len) {
		fileOff = (u32)(off + total);
		clusterIdx = fileOff / SFFS_CLUSTER_SIZE;
		clusterOff = fileOff % SFFS_CLUSTER_SIZE;
		pageNum = clusterOff / NAND_PAGE_SIZE;
		pageOff = clusterOff % NAND_PAGE_SIZE;
		toCopy = NAND_PAGE_SIZE - pageOff;

		if (clusterIdx >= snap->clusterCount)
			break;
		if (toCopy > len - total)
			toCopy = (u32)(len - total);

		cluster = snap->clusters[clusterIdx];
		page = (cluster - snap->firstDataCluster) * SFFS_PAGES_PER_CLUSTER + pageNum;
		if (nandReadPage(snap->part, buf, NAND_PAGE_SIZE, page) != NAND_PAGE_SIZE) {
			log_printf("SFFS_SnapshotRead: page %u read failed\r\n", page);
			return total > 0 ? (ssize_t)total : -EIO;
		}
		memcpy(out + total, buf + pageOff, toCopy);
		total += toCopy;
	}
	return (ssize_t)total;
}

void SFFS_SnapshotFree(struct sffsSnapshot *snap) {
	if (snap && snap->clusters) {
		free(snap->clusters);
		snap->clusters = NULL;
	}
}

struct filesystem FS_SFFS = {
	.name = "SFFS",
	.drvData = NULL,
	.probe = sffsProbe,
	.mount = sffsMount,
	.unmount = sffsUnmount,
	.open = sffsOpen,
	.close = sffsClose,
	.read = sffsRead,
	.seek = sffsSeek,
	.getSize = sffsGetSize,
	.flagMask = BLOCK_FLAG_HLWD_NAND
};
