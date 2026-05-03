/*
 * NPLL - Filesystems - SFFS
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "sffs"
#include <errno.h>
#include <string.h>
#include <npll/block.h>
#include <npll/endian.h>
#include <npll/fs.h>
#include <npll/hollywood/aes.h>
#include <npll/hollywood/otp.h>
#include <npll/log.h>
#include <npll/partition.h>
#include <npll/utils.h>

#define MAX_FILES 16

/* relative to start of SFFS partition, in pages */
#define SFFS_SUPERBLOCK_OFFSET 0x3f600u
#define SFFS_SUPERBLOCK_PAGES (sizeof(struct sffs_superblock) / NAND_PAGE_SIZE)
#define NAND_PAGE_SIZE 2048
#define SFFS_PAGES_PER_CLUSTER 8u
#define SFFS_CLUSTER_SIZE (NAND_PAGE_SIZE * SFFS_PAGES_PER_CLUSTER)
#define SFFS_FIRST_DATA_CLUSTER 0x40u

/* lengths of the FAT and FST arrays embedded in the superblock */
#define SFFS_FAT_LEN 0x8000u
#define SFFS_FST_LEN 0x17ffu

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
static struct sffs_superblock sb ALIGN(32);
struct sffsFdInfo {
	bool open;
	u16 firstClust;
	u32 size;
	u32 pos;
};
static struct sffsFdInfo files[MAX_FILES];
#define VALIDATE_FD(ret) if (fd < 0 || fd >= MAX_FILES || !files[fd].open) { return ret; }
static int allocateFd(void) {
	int i;

	for (i = 0; i < MAX_FILES; i++) {
		if (!files[i].open)
			return i;
	}

	return -1;
}

static int findNewestSuperblock(struct partition *part, uint *off) {
	uint offset, bestOffset = 0, bestGen = 0, i;
	bool found = false;
	ssize_t ret;

	for (i = 0; i < 16; i++) {
		offset = SFFS_SUPERBLOCK_OFFSET + i * SFFS_SUPERBLOCK_PAGES;

		ret = nandReadPage(part, &sb, NAND_PAGE_SIZE, offset);
		if (ret != NAND_PAGE_SIZE) {
			log_printf("findNewestSuperblock: nandReadPage failed: %d\r\n", ret);
			return -EIO;
		}

		if (memcmp(sb.magic, "SFFS", 4) != 0)
			continue;

		if (!found || sb.genNum > bestGen) {
			found = true;
			bestGen = sb.genNum;
			bestOffset = offset;
		}
	}

	if (!found)
		return -ENODEV;

	*off = bestOffset;
	return 0;
}

static u16 sffsWalkChain(u16 first, u32 n) {
	u16 cluster = first;

	while (n--) {
		if (cluster >= SFFS_FAT_LEN)
			return CLUSTER_END;
		cluster = sb.fat[cluster];
	}
	if (cluster >= SFFS_FAT_LEN)
		return CLUSTER_END;
	return cluster;
}

static int sffsLookupEntry(const char *path, struct sffs_fst_entry **out) {
	struct sffs_fst_entry *current = &sb.fst[0];
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
		if (!MODE_IS_DIR(current->mode))
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
		current = &sb.fst[current->sub];
		while (true) {
			if (!memcmp(current->fileName, component, 12))
				break;
			if (current->sib >= SFFS_FST_LEN)
				return -ENOENT;
			current = &sb.fst[current->sib];
		}
	}

	*out = current;
	return 0;
}

static bool sffsProbe(struct filesystem *fs, struct partition *part) {
	uint off;
	(void)fs;

	if (strcmp(part->bdev->name, "nand0") || part->index != 2)
		return false;

	return findNewestSuperblock(part, &off) == 0;
}

static int sffsMount(struct filesystem *fs, struct partition *part) {
	uint off;
	int ret;
	uint i;
	(void)fs;

	ret = findNewestSuperblock(part, &off);
	if (ret)
		return ret;

	log_printf("sffsMount: off=%u, sb.genNum=%u\r\n", off, sb.genNum);
	/* read the rest of it */
	for (i = 0; i < 128; i++) {
		ret = nandReadPage(part, ((void *)&sb) + (i * NAND_PAGE_SIZE), NAND_PAGE_SIZE, off + i);
		if (ret != NAND_PAGE_SIZE) {
			log_printf("sffsMount: nandReadPage failed: %d\r\n", ret);
			return -EIO;
		}
	}

	memset(files, 0, sizeof(files));
	mountedPart = part;

	return 0;
}

static void sffsUnmount(struct filesystem *fs) {
	(void)fs;
	memset(files, 0, sizeof(files));
	mountedPart = NULL;
}

static int sffsOpen(struct filesystem *fs, const char *path) {
	struct sffs_fst_entry *entry;
	int ret, fd;
	(void)fs;

	fd = allocateFd();
	if (fd < 0)
		return -EMFILE;

	ret = sffsLookupEntry(path, &entry);
	if (ret < 0)
		return ret;

	if (!MODE_IS_FILE(entry->mode))
		return -EISDIR;

	files[fd].open = true;
	files[fd].firstClust = entry->sub;
	files[fd].size = entry->size;
	files[fd].pos = 0;
	return fd;
}

static void sffsClose(struct filesystem *fs, int fd) {
	(void)fs;
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

	memcpy(key, H_OTPContents.wii.nandKey, sizeof(key));
	memset(iv, 0, sizeof(iv));

	while (total < len) {
		clusterIdx = file->pos / SFFS_CLUSTER_SIZE;
		clusterOff = file->pos % SFFS_CLUSTER_SIZE;
		pageNum = clusterOff / NAND_PAGE_SIZE;
		pageOff = clusterOff % NAND_PAGE_SIZE;
		toCopy = NAND_PAGE_SIZE - pageOff;

		if (toCopy > len - total)
			toCopy = (u32)(len - total);

		/* if this was a super common operation this could be cached, but eh */
		cluster = sffsWalkChain(file->firstClust, clusterIdx);
		if (cluster == CLUSTER_END)
			break;
		if (cluster < SFFS_FIRST_DATA_CLUSTER)
			return total > 0 ? (ssize_t)total : -EIO;

		page = ((u32)cluster - SFFS_FIRST_DATA_CLUSTER) * SFFS_PAGES_PER_CLUSTER + pageNum;
		if (nandReadPage(mountedPart, encBuf, NAND_PAGE_SIZE, page) != NAND_PAGE_SIZE)
			return total > 0 ? (ssize_t)total : -EIO;

		if (!pageNum)
			ret = H_AESDecrypt(encBuf, decBuf, iv, key, NAND_PAGE_SIZE);
		else
			ret = H_AESDecrypt(encBuf, decBuf, NULL, NULL, NAND_PAGE_SIZE);

		if (ret)
			return total > 0 ? (ssize_t)total : ret;

		memcpy((u8 *)dest + total, decBuf + pageOff, toCopy);
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
};
