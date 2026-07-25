/*
 * NPLL - Filesystems - ext4
 * Copyright (C) 2026 Techflash
 */

#define MODULE "ext4"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ext4.h>
#include <ext4_blockdev.h>
#include <ext4_errno.h>
#include <npll/block.h>
#include <npll/endian.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/partition.h>
#include <npll/types.h>
#include <npll/utils.h>

#define MAX_FILES 16
#define EXT4_DEVICE_NAME "npll-ext"
#define EXT_SUPERBLOCK_OFFSET 1024
#define EXT_SUPERBLOCK_MAGIC_OFFSET 56
#define EXT_SUPERBLOCK_MAGIC 0xef53
#define MAX_PHYSICAL_BLOCK_SIZE 4096

struct filesystem FS_EXT4;

static struct partition *mountedPart;
static ext4_file openFiles[MAX_FILES];
static u8 ALIGN(32) physicalBlockBuffer[MAX_PHYSICAL_BLOCK_SIZE];
static struct ext4_blockdev_iface blockIface;
static struct ext4_blockdev blockDev;
static bool journalStarted;

#define VALIDATE_FD(ret) \
	if (fd < 0 || fd >= MAX_FILES || !openFiles[fd].mp) { return ret; }

static int allocateFd(void) {
	int i;

	for (i = 0; i < MAX_FILES; i++) {
		if (!openFiles[i].mp)
			return i;
	}

	return -1;
}

static int blockOpen(struct ext4_blockdev *bdev) {
	(void)bdev;
	return mountedPart ? EOK : ENODEV;
}

static int blockRead(struct ext4_blockdev *bdev, void *buf, uint64_t block, uint32_t count) {
	u32 blockSize = bdev->bdif->ph_bsize;
	u64 off;
	size_t len;
	ssize_t ret;

	if (!mountedPart || block > mountedPart->size / blockSize ||
	    count > (size_t)-1 / blockSize)
		return EIO;
	off = block * blockSize;
	len = (size_t)count * blockSize;
	if (off > mountedPart->size ||
	    len > mountedPart->size - off)
		return EIO;

	ret = B_Read(mountedPart, buf, len, off);
	return ret == (ssize_t)len ? EOK : EIO;
}

static int blockWrite(struct ext4_blockdev *bdev, const void *buf, uint64_t block, uint32_t count) {
	u32 blockSize = bdev->bdif->ph_bsize;
	u64 off;
	size_t len;
	ssize_t ret;

	if (!mountedPart)
		return ENODEV;
	if (mountedPart->bdev->flags & BLOCK_FLAG_READ_ONLY)
		return EROFS;
	if (block > mountedPart->size / blockSize ||
	    count > (size_t)-1 / blockSize)
		return EIO;
	off = block * blockSize;
	len = (size_t)count * blockSize;
	if (off > mountedPart->size || len > mountedPart->size - off)
		return EIO;

	ret = B_Write(mountedPart, buf, len, off);
	return ret == (ssize_t)len ? EOK : EIO;
}

static int blockClose(struct ext4_blockdev *bdev) {
	(void)bdev;
	return EOK;
}

static bool ext4Probe(struct filesystem *fs, struct partition *part) {
	u8 ALIGN(32) sb[EXT_SUPERBLOCK_MAGIC_OFFSET + sizeof(u16)];
	u16 magic;
	ssize_t ret;

	(void)fs;
	if (part->size < EXT_SUPERBLOCK_OFFSET + sizeof(sb))
		return false;

	ret = B_Read(part, sb, sizeof(sb), EXT_SUPERBLOCK_OFFSET);
	if (ret != (ssize_t)sizeof(sb))
		return false;

	memcpy(&magic, sb + EXT_SUPERBLOCK_MAGIC_OFFSET, sizeof(magic));
	return npll_le16_to_cpu(magic) == EXT_SUPERBLOCK_MAGIC;
}

