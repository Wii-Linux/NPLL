/*
 * NPLL - Flipper/Hollywood Hardware - Video Interface
 *
 * Copyright (C) 2025-2026 Techflash
 *
 * Based on code from Wii-Linux:
 * Copyright (C) 2004-2009 The GameCube Linux Team
 * Copyright (C) 2004 Michael Steil <mist@c64.org>
 * Copyright (C) 2004,2005 Todd Jeffreys <todd@voidpointer.org>
 * Copyright (C) 2006,2007,2008,2009 Albert Herranz
 * Copyright (C) 2025-2026 Joe Mason <buddyjojo06@outlook.com>
 * Copyright (C) 2024-2026 Michael "Techflash" Garofalo <officialTechflashYT@gmail.com>
 *
 * Based on vesafb (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 */

#define MODULE "VI"

#include <assert.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/flipper/vi.h>
#include <npll/i2c.h>
#include <npll/irq.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/utils.h>
#include <npll/video.h>

static REGISTER_DRIVER(viDrv);

#define YUYV_BYTES_PER_PIX 2
#define VI_VERT_ALIGN      1
#define VI_HORZ_ALIGN      0xf
#define VI_HORZ_WORD_SIZE  32
/* most TVs crop off the top and bottom-most ~16px - try to account for that here */
#define XFB_OS_COMP_PIX 16
static u32 *rgbFb;
static u16 *xfb;

struct viRegs {
	u16 vtr;
	u16 dcr;
	u32 htr0;
	u32 htr1;
	u32 vto;
	u32 vte;
	u32 bboi;
	u32 bbei;
	u32 tfbl;
	u32 tfbr;
	u32 bfbl;
	u32 bfbr;
	u16 dpv;
	u16 dph;
	u32 di[4];
	u32 dl0;
	u32 dl1;
	u16 pcr;
	u16 hsr;
	u32 fct[7];
	u32 aa;
	u16 viclk;
	u16 visel;
	u16 unk_70;
	u16 hbe;
	u16 hbs;
	u16 unk_76;
	u32 unk_78;
	u32 unk_7c;
};
static volatile struct viRegs *regs = (volatile struct viRegs *)FLIPPER_VI_BASE;

#define VI_VTR_EQU_SHIFT		0
#define VI_VTR_EQU			(15u << VI_VTR_EQU_SHIFT)
#define VI_VTR_ACV_SHIFT		4
#define VI_VTR_ACV			(0x3ffu << VI_VTR_ACV_SHIFT)

#define VI_DCR_FMT_SHIFT		8
#define VI_DCR_FMT_NTSC			(0u << VI_DCR_FMT_SHIFT)
#define VI_DCR_FMT_PAL			(1u << VI_DCR_FMT_SHIFT)
#define VI_DCR_FMT_MPAL			(2u << VI_DCR_FMT_SHIFT)
#define VI_DCR_FMT_DEBUG		(3u << VI_DCR_FMT_SHIFT)
#define VI_DCR_FMT			(3u << VI_DCR_FMT_SHIFT)
#define VI_DCR_NIN			BIT(2)
#define VI_DCR_RST			BIT(1)
#define VI_DCR_ENB			BIT(0)

#define VI_HTR0_HLW_SHIFT		0
#define VI_HTR0_HCE_SHIFT		16
#define VI_HTR0_HCS_SHIFT		24

#define VI_HTR1_HSY_SHIFT		0
#define VI_HTR1_HBE_SHIFT		7
#define VI_HTR1_HBS_SHIFT		17

#define VI_VTO_PRB_SHIFT		0
#define VI_VTO_PSB_SHIFT		16

#define VI_VTE_PRB_SHIFT		0
#define VI_VTE_PSB_SHIFT		16

#define VI_BBOI_BS1_SHIFT		0
#define VI_BBOI_BE1_SHIFT		5
#define VI_BBOI_BS3_SHIFT		16
#define VI_BBOI_BE3_SHIFT		21

#define VI_BBEI_BS2_SHIFT		0
#define VI_BBEI_BE2_SHIFT		5
#define VI_BBEI_BS4_SHIFT		16
#define VI_BBEI_BE4_SHIFT		21

#define VI_FB_POB			BIT(28)

#define VI_PCR_STD_SHIFT		0
#define VI_PCR_WPL_SHIFT		8

#define VI_HSR_STP_SHIFT		0
#define VI_HSR_HS_EN_SHIFT		12

#define VI_VICLK_54MHZ			BIT(0)
#define VI_VICLK_27MHZ			0u

#define AVE_VID_OUT_CFG			0x01
#define AVE_VID_OUT_CFG_YUV_EN  	BIT(5)
#define AVE_VID_OUT_CFG_FMT		3u
#define AVE_VID_OUT_CFG_FMT_NTSC	0u
#define AVE_VID_OUT_CFG_FMT_MPAL	1u
#define AVE_VID_OUT_CFG_FMT_PAL		2u
#define AVE_VID_OUT_CFG_FMT_DEBUG	3u

static enum viMode videoMode;
static uint videoWidth, videoHeight;

/*
 * Video mode timings.
 */
