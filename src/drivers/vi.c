/*
 * NPLL - Flipper/Hollywood Hardware - Video Interface
 *
 * Copyright (C) 2025 Techflash
 *
 * Based on code in BootMii ppcskel:
 * Copyright (C) 2008, 2009	Hector Martin "marcan" <marcan@marcansoft.com>
 * Copyright (C) 2009			Haxx Enterprises <bushing@gmail.com>
 * Copyright (c) 2009		Sven Peter <svenpeter@gmail.com>
 *
 * Copyright (C) 2009		bLAStY <blasty@bootmii.org>
 * Copyright (C) 2009		John Kelley <wiidev@kelley.ca>
 *
 * Original license disclaimer:
 * This code is licensed to you under the terms of the GNU GPL, version 2;
 * see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
 *
 * Some routines and initialization constants originally came from the
 * "GAMECUBE LOW LEVEL INFO" document and sourcecode released by Titanik
 * of Crazy Nation and the GC Linux project.
*/

#define MODULE "VI"

#include <string.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/timer.h>
#include <npll/video.h>
#include <npll/log.h>
#include <npll/flipper/vi.h>
#include <npll/hollywood/gpio.h>
#include <npll/allocator.h>

static REGISTER_DRIVER(viDrv);

#define XFB_WIDTH 640
#define XFB_HEIGHT 480
/* most TVs crop off the top and bottom-most ~16px - try to account for that here */
#define XFB_OS_COMP_PIX 16
static u32 *rgbFb;
static u16 *xfb;

#ifdef VI_DEBUG
#define  VI_debug(f, arg...) log_printf(f, ##arg);
#else
#define  VI_debug(f, arg...) while(0)
#endif

// hardcoded VI init states
static const u16 VIDEO_Mode640X480NtsciYUV16[64] = {
  0x0F06, 0x0001, 0x4769, 0x01AD, 0x02EA, 0x5140, 0x0003, 0x0018,
  0x0002, 0x0019, 0x410C, 0x410C, 0x40ED, 0x40ED, 0x0043, 0x5A4E,
  0x0000, 0x0000, 0x0043, 0x5A4E, 0x0000, 0x0000, 0x0000, 0x0000,
  0x1107, 0x01AE, 0x1001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
  0x0000, 0x0000, 0x0000, 0x0000, 0x2850, 0x0100, 0x1AE7, 0x71F0,
  0x0DB4, 0xA574, 0x00C1, 0x188E, 0xC4C0, 0xCBE2, 0xFCEC, 0xDECF,
  0x1313, 0x0F08, 0x0008, 0x0C0F, 0x00FF, 0x0000, 0x0000, 0x0000,
  0x0280, 0x0000, 0x0000, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF};

static const u16 VIDEO_Mode640X480Pal50YUV16[64] = {
  0x11F5, 0x0101, 0x4B6A, 0x01B0, 0x02F8, 0x5640, 0x0001, 0x0023,
  0x0000, 0x0024, 0x4D2B, 0x4D6D, 0x4D8A, 0x4D4C, 0x0043, 0x5A4E,
  0x0000, 0x0000, 0x0043, 0x5A4E, 0x0000, 0x0000, 0x013C, 0x0144,
  0x1139, 0x01B1, 0x1001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
  0x0000, 0x0000, 0x0000, 0x0000, 0x2850, 0x0100, 0x1AE7, 0x71F0,
  0x0DB4, 0xA574, 0x00C1, 0x188E, 0xC4C0, 0xCBE2, 0xFCEC, 0xDECF,
  0x1313, 0x0F08, 0x0008, 0x0C0F, 0x00FF, 0x0000, 0x0000, 0x0000,
  0x0280, 0x0000, 0x0000, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF};

static const u16 VIDEO_Mode640X480Pal60YUV16[64] = {
  0x0F06, 0x0001, 0x4769, 0x01AD, 0x02EA, 0x5140, 0x0003, 0x0018,
  0x0002, 0x0019, 0x410C, 0x410C, 0x40ED, 0x40ED, 0x0043, 0x5A4E,
  0x0000, 0x0000, 0x0043, 0x5A4E, 0x0000, 0x0000, 0x0005, 0x0176,
  0x1107, 0x01AE, 0x1001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
  0x0000, 0x0000, 0x0000, 0x0000, 0x2850, 0x0100, 0x1AE7, 0x71F0,
  0x0DB4, 0xA574, 0x00C1, 0x188E, 0xC4C0, 0xCBE2, 0xFCEC, 0xDECF,
  0x1313, 0x0F08, 0x0008, 0x0C0F, 0x00FF, 0x0000, 0x0000, 0x0000,
  0x0280, 0x0000, 0x0000, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF};

