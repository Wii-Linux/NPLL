/*
 * NPLL - Hollywood/Latte EHCI
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _EHCI_H
#define _EHCI_H

#include <npll/revle.h>
#include <npll/utils.h>

#define _EHCI_BASE(i) (i == 0 ? 0xcd040000 : i == 1 ? 0xcd120000 : i == 2 ? 0x0d140000 : -1)
#define _EHCI_REG(hcidx, off) *(vu32 *)(_EHCI_BASE((hcidx)) + (off))
#define EHCI_CAPLENGTH(i) _EHCI_REG(i, 0x00)
#define _EHCI_OPREG(hcidx, off) _EHCI_REG(hcidx, revLEreadb(&EHCI_CAPLENGTH(hcidx)) + off)

#define EHCI_USBCMD(i)    _EHCI_OPREG(i, 0x00)
#define     EHCI_USBCMD_RS       BIT(0)
#define     EHCI_USBCMD_HCRESET  BIT(1)
#define EHCI_USBSTS(i)    _EHCI_OPREG(i, 0x04)
#define     EHCI_USBSTS_HCHALTED BIT(12)
#define EHCI_USBINTR(i)   _EHCI_OPREG(i, 0x08)

#endif /* _OHCI_H */
