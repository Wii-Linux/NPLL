/*
 * NPLL - SoC hardware block address map
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _SOC_H
#define _SOC_H

#include <npll/utils.h>

/* Flipper MMIO bus */
#define FLIPPER_CP_OFFSET      0x000000
#define FLIPPER_PE_OFFSET      0x001000
#define FLIPPER_VI_OFFSET      0x002000
#define FLIPPER_PI_OFFSET      0x003000
#define FLIPPER_MI_OFFSET      0x004000
#define FLIPPER_DSP_OFFSET     0x005000
#define FLIPPER_DI_OFFSET      0x006000
#define FLIPPER_SI_OFFSET      0x006400
#define FLIPPER_EXI_OFFSET     0x006800
#define FLIPPER_AI_OFFSET      0x006c00
#define FLIPPER_GX_FIFO_OFFSET 0x008000

#define FLIPPER_CP_BASE      (FLIPPER_MMIO_BASE + FLIPPER_CP_OFFSET)
#define FLIPPER_PE_BASE      (FLIPPER_MMIO_BASE + FLIPPER_PE_OFFSET)
#define FLIPPER_VI_BASE      (FLIPPER_MMIO_BASE + FLIPPER_VI_OFFSET)
#define FLIPPER_PI_BASE      (FLIPPER_MMIO_BASE + FLIPPER_PI_OFFSET)
#define FLIPPER_MI_BASE      (FLIPPER_MMIO_BASE + FLIPPER_MI_OFFSET)
#define FLIPPER_DSP_BASE     (FLIPPER_MMIO_BASE + FLIPPER_DSP_OFFSET)
#define FLIPPER_DI_BASE      (FLIPPER_MMIO_BASE + FLIPPER_DI_OFFSET)
#define FLIPPER_SI_BASE      (FLIPPER_MMIO_BASE + FLIPPER_SI_OFFSET)
#define FLIPPER_EXI_BASE     (FLIPPER_MMIO_BASE + FLIPPER_EXI_OFFSET)
#define FLIPPER_AI_BASE      (FLIPPER_MMIO_BASE + FLIPPER_AI_OFFSET)
#define FLIPPER_GX_FIFO_BASE (FLIPPER_MMIO_BASE + FLIPPER_GX_FIFO_OFFSET)

/* Latte hardware on the Flipper MMIO bus */
#define LATTE_PI_OFFSET   FLIPPER_CP_OFFSET
#define LATTE_R600_OFFSET 0x200000

#define LATTE_PI_BASE   (FLIPPER_MMIO_BASE + LATTE_PI_OFFSET)
#define LATTE_R600_BASE (FLIPPER_MMIO_BASE + LATTE_R600_OFFSET)

/* Hollywood and Latte AHB */
#define HOLLYWOOD_REGS_OFFSET    0x000000
#define LATTE_REGS_OFFSET        0x000400
#define HOLLYWOOD_DI_OFFSET      0x006000
#define HOLLYWOOD_SI_OFFSET      0x006400
#define HOLLYWOOD_EXI_OFFSET     0x006800
#define HOLLYWOOD_NAND_OFFSET    0x010000
#define HOLLYWOOD_AES_OFFSET     0x020000
#define HOLLYWOOD_SHA1_OFFSET    0x030000
#define HOLLYWOOD_EHCI0_OFFSET   0x040000
#define HOLLYWOOD_OHCI0_OFFSET   0x050000
#define HOLLYWOOD_OHCI1_OFFSET   0x060000
#define HOLLYWOOD_SDHC0_OFFSET   0x070000
#define HOLLYWOOD_SDHC1_OFFSET   0x080000
#define HOLLYWOOD_MEMCTRL_OFFSET 0x0b4200
#define LATTE_SDHC2_OFFSET       0x100000
#define LATTE_SDHC3_OFFSET       0x110000
#define LATTE_EHCI1_OFFSET       0x120000
#define LATTE_EHCI2_OFFSET       0x140000

