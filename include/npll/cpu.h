/*
 * NPLL - CPU macros
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _CPU_H
#define _CPU_H

/* SPR Numbers */
#define IBAT0U 528
#define IBAT0L 529
#define IBAT1U 530
#define IBAT1L 531
#define IBAT2U 532
#define IBAT2L 533
#define IBAT3U 534
#define IBAT3L 535

#define DBAT0U 536
#define DBAT0L 537
#define DBAT1U 538
#define DBAT1L 539
#define DBAT2U 540
#define DBAT2L 541
#define DBAT3U 542
#define DBAT3L 543

#define IBAT4U 560
#define IBAT4L 561
#define IBAT5U 562
#define IBAT5L 563
#define IBAT6U 564
#define IBAT6L 565
#define IBAT7U 566
#define IBAT7L 567

#define DBAT4U 568
#define DBAT4L 569
#define DBAT5U 570
#define DBAT5L 571
#define DBAT6U 572
#define DBAT6L 573
#define DBAT7U 574
#define DBAT7L 575

#define HID0   1008
#define HID4   1011
#define DABR   1013


/* MSR values */
#define MSR_EE (1 << (31 - 16))
#define MSR_PR (1 << (31 - 17))
#define MSR_FP (1 << (31 - 18))
#define MSR_IR (1 << (31 - 26))
#define MSR_DR (1 << (31 - 27))

/* HID0 values */
#define HID0_ICE  (1 << (31 - 16))
#define HID0_DCE  (1 << (31 - 17))
#define HID0_ICFI (1 << (31 - 20))
#define HID0_DCFI (1 << (31 - 21))

/* HID4 values */
#define HID4_SBE (1 << (31 - 6))

#define SETBAT_TYPE_DATA (1 << 0)
#define SETBAT_TYPE_INST (1 << 1)
#define SETBAT_TYPE_BOTH (SETBAT_TYPE_DATA | SETBAT_TYPE_INST)

#ifndef __ASSEMBLY__
#include <npll/types.h>
#include <npll/panic.h>
#include <npll/utils.h>

#define mfspr(rn) ({ \
	u32 __tmpSPRVal; \
	asm volatile("mfspr %0, " __stringifyResult(rn) "" : "=r" (__tmpSPRVal)); \
	__tmpSPRVal; \
})

#define mtspr(rn, v) asm volatile("mtspr " __stringifyResult(rn) ", %0" : : "r" (v))

/* Somehow GCC generates garbage */
__attribute__((optimize("no-jump-tables")))
static inline void setbat(int idx, int typeMask, u32 batu, u32 batl) {
	switch (idx) {
	case 0:
	case 1: {
		panic("Trying to set important BATs");
		break;
	}
	case 2: {
		if (typeMask & SETBAT_TYPE_DATA) {
			mtspr(DBAT2U, batu);
			mtspr(DBAT2L, batl);
		}
		if (typeMask & SETBAT_TYPE_INST) {
			mtspr(IBAT2U, batu);
			mtspr(IBAT2L, batl);
		}
		break;
	}
	case 3: {
		if (typeMask & SETBAT_TYPE_DATA) {
			mtspr(DBAT3U, batu);
			mtspr(DBAT3L, batl);
		}
		if (typeMask & SETBAT_TYPE_INST) {
			mtspr(IBAT3U, batu);
			mtspr(IBAT3L, batl);
		}
		break;
	}
	case 4: {
		if (typeMask & SETBAT_TYPE_DATA) {
			mtspr(DBAT4U, batu);
			mtspr(DBAT4L, batl);
		}
		if (typeMask & SETBAT_TYPE_INST) {
			mtspr(IBAT4U, batu);
			mtspr(IBAT4L, batl);
		}
		break;
	}
	case 5: {
		if (typeMask & SETBAT_TYPE_DATA) {
			mtspr(DBAT5U, batu);
			mtspr(DBAT5L, batl);
		}
		if (typeMask & SETBAT_TYPE_INST) {
			mtspr(IBAT5U, batu);
			mtspr(IBAT5L, batl);
		}
		break;
	}
	case 6: {
		if (typeMask & SETBAT_TYPE_DATA) {
			mtspr(DBAT6U, batu);
			mtspr(DBAT6L, batl);
		}
		if (typeMask & SETBAT_TYPE_INST) {
			mtspr(IBAT6U, batu);
			mtspr(IBAT6L, batl);
		}
		break;
	}
	case 7: {
		if (typeMask & SETBAT_TYPE_DATA) {
			mtspr(DBAT7U, batu);
			mtspr(DBAT7L, batl);
		}
		if (typeMask & SETBAT_TYPE_INST) {
			mtspr(IBAT7U, batu);
			mtspr(IBAT7L, batl);
		}
		break;
	}

	default: {
		panic("Unknown BAT number");
		break;
	}
	}
	asm volatile ("isync; sync");
}
#endif /* __ASSEMBLY__ */

#endif /* _CPU_H */
