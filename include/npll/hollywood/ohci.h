/*
 * NPLL - Hollywood/Latte OHCI
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _OHCI_H
#define _OHCI_H

#include <npll/soc.h>
#include <npll/utils.h>
#include <npll/endian.h>
#include <npll/revle.h>

struct ohciHCCA {
	u32 interruptTable[32];
	u16 frameNumber, pad;
	u32 doneHead;
	u8 reserved[116];
} __attribute__((aligned(256)));

struct ohciED {
	u32 control, tail, head, next;
} __attribute__((aligned(16)));

struct ohciTD {
	u32 control, currentBuffer, next, bufferEnd;
} __attribute__((aligned(16)));

_Static_assert(sizeof(struct ohciHCCA) == 256, "OHCI HCCA layout");
_Static_assert(sizeof(struct ohciED) == 16, "OHCI ED layout");
_Static_assert(sizeof(struct ohciTD) == 16, "OHCI TD layout");

static inline u32 OHCI_Read32(uintptr_t base, u32 offset) {
	return revLEreadl((volatile void *)(base + offset));
}

static inline void OHCI_Write32(uintptr_t base, u32 offset, u32 value) {
	revLEwritel((volatile void *)(base + offset), value);
}

#define _OHCI_BASE(i) ((uintptr_t)(i == 0 ? HOLLYWOOD_OHCI0_BASE : i == 1 ? HOLLYWOOD_OHCI1_BASE : (uintptr_t)-1))
#define _OHCI_REG(hcidx, off) *(vu32 *)(_OHCI_BASE(hcidx) + (off))
#define OHCI_HC_COMMAND_STATUS(i)    _OHCI_REG(i, 0x08)
#define     OHCI_HC_COMMAND_STATUS_HCR    BIT(0)
#define OHCI_HC_INTERRUPT_STATUS(i)  _OHCI_REG(i, 0x0c)
#define OHCI_HC_INTERRUPT_DISABLE(i) _OHCI_REG(i, 0x14)

#endif /* _OHCI_H */