#define HOLLYWOOD_REGS_BASE    (AHB_BASE + HOLLYWOOD_REGS_OFFSET)
#define LATTE_REGS_BASE        (AHB_BASE + LATTE_REGS_OFFSET)
#define HOLLYWOOD_DI_BASE      (AHB_BASE + HOLLYWOOD_DI_OFFSET)
#define HOLLYWOOD_SI_BASE      (AHB_BASE + HOLLYWOOD_SI_OFFSET)
#define HOLLYWOOD_EXI_BASE     (AHB_BASE + HOLLYWOOD_EXI_OFFSET)
#define HOLLYWOOD_NAND_BASE    (AHB_BASE + HOLLYWOOD_NAND_OFFSET)
#define HOLLYWOOD_AES_BASE     (AHB_BASE + HOLLYWOOD_AES_OFFSET)
#define HOLLYWOOD_SHA1_BASE    (AHB_BASE + HOLLYWOOD_SHA1_OFFSET)
#define HOLLYWOOD_EHCI0_BASE   (AHB_BASE + HOLLYWOOD_EHCI0_OFFSET)
#define HOLLYWOOD_OHCI0_BASE   (AHB_BASE + HOLLYWOOD_OHCI0_OFFSET)
#define HOLLYWOOD_OHCI1_BASE   (AHB_BASE + HOLLYWOOD_OHCI1_OFFSET)
#define HOLLYWOOD_SDHC0_BASE   (AHB_BASE + HOLLYWOOD_SDHC0_OFFSET)
#define HOLLYWOOD_SDHC1_BASE   (AHB_BASE + HOLLYWOOD_SDHC1_OFFSET)
#define HOLLYWOOD_MEMCTRL_BASE (AHB_BASE + HOLLYWOOD_MEMCTRL_OFFSET)
#define LATTE_SDHC2_BASE       (AHB_BASE + LATTE_SDHC2_OFFSET)
#define LATTE_SDHC3_BASE       (AHB_BASE + LATTE_SDHC3_OFFSET)
#define LATTE_EHCI1_BASE       (AHB_BASE + LATTE_EHCI1_OFFSET)
#define LATTE_EHCI2_BASE       (AHB_BASE + LATTE_EHCI2_OFFSET)

/*
 * Register access helpers
 */

#define _FLIPPER_PI_REG(x) (*(vu32 *)(FLIPPER_PI_BASE + (x)))
#define _HOLLYWOOD_REG(x) (*(vu32 *)(HOLLYWOOD_REGS_BASE + (x)))
#define _HOLLYWOOD_MC_REG(x) (*(vu16 *)(HOLLYWOOD_MEMCTRL_BASE + (x)))
#define _LATTE_REG(x) (*(vu32 *)(LATTE_REGS_BASE + (x)))

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
#  define PI_RESET_DI            BIT(2)

/* the weird Latte PI that got stuffed into the base of the Flipper reg space, where the GX CP used to be */
#define LATTE_PI_INTSR       (*(vu32 *)(LATTE_PI_BASE + 0x00))
#define LATTE_PI_INTMR       (*(vu32 *)(LATTE_PI_BASE + 0x04))
#define LATTE_PI_INTSR0      (*(vu32 *)(LATTE_PI_BASE + 0x78))
#define LATTE_PI_INTMR0      (*(vu32 *)(LATTE_PI_BASE + 0x7c))
#define LATTE_PI_INTSR1      (*(vu32 *)(LATTE_PI_BASE + 0x80))
#define LATTE_PI_INTMR1      (*(vu32 *)(LATTE_PI_BASE + 0x84))
#define LATTE_PI_INTSR2      (*(vu32 *)(LATTE_PI_BASE + 0x88))
#define LATTE_PI_INTMR2      (*(vu32 *)(LATTE_PI_BASE + 0x8c))

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
#  define HW_IPC_PPCCTRL_X1      BIT(0)
#  define HW_IPC_PPCCTRL_Y2      BIT(1)
#  define HW_IPC_PPCCTRL_Y1      BIT(2)
#  define HW_IPC_PPCCTRL_X2      BIT(3)
#  define HW_IPC_PPCCTRL_IY1     BIT(4)
#  define HW_IPC_PPCCTRL_IY2     BIT(5)
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
#  define SRNPROT_AHPEN          BIT(3)
#define HW_AHBPROT           _HOLLYWOOD_REG(0x64)
#  define AHBPROT_PPCKERN        BIT(31)
#define HW_AIPROT            _HOLLYWOOD_REG(0x70)
#define HW_COMPAT            _HOLLYWOOD_REG(0x180)
#  define HW_COMPAT_DVDVIDEO     BIT(21)
#define HW_RESETS            _HOLLYWOOD_REG(0x194)
#  define RESETS_RSTBINB         BIT(0)
#  define RESETS_RSTB_DIRSTB     BIT(10)
#  define RESETS_RSTB_DSP        BIT(22)
#define HW_OTP_COMMAND       _HOLLYWOOD_REG(0x1ec)
#  define HW_OTP_COMMAND_RD      BIT(31)
#  define HW_OTP_COMMAND_BANK_SHIFT 8
#define HW_OTP_DATA          _HOLLYWOOD_REG(0x1f0)
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

#  define LT_IRQDEV_SDHCI2       BIT(0)
#  define LT_IRQDEV_SDHCI3       BIT(1)

#define LT_EFUSEPROT   _LATTE_REG(0x110)
#define LT_CHIPREVID   _LATTE_REG(0x1a0)
#define LT_PIMCOMPAT   _LATTE_REG(0x1b0)

#endif /* _SOC_H */
