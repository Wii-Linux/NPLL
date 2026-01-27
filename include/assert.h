/*
 * NPLL - libc - assert
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _ASSERT_H
#define _ASSERT_H

#include <npll/panic.h>
#include <npll/utils.h>

#ifdef NDEBUG
#define assert(cond) __assume(cond)
#define assert_msg(cond, msg) __assume(cond)
#else
#define assert(cond) assert_msg(cond, "Assertion " __stringify(cond) " failed")
#define assert_msg(cond, msg) if (!(cond)) { panic(msg); }
#endif /* !NDEBUG */

#endif /* _ASSERT_H */
