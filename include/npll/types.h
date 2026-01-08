/*
 * NPLL - Common types
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _TYPES_H
#define _TYPES_H

#include <stdbool.h>

typedef unsigned char       u8;
typedef volatile u8         vu8;
typedef char                i8;
typedef volatile i8         vi8;

typedef unsigned short      u16;
typedef volatile u16        vu16;
typedef short               i16;
typedef volatile i16        vi16;

typedef unsigned int        u32;
typedef volatile u32        vu32;
typedef int                 i32;
typedef volatile i32        vi32;


typedef unsigned long long  u64;
typedef volatile u64        vu64;
typedef long long           i64;
typedef volatile i64        vi64;


/* stddef.h compat */
typedef i32 ssize_t;
typedef u32 size_t;
typedef u32 ptrdiff_t;
typedef u32 uintptr_t;
typedef i32 intmax_t;
typedef u32 uintmax_t;

#define NULL ((void *)0)

#endif /* _TYPES_H */
