/*
 * NPLL - Hardware registers
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _REGS_H
#define _REGS_H

#include <npll/types.h>

#define _FLIPPER_REG(x) (*(vu32 *)(0xcc000000 + x))
#define _HOLLYWOOD_REG(x) (*(vu32 *)(0xcd800000 + x))
#define _LATTE_REG(x) _HOLLYWOOD_REG(x)

/* Flipper Registers */
#define PI_RESET   _FLIPPER_REG(0x3024)

/* Hollywood Registers */
#define HW_AHBPROT _HOLLYWOOD_REG(0x64)
#define HW_AIPROT  _HOLLYWOOD_REG(0x70)
#define HW_RESETS  _HOLLYWOOD_REG(0x194)
#  define RESETS_RSTBINB (1 << 0)
#define HW_VERSION _HOLLYWOOD_REG(0x214)


/* Latte Registers */
#define LT_CHIPREVID _LATTE_REG(0x5a0)

#endif /* _REGS_H */
