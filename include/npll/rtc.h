/*
 * NPLL - GameCube/Wii/Wii U RTC/SRAM/IPL/UART support
 *
 * Copyright (C) 2026 Techflash
 *
 * Based on:
 *   - https://wiiubrew.org/wiki/Hardware/RTC
 *   - https://wiibrew.org/wiki/Hardware/MX23L4005
 *   - https://hitmen.c02.at/files/yagcd/yagcd/chap10.html#sec10
 *   - https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/EXI/EXI_DeviceIPL.cpp
 *   - https://github.com/doldecomp/dolsdk2004/blob/2328b4164b1a98422a2255d83ce9a5a7548990cc/include/dolphin/os/OSRtc.h
 *   - https://github.com/extremscorner/libogc2/blob/39ddd7be7233ddd650fc8949950bb50be46caa97/libogc/system.c#L1642
 *   - https://github.com/extremscorner/libogc2/blob/39ddd7be7233ddd650fc8949950bb50be46caa97/include/ogc/system.h#L199
 *   - https://github.com/doldecomp/ogws/blob/master/src/revolution/EUART/euart.c
 *   - https://github.com/doldecomp/dolsdk2001-dec/blob/416e54c6143c204bed0a1ebf0195ab721d3ea613/src/exi/EXIUart.c
 *   - https://github.com/doldecomp/ttyd/blob/62131fc31866f85a12e4817a8dc37f0c25eb5f8c/libs/dolsdk2004/src/os/OSRtc.c
 */

#ifndef _RTC_H
#define _RTC_H

#include <npll/types.h>

/* the RTC sits on EXI channel 0 CS 1 */
#define RTC_CHAN 0
#define RTC_CS   1

/* base of the IPL ROM */
#define RTC_ROM_BASE   0x00000000u
/* base of the RTC regs, SRAM kinda cuts in the middle though */
#define RTC_REGS_BASE  0x20000000u
/* base of SRAM */
#define RTC_SRAM_BASE  0x20000100u
/* legacy GameCube RTC UART ("Barnacle"?) */
#define RTC_UART_BASE  0x20010000u
/* Wii RTC "EUART" */
#define RTC_EUART_BASE 0x30000000u

/* when the transfer you send next is to be a write */
#define RTC_WR         BIT(31)

/*
 * RTC and control registers
 */
/* number of seconds which have passed since 1/1/2000, after the bias is accounted for */
#define RTC_COUNTER    0x20000000u
/* WiiUbrew claims it's a snapshot of RTC_COUNTER but I can't get this to work */
#define RTC_SNAPSHOT   0x20400000u
/* same locking effect as OFFTMR, see below */
#define RTC_ONTMR      0x21000000u
#  define RTC_ONTMR_SET             0x3fffffffu
#  define RTC_ONTMR_DTCT            BIT(30)
#  define RTC_ONTMR_EN              BIT(31)
/*
 * Weird effects on Wii:
 *   - When EN=0, writes with SET=X and/or EN=X apply immediately
 *   - When EN=1, SET cannot be modified; EN=0 must be written first, which
 *     retains SET, then SET can be modified (potentially setting EN=1 at the
 *     same time)
 *   - When SHUTDOWN GPIO is asserted while EN=1, green+red (yellow) power LED
 *     is lit, seemingly without Hollywood remaining active?!
 */
#define RTC_OFFTMR     0x21000100u
#  define RTC_OFFTMR_SET            0x3fffffffu
#  define RTC_OFFTMR_DTCT           BIT(30)
#  define RTC_OFFTMR_EN             BIT(31)
/* from WiiUbrew - name suggests these were not "compat" on the Wii's RTC, but I have no better name for them */
#define RTC_COMPATSET  0x21000400u
/*
 * DI flags, mirrored per byte
 * They're sticky flags, cleared by writing 0:
 *   0x01010101 - eject button pressed
 *   0x02020202 - disc changed
 *   0x03030303 - both
 */
#define RTC_COMPATINT  0x21000800u
#  define RTC_COMPATINT_EJECT_BTN   BIT(0)
#  define RTC_COMPATINT_DISC_CHANGE BIT(1)
/* seems to be Wii U only - reads all 0s regardless of anything done on Wii */
#define RTC_CONTROL0   0x21000c00u
#  define RTC_CONTROL0_REVISION     0x000000ffu
#  define RTC_CONTROL0_CANARY       BIT(8)
/* set when the battery is low voltage/not present on Wii U */
#  define RTC_CONTROL0_LOW_BATT     BIT(9)
#  define RTC_CONTROL0_UNSTBL_PWR   BIT(11)
#  define RTC_CONTROL0_PONLG_TMR    BIT(16)
#  define RTC_CONTROL0_PONFLG_SYS   BIT(17)
#  define RTC_CONTROL0_POFFLG_TMR   BIT(24)
#  define RTC_CONTROL0_POFFLG_4S    BIT(25)
#  define RTC_CONTROL0_POFFLG_FPOFF BIT(26)
/* seems to be Wii U only - reads all 0s regardless of anything done on Wii */
#define RTC_CONTROL1   0x21000d00u
/*
 * When set on Wii U, allows holding the power button for 4 seconds to force
 * power off, if clear, this does nothing.
 * On Wii, this functionality is always enabled regardless of this bit
 * (which always reads 0).
 */
