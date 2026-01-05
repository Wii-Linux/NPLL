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
#define _HOLLYWOOD_MC_REG(x) (*(vu16 *)(0xcd8b4200 + x))
#define _LATTE_REG(x) (*(vu32 *)(0xcd800400 + x))

/* Flipper Registers */
#define PI_RESET             _FLIPPER_REG(0x3024)

/* Hollywood Registers */
#define HW_IPC_PPCMSG        _HOLLYWOOD_REG(0x00)
#define HW_IPC_PPCCTRL       _HOLLYWOOD_REG(0x04)
#  define HW_IPC_PPCCTRL_X1      (1 << 0)
#  define HW_IPC_PPCCTRL_Y2      (1 << 1)
#  define HW_IPC_PPCCTRL_Y1      (1 << 2)
#  define HW_IPC_PPCCTRL_X2      (1 << 3)
#  define HW_IPC_PPCCTRL_IY1     (1 << 4)
#  define HW_IPC_PPCCTRL_IY2     (1 << 5)
#define HW_IPC_ARMMSG        _HOLLYWOOD_REG(0x08)
#define HW_IPC_ARMCTRL       _HOLLYWOOD_REG(0x0c)
#define HW_SRNPROT           _HOLLYWOOD_REG(0x60)
#  define SRNPROT_AHPEN          (1 << 3)
#define HW_AHBPROT           _HOLLYWOOD_REG(0x64)
#  define AHBPROT_PPCKERN        (1 << 31)
#define HW_AIPROT            _HOLLYWOOD_REG(0x70)
#define HW_RESETS            _HOLLYWOOD_REG(0x194)
#  define RESETS_RSTBINB         (1 << 0)
#define HW_VERSION           _HOLLYWOOD_REG(0x214)
#define HW_MEM_PROT_SPL      _HOLLYWOOD_MC_REG(0x4)
#define HW_MEM_PROT_SPL_BASE _HOLLYWOOD_MC_REG(0x6)
#define HW_MEM_PROT_SPL_END  _HOLLYWOOD_MC_REG(0x8)
#define HW_MEM_PROT_DDR      _HOLLYWOOD_MC_REG(0xa)
#define HW_MEM_PROT_DDR_BASE _HOLLYWOOD_MC_REG(0xc)
#define HW_MEM_PROT_DDR_END  _HOLLYWOOD_MC_REG(0xe)


/* Latte Registers */
#define LT_CHIPREVID _LATTE_REG(0x1a0)
#define LT_PIMCOMPAT _LATTE_REG(0x1b0)

#endif /* _REGS_H */