struct viModeTimings {
	/* VERTICAL SETTINGS */

	/*
	 * NTSC 480i
	 * 1 field = 262.5 lines (242.5 active, 20 blank)
	 * 1 frame = 2 fields = 2 x 262.5 = 525 lines (485 active, 40 blank)
	 *
	 * PAL 576i
	 * 1 field = 312.5 lines (287.5 active, 25 blank)
	 * 1 frame = 2 fields = 2 x 312.5 = 625 lines (575 active, 50 blank)
	 *
	 * NOTES:
	 * - the start of sync is considered the start of a line
	 * - the width of a half line is the width of a line divided by two
	 *
	 */

	/*
	 * Vertical position of the first active video line (0=top).
	 */
	uint ypos;

	/*
	 * Horizontal position in pixels where the vertical blanking
	 * interval starts. Used for signaling the start of the vertical
	 * retrace.
	 */
	uint htrap;

	/*
	 * Vertical position in field lines where the vertical blanking
	 * interval starts. Used for signaling the start of the vertical
	 * retrace.
	 */
	uint vtrap;

	/*
	 * Active Video, specified in number of field lines.
	 */
	u16	acv;
	/*
	 * Equalization pulse, specified in number of half lines.
	 */
	u8	equ;

	/*
	 * Pre-blanking, specified in half lines.
	 */
	u16	prbOdd;
	u16	prbEven;

	/*
	 * Post-blanking, specified in half lines.
	 */
	u16	psbOdd;
	u16	psbEven;

	/*
	 * NOTE:
	 * Irrespective of what patent 6,609,977 says:
	 * - "bs*" seems to tell where the burst blanking for the current
	 *   field ends
	 * - "be*" seems to tell where the next burst blanking starts
	 */

	/*
	 * Patent says: "Start to burst blanking start in half lines".
	 */
	u8	bs1;
	u8	bs2;
	u8	bs3;
	u8	bs4;

	/*
	 * Patent says: "Start to burst blanking end in half lines".
	 */
	u16	be1;
	u16	be2;
	u16	be3;
	u16	be4;

	/* HORIZONTAL SETTINGS */

	/*
	 * A = Blank Start to Horizontal Sync Start, "Front Porch"
	 *     right_margin
	 * B = Horizontal Sync Width
	 *     hsync_len
	 * C = Horizontal Sync End to Blank End, "Back Porch"
	 *     left_margin
	 * D = Horizontal Line Width
	 *     hsync_len + left_margin + xres + right_margin
	 * E = Horizontal Visible Width
	 *     xres
	 *
	 *               :<-----------------D----------------->:
	 *           :   :     :     :<----------E-------->:   :
	 *           :<A>:<-B->:<-C->:                     :<A>:<-B->:<-C->:
	 *           :   :     :     :                     :   :     :     :
	 *        ___                 __________//_________                 __
	 *           |               |                     |               |
	 *           |               |                     |               |
	 * Blank     |___       _____|                     |___       _____|
	 *               |     |                               |     |
	 * Sync          |_____|                               |_____|
	 *
	 *
	 * f = Sync Start to Color Burst Start
	 * g = Color Burst Width
	 *
	 *  :       :             :                  :
	 *  :<--A-->:<-----B----->:<-------C-------->:
	 *  :       :             :                  :
	 *                                             _ Peak white level
	 *  |                            Color       |
	 *  |                            Burst       |
	 *  |_______               ______|||||||||___| _ Blanking level
	 *          | Sync        |      |||||||||
	 *          |_____________|                    _ Sync level
	 *
	 *  :       :                    :       :   :
	 *  :       :<---------f-------->:<--g-->:   :
	 *  :<--------------- A + B + C ------------>:
	 *  :                                        :
	 *
	 */

	/* Half (horizontal) line width, in pixel clocks (D/2)  */
	u16 hlw;

	/* Horizontal Sync Width, in pixel clocks (B) */
	u8 hsy;

	/* NOTE
	 * The color burst interval falls within the back porch,
	 * i.e. hcs must be greater than B and hce lower than B+C.
	 */

	/* Horizontal sync start to color burst start in pixel clocks (f) */
	u8 hcs;
	/* Horizontal sync start to color burst end in pixel clocks (f+g) */
	u8 hce;

	/*
	 * The following two settings depend on the effective horizontal
	 * line length, as they rely on A or C.
	 */

	/* Half line to horizontal blank start (D/2 - A)*/
	u16 hbs;

	/* Horizontal sync start to horizontal blank end (B+C)*/
	u16 hbe;
};

static struct viModeTimings timings;

struct viModeChoice {
	/* has this mode choice been filled yet? */
	bool valid;
	/* was the mode successfully applied? */
	bool succeeded;
	/* desired mode */
	enum viMode mode;
};

enum viModeChoiceIdx {
	/*
	 * Best guess from existing hardware state and data available at viDrvInit
	 * time
	 */
	VI_MODE_CHOICE_EARLY,
	/*
	 * Desired mode derived from Wii SFFS
	 */
	VI_MODE_CHOICE_SYS,
	/*
	 * Desired mode from config
	 */
	VI_MODE_CHOICE_CONF,

