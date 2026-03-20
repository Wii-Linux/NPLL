/*
 * NPLL - Endianness handling
 *
 * Copyright (C) 2026 Techflash
 *
 * Based on code from libppcemu:
 * Copyright (C) 2026 Techflash
 *
 * Based on code from EverythingNet:
 * Copyright (C) 2025 Techflah
 */

#ifndef _ENDIAN_H
#define _ENDIAN_H
#include <npll/types.h>

#if defined(__has_builtin)
#  if __has_builtin(__builtin_bswap16)
#    define NO_CUSTOM_SWAP16
#  endif
#  if __has_builtin(__builtin_bswap32)
#    define NO_CUSTOM_SWAP32
#  endif
#  if __has_builtin(__builtin_bswap64)
#    define NO_CUSTOM_SWAP64
#  endif
#endif

#ifdef NO_CUSTOM_SWAP16
#  define __npll_swap16 __builtin_bswap16
#  undef NO_CUSTOM_SWAP16
#else
static inline u16 __npll_swap16(u16 in) {
	return (u16)((in << 8) | (in >> 8));
}
#endif

#ifdef NO_CUSTOM_SWAP32
#  define __npll_swap32 __builtin_bswap32
#  undef NO_CUSTOM_SWAP32
#else
static inline u32 __npll_swap32(u32 in) {
	return (u32)(
		((u32)__npll_swap16((u16)in) << 16) |
		((u32)__npll_swap16((u16)(in >> 16)))
	);
}
#endif

#ifdef NO_CUSTOM_SWAP64
#  define __npll_swap64 __builtin_bswap64
#  undef NO_CUSTOM_SWAP64
#else
static inline u64 __npll_swap64(u64 in) {
	return (u64)(
		((u64)__npll_swap32((u32)in) << 32) |
		((u64)__npll_swap32((u32)(in >> 32)))
	);
}
#endif

/* to/from host endianness */
#  define npll_cpu_to_le64(x)  __npll_swap64(x)
#  define npll_cpu_to_le32(x)  __npll_swap32(x)
#  define npll_cpu_to_le16(x)  __npll_swap16(x)
#  define npll_cpu_to_be64(x)  x
#  define npll_cpu_to_be32(x)  x
#  define npll_cpu_to_be16(x)  x
#  define npll_le64_to_cpu(x)  __npll_swap64(x)
#  define npll_le32_to_cpu(x)  __npll_swap32(x)
#  define npll_le16_to_cpu(x)  __npll_swap16(x)
#  define npll_be64_to_cpu(x)  x
#  define npll_be32_to_cpu(x)  x
#  define npll_be16_to_cpu(x)  x

/* to/from specific endianness */
#  define npll_be64_to_le64(x) __npll_swap64(x)
#  define npll_be32_to_le32(x) __npll_swap32(x)
#  define npll_be16_to_le16(x) __npll_swap16(x)
#  define npll_le64_to_be64(x) __npll_swap64(x)
#  define npll_le32_to_be32(x) __npll_swap32(x)
#  define npll_le16_to_be16(x) __npll_swap16(x)

#endif /* _ENDIAN_H */
