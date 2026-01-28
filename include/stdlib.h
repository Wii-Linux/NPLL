/*
 * NPLL - libc - stdlib
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _STDLIB_H
#define _STDLIB_H

/* gets us malloc/free */
#include <npll/allocator.h>
/* for NULL and whatnot */
#include <stddef.h>

long strtol(const char *str, char **endPtr, int base);
unsigned long strtoul(const char *str, char **endPtr, int base);
long long strtoll(const char *str, char **endPtr, int base);
unsigned long long strtoull(const char *str, char **endPtr, int base);

#endif /* _STDLIB_H */