static const u16 VIDEO_Mode640X480NtscpYUV16[64] = {
  0x1E0C, 0x0005, 0x4769, 0x01AD, 0x02EA, 0x5140, 0x0006, 0x0030,
  0x0006, 0x0030, 0x81D8, 0x81D8, 0x81D8, 0x81D8, 0x0015, 0x77A0,
  0x0000, 0x0000, 0x0015, 0x77A0, 0x0000, 0x0000, 0x022A, 0x01D6,
  0x120E, 0x0001, 0x1001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
  0x0000, 0x0000, 0x0000, 0x0000, 0x2828, 0x0100, 0x1AE7, 0x71F0,
  0x0DB4, 0xA574, 0x00C1, 0x188E, 0xC4C0, 0xCBE2, 0xFCEC, 0xDECF,
  0x1313, 0x0F08, 0x0008, 0x0C0F, 0x00FF, 0x0000, 0x0001, 0x0001,
  0x0280, 0x807A, 0x019C, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF};

static int video_mode;

void VIDEO_Init(int VideoMode)
{
	u32 Counter=0;
	const u16 *video_initstate=NULL;

	VI_debug("Resetting VI...\n");
	R_VIDEO_STATUS1 = 2;
	udelay(2);
	R_VIDEO_STATUS1 = 0;
	VI_debug("VI reset...\n");

	switch(VideoMode)
	{
	case VIDEO_640X480_NTSCi_YUV16:
		video_initstate = VIDEO_Mode640X480NtsciYUV16;
		break;

	case VIDEO_640X480_PAL50_YUV16:
		video_initstate = VIDEO_Mode640X480Pal50YUV16;
		break;

	case VIDEO_640X480_PAL60_YUV16:
		video_initstate = VIDEO_Mode640X480Pal60YUV16;
		break;

	case VIDEO_640X480_NTSCp_YUV16:
		video_initstate = VIDEO_Mode640X480NtscpYUV16;
		break;

	/* Use NTSC as default */
	default:
		VideoMode = VIDEO_640X480_NTSCi_YUV16;
		video_initstate = VIDEO_Mode640X480NtsciYUV16;
		break;
	}

	VI_debug("Configuring VI...\n");
	for(Counter=0; Counter<64; Counter++)
	{
		if(Counter==1)
			*(vu16 *)(MEM_VIDEO_BASE + 2*Counter) = video_initstate[Counter] & 0xFFFE;
		else
			*(vu16 *)(MEM_VIDEO_BASE + 2*Counter) = video_initstate[Counter];
	}

	video_mode = VideoMode;

	R_VIDEO_STATUS1 = video_initstate[1];
#ifdef VI_DEBUG
	VI_debug("VI dump:\n");
	for(Counter=0; Counter<32; Counter++)
		log_printf("%02x: %04x %04x,\n", Counter*4, *(vu16 *)(MEM_VIDEO_BASE + Counter*4), *(vu16 *)(MEM_VIDEO_BASE + Counter*4+2));

	log_printf("---\n");
#endif
}

void VIDEO_SetFrameBuffer(void *FrameBufferAddr)
{
	u32 fb = (u32)FrameBufferAddr & ~0xc0000000; /* physical addr */

	R_VIDEO_FRAMEBUFFER_1 = (fb >> 5) | 0x10000000;
	if(video_mode != VIDEO_640X480_NTSCp_YUV16)
		fb += 2 * 640; // 640 pixels == 1 line
	R_VIDEO_FRAMEBUFFER_2 = (fb >> 5) | 0x10000000;
}

void VIDEO_WaitVSync(void)
{
	while(R_VIDEO_HALFLINE_1 >= 200);
	while(R_VIDEO_HALFLINE_1 <  200);
}

