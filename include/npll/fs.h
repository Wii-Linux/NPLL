/*
 * NPLL - Filesystem abstraction
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _FS_H
#define _FS_H

#include <npll/block.h>
#include <npll/partition.h>
#include <npll/types.h>

/*
 * Filesystem driver.
 *
 * Each filesystem implementation (FAT, ext2, etc.) provides one of these.
 * Only one filesystem can be mounted at a time; all FS_* operations act
 * on the currently mounted filesystem.
 */
struct filesystem {
	/* human-readable name (e.g. "fat32", "ext2") */
	const char *name;

	/* optional driver-specific data (set during mount, cleared on unmount) */
	void *drvData;

	/*
	 * Probe for this filesystem on this partition.
	 * Returns true if this partitions contains the given filesystem.
	 */
	bool (*probe)(struct filesystem *fs, struct partition *part);

	/*
	 * Mount this filesystem on the given partition.
	 * Should allocate any internal state and store it in drvData.
	 * Returns 0 on success.
	 */
	int (*mount)(struct filesystem *fs, struct partition *part);

	/*
	 * Unmount the filesystem, freeing any internal state.
	 */
	void (*unmount)(struct filesystem *fs);

	/*
	 * Open a file by path.  Returns a file descriptor (>= 0) on success,
	 * negative errno on error.
	 */
	int (*open)(struct filesystem *fs, const char *path);

	/*
	 * Read up to len bytes from an open file descriptor into dest.
	 * Returns number of bytes read, 0 on EOF, negative on error.
	 */
	ssize_t (*read)(struct filesystem *fs, int fd, void *dest, size_t len);

	/*
	 * Seek to an exact position within an open file.
	 * Returns the new position, or (size_t)-1 on error.
	 */
	ssize_t (*seek)(struct filesystem *fs, int fd, ssize_t off);

	/* Close an open file descriptor. */
	void (*close)(struct filesystem *fs, int fd);
};

/* the currently mounted filesystem (NULL if none) */
extern struct filesystem *FS_Mounted;

/* the partition the current filesystem is mounted on (NULL if none) */
extern struct partition *FS_MountedPartition;

/* initialize the FS core */
extern void FS_Init(void);

/* shut down the FS core */
extern void FS_Shutdown(void);

/* mount a filesystem on a partition (unmounts any currently mounted FS first) */
extern int FS_Mount(struct filesystem *fs, struct partition *part);

/* unmount the currently mounted filesystem */
extern void FS_Unmount(void);

/* open a file on the mounted filesystem */
extern int FS_Open(const char *path);

/* read from an open file */
extern ssize_t FS_Read(int fd, void *dest, size_t len);

/* seek within an open file */
extern ssize_t FS_Seek(int fd, ssize_t off);

/* close an open file */
extern void FS_Close(int fd);

/* probe a partition and return the filesystem that it matches, or NULL if unknown / unsupported */
extern struct filesystem *FS_Probe(struct partition *part);

#endif /* _FS_H */
