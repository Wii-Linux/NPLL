/*
 * NPLL - ELF handling
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _ELF_H
#define _ELF_H

#include <npll/elf_abi.h>

extern int ELF_CheckValid(const void *data);
extern int ELF_LoadMem(const void *data);

#define ELF_ERR_WRONG_MAGIC -1
#define ELF_ERR_INVALID_EXEC -2

#endif /* _ELF_H */
