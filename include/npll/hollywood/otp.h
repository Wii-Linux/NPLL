/*
 * NPLL - Hollywood/Latte Hardware - OTP / eFuse memory
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _HOLLYWOOD_OTP_H
#define _HOLLYWOOD_OTP_H

#include <npll/types.h>

/* from Wiibrew */
struct WiiOTP {
	u32 boot1Hash[5];
	u32 commonKey[4];
	u32 ngID;
	union {
		struct {
			u16 ngPrivateKey[15];
			u16 __junk1[9];
		};
		struct {
			u16 __junk2[14];
			u16 nandHMAC[5];
		};
	};
	u32 nandKey[4];
	u32 prngKey[4];
	u32 unk[2];
} __attribute__((packed));


/* from WiiUBrew */
struct WiiUOTPBank1 {
	u32 boot0Config;
	u32 iostrengthConfig;
	u32 eepromClkLen;
	u32 signatureType;
	u32 starbuckAncastKey[4];
	u32 eepromKey[4];
	u32 unkb0[4];
	u32 unkc0[4];
	u32 vWiiCommonkey[4];
	u32 wiiUCommonKey[4];
	u32 unkf0[4];
} __attribute__((packed));
struct WiiUOTPBank2 {
	u32 unk100[4];
	u32 unk110[4];
	u32 sslRSAKeyEncryptionKey[4];
	u32 ivsKey[4];
	u32 wiiMediaTitleKey[4];
	u32 xorKey[4];
	u32 wiiUBackupKey[4];
	u32 slcNANDKey[4];
};
struct WiiUOTPBank3 {
	u32 mlcNANDKey[4];
	u32 shddKey[4];
	u32 drhWLANKey[4];
	u32 unk1b0[12];
	u32 slcNANDHMACKey[5];
	u32 unk1f4[3];
} __attribute__((packed));
struct WiiUOTPBank4 {
	u32 unk200[4];
	u32 unk210[3];
	u32 wiiUDeviceID;
	u32 wiiUDevicePrivateKey[8];
	u32 wiiUDeviceUniqueCertPrivateKey[8];
	u32 rngSeed[4];
	u32 unk270[4];
} __attribute__((packed));
struct WiiUOTPBank5 {
	u32 uniqueCertMSID;
	u32 uniqueCertCAID;
	u32 uniqueCertMSDate;
	u32 uniqueCertSig[15];
	u32 unk2c8[6];
	u32 wiiUDiagAncastKey[4];
	u32 unk2f0[4];
} __attribute__((packed));
struct WiiUOTPBank6 {
	u32 deviceAuthMSID;
	u32 deviceAuthCAID;
	u32 deviceAuthMSDate;
	u32 deviceAuthSig[15];
	u32 vWiiKoreanKey[4];
	u32 unk358[2];
	u32 deviceAuthCommonKey[8];
} __attribute__((packed));
struct WiiUOTPBank7 {
	u32 unk380[8];
	u32 boot1Key[4];
	u32 unk3a0[4];
	u32 rsrvd1[9];
	u32 latteWaferPos;
	u32 unk3e8;
	u32 latteRev;
	char latteID[8];
	u32 rsrvd2;
	u32 debugType;
} __attribute__((packed));

struct WiiUOTP {
	struct WiiUOTPBank1 bank1;
	struct WiiUOTPBank2 bank2;
	struct WiiUOTPBank3 bank3;
	struct WiiUOTPBank4 bank4;
	struct WiiUOTPBank5 bank5;
	struct WiiUOTPBank6 bank6;
	struct WiiUOTPBank7 bank7;
} __attribute__((packed));

struct otp {
	union {
		struct WiiOTP wii;
		struct WiiUOTP wiiu;
		u32 u32[256];
	};
} __attribute__((packed));
extern struct otp H_OTPContents;

#endif /* _HOLLYWOOD_OTP_H */