#  define RTC_CONTROL1_4COUNT_EN    BIT(0)
#  define RTC_CONTROL1_CLKOUT_EN    BIT(2)
#  define RTC_CONTROL1_FPOFF_MODE   BIT(3)
#  define RTC_CONTROL1_SLEEP_EN     BIT(8)
#  define RTC_CONTROL1_DEVPWR_EN    BIT(10)
/* when set on Wii U, immediately powers off the system; always reads 0 */
#  define RTC_CONTROL1_POFF_EXE     BIT(16)
#define RTC_CONTROL2   0x21000e00u
#define RTC_CONTROL3   0x21000f00u
#define RTC_SCRATCH0   0x21001000u
#define RTC_SCRATCH1   0x21001100u
#define RTC_SCRATCH2   0x21001200u
#define RTC_SCRATCH3   0x21001300u
/* see above - probably nonexistent on GC/Wii */
#define RTC_NORMALINT  0x21001800u

/*
 * SRAM
 */
struct _rtcSRAM {
	u16 checkSum;
	u16 checkSumInv;
	u32 ead0;
	u32 ead1;
	/*
	 * Relies on 32-bit unsigned overflow semantics; Wii sysmenu can intentionally
	 * set this to a negative value to cause overflow and wrap back around, but
	 * it's also not a signed integer; a large (>0x80000000) but positive bias, but
	 * small enough that it doesn't overflow, is also valid.
	 */
	u32 counterBias;
	i8 displayOffsetH;
	u8 ntd;
	u8 language;
	u8 flags;
} __attribute__((packed));

struct _rtcSRAMEx {
	u8 flashID[2][12];
	u32 wirelessKeyboardID;
	u16 wirelessPadID[4];
	u8 dvdErrorCode;
	u8 _padding0;
	u8 flashIDCheckSum[2];
	u16 gbs;
	/* libogc2-specific, other software doesn't use it */
	u16 dvdDevCode;
} __attribute__((packed));

struct rtcSRAM {
	struct _rtcSRAM main;
	struct _rtcSRAMEx ext;
} __attribute__((packed));

/* set = on, clear = off */
#define RTC_SRAM_NTD_EURGB60       BIT(6)
/* set = production, clear = development */
#define RTC_SRAM_NTD_BOOT_MODE     BIT(7)
/* Gekko TAU calibration used by libogc2 */
#define RTC_SRAM_NTD_TAU_CALIB     0x3fu

#define RTC_SRAM_LANGUAGE_ENGLISH    0x00u
#define RTC_SRAM_LANGUAGE_GERMAN     0x01u
#define RTC_SRAM_LANGUAGE_FRENCH     0x02u
#define RTC_SRAM_LANGUAGE_SPANISH    0x03u
#define RTC_SRAM_LANGUAGE_ITALIAN    0x04u
#define RTC_SRAM_LANGUAGE_DUTCH      0x05u
/* Below 2 are only used by Super Mario Sunshine in Super Mario 3D All Stars */
#define RTC_SRAM_LANGUAGE_JAPANESE   0x06u
#define RTC_SRAM_LANGUAGE_ENGLISH_US 0x07u

#define RTC_SRAM_FLAGS_VIDEO_MODE  0x03u
#  define RTC_SRAM_FLAGS_VIDEO_MODE_NTSC  0x00u
#  define RTC_SRAM_FLAGS_VIDEO_MODE_PAL   0x01u
#  define RTC_SRAM_FLAGS_VIDEO_MODE_MPAL  0x02u
/* set = stereo, clear = mono */
#define RTC_SRAM_FLAGS_SOUND_MODE  BIT(2)
/* set = progressive, clear = interlaced */
#define RTC_SRAM_FLAGS_PROGRESSIVE BIT(7)

/*
 * UART - Dolphin SDK 12-17-2001 decomp, as well as Dolphin Emulator, call this a "Barnacle"
 */
#define RTC_UART_FIFO     0x20010000u
/* According to Dolphin Emulator: "Seen being written to after reading 4 bytes from barnacle" */
#define RTC_UART_UNK      0x20010300u
#define RTC_UART_REVISION 0x20011300u

/*
 * EUART - only on Wii/Wii U RTC?
 * The FIFO seems to behave identically to the old UART
 */
#define RTC_EUART_CTRL 0x30000000u
/* unlabeled init writes from RVL_SDK decomp */
#  define RTC_EUART_CTRL_INIT1  0x000000f2
#  define RTC_EUART_CTRL_INIT2  0x000000f3
#define RTC_EUART_FIFO 0x30000100u

extern u32 H_RTCReadReg(u32 reg);
extern void H_RTCWriteReg(u32 reg, u32 val);

/*
 * Read the IPL ROM.
 * Address and length must be 32B aligned.
 */
extern int H_RTCReadROM(void *buf, uint len, uint offset);

#endif /* _RTC_H */