	VI_MODE_CHOICE_MAX
};
static struct viModeChoice modes[VI_MODE_CHOICE_MAX] = { 0 };

static struct viModeChoice *viGetBestMode(void) {
	int i;

	for (i = VI_MODE_CHOICE_MAX - 1; i >= 0; i--) {
		if (modes[i].valid)
			return &modes[i];
	}

	return NULL;
}

static const char *viModeToStr(enum viMode mode) {
	static const char *strs[VI_MODE_MAX] = {
		"NTSC 480i",
		"NTSC 480p",
		"PAL50 576i",
		"PAL60 480i",
		"PAL60 480p"
	};

	return strs[mode];
}

static bool viModeIsProgressive(void) {
	switch (videoMode) {
	case VI_MODE_640X480_NTSC_PROG:
	case VI_MODE_640X480_PAL60_PROG:
		return true;
	default:
		return false;
	}
}

static bool viModeIsPAL(void) {
	switch (videoMode) {
	case VI_MODE_640X480_PAL60_PROG:
	case VI_MODE_640X480_PAL60_INT:
	case VI_MODE_640X576_PAL50_INT:
		return true;
	default:
		return false;
	}
}

static bool viModeIsPAL60(void) {
	switch (videoMode) {
	case VI_MODE_640X480_PAL60_PROG:
	case VI_MODE_640X480_PAL60_INT:
		return true;
	default:
		return false;
	}
}

static void viSetXFB(void *xfbAddr) {
	u32 fb = (u32)(uintptr_t)virtToPhys(xfbAddr); /* physical addr */

	regs->tfbl = (fb >> 5) | VI_FB_POB;
	if (!viModeIsProgressive())
		fb += 2 * 640; // 640 pixels == 1 line
	regs->bfbl = (fb >> 5) | VI_FB_POB;
}

static int viCalcHorzTimings(u16 width, u16 maxActiveWidth, u16 A, u8 B, u16 C, u16 D, u8 f, u16 g) {
	u16 extraBlanking, margin;

	assert(width < maxActiveWidth);

	/* adjusted horizontal settings */
	extraBlanking = maxActiveWidth - width;
	margin = extraBlanking / 2;
	A += margin;
	C += (u16)(extraBlanking - margin);

	timings.hlw = D / 2;
	timings.hsy = B;
	timings.hcs = f;
	timings.hce = (u8)(f + g);
	timings.hbs = (D/2) - A;
	timings.hbe = B + C;

	/*
	 * Start of the blanking interval, between the first and second fields,
	 * begins after the last half line of the field.
	 */
	timings.htrap = (D / 2) + 1;

	#if 0
	var->left_margin = C;
	var->right_margin = A;
	var->hsync_len = B;
	#endif

	return 0;
}

static int viNTSC525CalcHorzTimings(u16 width) {
	u16 maxActiveWidth;
	u16 A, C, D, g;
	u8 B, f;

	/* standard horizontal settings for 714 pixels */
	D = 858;		/* pixel clocks (H=63.556us, 13.5MHz clock) */
	maxActiveWidth = 714;	/* (52.9us) 714.15 pixel clocks */
	B = 64;			/* ( 4.7us)  63.45 pixel clocks */
	f = 71;			/* ( 5.3us)  71.55 pixel clocks */
	g = 34;			/* ( 2.5us)  33.75 pixel clocks */
	A = 20;			/* ( 1.5us)  20.25 pixel clocks */
	C = 60;			/* ( 4.5us)  60.75 pixel clocks */

	return viCalcHorzTimings(width, maxActiveWidth, A, B, C, D, f, g);
}

static int viCalcVertTimings(u16 height, u16 maxActiveHeight, u16 P, u16 Q, u8 equ) {
	u16 extraBlanking, margin, prb, psb;
	u8 interlace_bias;
	u8 shift;

	assert(height < maxActiveHeight);

	extraBlanking = maxActiveHeight - height;	/* in frame lines */
	margin = extraBlanking / 2;			/* centered margins */
	prb = margin; 					/* in half lines */
	psb = extraBlanking - margin;			/* in half lines */

	/*
	 * Start of the blanking interval, between the first and second fields,
	 * begins after the last line of the field.
	 */
	if (viModeIsProgressive()) {
		timings.acv = height;
		timings.vtrap = prb + height;
		interlace_bias = 0;
		shift = 1;
	}
	else {
		timings.acv = height / 2;
		timings.vtrap = (uint)((prb + height) / 2);
		interlace_bias = 1;
		shift = 0;
	}

	timings.equ = (u8)(equ << shift);
	#if 0
	var->vsync_len = (3 * timings.equ) / 2; /* pre-eq + sync + post-eq */
	#endif

	/*
	 * prb_* is specified as the number of half-lines since the end of
	 * the post-equalizing period.
	 * psb_* is specified as the number of half-lines from the end of
	 * the field.
	 */

	timings.ypos = margin;

	if (timings.ypos & 0x01) {
		/* odd field (1,3,5,...) */
		timings.prbOdd = (u8)((P + interlace_bias + prb) << shift);
		timings.psbOdd = (u8)((Q - interlace_bias + psb) << shift);
		timings.prbEven = (u8)((P + prb) << shift);
		timings.psbEven = (u8)((Q + psb) << shift);
	}
	else {
		/* even field (2,4,6,...) */
		timings.prbEven = (u8)((P + interlace_bias + prb) << shift);
		timings.psbEven = (u8)((Q - interlace_bias + psb) << shift);
		timings.prbOdd = (u8)((P + prb) << shift);
		timings.psbOdd = (u8)((Q + psb) << shift);
	}

	#if 0
	var->upper_margin = (Q + prb) / 2;
	var->lower_margin = (P + psb) / 2;
	#endif

	return 0;
}

