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

extern int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
extern int sprintf(char* buffer, const char* format, ...) __attribute__((format(printf, 2, 3)));
extern int snprintf(char* buffer, size_t count, const char* format, ...) __attribute__((format(printf, 3, 4)));
extern int vsnprintf(char* buffer, size_t count, const char* format, va_list va) __attribute__((format(printf, 3, 0)));
extern int vprintf(const char* format, va_list va) __attribute__((format(printf, 1, 0)));
extern int fctvprintf(void (*out)(char character, void* arg), void* arg, const char* format, va_list va) __attribute__((format(printf, 3, 0)));
extern int fctprintf(void (*out)(char character, void* arg), void* arg, const char* format, ...) __attribute__((format(printf, 3, 4)));

extern int puts(const char *s);
extern int putchar(int c);
extern void perror(const char *s);

#endif /* _STDIO_H */
