/*
 * NPLL - Hollywood/Latte Hardware - AES Engine
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _HOLLYWOOD_AES_H
#define _HOLLYWOOD_AES_H

#include <npll/types.h>

/* iv=NULL and key=NULL to continue CBC mode */
extern int H_AESEncrypt(const void *in, void *out, u32 *iv, u32 *key, size_t size);
/* iv=NULL and key=NULL to continue CBC mode */
extern int H_AESDecrypt(const void *in, void *out, u32 *iv, u32 *key, size_t size);
/* copy data in memory using the AES engine */
extern int H_AESCopy(const void *in, void *out, size_t size);

#endif /* _HOLLYWOOD_AES_H */
