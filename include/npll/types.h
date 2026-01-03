/*
 * NPLL - Common types
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _TYPES_H
#define _TYPES_H

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

#define NULL ((void *)0)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ > 201710L
/* bool, true, and false are already defined */
#else /* __STDC_VERSION__ && __STDC_VERSION__ > 201710L */
typedef int bool;
#define true ((bool)1)
#define false ((bool)0)
#endif /* !(__STDC_VERSION__ && __STDC_VERSION__ > 201710L) */

typedef u32 size_t;

#endif /* _TYPES_H */