static int ext4Mount(struct filesystem *fs, struct partition *part) {
	u32 blockSize = part->bdev->blockSize;
	int ret;

	if (!blockSize || blockSize > MAX_PHYSICAL_BLOCK_SIZE || part->size < blockSize || part->size % blockSize)
		return -EINVAL;

	memset(&blockIface, 0, sizeof(blockIface));
	memset(&blockDev, 0, sizeof(blockDev));
	memset(openFiles, 0, sizeof(openFiles));

	blockIface.open = blockOpen;
	blockIface.bread = blockRead;
	blockIface.bwrite = blockWrite;
	blockIface.close = blockClose;
	blockIface.ph_bsize = blockSize;
	blockIface.ph_bcnt = part->size / blockSize;
	blockIface.ph_bbuf = physicalBlockBuffer;
	blockIface.p_user = part;
	blockDev.bdif = &blockIface;
	blockDev.part_size = part->size;
	mountedPart = part;

	ret = ext4_device_register(&blockDev, EXT4_DEVICE_NAME);
	if (ret != EOK)
		goto fail;

	ret = ext4_mount(EXT4_DEVICE_NAME, !!(part->bdev->flags & BLOCK_FLAG_READ_ONLY));
	if (ret != EOK)
		goto unregister;

	if (!(part->bdev->flags & BLOCK_FLAG_READ_ONLY)) {
		ret = ext4_journal_start();
		if (ret != EOK)
			goto unmount;
		journalStarted = true;
	}

	fs->drvData = &blockDev;
	return 0;

unmount:
	ext4_umount();
unregister:
	ext4_device_unregister(EXT4_DEVICE_NAME);
fail:
	mountedPart = NULL;
	memset(&blockDev, 0, sizeof(blockDev));
	memset(&blockIface, 0, sizeof(blockIface));
	return -ret;
}

static void ext4Unmount(struct filesystem *fs) {
	int ret;

	if (journalStarted) {
		ret = ext4_journal_stop();
		if (ret != EOK)
			log_printf("ext4_journal_stop failed: %d\r\n", ret);
		journalStarted = false;
	}

	ret = ext4_umount();
	if (ret != EOK)
		log_printf("ext4_umount failed: %d\r\n", ret);
	ret = ext4_device_unregister(EXT4_DEVICE_NAME);
	if (ret != EOK)
		log_printf("ext4_device_unregister failed: %d\r\n", ret);

	memset(openFiles, 0, sizeof(openFiles));
	memset(&blockDev, 0, sizeof(blockDev));
	memset(&blockIface, 0, sizeof(blockIface));
	mountedPart = NULL;
	fs->drvData = NULL;
}

static int ext4Open(struct filesystem *fs, const char *path) {
	int fd, ret;

	(void)fs;
	fd = allocateFd();
	if (fd < 0)
		return -EMFILE;

	ret = ext4_fopen(&openFiles[fd], path, "r");
	if (ret != EOK) {
		memset(&openFiles[fd], 0, sizeof(openFiles[fd]));
		return -ret;
	}

	return fd;
}

static int ext4Create(struct filesystem *fs, const char *path) {
	int fd, ret;

	(void)fs;
	fd = allocateFd();
	if (fd < 0)
		return -EMFILE;

	ret = ext4_fopen(&openFiles[fd], path, "w");
	if (ret != EOK) {
		memset(&openFiles[fd], 0, sizeof(openFiles[fd]));
		return -ret;
	}

	return fd;
}

static void ext4Close(struct filesystem *fs, int fd) {
	int ret;

	(void)fs;
	VALIDATE_FD();
	ret = ext4_fclose(&openFiles[fd]);
	if (ret != EOK)
		log_printf("ext4_fclose failed: %d\r\n", ret);
	memset(&openFiles[fd], 0, sizeof(openFiles[fd]));
}

static ssize_t ext4Read(struct filesystem *fs, int fd, void *dest, size_t len) {
	size_t read;
	int ret;

	(void)fs;
	VALIDATE_FD(-EBADF);
	ret = ext4_fread(&openFiles[fd], dest, len, &read);
	return ret == EOK ? (ssize_t)read : -ret;
}

static ssize_t ext4Write(struct filesystem *fs, int fd, const void *src, size_t len) {
	size_t written;
	int ret;

	(void)fs;
	VALIDATE_FD(-EBADF);
	ret = ext4_fwrite(&openFiles[fd], src, len, &written);
	return ret == EOK ? (ssize_t)written : -ret;
}

static ssize_t ext4Seek(struct filesystem *fs, int fd, ssize_t off) {
	int ret;

	(void)fs;
	VALIDATE_FD(-EBADF);
	ret = ext4_fseek(&openFiles[fd], off, SEEK_SET);
	return ret == EOK ? off : -ret;
}

static ssize_t ext4GetSize(struct filesystem *fs, int fd) {
	uint64_t size;

	(void)fs;
	VALIDATE_FD(-EBADF);
	size = ext4_fsize(&openFiles[fd]);
	if (size > (uint64_t)((size_t)-1 >> 1))
		return -EFBIG;
	return (ssize_t)size;
}

struct filesystem VISIBLE FS_EXT4 = {
	.name = "ext2/3/4",
	.drvData = NULL,
	.probe = ext4Probe,
	.mount = ext4Mount,
	.unmount = ext4Unmount,
	.open = ext4Open,
	.create = ext4Create,
	.close = ext4Close,
	.read = ext4Read,
	.write = ext4Write,
	.seek = ext4Seek,
	.getSize = ext4GetSize,
	.flagMask = BLOCK_FLAG_STANDARD
};
