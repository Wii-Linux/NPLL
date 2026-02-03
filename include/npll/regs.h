/*
 * NPLL - Hardware registers
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _REGS_H
#define _REGS_H

#include <npll/utils.h>
#include <npll/types.h>

#define _FLIPPER_REG(x) (*(vu32 *)(0xcc000000 + x))
#define _FLIPPER_PI_REG(x) _FLIPPER_REG(0x3000 + x)
#define _HOLLYWOOD_REG(x) (*(vu32 *)(0xcd800000 + x))
#define _HOLLYWOOD_MC_REG(x) (*(vu16 *)(0xcd8b4200 + x))
#define _LATTE_REG(x) (*(vu32 *)(0xcd800400 + x))

/* Flipper Registers */
#define PI_INTSR             _FLIPPER_PI_REG(0x00)
#define PI_INTMR             _FLIPPER_PI_REG(0x04)
#  define PI_IRQDEV_GX_ERR       BIT(0)
#  define PI_IRQDEV_RSW          BIT(1)
#  define PI_IRQDEV_DI           BIT(2)
#  define PI_IRQDEV_SI           BIT(3)
#  define PI_IRQDEV_EXI          BIT(4)
#  define PI_IRQDEV_AI           BIT(5)
#  define PI_IRQDEV_DSP          BIT(6)
#  define PI_IRQDEV_MI           BIT(7)
#  define PI_IRQDEV_VI           BIT(8)
#  define PI_IRQDEV_PE_TOKEN     BIT(9)
#  define PI_IRQDEV_PE_FINISH    BIT(10)
#  define PI_IRQDEV_CP           BIT(11)
#  define PI_IRQDEV_DEBUG        BIT(12)
#  define PI_IRQDEV_HSP          BIT(13)
#  define PI_IRQDEV_HLWD         BIT(14)
#  define PI_IRQDEV_RSWST        BIT(16)
#  define PI_IRQDEV_LATTE        BIT(24)
#define PI_RESET             _FLIPPER_PI_REG(0x24)

/*
 * The chipid register is _weird_, super undocumented.
 * This is the best I could come up with regarding it's format:
 * Bits:            31...28               27...12                   11...0
 * Hex of RevC:        2                    4650                      0b1
 * Meaning:       Revision ID            Identifier?       Chip Stepping/nonsense filler?
 *            according to Dolphin       ASCII "FP"         Dolphin Emulator source shows
 *         and Swiss code, YAGCD is     for "FliPper"?           this as 0b0 for RevA
 *          *close*, calling it a
 *         hardware type.  All
 *         lines up w/ values from
 *         Dolphin Emulator source
 */
#define PI_CHIPID            _FLIPPER_PI_REG(0x2c)
#  define PI_CHIPID_REV          0xf0000000 /* some masks */
#  define PI_CHIPID_ID           0x0ffff000
#  define PI_CHIPID_UNK          0x00000fff

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
#define HW_PPCIRQFLAG        _HOLLYWOOD_REG(0x30)
#define HW_PPCIRQMASK        _HOLLYWOOD_REG(0x34)
#  define HW_IRQDEV_TIMER        BIT(0)
#  define HW_IRQDEV_NAND_IF      BIT(1)
#  define HW_IRQDEV_AES_ENG      BIT(2)
#  define HW_IRQDEV_SHA1_ENG     BIT(3)
#  define HW_IRQDEV_EHCI         BIT(4)
#  define HW_IRQDEV_OHCI0        BIT(5)
#  define HW_IRQDEV_OHCI1        BIT(6)
#  define HW_IRQDEV_SDHCI0       BIT(7)
#  define HW_IRQDEV_SDHCI1       BIT(8)
#  define HW_IRQDEV_GPIOB        BIT(10)
#  define HW_IRQDEV_GPIO         BIT(11)
#  define HW_IRQDEV_RSW          BIT(17)
#  define HW_IRQDEV_DI           BIT(18)
#  define HW_IRQDEV_IPCB         BIT(30)
#  define HW_IRQDEV_IPCS         BIT(31)
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
#define LT_PPC0INT1STS _LATTE_REG(0x40)
#define LT_PPC0INT2STS _LATTE_REG(0x44)
#define LT_PPC0INT1EN  _LATTE_REG(0x48)
#define LT_PPC0INT2EN  _LATTE_REG(0x4c)

#define LT_PPC1INT1STS _LATTE_REG(0x50)
#define LT_PPC1INT2STS _LATTE_REG(0x54)
#define LT_PPC1INT1EN  _LATTE_REG(0x58)
#define LT_PPC1INT2EN  _LATTE_REG(0x5c)

#define LT_PPC2INT1STS _LATTE_REG(0x60)
#define LT_PPC2INT2STS _LATTE_REG(0x64)
#define LT_PPC2INT1EN  _LATTE_REG(0x68)
#define LT_PPC2INT2EN  _LATTE_REG(0x6c)

#define LT_CHIPREVID   _LATTE_REG(0x1a0)
#define LT_PIMCOMPAT   _LATTE_REG(0x1b0)

#endif /* _REGS_H */
