/*
 * NPLL - libc - stdring
 *
 * Copyright (C) 2025 Techflash
 *
 * Based on code from EverythingNet:
 * Copyright (C) 2023-2025 Techflash
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int memcmp(const void *s1, const void *s2, size_t n) {
	const uint8_t *a = s1;
	const uint8_t *b = s2;
	for (size_t i = 0; i < n; i++) {
		if (a[i] != b[i]) return a[i] - b[i];
	}

	return 0;
}

void *memcpy(void *dest, const void *src, size_t n) {
	for (size_t i = 0; i < n; i++) {
		((uint8_t*)dest)[i] = ((const uint8_t*)src)[i];
	}

	return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
	uint8_t *dst;
	const uint8_t *srcTmp;
	dst = (uint8_t *)dest;
	srcTmp = (const uint8_t *)src;

	if (dst < srcTmp) {
		for (size_t i = 0; i < n; i++) {
			dst[i] = srcTmp[i];
		}
	}
	else {
		for (size_t i = n; i != 0; i--) {
			dst[i - 1] = srcTmp[i - 1];
		}
	}
	return dest;
}


/*
 * str* functions
 */

char *strcat(char *dest, const char *src) {
	char *ptr = dest + strlen(dest);
	
	while (*src != '\0') {
		*ptr++ = *src++;
	}
	*ptr = '\0';

	return dest;
}

int strcmp(const char *str1, const char *str2) {
	while (*str1) {
		if (*str1 != *str2) {
			break;
		}

		str1++;
		str2++;
	}

	return *(const uint8_t *)str1 - *(const uint8_t *)str2;
}

char *strcpy(char *dest, const char *src) {
	char *ptr = dest;
	if (dest == NULL) {
		return NULL;
	}

	while (*src != '\0') {
		*dest = *src;
		dest++;
		src++;
	}
	*dest = '\0';

	return ptr;
}


size_t strlen(const char *str) {
	size_t i = 0;
	for (; str[i] != '\0'; i++);
	return i;
}

size_t strnlen(const char *str, size_t maxlen) {
	size_t i = 0;
	for (; str[i] != '\0' || i != maxlen; i++);
	return i;
}

char *strchr(const char *s, int c) {
	while (*s) {
		if (*s == c) return (char *)s;
		s++;
	}
	return NULL;
}


char *strstr(const char *haystack, const char *needle) {
	size_t size = strlen(needle);

	while (*haystack) {
		if (!memcmp(haystack, needle, size)) {
			return (char *)haystack;
		}
		haystack++;
	}
	return NULL;
}