/* black out video (not reversible!) */
void VIDEO_BlackOut(void)
{
	VIDEO_WaitVSync();

	int active = R_VIDEO_VTIMING >> 4;

	R_VIDEO_PRB_ODD = R_VIDEO_PRB_ODD + ((active<<1)-2);
	R_VIDEO_PRB_EVEN = R_VIDEO_PRB_EVEN + ((active<<1)-2);
	R_VIDEO_PSB_ODD = R_VIDEO_PSB_ODD + 2;
	R_VIDEO_PSB_EVEN = R_VIDEO_PSB_EVEN + 2;

	R_VIDEO_VTIMING &= ~0xfffffff0;
}

//static vu16* const _viReg = (u16*)0xCC002000;

void VIDEO_Shutdown(void)
{
	VIDEO_BlackOut();
	R_VIDEO_STATUS1 = 0;
}

#define SLAVE_AVE 0xe0

static inline void aveSetDirection(u32 dir)
{
	u32 val = (HW_GPIOB_DIR & ~0x8000)|0x4000;
	if(dir) val |= 0x8000;
	HW_GPIOB_DIR = val;
}

static inline void aveSetSCL(u32 scl)
{
	u32 val = HW_GPIOB_OUT & ~0x4000;
	if(scl) val |= 0x4000;
	HW_GPIOB_OUT = val;
}

static inline void aveSetSDA(u32 sda)
{
	u32 val = HW_GPIOB_OUT & ~0x8000;
	if(sda) val |= 0x8000;
	HW_GPIOB_OUT = val;
}

static inline u32 aveGetSDA()
{
	if(HW_GPIOB_IN & 0x8000)
		return 1;
	else
		return 0;
}

static u32 __sendSlaveAddress(u8 addr)
{
	u32 i;

	aveSetSDA(0);
	udelay(2);

	aveSetSCL(0);
	for(i=0;i<8;i++) {
		if(addr&0x80) aveSetSDA(1);
		else aveSetSDA(0);
		udelay(2);

		aveSetSCL(1);
		udelay(2);

		aveSetSCL(0);
		addr <<= 1;
	}

	aveSetDirection(0);
	udelay(2);

	aveSetSCL(1);
	udelay(2);

	if(aveGetSDA()!=0) {
		VI_debug("No ACK\n");
		return 0;
	}

	aveSetSDA(0);
	aveSetDirection(1);
	aveSetSCL(0);

	return 1;
}

static u32 __VISendI2CData(u8 addr,void *val,u32 len)
{
	u8 c;
	u32 i,j;
	u32 ret;

	VI_debug("I2C[%02x]:",addr);
	for(i=0;i<len;i++)
		VI_debug(" %02x", ((u8*)val)[i]);
	VI_debug("\n");

	aveSetDirection(1);
	aveSetSCL(1);

	aveSetSDA(1);
	udelay(4);

	ret = __sendSlaveAddress(addr);
	if(ret==0) {
		return 0;
	}

	aveSetDirection(1);
	for(i=0;i<len;i++) {
		c = ((u8*)val)[i];
		for(j=0;j<8;j++) {
			if(c&0x80) aveSetSDA(1);
			else aveSetSDA(0);
			udelay(2);

			aveSetSCL(1);
			udelay(2);
			aveSetSCL(0);

			c <<= 1;
		}
		aveSetDirection(0);
		udelay(2);
		aveSetSCL(1);
		udelay(2);

		if(aveGetSDA()!=0) {
			VI_debug("No ACK\n");
			return 0;
		}

		aveSetSDA(0);
		aveSetDirection(1);
		aveSetSCL(0);
	}

	aveSetDirection(1);
	aveSetSDA(0);
	udelay(2);
	aveSetSDA(1);

	return 1;
}

static void __VIWriteI2CRegister8(u8 reg, u8 data)
{
	u8 buf[2];
	buf[0] = reg;
	buf[1] = data;
	__VISendI2CData(SLAVE_AVE,buf,2);
	udelay(2);
}

static void __VIWriteI2CRegister16(u8 reg, u16 data)
{
	u8 buf[3];
	buf[0] = reg;
	buf[1] = data >> 8;
	buf[2] = data & 0xFF;
	__VISendI2CData(SLAVE_AVE,buf,3);
	udelay(2);
}

