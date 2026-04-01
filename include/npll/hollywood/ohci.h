/*
 * NPLL - Hollywood/Latte OHCI
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _OHCI_H
#define _OHCI_H

#include <npll/regs.h>
#include <npll/utils.h>

#define _OHCI_REG(hcidx, off) *(vu32 *)(0xcd850000 + (0x10000 * (hcidx)) + (off))
#define OHCI_HC_COMMAND_STATUS(i)    _OHCI_REG(i, 0x08)
#define     OHCI_HC_COMMAND_STATUS_HCR    BIT(0)
#define OHCI_HC_INTERRUPT_STATUS(i)  _OHCI_REG(i, 0x0c)
#define OHCI_HC_INTERRUPT_DISABLE(i) _OHCI_REG(i, 0x14)

#endif /* _OHCI_H */
