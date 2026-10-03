/*
 * NPLL - Wii U SMC
 *
 * Copyright (C) 2026 Techflash
 *
 */

#ifndef _LATTE_SMC_H
#define _LATTE_SMC_H

#include <npll/types.h>
#include <npll/utils.h>

#define SMC_ADDRESS             0x50

#define SMC_CMD_ODD_EJECT       0x02
#define SMC_CMD_RESET_WIFI5     0x22
#define SMC_REG_PROGRAM_REV     0x40
#define SMC_REG_SYSTEM_EVENT    0x41
#define SMC_REG_ODD_FLAG        0x42
#define SMC_REG_DEVICE_ENABLE   0x46
#define SMC_REG_INTERRUPT_MASK  0x47
#define SMC_REG_CHIP_REV        0x48

#define SMC_EVENT_DISC_INSERT   BIT(4)
#define SMC_EVENT_EJECT_BUTTON  BIT(5)
#define SMC_EVENT_POWER_BUTTON  BIT(6)
#define SMC_EVENT_BUTTONS       (SMC_EVENT_EJECT_BUTTON | SMC_EVENT_POWER_BUTTON)

#define SMC_DEVICE_WIFI24       BIT(0)

extern int H_WiiUSMCReadRegister(u8 reg, u8 *value);
extern int H_WiiUSMCWriteRegister(u8 reg, u8 value);
extern int H_WiiUSMCSendCmd(u8 cmd);

#endif /* _LATTE_SMC_H */
