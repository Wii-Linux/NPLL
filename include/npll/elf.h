/*
 * NPLL - ELF handling
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _ELF_H
#define _ELF_H

#include <npll/utils.h>
#include <npll/elf_abi.h>

extern int ELF_CheckValid(const void *data);
extern int ELF_LoadMem(const void *data);
extern int ELF_LoadFile(int fd);
extern int ELF_LoadLinuxFile(int fd, const void *dtb, const void *initrd, u32 initrdSize, const char *cmdline, u32 cmdlineFlags);
extern void __attribute__((noreturn)) ELF_DoEntry(u32 arg3, u32 arg4, u32 arg5, const void *entry, bool keepCaches);

#define ELF_LINUX_CMDLINE_DKP    BIT(0)
#define ELF_LINUX_CMDLINE_LNXLDR BIT(1)

#define ELF_ERR_WRONG_MAGIC -1
#define ELF_ERR_INVALID_EXEC -2
#define ELF_ERR_FS_ERROR -3

#endif /* _ELF_H */