static int viNTSC525CalcVertTimings(u16 height) {
	u16 maxActiveHeight;
	u16 P, Q;
	u8 equ;

	/* standard vertical settings for 484 active lines */
	maxActiveHeight = 484;	/* 2 * 242.5 = 485 (*1) */

	/* blanking interval */
	/* from start of line 10, field 1 to end of line 20, field 1 */
	P = 2 * (20-10 + 1);
	Q = 1;	/* (*1) field line compensation for 484 vs 485 lines */

	equ = 2 * 3;	/* 3 lines of equalization */

	return viCalcVertTimings(height, maxActiveHeight, P, Q, equ);
}

static int viPAL625CalcTimings(uint width, uint height) {
	u16 maxActiveHeight, maxActiveWidth;
	u16 A, C, D, g, P, Q;
	u8 B, f, equ;
	int error;

	/* standard horizontal settings for 702 pixels */
	D = 864;		/* pixel clocks (H=64us, 13.5MHz clock) */
	maxActiveWidth = 702;   /* (51.95us) 701.32 pixel clocks */
	B = 64;			/* ( 4.7us)   63.45 pixel clocks */
	f = 75;			/* ( 5.6us)   75.6  pixel clocks */
	g = 30;			/* ( 2.25us)  30.38 pixel clocks */
	A = 22;			/* ( 1.65us)  22.27 pixel clocks */
	C = 76;			/* ( 5.7us)   76.95 pixel clocks */

	error = viCalcHorzTimings((u16)width, maxActiveWidth, A, B, C, D, f, g);
	if (error)
		return error;

	/* standard vertical settings for 574 active lines */
	maxActiveHeight = 574;	/* 2 * 287.5 = 575 (*1) */

	/* blanking interval */
	/* from start of line 6, field 1 to mid of line 23, field 1 */
	P = (2 * (23-6 + 1)) - 1;
	Q = 1;	/* (*1) field line compensation for 574 vs 575 lines */

	equ = (u8)(2 * 2.5);		/* 2.5 lines of equalization */

	error = viCalcVertTimings((u16)height, maxActiveHeight, P, Q, equ);
	if (error)
		return error;

	/*
	 * Location of the 9 lines of burst blanking for each field
	 * (settings expressed in half lines).
	 */

	/* from start of line 1, field 1 to end of line 6, field 1 */
	timings.bs1 = 2 * (6-1 + 1);

	/* from start of line 1, field 1 to end of line 309, field 2 */
	timings.be1 = 2 * (309-1 + 1);

	/* from mid of line 313, field 2 to end of line 318, field 2 */
	timings.bs2 = (2 * (318-313 + 1)) - 1;

	/* from mid of line 313, field 2 to end of line 621, field 2 */
	timings.be2 = (u16)((2 * (612-617 + 1)) - 1);

	/* from start of line 1, field 3 to end of line 5, field 3 */
	timings.bs3 = 2 * (5-1 + 1);

	/* from start of line 1, field 3 to end of line 310, field 4 */
	timings.be3 = 2 * (310-1 + 1);

	/* from mid of line 313, field 4 to end of line 319, field 4 */
	timings.bs4 = (2 * (319-313 + 1)) - 1;

	/* from mid of line 313, field 4 to end of line 622, field 4 */
	timings.be4 = (2 * (622-313 + 1)) - 1;

	return 0;
}

static int viNTSC525CalcTimings(uint width, uint height) {
	int error;

	error = viNTSC525CalcHorzTimings((u16)width);
	if (error)
		return error;

	error = viNTSC525CalcVertTimings((u16)height);
	if (error)
		return error;

	/*
	 * Location of the 9 lines of burst blanking for each field
	 * (settings expressed in half lines).
	 */

	/* from start of line 4, field 1 to end of line 9, field 1 */
	timings.bs1 = 2 * (9-4 + 1);

	/* from start of line 4, field 1 to end of line 263, field 2 */
	timings.be1 = 2 * (263-4 + 1);

	/* from mid of line 266, field 2 to end of line 272, field 2 */
	timings.bs2 = (2 * (272-266 + 1)) - 1;

	/* from mid of line 266, field 2 to end of line 525, field 2 */
	timings.be2 = (2 * (525-266 + 1)) - 1;

	/* from start of line 4, field 3 to end of line 9, field 3 */
	timings.bs3 = 2 * (9-4 + 1);

	/* from start of line 4, field 3 to end of line 263, field 4 */
	timings.be3 = 2 * (263-4 + 1);

	/* from mid of line 266, field 4 to end of line 272, field 4 */
	timings.bs4 = (2 * (272-266 + 1)) - 1;

	/* from mid of line 266, field 4 to end of line 525, field 4 */
	timings.be4 = (2 * (525-266 + 1)) - 1;

	return 0;
}

