/*
 * NPLL - Hollywood/Latte GPIO
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _GPIO_H
#define _GPIO_H

#include <npll/regs.h>
#define HW_GPIOB_OUT   _HOLLYWOOD_REG(0xc0)
#define HW_GPIOB_DIR   _HOLLYWOOD_REG(0xc4)
#define HW_GPIOB_IN    _HOLLYWOOD_REG(0xc8)
#define HW_GPIO_ENABLE _HOLLYWOOD_REG(0xdc)
#define HW_GPIO_OUT    _HOLLYWOOD_REG(0xe0)
#define HW_GPIO_DIR    _HOLLYWOOD_REG(0xe4)
#define HW_GPIO_IN     _HOLLYWOOD_REG(0xe8)
#define HW_GPIO_OWNER  _HOLLYWOOD_REG(0xfc)

#define GPIO_POWER      (1 << 0)
#define GPIO_SHUTDOWN   (1 << 1)
#define GPIO_FAN        (1 << 2)
#define GPIO_DC_DC      (1 << 3)
#define GPIO_DI_SPIN    (1 << 4)
#define GPIO_SLOT_LED   (1 << 5)
#define GPIO_EJECT_BTN  (1 << 6)
#define GPIO_SLOT_IN    (1 << 7)
#define GPIO_SENSOR_BAR (1 << 8)
#define GPIO_DO_EJECT   (1 << 9)
#define GPIO_EEP_CS     (1 << 10)
#define GPIO_EEP_CLK    (1 << 11)
#define GPIO_EEP_MOSI   (1 << 12)
#define GPIO_EEP_MISO   (1 << 13)
#define GPIO_AVE_SCL    (1 << 14)
#define GPIO_AVE_SDA    (1 << 15)
#define GPIO_DEBUG0     (1 << 16)
#define GPIO_DEBUG1     (1 << 17)
#define GPIO_DEBUG2     (1 << 18)
#define GPIO_DEBUG3     (1 << 19)
#define GPIO_DEBUG4     (1 << 20)
#define GPIO_DEBUG5     (1 << 21)
#define GPIO_DEBUG6     (1 << 22)
#define GPIO_DEBUG7     (1 << 23)

/*
 * has odd effected on the GamePad on Wii U:
 * - output, active = connected
 * - output, inactive = disconnected
 * - input (default state from CafeOS & IOSU) = connected
 */
#define GPIO_GAMEPAD_EN (1 << 28)

/* seems to also be important for GamePad */
#define GPIO_PADPD      (1 << 8)

#endif /* _GPIO_H */
