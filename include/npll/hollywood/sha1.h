/*
 * NPLL - Hollywood/Latte Hardware - SHA-1 Engine
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _HOLLYWOOD_SHA1_H
#define _HOLLYWOOD_SHA1_H

#include <npll/types.h>

#define SHA1_DIGEST_SIZE 20

extern int H_SHA1Process(const void *in, u32 *out, size_t size);

#endif /* _HOLLYWOOD_SHA1_H */