static int viNTSC525ProgCalcTimings(uint width, uint height) {
	int error;

	error = viNTSC525CalcHorzTimings((u16)width);
	if (error)
		return error;

	error = viNTSC525CalcVertTimings((u16)height);
	if (error)
		return error;

	/*
	 * Location of the 18 lines of burst blanking
	 * (settings expressed in half lines).
	 */

	/*
	 * |0 0 0 0 0 0|0 0 0 1 1 1|1 1 1 1 1 1|
	 * |1,2,3,4,5,6|7,8,9,0,1,2|3,4,5,6,7,8|
	 * :pre-equ    :sync       : post-equ  :
	 */

	/* from start of line 7 to end of line 18 */
	timings.bs1 = 2 * (18-7 + 1);
	timings.bs2 = timings.bs3 = timings.bs4 = timings.bs1;

	/* from start of line 7 to end of line 525 (last) */
	timings.be1 = 2 * (525-7 + 1);
	timings.be2 = timings.be3 = timings.be4 = timings.be1;

	return 0;
}

enum viInitResult {
	/* The desired mode was applied */
	VI_INIT_RESULT_SUCCESS,
	/*
	 * A fallback mode was applied as the desired mode could not be applied due
	 * to some hardware conditions (e.g. _PROG mode on composite cables).
	 */
	VI_INIT_RESULT_FALLBACK
};

static enum viInitResult viInit(enum viMode mode) {
	uint xres = 640, yres = 480, std, ppl, i;
	bool hasComponentCable;
	enum viInitResult ret = VI_INIT_RESULT_SUCCESS;
	static const u16 dcrVals[VI_MODE_MAX] = {
		/* NTSC 480i  */ VI_DCR_FMT_NTSC,
		/* NTSC 480p  */ VI_DCR_FMT_NTSC | VI_DCR_NIN,
		/* PAL60 576i */ VI_DCR_FMT_PAL,
		/* FIXME: Linux uses NTSC for PAL60 modes, is this right? */
		/* PAL60 480i */ VI_DCR_FMT_NTSC,
		/* PAL60 480p */ VI_DCR_FMT_NTSC | VI_DCR_NIN
	};
	static const u32 fct[7] = {
		0x1AE771F0, 0x0DB4A574, 0x00C1188E, 0xC4C0CBE2,
		0xFCECDECF, 0x13130F08, 0x00080C0F,
	};

	regs->dcr = VI_DCR_RST;
	udelay(2);
	regs->dcr = 0;

	hasComponentCable = !!(regs->visel & 1);
	/* if not component, downgrade to interlaced mode */
	if (!hasComponentCable) {
		switch (mode) {
		case VI_MODE_640X480_NTSC_PROG: {
			mode = VI_MODE_640X480_NTSC_INT;
			ret = VI_INIT_RESULT_FALLBACK;
			break;
		}
		case VI_MODE_640X480_PAL60_PROG: {
			mode = VI_MODE_640X480_PAL60_INT;
			ret = VI_INIT_RESULT_FALLBACK;
			break;
		}
		default:
			break;
		}
	}
	videoMode = mode;

	if (viModeIsProgressive())
		viNTSC525ProgCalcTimings(xres, yres);
	else {
		if (mode == VI_MODE_640X576_PAL50_INT) {
			yres = 576;
			viPAL625CalcTimings(xres, yres);
		}
		else
			viNTSC525CalcTimings(xres, yres);
	}

	/* Keep scanout disabled until the XFB has been allocated and cleared. */
	regs->dcr = dcrVals[mode];
	regs->vtr = (u16)(((u16)timings.equ << VI_VTR_EQU_SHIFT) | ((u16)timings.acv << VI_VTR_ACV_SHIFT));
	regs->htr0 = ((u32)timings.hcs << VI_HTR0_HCS_SHIFT) |
		((u32)timings.hce << VI_HTR0_HCE_SHIFT) |
		((u32)timings.hlw << VI_HTR0_HLW_SHIFT);
	regs->htr1 = ((u32)timings.hbs << VI_HTR1_HBS_SHIFT) |
		((u32)timings.hbe << VI_HTR1_HBE_SHIFT) |
		((u32)timings.hsy << VI_HTR1_HSY_SHIFT);
	regs->vto = ((u32)timings.prbOdd << VI_VTO_PRB_SHIFT) | ((u32)timings.psbOdd << VI_VTO_PSB_SHIFT);
	regs->vte = ((u32)timings.prbEven << VI_VTE_PRB_SHIFT) | ((u32)timings.psbEven << VI_VTE_PSB_SHIFT);
	regs->bboi = ((u32)timings.bs1 << VI_BBOI_BS1_SHIFT) |
		((u32)timings.be1 << VI_BBOI_BE1_SHIFT) |
		((u32)timings.bs3 << VI_BBOI_BS3_SHIFT) |
		((u32)timings.be3 << VI_BBOI_BE3_SHIFT);
	regs->bbei = ((u32)timings.bs2 << VI_BBEI_BS2_SHIFT) |
		((u32)timings.be2 << VI_BBEI_BE2_SHIFT) |
		((u32)timings.bs4 << VI_BBEI_BS4_SHIFT) |
		((u32)timings.be4 << VI_BBEI_BE4_SHIFT);

