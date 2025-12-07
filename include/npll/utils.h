/*
 * NPLL - Preprocessor macros
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _UTILS_H
#define _UTILS_H

#define __assume(cond) do { if (!(cond)) __builtin_unreachable(); } while (0)
#define __likely(cond) __builtin_expect((cond), 1)
#define __unlikely(cond) __builtin_expect((cond), 0)


#define __stringify(str) #str
#define __stringifyResult(str) __stringify(str)

#endif /* _UTILS_H */
