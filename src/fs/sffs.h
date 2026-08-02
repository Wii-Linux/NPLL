/*
 * NPLL - Filesystems - SFFS
 * Copyright (C) 2026 Techflash
 */

#ifndef _FS_SFFS_H
#define _FS_SFFS_H

#include <npll/types.h>

struct partition;

extern struct filesystem FS_SFFS;

/*
 * A captured file layout that outlives the mount: its cluster chain plus the
 * block device it lives on. The Wii U SCFM overlay uses this to read scfm.img
 * on demand directly from the SLC after SFFS is no longer the mounted FS,
 * instead of preloading the whole 128 MiB image.
 */
struct sffsSnapshot {
	struct partition *part;
	u32 firstDataCluster;
	u32 size;
	u32 clusterCount;
	u16 *clusters;          /* malloc'd chain, freed by SFFS_SnapshotFree */
};

/* Capture an open file's layout. SFFS must currently be mounted. */
int SFFS_Snapshot(int fd, struct sffsSnapshot *out);

/*
 * Read `len` bytes at file offset `off` through a snapshot (plaintext, no
 * decryption). Returns bytes read, or negative errno.
 */
ssize_t SFFS_SnapshotRead(const struct sffsSnapshot *snap, void *dst, u64 off, size_t len);

/* Release a snapshot's cluster chain. */
void SFFS_SnapshotFree(struct sffsSnapshot *snap);

#endif /* _FS_SFFS_H */
