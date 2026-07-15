/*
 * NPLL - PowerPC Linux direct-entry boot support
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _LINUX_H
#define _LINUX_H

#include <npll/allocator.h>
#include <npll/types.h>

struct linuxBootFiles {
	void *dtb;
	u32 dtbSize;
	void *initrd;
	u32 initrdSize;
};

extern int L_LoadAuxFile(int fd, enum pool_idx pool, void **dataOut, u32 extra, u32 *sizeOut);
extern int L_PrepareDTB(struct linuxBootFiles *files, const char *cmdline);

#endif /* _LINUX_H */
