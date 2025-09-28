/*
 * NPLL - Panic / error handling
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _PANIC_H
#define _PANIC_H

extern void __attribute__((noreturn)) panic(const char *msg);

#endif /* _PANIC_H */
