/*
 * NPLL - Filesystems - FAT - FS Layer Glue
 * Copyright (C) 2026 Techflash
 */

#ifndef _FS_FAT_GLUE_H
#define _FS_FAT_GLUE_H

#include <npll/partition.h>

extern int FS_ProbeFAT_exFAT(struct partition *part);
extern struct filesystem FS_FAT;
extern struct filesystem FS_exFAT;

#endif /* _FS_FAT_GLUE_H */
