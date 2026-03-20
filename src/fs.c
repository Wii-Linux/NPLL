/*
 * NPLL - Filesystem core
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "fs"

#include <assert.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/types.h>

struct filesystem *FS_Mounted = NULL;
struct partition *FS_MountedPartition = NULL;

static bool initialized = false;

void FS_Init(void) {
	if (initialized)
		return;

	FS_Mounted = NULL;
	FS_MountedPartition = NULL;
	initialized = true;
}

int FS_Mount(struct filesystem *fs, struct partition *part) {
	int ret;

	assert_msg(initialized, "fs: FS_Mount w/o FS_Init");
	assert_msg(fs, "fs: FS_Mount with NULL filesystem");
	assert_msg(part, "fs: FS_Mount with NULL partition");
	assert_msg(fs->mount, "fs: filesystem has no mount op");

	/* unmount whatever is currently mounted */
	if (FS_Mounted)
		FS_Unmount();

	ret = fs->mount(fs, part);
	if (ret) {
		log_printf("failed to mount %s: %d\r\n", fs->name, ret);
		return ret;
	}

	FS_Mounted = fs;
	FS_MountedPartition = part;
	log_printf("mounted %s on partition %d of %s\r\n",
		   fs->name, part->index, part->bdev->name);
	return 0;
}

void FS_Unmount(void) {
	assert_msg(initialized, "fs: FS_Unmount w/o FS_Init");

	if (!FS_Mounted)
		return;

	if (FS_Mounted->unmount)
		FS_Mounted->unmount(FS_Mounted);

	log_printf("unmounted %s\r\n", FS_Mounted->name);
	FS_Mounted = NULL;
	FS_MountedPartition = NULL;
}

int FS_Open(const char *path) {
	assert_msg(initialized, "fs: FS_Open w/o FS_Init");
	assert_msg(FS_Mounted, "fs: FS_Open with no mounted filesystem");
	assert_msg(FS_Mounted->open, "fs: filesystem has no open op");

	return FS_Mounted->open(FS_Mounted, path);
}

ssize_t FS_Read(int fd, void *dest, size_t len) {
	assert_msg(initialized, "fs: FS_Read w/o FS_Init");
	assert_msg(FS_Mounted, "fs: FS_Read with no mounted filesystem");
	assert_msg(FS_Mounted->read, "fs: filesystem has no read op");

	return FS_Mounted->read(FS_Mounted, fd, dest, len);
}

ssize_t FS_Seek(int fd, ssize_t off) {
	assert_msg(initialized, "fs: FS_Seek w/o FS_Init");
	assert_msg(FS_Mounted, "fs: FS_Seek with no mounted filesystem");
	assert_msg(FS_Mounted->seek, "fs: filesystem has no seek op");

	return FS_Mounted->seek(FS_Mounted, fd, off);
}

void FS_Close(int fd) {
	assert_msg(initialized, "fs: FS_Close w/o FS_Init");
	assert_msg(FS_Mounted, "fs: FS_Close with no mounted filesystem");
	assert_msg(FS_Mounted->close, "fs: filesystem has no close op");

	FS_Mounted->close(FS_Mounted, fd);
}
