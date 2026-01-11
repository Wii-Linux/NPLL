/*
 * NPLL - libc - assert
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _ASSERT_H
#define _ASSERT_H

#include <npll/panic.h>
#include <npll/utils.h>

#define assert(cond) if (!(cond)) { panic("Assertion " __stringify(cond) " failed"); }

#endif /* _ASSERT_H */