	/* used only for 3D stuff */
	regs->tfbr = 0;
	regs->bfbr = 0;

	std = (xres * YUYV_BYTES_PER_PIX) / VI_HORZ_WORD_SIZE;
	if (!viModeIsProgressive())
		std *= 2;

	ppl = alignUpU32(xres, VI_HORZ_ALIGN + 1);
	regs->pcr = (u16)(((u16)std << VI_PCR_STD_SHIFT) | ((u16)((ppl * YUYV_BYTES_PER_PIX) / VI_HORZ_WORD_SIZE) << VI_PCR_WPL_SHIFT));

	/* disable horizontal scaler */
	regs->hsr = (u16)256 << VI_HSR_STP_SHIFT;

	/* filter coefficient table, anti-aliasing */
	for (i = 0; i < 7; i++)
		regs->fct[i] = fct[i];
	regs->aa = 0x00ff0000u;

	/* clock */
	regs->viclk = viModeIsProgressive() ? VI_VICLK_54MHZ : VI_VICLK_27MHZ;

	/* borders for DEBUG mode encoder, not used in retail consoles */
	regs->hbe = 0;
	regs->hbs = 0;

	/* whatever */
	regs->unk_76 = 0x00ffu;
	regs->unk_78 = 0x00ff00ffu;
	regs->unk_7c = 0x00ff00ffu;

	/* we don't use any of the display interrupts */
	for (i = 0; i < 4; i++)
		regs->di[i] = 0;

	videoWidth = xres;
	videoHeight = yres;

	return ret;
}

#define SLAVE_AVE 0x70

static int viAVEOuts(u8 reg, void *data, size_t len) {
	u8 buf[34];
	int error, result;

	if (len > sizeof(buf)-1)
		goto err_out;

	buf[0] = reg;
	memcpy(&buf[1], data, len);

	result = I2C_Write(I2C_BUS_AVE, SLAVE_AVE, buf, (uint)len + 1);
	if (result)
		error = result;
	else {
		/*
		 *  The AVE needs a short interval to process a write before
		 * accepting the next transaction.
		 */
		udelay(2);
		error = 0;
	}

err_out:
	if (error)
		log_printf("AVE-RVL: error (%d) writing to register %02Xh\r\n", error, reg);
	return error;
}

static int viAVEOut8(u8 reg, u8 data) {
	return viAVEOuts(reg, &data, sizeof(data));
}

static int viAVEOut16(u8 reg, u16 data) {
	return viAVEOuts(reg, &data, sizeof(data));
}

static int viAVEOut32(u8 reg, u32 data) {
	return viAVEOuts(reg, &data, sizeof(data));
}

static int viAVEIns(u8 reg, void *data, size_t len) {
	int error;

	error = I2C_Write(I2C_BUS_AVE, SLAVE_AVE, &reg, 1);
	if (error)
		goto err;

	error = I2C_Read(I2C_BUS_AVE, SLAVE_AVE, data, (uint)len);

err:
	if (error)
		log_printf("AVE-RVL: error (%d) reading from register %02Xh\r\n", error, reg);

	return error;
}

static int viAVEIn8(u8 reg, u8 *data) {
	return viAVEIns(reg, data, sizeof(*data));
}

/*
 * Try to detect current video format.
 */
static int viAVEGetVideoFormat(u8 *out) {
	int error = viAVEIn8(AVE_VID_OUT_CFG, out);
	if (error)
		return error;

	*out &= AVE_VID_OUT_CFG_FMT;
	return error;
}

