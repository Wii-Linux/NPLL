/*
 * NPLL - libc - string
 *
 * Copyright (C) 2025 Techflash
 *
 * Based on code from EverythingNet:
 * Copyright (C) 2023-2025 Techflash
 */
#include <stddef.h>

extern int memcmp(const void *s1, const void *s2, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memmove(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t len);
extern char *strcat(char *dest, const char *src);
extern int strcmp(const char *str1, const char *str2);
extern char *strcpy(char *dest, const char *src);
extern size_t strlen(const char *str);
extern size_t strnlen(const char *str, size_t maxlen);
extern char *strchr(const char *s, int c);
extern char *strstr(const char *haystack, const char *needle);
