/*
 * NPLL - Hollywood/Latte GPIO
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _GPIO_H
#define _GPIO_H

#include <npll/regs.h>
#include <npll/utils.h>

#define HW_GPIOB_OUT   _HOLLYWOOD_REG(0xc0)
#define HW_GPIOB_DIR   _HOLLYWOOD_REG(0xc4)
#define HW_GPIOB_IN    _HOLLYWOOD_REG(0xc8)
#define HW_GPIO_ENABLE _HOLLYWOOD_REG(0xdc)
#define HW_GPIO_OUT    _HOLLYWOOD_REG(0xe0)
#define HW_GPIO_DIR    _HOLLYWOOD_REG(0xe4)
#define HW_GPIO_IN     _HOLLYWOOD_REG(0xe8)
#define HW_GPIO_OWNER  _HOLLYWOOD_REG(0xfc)

#define GPIO_POWER      BIT(0)
#define GPIO_SHUTDOWN   BIT(1)
#define GPIO_FAN        BIT(2)
#define GPIO_DC_DC      BIT(3)
#define GPIO_DI_SPIN    BIT(4)
#define GPIO_SLOT_LED   BIT(5)
#define GPIO_EJECT_BTN  BIT(6)
#define GPIO_SLOT_IN    BIT(7)
#define GPIO_SENSOR_BAR BIT(8)
#define GPIO_DO_EJECT   BIT(9)
#define GPIO_EEP_CS     BIT(10)
#define GPIO_EEP_CLK    BIT(11)
#define GPIO_EEP_MOSI   BIT(12)
#define GPIO_EEP_MISO   BIT(13)
#define GPIO_AVE_SCL    BIT(14)
#define GPIO_AVE_SDA    BIT(15)
#define GPIO_DEBUG0     BIT(16)
#define GPIO_DEBUG1     BIT(17)
#define GPIO_DEBUG2     BIT(18)
#define GPIO_DEBUG3     BIT(19)
#define GPIO_DEBUG4     BIT(20)
#define GPIO_DEBUG5     BIT(21)
#define GPIO_DEBUG6     BIT(22)
#define GPIO_DEBUG7     BIT(23)

/*
 * has odd effected on the GamePad on Wii U:
 * - output, active = connected
 * - output, inactive = disconnected
 * - input (default state from CafeOS & IOSU) = connected
 */
#define GPIO_GAMEPAD_EN BIT(28)

/* seems to also be important for GamePad */
#define GPIO_PADPD      BIT(8)

#endif /* _GPIO_H */