static int viAVESetup(bool usingComponent) {
	static u8 viAVEGamma[] = {
		0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00,
		0x10, 0x00, 0x10, 0x00, 0x10, 0x20, 0x40, 0x60,
		0x80, 0xa0, 0xeb, 0x10, 0x00, 0x20, 0x00, 0x40,
		0x00, 0x60, 0x00, 0x80, 0x00, 0xa0, 0x00, 0xeb,
		0x00
	};

	u8 macrovision[26];
	u8 component, format;
	int error;

#define aveWrite(_call)				\
	do {					\
		error = (_call);		\
		if (error)			\
			return error;		\
	} while (0)

	memset(macrovision, 0, sizeof(macrovision));

	/*
	 * Magic initialization sequence borrowed from libogc.
	 */

	aveWrite(viAVEOut8(0x6a, 1));
	aveWrite(viAVEOut8(0x65, 1));

	/*
	 * NOTE
	 * We _can't use the fmt field in DCR to derive "format" here.
	 * DCR uses fmt=0 (NTSC) also for PAL 525 modes.
	 */

	format = AVE_VID_OUT_CFG_FMT_NTSC;		/* default to NTSC */
	if (viModeIsPAL())
		format = AVE_VID_OUT_CFG_FMT_PAL;	/* PAL */
	component = (usingComponent) ? AVE_VID_OUT_CFG_YUV_EN : 0;
	aveWrite(viAVEOut8(AVE_VID_OUT_CFG, component | format));

	aveWrite(viAVEOut8(0x00, 0));
	aveWrite(viAVEOut16(0x71, 0x8e8e));
	aveWrite(viAVEOut8(0x02, 7));
	aveWrite(viAVEOut16(0x05, 0x0000));
	aveWrite(viAVEOut16(0x08, 0x0000));
	aveWrite(viAVEOut32(0x7a, 0x00000000));
	aveWrite(viAVEOuts(0x40, macrovision, sizeof(macrovision)));
	aveWrite(viAVEOut8(0x0a, 0));
	aveWrite(viAVEOut8(0x03, 1));
	aveWrite(viAVEOuts(0x10, viAVEGamma, sizeof(viAVEGamma)));
	aveWrite(viAVEOut8(0x04, 1));

	aveWrite(viAVEOut32(0x7a, 0x00000000));
	aveWrite(viAVEOut16(0x08, 0x0000));

	aveWrite(viAVEOut8(0x03, 1));

	/* clear bit 1 otherwise red and blue get swapped  */
	if (component)
		aveWrite(viAVEOut8(0x62, 0));

	/* PAL 480i/60 supposedly needs a "filter" */
	aveWrite(viAVEOut8(0x6e, (u8)viModeIsPAL60()));

#undef aveWrite
	return 0;
}

typedef union {
	struct PACKED {
		u8 x, r, g, b;
	} as_xrgb;
	u32 as_u32;
} rgb;

static u32 makeYUV(rgb c1, rgb c2) {
	int y1, y2, cb, cr, r1, g1, b1, r2, g2, b2, r, g, b;

	/* unpack them */
	r1 = c1.as_xrgb.r;
	g1 = c1.as_xrgb.g;
	b1 = c1.as_xrgb.b;
	r2 = c2.as_xrgb.r;
	g2 = c2.as_xrgb.g;
	b2 = c2.as_xrgb.b;

	/* calculate split luminance */
	y1 = ((77 * r1) + (150 * g1) +  (29 * b1)) / 256;
	y2 = ((77 * r2) + (150 * g2) +  (29 * b2)) / 256;

	/* get average for the color */
	r = (r1 + r2) / 2;
	g = (g1 + g2) / 2;
	b = (b1 + b2) / 2;

	/* calculate color */
	cb = (((-44 * r) - (87  * g) + (131 * b)) / 256) + 128;
	cr = (((131 * r) - (110 * g) - (21  * b)) / 256) + 128;

	/* pack into YUYV */
	return ((u32)y1 << 24) | ((u32)cb << 16) | ((u32)y2 << 8) | (u32)cr;
}

static void clearFb(rgb fillRGB) {
	u32 *fb = (u32 *)xfb;
	u32 fillYUV = makeYUV(fillRGB, fillRGB);
	uint i;

	for (i = 0; i < (videoWidth * videoHeight) / 2; i++)
		fb[i] = fillYUV;

	dcache_flush(xfb, videoHeight * videoWidth * sizeof(u16));
}

static void clearFbRGB(rgb fillRGB) {
	u32 *fb = rgbFb;
	uint i;

	for (i = 0; i < videoWidth * videoHeight; i++)
		fb[i] = fillRGB.as_u32;
}

static struct videoInfo viVidInfo;

static void viScroll(uint rows) {
	u8 *dest = (u8 *)xfb + XFB_OS_COMP_PIX * videoWidth * (uint)sizeof(u16);
	uint rowSize = videoWidth * (uint)sizeof(u16);
	uint size = (viVidInfo.height - rows) * rowSize;

	memmove(dest, dest + rows * rowSize, size);
	dcache_flush(dest, size);
}

static void viFlush(uint x, uint y, uint width, uint height) {
	u32 *dest, *src;
	rgb rgb1, rgb2;
	uint endX, row, col;

	/* YUYV stores pairs of pixels, so expand odd dirty edges to a pair. */
	endX = (x + width + 1u) & ~1u;
	x &= ~1u;
	width = endX - x;
	y += XFB_OS_COMP_PIX;

	for (row = 0; row < height; row++) {
		src = rgbFb + (y + row) * videoWidth + x;
		dest = (u32 *)xfb + ((y + row) * videoWidth + x) / 2;
		for (col = 0; col < width; col += 2) {
			rgb1 = (rgb)src[col];
			rgb2 = (rgb)src[col + 1];
			dest[col / 2] = makeYUV(rgb1, rgb2);
		}
	}

	/* Flush one span to avoid paying for a sync on every scanline. */
	dcache_flush((u16 *)xfb + y * videoWidth + x,
	    ((height - 1) * videoWidth + width) * sizeof(u16));
}

static struct videoInfo viVidInfo = {
	.fb = NULL,
	.flush = viFlush,
	.scroll = viScroll,
	.driver = &viDrv
};

static enum viMode viGuessEarlyMode(void) {
	u8 aveFmt;
	u32 dcrMode, viClk;
	int error;