static void __VIWriteI2CRegister32(u8 reg, u32 data)
{
	u8 buf[5];
	buf[0] = reg;
	buf[1] = data >> 24;
	buf[2] = (data >> 16) & 0xFF;
	buf[3] = (data >> 8) & 0xFF;
	buf[4] = data & 0xFF;
	__VISendI2CData(SLAVE_AVE,buf,5);
	udelay(2);
}

static void __VIWriteI2CRegisterBuf(u8 reg, int size, u8 *data)
{
	u8 buf[0x100];
	buf[0] = reg;
	memcpy(&buf[1], data, size);
	__VISendI2CData(SLAVE_AVE,buf,size+1);
	udelay(2);
}

static void __VISetYUVSEL(u8 dtvstatus)
{
	int vdacFlagRegion;
	switch(video_mode) {
	case VIDEO_640X480_NTSCi_YUV16:
	case VIDEO_640X480_NTSCp_YUV16:
	default:
		vdacFlagRegion = 0;
		break;
	case VIDEO_640X480_PAL50_YUV16:
	case VIDEO_640X480_PAL60_YUV16:
		vdacFlagRegion = 2;
		break;
	}
	__VIWriteI2CRegister8(0x01, (dtvstatus<<5) | (vdacFlagRegion&0x1f));
}

static void __VISetFilterEURGB60(u8 enable)
{
	__VIWriteI2CRegister8(0x6e, enable);
}

void VISetupEncoder(void)
{
	u8 macrobuf[0x1a];

	u8 gamma[0x21] = {
		0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00,
		0x10, 0x00, 0x10, 0x00, 0x10, 0x20, 0x40, 0x60,
		0x80, 0xa0, 0xeb, 0x10, 0x00, 0x20, 0x00, 0x40,
		0x00, 0x60, 0x00, 0x80, 0x00, 0xa0, 0x00, 0xeb,
		0x00
	};

	u8 dtv;

	//tv = VIDEO_GetCurrentTvMode();
	dtv = R_VIDEO_VISEL & 1;
	//oldDtvStatus = dtv;

	// SetRevolutionModeSimple

	VI_debug("DTV status: %d\n", dtv);

	memset(macrobuf, 0, 0x1a);

	__VIWriteI2CRegister8(0x6a, 1);
	__VIWriteI2CRegister8(0x65, 1);
	__VISetYUVSEL(dtv);
	__VIWriteI2CRegister8(0x00, 0);
	__VIWriteI2CRegister16(0x71, 0x8e8e);
	__VIWriteI2CRegister8(0x02, 7);
	__VIWriteI2CRegister16(0x05, 0x0000);
	__VIWriteI2CRegister16(0x08, 0x0000);
	__VIWriteI2CRegister32(0x7A, 0x00000000);

	// Macrovision crap
	__VIWriteI2CRegisterBuf(0x40, sizeof(macrobuf), macrobuf);

	// Sometimes 1 in RGB mode? (reg 1 == 3)
	__VIWriteI2CRegister8(0x0A, 0);

	__VIWriteI2CRegister8(0x03, 1);

	__VIWriteI2CRegisterBuf(0x10, sizeof(gamma), gamma);

	__VIWriteI2CRegister8(0x04, 1);
	__VIWriteI2CRegister32(0x7A, 0x00000000);
	__VIWriteI2CRegister16(0x08, 0x0000);
	__VIWriteI2CRegister8(0x03, 1);

	//if(tv==VI_EURGB60) __VISetFilterEURGB60(1);
	//else
	__VISetFilterEURGB60(0);

	//oldTvStatus = tv;
}

typedef union {
		struct __attribute__((__packed__)) {
			u8 x, r, g, b;
		} as_xrgb;
		u32 as_u32;
} rgb;

