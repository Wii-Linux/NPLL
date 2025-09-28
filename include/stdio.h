/*
 * NPLL - libc - stdio
 *
 * Copyright (C) 2025 Techflash
 *
 * Based on code from EverythingNet:
 * Copyright (C) 2025 Techflash
 */

#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>

extern int printf(const char *format, ...);
extern int sprintf(char* buffer, const char* format, ...);
extern int snprintf(char* buffer, size_t count, const char* format, ...);
extern int vsnprintf(char* buffer, size_t count, const char* format, va_list va);
extern int vprintf(const char* format, va_list va);

extern int puts(const char *s);
extern int putchar(int c);
extern void perror(const char *s);

#endif /* _STDIO_H */