	if (regs->dcr & VI_DCR_ENB) {
		/* extract existing config */
		dcrMode = regs->dcr & (VI_DCR_FMT | VI_DCR_NIN);
		viClk = regs->viclk;
		if (H_ConsoleType == CONSOLE_TYPE_WII) {
			error = viAVEGetVideoFormat(&aveFmt);
			if (error) {
				log_printf("Can't get format from AVE for initial guess (%d)\r\n", error);
				goto fallback;
			}

			if (dcrMode == VI_DCR_FMT_NTSC && aveFmt == AVE_VID_OUT_CFG_FMT_NTSC && viClk == VI_VICLK_27MHZ)
				return VI_MODE_640X480_NTSC_INT;
			else if (dcrMode == (VI_DCR_FMT_NTSC | VI_DCR_NIN) && aveFmt == AVE_VID_OUT_CFG_FMT_NTSC && viClk == VI_VICLK_54MHZ)
				return VI_MODE_640X480_NTSC_PROG;
			else if (dcrMode == VI_DCR_FMT_PAL && aveFmt == AVE_VID_OUT_CFG_FMT_PAL && viClk == VI_VICLK_27MHZ)
				return VI_MODE_640X576_PAL50_INT;
			else if (dcrMode == VI_DCR_FMT_NTSC && aveFmt == AVE_VID_OUT_CFG_FMT_PAL && viClk == VI_VICLK_27MHZ)
				return VI_MODE_640X480_PAL60_INT;
			else if (dcrMode == (VI_DCR_FMT_NTSC | VI_DCR_NIN) && aveFmt == AVE_VID_OUT_CFG_FMT_PAL && viClk == VI_VICLK_54MHZ)
				return VI_MODE_640X480_PAL60_PROG;
		}
		else if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
			if (dcrMode == VI_DCR_FMT_NTSC && viClk == VI_VICLK_27MHZ)
				return VI_MODE_640X480_NTSC_INT;
			else if (dcrMode == (VI_DCR_FMT_NTSC | VI_DCR_NIN) && viClk == VI_VICLK_54MHZ)
				return VI_MODE_640X480_NTSC_PROG;
			else if (dcrMode == VI_DCR_FMT_PAL && viClk == VI_VICLK_27MHZ)
				return VI_MODE_640X576_PAL50_INT;
		}
		else
			assert_unreachable();
	}

	/*
	 * VI is either not enabled, or using a config we can't match... but, if
	 * we're on a GameCube, we can check the IPL.
	 */
	if (H_ConsoleType == CONSOLE_TYPE_GAMECUBE) {
		/* TODO: check IPL */
	}

fallback:
	/* We can't guess a valid mode, return fallback */
	log_puts("Can't derive valid early mode from current state!");
	log_puts("Assuming NTSC 480i is fine for now.");
	return VI_MODE_640X480_NTSC_INT;
}

static void viDrvInit(void) {
	int error;
	enum viMode desired;
	rgb black = {.as_u32 = 0xff000000};

	desired = viGuessEarlyMode();

	/* Establish the dimensions before sizing either framebuffer. */
	if (viInit(desired) == VI_INIT_RESULT_SUCCESS)
		modes[VI_MODE_CHOICE_EARLY].succeeded = true;
	modes[VI_MODE_CHOICE_EARLY].mode = desired;

	/* XFB must be in MEM1, 32B aligned */
	xfb = M_PoolAlloc(POOL_MEM1, sizeof(u16) * videoWidth * videoHeight, 32);
	clearFb(black);
	viSetXFB(xfb);
	if (H_ConsoleType == CONSOLE_TYPE_WII) {
		error = viAVESetup(!!(regs->visel & 1));
		if (error) {
			log_printf("AVE-RVL init failed: %d\r\n", error);
			viDrv.state = DRIVER_STATE_FAULTED;
			free(xfb);
			return;
		}
	}
	log_printf("Chose early mode: %s\r\n", viModeToStr(desired));
	modes[VI_MODE_CHOICE_EARLY].valid = true;
	regs->dcr |= VI_DCR_ENB;

	/* rgbFB can go wherever */
	rgbFb = malloc(sizeof(u32) * videoWidth * videoHeight);
	viVidInfo.fb = (u32 *)((uintptr_t)rgbFb + (videoWidth * XFB_OS_COMP_PIX * 4));

	clearFbRGB(black);
	viVidInfo.width = videoWidth,
	viVidInfo.height = videoHeight - (XFB_OS_COMP_PIX * 2),
	V_Register(&viVidInfo);

	viDrv.state = DRIVER_STATE_READY;
}

static void viDrvCleanup(void) {
	viFlush(0, 0, viVidInfo.width, viVidInfo.height);
	free(rgbFb);
	free(xfb);
	viDrv.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(viDrv) = {
	.name = "Flipper/Hollywood Video Interface",
	.state = DRIVER_STATE_NOT_READY,
	.mask = DRIVER_ALLOW_GAMECUBE | DRIVER_ALLOW_WII,
	.init = viDrvInit,
	.cleanup = viDrvCleanup,
	.type = DRIVER_TYPE_GFX,
};