static int make_yuv(rgb c1, rgb c2) {
  int y1, cb1, cr1, y2, cb2, cr2, cb, cr;

  y1 = (299 * c1.as_xrgb.r + 587 * c1.as_xrgb.g  + 114 * c1.as_xrgb.b) / 1000;
  cb1 = (-16874 * c1.as_xrgb.r  - 33126 * c1.as_xrgb.g + 50000 * c1.as_xrgb.b + 12800000) / 100000;
  cr1 = (50000 * c1.as_xrgb.r  - 41869 * c1.as_xrgb.g - 8131 * c1.as_xrgb.b + 12800000) / 100000;

  y2 = (299 * c2.as_xrgb.r  + 587 * c2.as_xrgb.g + 114 * c2.as_xrgb.b) / 1000;
  cb2 = (-16874 * c2.as_xrgb.r  - 33126 * c2.as_xrgb.g + 50000 * c2.as_xrgb.b + 12800000) / 100000;
  cr2 = (50000 * c2.as_xrgb.r  - 41869 * c2.as_xrgb.g - 8131 * c2.as_xrgb.b + 12800000) / 100000;

  cb = (cb1 + cb2) >> 1;
  cr = (cr1 + cr2) >> 1;

  return ((y1 << 24) | (cb << 16) | (y2 << 8) | cr);
}

static void clear_fb(rgb fill_rgb) {
	u32 *fb = (u32 *)xfb;
	u32 fill_yuv = make_yuv(fill_rgb, fill_rgb);

	while ((void *)fb < ((void *)xfb) + (XFB_HEIGHT * XFB_WIDTH * sizeof(u16))) {
		*fb = fill_yuv;
		dcache_flush(fb, 4);
		fb++;
	}
}

static void clear_fb_rgb(rgb fill_rgb) {
	u32 *fb = rgbFb;
	while ((void *)fb < ((void *)rgbFb) + (XFB_HEIGHT * XFB_WIDTH * sizeof(u32))) {
		*fb = fill_rgb.as_u32;
		dcache_flush(fb, 4);
		fb++;
	}
}

static struct videoInfo viVidInfo;

static void viFlush(void) {
	u32 *dest, *src;
	rgb rgb1, rgb2;

	src  = rgbFb;
	dest = (u32 *)xfb;
	while ((void *)src < ((void *)rgbFb) + (XFB_HEIGHT * XFB_WIDTH * sizeof(u32)) &&
	       (void *)dest < ((void *)xfb) + (XFB_HEIGHT * XFB_WIDTH * sizeof(u16))) {
		rgb1 = (rgb)*src;
		src++;
		rgb2 = (rgb)*src;
		src++;

		*dest = make_yuv(rgb1, rgb2);
		dcache_flush(dest, 4);
		dest++;
	}
}

static struct videoInfo viVidInfo = {
#if 0
	.fb = rgbFb + (XFB_WIDTH * XFB_OS_COMP_PIX * 2),
	.width = XFB_WIDTH,
	.height = XFB_HEIGHT - (XFB_OS_COMP_PIX * 2),
#endif
	.fb = NULL,
	.width = XFB_WIDTH,
	.height = XFB_HEIGHT,
	.flush = viFlush,
	.driver = &viDrv
};

static void viDrvInit(void) {
	rgb black = {.as_u32 = 0xff000000};
	//rgb gray = {.as_u32 = 0xffaaaaaa};
	//rgb yellow = {.as_u32 = 0xffffff00};

	/* XFB must be in MEM1 */
	xfb = M_PoolAlloc(POOL_MEM1, sizeof(u16) * XFB_WIDTH * XFB_HEIGHT);

	/* rgbFB can go wherever */
	rgbFb = malloc(sizeof(u32) * XFB_WIDTH * XFB_HEIGHT);
	viVidInfo.fb = rgbFb;

	VIDEO_Init(0);
	VIDEO_SetFrameBuffer(xfb);
	if (H_ConsoleType == CONSOLE_TYPE_WII)
		VISetupEncoder();

	clear_fb(black);
	clear_fb_rgb(black);
	V_Register(&viVidInfo);

	viDrv.state = DRIVER_STATE_READY;
}

static void viDrvCleanup(void) {
	log_puts("TODO: VI Cleanup");
}

static REGISTER_DRIVER(viDrv) = {
	.name = "Flipper/Hollywood Video Interface",
	.state = DRIVER_STATE_NOT_READY,
	.mask = DRIVER_ALLOW_GAMECUBE | DRIVER_ALLOW_WII,
	.init = viDrvInit,
	.cleanup = viDrvCleanup,
	.type = DRIVER_TYPE_GFX,
};
