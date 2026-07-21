/*
 * NPLL - Hollywood/Latte EHCI
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _EHCI_H
#define _EHCI_H

#include <npll/revle.h>
#include <npll/soc.h>
#include <npll/endian.h>

/* EHCI DMA structures are little-endian even though Latte/Hollywood expose
 * their MMIO registers through the CPU's big-endian AHB view. */
struct ehciQtd {
	u32 next, alternate, token, buffer[5];
} __attribute__((aligned(32)));

struct ehciQh {
	u32 horizontal, endpoint, endpointCaps, current;
	u32 overlayNext, overlayAlternate, overlayToken, overlayBuffer[5];
} __attribute__((aligned(32)));

_Static_assert(sizeof(struct ehciQtd) == 32, "EHCI qTD layout");
_Static_assert(sizeof(struct ehciQh) == 64, "EHCI qH layout");

static inline volatile void *EHCI_Reg(uintptr_t base, u32 offset) {
	return (volatile void *)(base + offset);
}

static inline u32 EHCI_Read32(uintptr_t base, u32 offset) {
	return revLEreadl(EHCI_Reg(base, offset));
}

static inline void EHCI_Write32(uintptr_t base, u32 offset, u32 value) {
	revLEwritel(EHCI_Reg(base, offset), value);
}

static inline u8 EHCI_CapLength(uintptr_t base) {
	return revLEreadb(EHCI_Reg(base, 0));
}

static inline u32 EHCI_ReadOp32(uintptr_t base, u32 offset) {
	return EHCI_Read32(base, (u32)EHCI_CapLength(base) + offset);
}

static inline void EHCI_WriteOp32(uintptr_t base, u32 offset, u32 value) {
	EHCI_Write32(base, (u32)EHCI_CapLength(base) + offset, value);
}

#define _EHCI_BASE(i) ((uintptr_t)(i == 0 ? HOLLYWOOD_EHCI0_BASE : i == 1 ? LATTE_EHCI1_BASE : i == 2 ? LATTE_EHCI2_BASE : (uintptr_t)-1))
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
