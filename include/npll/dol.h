/*
 * NPLL - DOL handling
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _DOL_H
#define _DOL_H

#include <npll/types.h>

#define DOL_NUM_TEXT 7
#define DOL_NUM_DATA 11
#define DOL_NUM_SECTIONS (DOL_NUM_TEXT + DOL_NUM_DATA)

struct dolHdr {
	u32 sectOff[DOL_NUM_SECTIONS];
	u32 sectAddr[DOL_NUM_SECTIONS];
	u32 sectSize[DOL_NUM_SECTIONS];
	u32 bssAddr;
	u32 bssSize;
	u32 entry;
	u8 pad[28];
};

#define DOL_ERR_WRONG_MAGIC -1
#define DOL_ERR_INVALID_EXEC -2
#define DOL_ERR_FS_ERROR -3

extern int DOL_LoadFile(int fd);

#endif /* _DOL_H */
