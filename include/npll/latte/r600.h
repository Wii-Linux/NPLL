/*
 * NPLL - Latte R600 GPU registers
 *
 * Copyright (C) 2026 Techflash
 *
 * Based on drivers/gpu/drm/r600fb/r600fb_reg.h in linux-wiiu:
 * Copyright (C) 2018 Ash Logan <quarktheawesome@gmail.com>
 * Copyright (C) 2018 Roberto Van Eeden <rwrr0644@gmail.com>
 *
 * Based on AMD RV630 Reference Guide
 * Copyright (C) 2007 Advanced Micro Devices, Inc.
 *
 */

#ifndef _LATTE_R600_H
#define _LATTE_R600_H

#include <npll/utils.h>
#define R600_BASE (UNCACHED_BASE + 0x0c200000)
#define D1(x) (*(vu32 *)(R600_BASE + 0x6100 + x))

#define DGRPH_ENABLE D1(0x0000)
    #define DGRPH_ENABLE_REG 0x1
#define DGRPH_CONTROL D1(0x0004)
    #define DGRPH_DEPTH 0x3
        #define DGRPH_DEPTH_8BPP 0x0
        #define DGRPH_DEPTH_16BPP 0x1
        #define DGRPH_DEPTH_32BPP 0x2
        #define DGRPH_DEPTH_64BPP 0x3
    #define DGRPH_FORMAT 0x700
        #define DGRPH_FORMAT_8BPP_INDEXED 0x000
        #define DGRPH_FORMAT_16BPP_ARGB1555 0x000
        #define DGRPH_FORMAT_16BPP_RGB565 0x100
        #define DGRPH_FORMAT_16BPP_ARGB4444 0x200
        //todo: 16bpp alpha index 88, mono 16, brga 5551
        #define DGRPH_FORMAT_32BPP_ARGB8888 0x000
        #define DGRPH_FORMAT_32BPP_ARGB2101010 0x100
        #define DGRPH_FORMAT_32BPP_DIGITAL 0x200
        #define DGRPH_FORMAT_32BPP_8ARGB2101010 0x300
        #define DGRPH_FORMAT_32BPP_BGRA1010102 0x400
        #define DGRPH_FORMAT_32BPP_8BGRA1010102 0x500
        #define DGRPH_FORMAT_32BPP_RGB111110 0x600
        #define DGRPH_FORMAT_32BPP_BGR101111 0x700
        //todo: 64bpp
    #define DGRPH_ADDRESS_TRANSLATION 0x10000
        #define DGRPH_ADDRESS_TRANSLATION_PHYS 0x00000
        #define DGRPH_ADDRESS_TRANSLATION_VIRT 0x10000
    #define DGRPH_PRIVILEGED_ACCESS 0x20000
        #define DGRPH_PRIVILEGED_ACCESS_DISABLE 0x00000
        #define DGRPH_PRIVILEGED_ACCESS_ENABLE 0x20000
    #define DGRPH_ARRAY_MODE 0xF00000
        #define DGRPH_ARRAY_LINEAR_GENERAL 0x000000
        #define DGRPH_ARRAY_LINEAR_ALIGNED 0x100000
        #define DGRPH_ARRAY_1D_TILES_THIN1 0x200000
        //todo: rest of these array modes

#define DGRPH_SWAP_CNTL D1(0x000C)
	#define DGRPH_ENDIAN_SWAP 0x3
		#define DGRPH_ENDIAN_SWAP_NONE 0x0
		#define DGRPH_ENDIAN_SWAP_16 0x1
		#define DGRPH_ENDIAN_SWAP_32 0x2
		#define DGRPH_ENDIAN_SWAP_64 0x3
	#define DGRPH_RED_CROSSBAR 0x30
		#define DGRPH_RED_CROSSBAR_R 0x00
		#define DGRPH_RED_CROSSBAR_G 0x10
		#define DGRPH_RED_CROSSBAR_B 0x20
		#define DGRPH_RED_CROSSBAR_A 0x30
	#define DGRPH_GREEN_CROSSBAR 0xC0
		#define DGRPH_GREEN_CROSSBAR_G 0x00
		#define DGRPH_GREEN_CROSSBAR_B 0x40
		#define DGRPH_GREEN_CROSSBAR_A 0x80
		#define DGRPH_GREEN_CROSSBAR_R 0xC0
	#define DGRPH_BLUE_CROSSBAR 0x300
		#define DGRPH_BLUE_CROSSBAR_B 0x000
		#define DGRPH_BLUE_CROSSBAR_A 0x100
		#define DGRPH_BLUE_CROSSBAR_R 0x200
		#define DGRPH_BLUE_CROSSBAR_G 0x300
	#define DGRPH_ALPHA_CROSSBAR 0xC00
		#define DGRPH_ALPHA_CROSSBAR_A 0x000
		#define DGRPH_ALPHA_CROSSBAR_R 0x400
		#define DGRPH_ALPHA_CROSSBAR_G 0x800
		#define DGRPH_ALPHA_CROSSBAR_B 0xC00

#define DGRPH_CROSSBAR_RGBA(r, g, b, a) (DGRPH_RED_CROSSBAR_ ## r | DGRPH_GREEN_CROSSBAR_ ## g | DGRPH_BLUE_CROSSBAR_ ## b | DGRPH_ALPHA_CROSSBAR_ ## a)

#endif /* _LATTE_R600_H */
