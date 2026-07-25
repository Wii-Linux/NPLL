/*
 * NPLL - Wii U SMC
 *
 * Copyright (C) 2026 Techflash
 *
 */

#ifndef _LATTE_SMC_H
#define _LATTE_SMC_H

#define SMC_ADDRESS             0x50

#define SMC_CMD_ODD_EJECT       0x02
#define SMC_REG_PROGRAM_REV     0x40
#define SMC_REG_SYSTEM_EVENT    0x41
#define SMC_REG_ODD_FLAG        0x42
#define SMC_REG_CHIP_REV        0x48

#define SMC_EVENT_DISC_INSERT   BIT(4)
#define SMC_EVENT_EJECT_BUTTON  BIT(5)
#define SMC_EVENT_POWER_BUTTON  BIT(6)
#define SMC_EVENT_BUTTONS       (SMC_EVENT_EJECT_BUTTON | SMC_EVENT_POWER_BUTTON)

#endif /* _LATTE_SMC_H */
