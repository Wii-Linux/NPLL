/*
 * NPLL - Filesystems - SFFS
 * Copyright (C) 2026 Techflash
 */

#ifndef _FS_SFFS_H
#define _FS_SFFS_H

#include <npll/partition.h>

extern int FS_ProbeSFFS(struct partition *part);
extern struct filesystem FS_SFFS;

#endif /* _FS_SFFS_H */
