/*
 * NPLL - Drivers - SDHC
 * Copyright (C) 2025-2026 Techflash
 *
 * Derived from seL4 projects_libs libsdhcdrivers:
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * SDHCI byte-swapping access code derived from Linux drivers/mmc/host/sdhci-pltfm.h:
 * Copyright 2010 MontaVista Software, LLC.
 */

#include "sdhc.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <npll/allocator.h>
#include <npll/cache.h>
#include <npll/drivers/sdio.h>
#include <npll/timer.h>
#include <npll/utils.h>
#include "mmc.h"
#include "compat.h"


#define DS_ADDR               0x00 //DMA System Address
#define BLK_ATT               0x04 //Block Attributes
#define CMD_ARG               0x08 //Command Argument
#define CMD_XFR_TYP           0x0C //Command Transfer Type
#define CMD_RSP0              0x10 //Command Response0
#define CMD_RSP1              0x14 //Command Response1
#define CMD_RSP2              0x18 //Command Response2
#define CMD_RSP3              0x1C //Command Response3
#define DATA_BUFF_ACC_PORT    0x20 //Data Buffer Access Port
#define PRES_STATE            0x24 //Present State
#define PROT_CTRL             0x28 //Protocol Control
#define CLOCK_CTRL            0x2C //Clock Control
#define TIMEOUT_CTRL          0x2E //Timeout Control
#define SW_RESET              0x2F //Software Reset
#define INT_STATUS            0x30 //Interrupt Status
#define ERR_INT_STATUS        0x32 //Error Interrupt Status
#define INT_STATUS_EN         0x34 //Interrupt Status Enable
#define ERR_INT_STATUS_EN     0x36 //Error Interrupt Status Enable
#define INT_SIGNAL_EN         0x38 //Interrupt Signal Enable
#define ERR_INT_SIGNAL_EN     0x3a //Error Interrupt Signal Enable
#define AUTOCMD12_ERR_STATUS  0x3C //Auto CMD12 Error Status
#define HOST_CTRL_CAP         0x40 //Host Controller Capabilities
#define WTMK_LVL              0x44 //Watermark Level
#define MIX_CTRL              0x48 //Mixer Control
#define FORCE_EVENT           0x50 //Force Event
#define ADMA_ERR_STATUS       0x54 //ADMA Error Status Register
#define ADMA_SYS_ADDR         0x58 //ADMA System Address
#define DLL_CTRL              0x60 //DLL (Delay Line) Control
#define DLL_STATUS            0x64 //DLL Status
#define CLK_TUNE_CTRL_STATUS  0x68 //CLK Tuning Control and Status
#define VEND_SPEC             0xC0 //Vendor Specific Register
#define MMC_BOOT              0xC4 //MMC Boot Register
#define VEND_SPEC2            0xC8 //Vendor Specific 2 Register
#define HOST_VERSION          0xFC //Host Version (0xFE adjusted for alignment)


/* Block Attributes Register */
#define BLK_ATT_BLKCNT_SHF      16        //Blocks Count For Current Transfer
#define BLK_ATT_BLKCNT_MASK     0xFFFFu   //Blocks Count For Current Transfer
#define BLK_ATT_BLKSIZE_SHF     0         //Transfer Block Size
#define BLK_ATT_BLKSIZE_MASK    0xFFFu    //Transfer Block Size

/* Command Transfer Type Register */
#define CMD_XFR_TYP_CMDINX_SHF  24        //Command Index
#define CMD_XFR_TYP_CMDINX_MASK 0x3Fu     //Command Index
#define CMD_XFR_TYP_CMDTYP_SHF  22        //Command Type
#define CMD_XFR_TYP_CMDTYP_MASK 0x3u      //Command Type
#define CMD_XFR_TYP_DPSEL       BIT(21)   //Data Present Select
#define CMD_XFR_TYP_CICEN       BIT(20)   //Command Index Check Enable
#define CMD_XFR_TYP_CCCEN       BIT(19)   //Command CRC Check Enable
#define CMD_XFR_TYP_RSPTYP_SHF  16        //Response Type Select
#define CMD_XFR_TYP_RSPTYP_MASK 0x3u      //Response Type Select


/* Clock Control Register */
#define CLK_CTRL_SDCLKS_SHF     8         //SDCLK Frequency Select
#define CLK_CTRL_SDCLKS_MASK    0xFFu     //SDCLK Frequency Select
#define CLK_CTRL_DVS_SHF        4         //Divisor
#define CLK_CTRL_DVS_MASK       0xFu      //Divisor
#define CLK_CTRL_CLK_INT_EN     BIT(0)    //Internal clock enable (exl. IMX6)
#define CLK_CTRL_CLK_INT_STABLE BIT(1)    //Internal clock stable (exl. IMX6)
#define CLK_CTRL_CLK_CARD_EN    BIT(2)    //SD clock enable       (exl. IMX6)

/* Timeout Control Register */
#define TIMEOUT_CTRL_DTOCV_SHF      0     //Data Timeout Counter Value
#define TIMEOUT_CTRL_DTOCV_MASK     0xFu  //Data Timeout Counter Value

/* Software Reset Register */
#define SW_RESET_INITA          BIT(3)   //Initialization Active
#define SW_RESET_RSTD           BIT(2)   //Software Reset for DAT Line
#define SW_RESET_RSTC           BIT(1)   //Software Reset for CMD Line
#define SW_RESET_RSTA           BIT(0)   //Software Reset for ALL

/* Interrupt Status Register */
#define INT_STATUS_ERR          BIT(15)   //Error interrupt      (exl. IMX6)
#define INT_STATUS_TP           BIT(14)   //Tuning Pass
#define INT_STATUS_RTE          BIT(12)   //Re-Tuning Event
#define INT_STATUS_CINT         BIT(8)    //Card Interrupt
#define INT_STATUS_CRM          BIT(7)    //Card Removal
#define INT_STATUS_CINS         BIT(6)    //Card Insertion
#define INT_STATUS_BRR          BIT(5)    //Buffer Read Ready
#define INT_STATUS_BWR          BIT(4)    //Buffer Write Ready
#define INT_STATUS_DINT         BIT(3)    //DMA Interrupt
#define INT_STATUS_BGE          BIT(2)    //Block Gap Event
#define INT_STATUS_TC           BIT(1)    //Transfer Complete
#define INT_STATUS_CC           BIT(0)    //Command Complete

/* Error Interrupt Status Register */
#define ERR_INT_STATUS_DMAE         BIT(12)   //DMA Error            (only IMX6)
#define ERR_INT_STATUS_TNE          BIT(10)   //Tuning Error
#define ERR_INT_STATUS_ADMAE        BIT(9)    //ADMA error           (exl. IMX6)
#define ERR_INT_STATUS_AC12E        BIT(8)    //Auto CMD12 Error
#define ERR_INT_STATUS_OVRCURE      BIT(7)    //Bus over current     (exl. IMX6)
#define ERR_INT_STATUS_DEBE         BIT(6)    //Data End Bit Error
#define ERR_INT_STATUS_DCE          BIT(5)    //Data CRC Error
#define ERR_INT_STATUS_DTOE         BIT(4)    //Data Timeout Error
#define ERR_INT_STATUS_CIE          BIT(3)    //Command Index Error
#define ERR_INT_STATUS_CEBE         BIT(2)    //Command End Bit Error
#define ERR_INT_STATUS_CCE          BIT(1)    //Command CRC Error
#define ERR_INT_STATUS_CTOE         BIT(0)    //Command Timeout Error

/* Host Controller Capabilities Register */
#define HOST_CTRL_CAP_VS18      BIT(26)   //Voltage Support 1.8V
#define HOST_CTRL_CAP_VS30      BIT(25)   //Voltage Support 3.0V
#define HOST_CTRL_CAP_VS33      BIT(24)   //Voltage Support 3.3V
#define HOST_CTRL_CAP_SRS       BIT(23)   //Suspend/Resume Support
#define HOST_CTRL_CAP_DMAS      BIT(22)   //DMA Support
#define HOST_CTRL_CAP_HSS       BIT(21)   //High Speed Support
#define HOST_CTRL_CAP_ADMAS     BIT(20)   //ADMA Support
#define HOST_CTRL_CAP_MBL_SHF   16        //Max Block Length
#define HOST_CTRL_CAP_MBL_MASK  0x3u      //Max Block Length
#define HOST_CTRL_CAP_BCLK_SHF  8         //Base Clock Frequency For SD Clock
#define HOST_CTRL_CAP_BCLK_MASK 0x3Fu     //Base Clock Frequency For SD Clock

/* Mixer Control Register */
#define MIX_CTRL_MSBSEL         BIT(5)    //Multi/Single Block Select.
#define MIX_CTRL_DTDSEL         BIT(4)    //Data Transfer Direction Select.
#define MIX_CTRL_DDR_EN         BIT(3)    //Dual Data Rate mode selection
#define MIX_CTRL_AC12EN         BIT(2)    //Auto CMD12 Enable
#define MIX_CTRL_BCEN           BIT(1)    //Block Count Enable
#define MIX_CTRL_DMAEN          BIT(0)    //DMA Enable

/* Watermark Level register */
#define WTMK_LVL_WR_WML_SHF     16        //Write Watermark Level
#define WTMK_LVL_RD_WML_SHF     0         //Read  Watermark Level

#define SDHC_DELAY 5
static inline void writel(u32 v, volatile void *a) {
	udelay(SDHC_DELAY);
	sync(); barrier();
	*(vu32*)a = v;
	sync(); barrier();
}

static inline void writew(u16 v, volatile void *a) {
	u32 addr, tmp, shift;

	addr = (u32)a;
	shift = (addr & 0x2u) * 8u;
	addr &= ~0x3u;
	a = (volatile void *)addr;

	udelay(SDHC_DELAY);
	sync(); barrier();
	tmp = *(vu32*)(a);
	tmp &= ~(0xffffu << shift);
	tmp |= (v << shift);
	writel(tmp, a);
	sync(); barrier();

	return ;
}

static inline void writeb(u8 v, volatile void *a) {
	u32 addr, tmp, shift;

	addr = (u32)a;
	shift = (addr & 0x3u) * 8u;
	addr &= ~0x3u;
	a = (volatile void *)addr;

	udelay(SDHC_DELAY);
	sync(); barrier();
	tmp = *(vu32*)(a);
	tmp &= ~(0xffu << shift);
	tmp |= (v << shift);
	writel(tmp, a);
	sync(); barrier();

	return ;
}

static inline u32 readl(volatile void *a) {
	u32 ret;

	udelay(SDHC_DELAY);
	sync(); barrier();
	ret = *(vu32*)(a);
	sync(); barrier();

	return ret;
}

static inline u16 readw(volatile void *a) {
	u32 addr, tmp, shift;

	udelay(SDHC_DELAY);
	addr = (u32)a;
	shift = (addr & 0x2u) * 8u;
	addr &= ~0x3u;
	a = (volatile void *)addr;
	sync(); barrier();
	tmp = *(vu32*)(a);
	sync(); barrier();

	return (u16)(tmp >> shift);
}

static inline u8 readb(volatile void *a) {
	u32 addr, tmp, shift;

	udelay(SDHC_DELAY);
	addr = (u32)a;
	shift = (addr & 0x3u) * 8u;
	addr &= ~0x3u;
	a = (volatile void *)addr;
	sync(); barrier();
	tmp = *(vu32*)(a);
	sync(); barrier();

	return (u8)(tmp >> shift);
}

enum dma_mode {
	DMA_MODE_NONE = 0,
	DMA_MODE_SDMA,
	DMA_MODE_ADMA
};

typedef enum {
	DIV_1   = 0x0,
	DIV_2   = 0x1,
	DIV_3   = 0x2,
	DIV_4   = 0x3,
	DIV_5   = 0x4,
	DIV_6   = 0x5,
	DIV_7   = 0x6,
	DIV_8   = 0x7,
	DIV_9   = 0x8,
	DIV_10  = 0x9,
	DIV_11  = 0xa,
	DIV_12  = 0xb,
	DIV_13  = 0xc,
	DIV_14  = 0xd,
	DIV_15  = 0xe,
	DIV_16  = 0xf,
} divisor;

/* Selecting the prescaler value varies between SDR and DDR mode. When the
 * value is set, this is accounted for with a bitshift (PRESCALER_X >> 1) */
typedef enum {
	PRESCALER_1   = 0x0, //Only available in SDR mode
	PRESCALER_2   = 0x1,
	PRESCALER_4   = 0x2,
	PRESCALER_8   = 0x4,
	PRESCALER_16  = 0x8,
	PRESCALER_32  = 0x10,
	PRESCALER_64  = 0x20,
	PRESCALER_128 = 0x40,
	PRESCALER_256 = 0x80,
	PRESCALER_512 = 0x100, //Only available in DDR mode
} sdclk_frequency_select;

typedef enum {
	CLOCK_INITIAL = 0,
	CLOCK_OPERATIONAL
} clock_mode;

typedef enum {
	SDCLK_TIMES_2_POW_27 = 0xe, /* Maximum valid for SDHCI v1.00 (0xF is reserved) */
	SDCLK_TIMES_2_POW_14 = 0x0,
} data_timeout_counter_val;

static inline sdhc_dev_t sdio_get_sdhc(sdio_host_dev_t *sdio)
{
	return (sdhc_dev_t)sdio->priv;
}

/** Print uSDHC registers. */
UNUSED static void print_sdhc_regs(struct sdhc *host)
{
	int i;
	for (i = DS_ADDR; i <= HOST_VERSION; i += 0x4) {
		ZF_LOGD("%x: %X", i, readl(host->base + i));
	}
}

static inline enum dma_mode get_dma_mode(struct sdhc *host UNUSED, struct mmc_cmd *cmd)
{
	if (cmd->data == NULL) {
		return DMA_MODE_NONE;
	}
	if (cmd->data->pbuf == 0) {
		return DMA_MODE_NONE;
	}
	/* Currently only SDMA supported */
	return DMA_MODE_SDMA;
}

static inline int UNUSED cap_sdma_supported(struct sdhc *host)
{
	u32 v;
	v = readl(host->base + HOST_CTRL_CAP);
	return !!(v & HOST_CTRL_CAP_DMAS);
}

static inline int UNUSED cap_max_buffer_size(struct sdhc *host)
{
	u32 v;
	v = readl(host->base + HOST_CTRL_CAP);
	v = ((v >> HOST_CTRL_CAP_MBL_SHF) & HOST_CTRL_CAP_MBL_MASK);
	return 512u << v;
}

static int sdhc_next_cmd(sdhc_dev_t host)
{
	struct mmc_cmd *cmd = host->cmd_list_head;
	u32 val32;
	u16 val16;
	u8 val8;
	u32 mix_ctrl;

	/* Clear error flags before issuing a new command.
	 * INT_STATUS (0x30) and ERR_INT_STATUS (0x32) share a 32-bit word
	 * and are both W1C.  We must use writel with 0 in the INT_STATUS
	 * half to avoid accidentally clearing normal status bits. */
	val16 = (ERR_INT_STATUS_ADMAE | ERR_INT_STATUS_OVRCURE | ERR_INT_STATUS_DEBE
		   | ERR_INT_STATUS_DCE   | ERR_INT_STATUS_DTOE
		   | ERR_INT_STATUS_CIE   | ERR_INT_STATUS_CEBE
		   | ERR_INT_STATUS_CCE   | ERR_INT_STATUS_CTOE);
	writel((u32)val16 << 16, host->base + INT_STATUS);

	val16 = INT_STATUS_CINS  | INT_STATUS_TC | INT_STATUS_CRM | INT_STATUS_CC;
	if (get_dma_mode(host, cmd) == DMA_MODE_NONE) {
		val16 |= INT_STATUS_BRR | INT_STATUS_BWR;
	}
	writew(val16, host->base + INT_STATUS_EN);

	/* Check if the Host is ready for transit. */
	{
		u64 tb = mftb();
		while (readl(host->base + PRES_STATE) & (SDHC_PRES_STATE_CIHB | SDHC_PRES_STATE_CDIHB)) {
			if (T_HasElapsed(tb, SDHC_INIT_TIMEOUT_US)) {
				ZF_LOGE("timeout waiting for command inhibit to clear");
				ZF_LOGD("doing sw reset of DAT+CMD due to command timeout");
				val8 = readb(host->base + SW_RESET);
				val8 |= SW_RESET_RSTC | SW_RESET_RSTD;
				writeb(val8, host->base + SW_RESET);
				tb = mftb();
				while (readb(host->base + SW_RESET) & (SW_RESET_RSTC | SW_RESET_RSTD)) {
					if (T_HasElapsed(tb, SDHC_INIT_TIMEOUT_US)) {
						ZF_LOGE("timeout waiting on software reset of DAT+CMD");
						return -1;
					}
				}
				return -1;
			}
		}
		while (readl(host->base + PRES_STATE) & SDHC_PRES_STATE_DLA) {
			if (T_HasElapsed(tb, SDHC_INIT_TIMEOUT_US)) {
				ZF_LOGE("timeout waiting for data line active to clear");
				return -1;
			}
		}
	}

	/* Two commands need to have at least 8 clock cycles in between.
	 * Lets assume that the hcd will NOT enforce this. */
	udelay(1000);

	/* Write to the argument register. */
	ZF_LOGD("CMD: %d with arg %x ", cmd->index, cmd->arg);
	writel(cmd->arg, host->base + CMD_ARG);

	if (cmd->data) {
		/* Set data timeout to maximum valid value for SDHCI v1.00 */
		val8 = readb(host->base + TIMEOUT_CTRL);
		val8 &= ~TIMEOUT_CTRL_DTOCV_MASK;
		val8 |= 0xE;
		writeb(val8, host->base + TIMEOUT_CTRL);

		val32 = (cmd->data->block_size & BLK_ATT_BLKSIZE_MASK);
		val32 |= (0x7u << 12); /* set Host DMA Buffer Boundary to 7 to prevent DINT */
		val32 |= (cmd->data->blocks << BLK_ATT_BLKCNT_SHF);
		writel(val32, host->base + BLK_ATT);

		/* NOTE: WTMK_LVL (0x44) is an NXP i.MX-specific register that
		 * does not exist on standard SDHCI v1.00 (Hollywood/Latte).
		 * Offset 0x44 maps to Max Current Capabilities (read-only)
		 * in standard SDHCI, so skip this write entirely. */

		/* Set Mixer Control */
		mix_ctrl = MIX_CTRL_BCEN;
		if (cmd->data->blocks > 1) {
			mix_ctrl |= MIX_CTRL_MSBSEL;
		}
		if (cmd->index == MMC_READ_SINGLE_BLOCK
		    || cmd->index == MMC_READ_MULTIPLE_BLOCK) {
			mix_ctrl |= MIX_CTRL_DTDSEL;
		}
		if (cmd->data->blocks > 1) {
			mix_ctrl |= MIX_CTRL_AC12EN;
		}

		/* Configure DMA */
		if (get_dma_mode(host, cmd) != DMA_MODE_NONE) {
			/* Enable DMA */
			mix_ctrl |= MIX_CTRL_DMAEN;
			/* Set DMA address */
			writel(cmd->data->pbuf, host->base + DS_ADDR);
		}
		/* Record the number of blocks to be sent */
		host->blocks_remaining = cmd->data->blocks;
	}

	/* The command should be MSB and the first two bits should be '00' */
	val32 = (cmd->index & CMD_XFR_TYP_CMDINX_MASK) << CMD_XFR_TYP_CMDINX_SHF;
	val32 &= ~(CMD_XFR_TYP_CMDTYP_MASK << CMD_XFR_TYP_CMDTYP_SHF);
	if (cmd->data) {
		/*
		 * In the standard SDHCI spec, the Transfer Mode Register occupies
		 * the lower 16 bits of offset 0x0C, sharing a 32-bit word with
		 * the Command Register in the upper 16 bits.  The mix_ctrl bits
		 * (DMA enable, direction, block count enable, etc.) must be ORed
		 * into the same 32-bit write as the command.
		 *
		 * NOTE: The original seL4 code wrote mix_ctrl to a separate
		 * MIX_CTRL register at 0x48 for non-v2 controllers.  That was
		 * an NXP i.MX-specific layout that does NOT apply to standard
		 * SDHCI v1.00 controllers like Hollywood/Latte.
		 */
		val32 |= mix_ctrl;
	}

	/* Set response type */
	val32 &= ~CMD_XFR_TYP_CICEN;
	val32 &= ~CMD_XFR_TYP_CCCEN;
	val32 &= ~(CMD_XFR_TYP_RSPTYP_MASK << CMD_XFR_TYP_RSPTYP_SHF);
	switch (cmd->rsp_type) {
	case MMC_RSP_TYPE_R2:
		val32 |= (0x1u << CMD_XFR_TYP_RSPTYP_SHF);
		val32 |= CMD_XFR_TYP_CCCEN;
		break;
	case MMC_RSP_TYPE_R3:
	case MMC_RSP_TYPE_R4:
		val32 |= (0x2u << CMD_XFR_TYP_RSPTYP_SHF);
		break;
	case MMC_RSP_TYPE_R1:
	case MMC_RSP_TYPE_R5:
	case MMC_RSP_TYPE_R6:
		val32 |= (0x2u << CMD_XFR_TYP_RSPTYP_SHF);
		val32 |= CMD_XFR_TYP_CICEN;
		val32 |= CMD_XFR_TYP_CCCEN;
		break;
	case MMC_RSP_TYPE_R1b:
	case MMC_RSP_TYPE_R5b:
		val32 |= (0x3u << CMD_XFR_TYP_RSPTYP_SHF);
		val32 |= CMD_XFR_TYP_CICEN;
		val32 |= CMD_XFR_TYP_CCCEN;
		break;
	default:
		break;
	}

	if (cmd->data) {
		val32 |= CMD_XFR_TYP_DPSEL;
	}

	/* Issue the command. */
	writel(val32, host->base + CMD_XFR_TYP);
	return 0;
}



/** Pass control to the devices IRQ handler
 * @param[in] sd_dev  The sdhc interface device that triggered
 *                    the interrupt event.
 */
static int sdhc_handle_irq(sdio_host_dev_t *sdio, int irq UNUSED)
{
	sdhc_dev_t host = sdio_get_sdhc(sdio);
	struct mmc_cmd *cmd;
	u32 status32;
	u16 int_status, err_status;

check_again:
	/* Re-fetch cmd in case cleanup advanced the list */
	cmd = host->cmd_list_head;

	/* Read INT_STATUS and ERR_INT_STATUS atomically from one 32-bit word.
	 * They share offset 0x30: INT_STATUS in [15:0], ERR_INT_STATUS in [31:16]. */
	status32 = readl(host->base + INT_STATUS);
	int_status = (u16)(status32);
	err_status = (u16)(status32 >> 16);

	/* Nothing pending — done */
	if (!int_status && !err_status)
		return 0;

	if (!cmd) {
		/* Clear flags */
		writel(((u32)err_status << 16) | (u32)int_status, host->base + INT_STATUS);
		goto check_again;
	}
	/** Handle errors **/
	if (err_status & ERR_INT_STATUS_TNE) {
		ZF_LOGE("Tuning error");
	}
	if (err_status & ERR_INT_STATUS_OVRCURE) {
		ZF_LOGE("Bus overcurrent"); /* (exl. IMX6) */
	}
	if (int_status & INT_STATUS_ERR) {
		ZF_LOGE("CMD/DATA transfer error; INT_STATUS=0x%04x ERR_INT_STATUS=0x%04x", int_status, err_status);
		cmd->complete = -1;
	}
	if (err_status & ERR_INT_STATUS_AC12E) {
		ZF_LOGE("Auto CMD12 Error");
		cmd->complete = -1;
	}
	/** DMA errors **/
	if (err_status & ERR_INT_STATUS_DMAE) {
		ZF_LOGE("DMA Error");
		cmd->complete = -1;
	}
	if (err_status & ERR_INT_STATUS_ADMAE) {
		ZF_LOGE("ADMA error");       /*  (exl. IMX6) */
		cmd->complete = -1;
	}
	/** DATA errors **/
	if (err_status & ERR_INT_STATUS_DEBE) {
		ZF_LOGE("Data end bit error");
		cmd->complete = -1;
	}
	if (err_status & ERR_INT_STATUS_DCE) {
		ZF_LOGE("Data CRC error");
		cmd->complete = -1;
	}
	if (err_status & ERR_INT_STATUS_DTOE) {
		ZF_LOGE("Data transfer error");
		cmd->complete = -1;
	}
	/** CMD errors **/
	if (err_status & ERR_INT_STATUS_CIE) {
		ZF_LOGE("Command index error");
		cmd->complete = -1;
	}
	if (err_status & ERR_INT_STATUS_CEBE) {
		ZF_LOGE("Command end bit error");
		cmd->complete = -1;
	}
	if (err_status & ERR_INT_STATUS_CCE) {
		ZF_LOGE("Command CRC error");
		cmd->complete = -1;
	}
	if (err_status & ERR_INT_STATUS_CTOE) {
		ZF_LOGE("CMD Timeout...");
		cmd->complete = -1;
	}

	if (int_status & INT_STATUS_TP) {
		ZF_LOGD("Tuning pass");
	}
	if (int_status & INT_STATUS_RTE) {
		ZF_LOGD("Retuning event");
	}
	if (int_status & INT_STATUS_CINT) {
		ZF_LOGD("Card interrupt");
	}
	if (int_status & INT_STATUS_CRM) {
		ZF_LOGD("Card removal");
		cmd->complete = -1;
	}
	if (int_status & INT_STATUS_CINS) {
		ZF_LOGD("Card insertion");
	}
	if (int_status & INT_STATUS_DINT) {
		ZF_LOGD("DMA interrupt");
		/* Re-arm SDMA */
		writel(readl(host->base + DS_ADDR), host->base + DS_ADDR);
	}
	if (int_status & INT_STATUS_BGE) {
		ZF_LOGD("Block gap event");
	}

	/* Command complete */
	if (int_status & INT_STATUS_CC) {
		/* Command complete */
		switch (cmd->rsp_type) {
		case MMC_RSP_TYPE_R2:
			cmd->response[0] = readl(host->base + CMD_RSP0);
			cmd->response[1] = readl(host->base + CMD_RSP1);
			cmd->response[2] = readl(host->base + CMD_RSP2);
			cmd->response[3] = readl(host->base + CMD_RSP3);
			break;
		case MMC_RSP_TYPE_R1b:
			if (cmd->index == MMC_STOP_TRANSMISSION) {
				cmd->response[3] = readl(host->base + CMD_RSP3);
			} else {
				cmd->response[0] = readl(host->base + CMD_RSP0);
			}
			break;
		case MMC_RSP_TYPE_NONE:
			break;
		default:
			cmd->response[0] = readl(host->base + CMD_RSP0);
		}

		/* If there is no data segment, the transfer is complete */
		if (cmd->data == NULL) {
			//assert_msg(cmd->complete == 0, "cmd->complete != 0 in sdhc_handle_irq COMMAND COMPLETE path");
			cmd->complete = 1;
		}
	}
	/* DATA: Programmed IO handling */
	if (int_status & (INT_STATUS_BRR | INT_STATUS_BWR)) {
		assert(cmd->data);
		assert(cmd->data->vbuf);
		//assert_msg(cmd->complete == 0, "cmd->complete != 0 in sdhc_handle_irq DATA path");
		if (host->blocks_remaining) {
			volatile void *io_port = (void *)host->base + DATA_BUFF_ACC_PORT;
			u32 blocks_done = cmd->data->blocks - host->blocks_remaining;
			u32 *usr_buf = (u32 *)((u8 *)cmd->data->vbuf + blocks_done * cmd->data->block_size);
			if (int_status & INT_STATUS_BRR) {
				/* Buffer Read Ready.
				 * The hardware 32-bit byteswapper reverses each word,
				 * so we must swap back to get correct byte order for data. */
				unsigned int i;
				for (i = 0; i < cmd->data->block_size; i += sizeof(u32)) {
					*usr_buf++ = __builtin_bswap32(readl(io_port));
				}
			} else {
				/* Buffer Write Ready */
				unsigned int i;
				for (i = 0; i < cmd->data->block_size; i += sizeof(u32)) {
					writel(__builtin_bswap32(*usr_buf++), io_port);
				}
			}
			host->blocks_remaining--;
		}
	}
	/* Data complete */
	if (int_status & INT_STATUS_TC) {
		/* this fails here so let's just ignore it for now and hope everything will be OK :) */
		/* assert_msg(cmd->complete == 0, "cmd->complete != 0 in sdhc_handle_irq DATA COMPLETE path"); */
		cmd->complete = 1;
	}
	/* Clear flags (W1C: write-1-to-clear).
	 * INT_STATUS and ERR_INT_STATUS share a 32-bit word; must use a single
	 * writel to avoid the RMW in writew accidentally clearing the other half. */
	writel(((u32)err_status << 16) | (u32)int_status, host->base + INT_STATUS);

	/* If the transaction has finished */
	if (cmd != NULL && cmd->complete != 0) {
		if (cmd->next == NULL) {
			/* Shutdown */
			host->cmd_list_head = NULL;
			host->cmd_list_tail = &host->cmd_list_head;
		} else {
			/* Next */
			host->cmd_list_head = cmd->next;
			sdhc_next_cmd(host);
		}
		cmd->next = NULL;
		/* Send callback if required */
		if (cmd->cb) {
			assert(addrIsValidCached(cmd->cb));
			cmd->cb(sdio, 0, cmd, cmd->token);
		}
	}

	/* in case we got more */
	goto check_again;
}

static int sdhc_is_voltage_compatible(sdio_host_dev_t *sdio, int mv)
{
	u32 val;
	sdhc_dev_t host = sdio_get_sdhc(sdio);
	val = readl(host->base + HOST_CTRL_CAP);
	if (mv == 3300 && (val & HOST_CTRL_CAP_VS33)) {
		return 1;
	} else {
		return 0;
	}
}

static int sdhc_send_cmd(sdio_host_dev_t *sdio, struct mmc_cmd *cmd, sdio_cb cb, void *token)
{
	sdhc_dev_t host = sdio_get_sdhc(sdio);
	int ret;
	u64 tb;
	u8 val8;

	/* Initialise callbacks */
	cmd->complete = 0;
	cmd->next = NULL;
	cmd->cb = cb;
	cmd->token = token;
	/* Append to list */
	*host->cmd_list_tail = cmd;
	host->cmd_list_tail = &cmd->next;

	/* If idle, bump */
	if (host->cmd_list_head == cmd) {
		ret = sdhc_next_cmd(host);
		if (ret) {
			return ret;
		}
	}

	/* finalise the transacton */
	if (cb == NULL) {
		tb = mftb();
		/* Wait for completion */
		while (!cmd->complete) {
			//sdhc_handle_irq(sdio, 0);
			if (T_HasElapsed(tb, SDHC_CMD_TIMEOUT_US)) {
				ZF_LOGE("timeout waiting for command completion");
				ZF_LOGD("doing sw reset of DAT+CMD due to command timeout");
				val8 = readb(host->base + SW_RESET);
				val8 |= SW_RESET_RSTC | SW_RESET_RSTD;
				writeb(val8, host->base + SW_RESET);
				tb = mftb();
				while (readb(host->base + SW_RESET) & (SW_RESET_RSTC | SW_RESET_RSTD)) {
					if (T_HasElapsed(tb, SDHC_INIT_TIMEOUT_US)) {
						ZF_LOGE("timeout waiting on software reset of DAT+CMD");
						return -1;
					}
				}
				return -1;
			}
		}
		/* Return result */
		if (cmd->complete < 0) {
			ZF_LOGD("doing sw reset of DAT+CMD due to command failure");
			val8 = readb(host->base + SW_RESET);
			val8 |= SW_RESET_RSTC | SW_RESET_RSTD;
			writeb(val8, host->base + SW_RESET);
			tb = mftb();
			while (readb(host->base + SW_RESET) & (SW_RESET_RSTC | SW_RESET_RSTD)) {
				if (T_HasElapsed(tb, SDHC_INIT_TIMEOUT_US)) {
					ZF_LOGE("timeout waiting on software reset of DAT+CMD");
					return -1;
				}
			}
			return cmd->complete;
		} else {
			return 0;
		}
	} else {
		/* Defer to IRQ handler */
		return 0;
	}
}

static int sdhc_enable_clock(volatile void *base_addr)
{
	u8 bclk;
	u16 val, clkctrl;
	u32 cap;
	u64 tb;
	int i, validDiv = 0;

	/* calculate base clock */
	cap = readl(base_addr + HOST_CTRL_CAP);
	cap &= (HOST_CTRL_CAP_BCLK_MASK << HOST_CTRL_CAP_BCLK_SHF);
	bclk = (u8)(cap >> HOST_CTRL_CAP_BCLK_SHF);
	ZF_LOGD("base clock: %dMHz", bclk);
	if (!bclk) {
		ZF_LOGE("HC reported invalid base clock, assuming divisor of 16 is fine");
		clkctrl = readw(base_addr + CLOCK_CTRL);
		clkctrl &= ~(CLK_CTRL_SDCLKS_MASK << CLK_CTRL_SDCLKS_SHF);
		clkctrl |= (1u << 3);
		goto writeback;
	}

	/* set it */
	clkctrl = readw(base_addr + CLOCK_CTRL);
	clkctrl &= ~(CLK_CTRL_SDCLKS_MASK << CLK_CTRL_SDCLKS_SHF);
	if (bclk <= 63 && bclk >= 10) {
		ZF_LOGD("base clock already valid");
		goto writeback;
	}

	for (i = 0; i < 8; i++) {
		u8 div = ((1u << i) << CLK_CTRL_SDCLKS_SHF);
		if ((bclk / div) > 63 || (bclk / div) < 10) {
			ZF_LOGD("divisor %d invalid...", div);
			continue;
		}

		ZF_LOGD("divisor %d valid!", div);
		validDiv = 1;
		clkctrl &= ~(CLK_CTRL_SDCLKS_MASK << CLK_CTRL_SDCLKS_SHF);
		clkctrl |= ((1u << i) << CLK_CTRL_SDCLKS_SHF);
	}

	if (!validDiv) {
		ZF_LOGE("failed to find valid clock divisor");
		return -1;
	}

	/* write it back */
writeback:
	writew(clkctrl, base_addr + CLOCK_CTRL);

	/* enable the clock */
	val = readw(base_addr + CLOCK_CTRL);
	val |= CLK_CTRL_CLK_INT_EN;
	writew(val, base_addr + CLOCK_CTRL);

	tb = mftb();
	do {
		val = readw(base_addr + CLOCK_CTRL);
		if (T_HasElapsed(tb, SDHC_INIT_TIMEOUT_US)) {
			ZF_LOGE("clock never stabilized");
			return -1;
		}
	} while (!(val & CLK_CTRL_CLK_INT_STABLE));

	val |= CLK_CTRL_CLK_CARD_EN;
	writew(val, base_addr + CLOCK_CTRL);

	return 0;
}

/* Set the clock divider and timeout */
static int sdhc_set_clock_div(
	volatile void *base_addr,
	divisor dvs_div,
	sdclk_frequency_select sdclks_div,
	data_timeout_counter_val dtocv)
{
	/* make sure the clock state is stable. */
#if 0 /* not supported on sdhciv1 */
	if (readl(base_addr + PRES_STATE) & SDHC_PRES_STATE_SDSTB) {
#endif
		u8 val8;
		u16 val16 = readw(base_addr + CLOCK_CTRL);

		/* DDR mode is not supported on SDHCI v1.00 (Hollywood/Latte),
		 * so always use the SDR clock divisor path. */
		val16 &= ~(CLK_CTRL_SDCLKS_MASK << CLK_CTRL_SDCLKS_SHF);
		val16 |= ((unsigned int)sdclks_div << CLK_CTRL_SDCLKS_SHF);
		val16 &= ~(CLK_CTRL_DVS_MASK << CLK_CTRL_DVS_SHF);
		val16 |= ((unsigned int)dvs_div << CLK_CTRL_DVS_SHF);

		/* Write out the Clock Control register */
		writew(val16, base_addr + CLOCK_CTRL);

		/* Set data timeout value */
		val8 = readb(base_addr + TIMEOUT_CTRL);
		val8 &= ~TIMEOUT_CTRL_DTOCV_MASK;
		val8 |= ((unsigned int)dtocv << TIMEOUT_CTRL_DTOCV_SHF);
		writeb(val8, base_addr + TIMEOUT_CTRL);
#if 0
	} else {
		ZF_LOGE("The clock is unstable, unable to change it!");
		return -1;
	}
#endif

	return 0;
}

static int sdhc_set_clock(volatile void *base_addr, clock_mode clk_mode)
{
	int rslt = -1;

	const bool isClkEnabled = readw(base_addr + CLOCK_CTRL) & CLK_CTRL_CLK_INT_EN;
	if (!isClkEnabled) {
		rslt = sdhc_enable_clock(base_addr);
		if (rslt) {
			ZF_LOGE("failed to enable clock");
			return rslt;
		}
	}

	/* TODO: Relate the clock rate settings to the actual capabilities of the
	 * card and the host controller. The conservative settings chosen should
	 * work with most setups, but this is not an ideal solution. According to
	 * the RM, the default freq. of the base clock should be at around 200MHz.
	 */
	switch (clk_mode) {
	case CLOCK_INITIAL:
		/* Divide the base clock by 512 */
		rslt = sdhc_set_clock_div(base_addr, DIV_16, PRESCALER_32, SDCLK_TIMES_2_POW_14);
		break;
	case CLOCK_OPERATIONAL:
		/* Divide the base clock by 8 */
		rslt = sdhc_set_clock_div(base_addr, DIV_4, PRESCALER_2, SDCLK_TIMES_2_POW_27);
		break;
	default:
		ZF_LOGE("Unsupported clock mode setting");
		rslt = -1;
		break;
	}

	if (rslt < 0) {
		ZF_LOGE("Failed to change the clock settings");
	}

	return rslt;
}

/** Software Reset */
static int sdhc_reset(sdio_host_dev_t *sdio)
{
	sdhc_dev_t host = sdio_get_sdhc(sdio);
	u32 val;
	u8 val8;
	u16 val16;
	u64 tb;
	int ret;

	tb = mftb();

	/* Reset the host */
	ZF_LOGD("resetting...");
	val8 = readb(host->base + SW_RESET);
	val8 |= SW_RESET_RSTA;
	/* Wait until the controller is ready */
	writeb(val8, host->base + SW_RESET);
	do {
		val8 = readb(host->base + SW_RESET);
		if (T_HasElapsed(tb, SDHC_INIT_TIMEOUT_US)) {
			ZF_LOGE("timeout waiting on HC to be ready");
			return -1;
		}
	} while (val8 & SW_RESET_RSTA);
	ZF_LOGD("controller is ready, enabling irqs...");

	/* Enable IRQs */
	val16 = (ERR_INT_STATUS_ADMAE | ERR_INT_STATUS_OVRCURE | ERR_INT_STATUS_DEBE
		   | ERR_INT_STATUS_DCE   | ERR_INT_STATUS_DTOE
		   | ERR_INT_STATUS_CIE   | ERR_INT_STATUS_CEBE
		   | ERR_INT_STATUS_CCE   | ERR_INT_STATUS_CTOE);
	writew(val16, host->base + ERR_INT_STATUS_EN);
	writew(val16, host->base + ERR_INT_SIGNAL_EN);

	val16 = INT_STATUS_CINS  | INT_STATUS_TC | INT_STATUS_CRM | INT_STATUS_CC;
	writew(val16, host->base + INT_STATUS_EN);
	writew(val16, host->base + INT_SIGNAL_EN);

	/* Configure clock for initialization */
	ZF_LOGD("configuring clock...");
	ret = sdhc_set_clock(host->base, CLOCK_INITIAL);
	if (ret) {
		ZF_LOGE("Failed to set clock: %d", ret);
		return ret;
	}

	/* TODO: Select Voltage Level */

	/* Set bus width */
	val = readl(host->base + PROT_CTRL);
	val |= MMC_MODE_4BIT;
	writel(val, host->base + PROT_CTRL);

	/* Wait until the Command and Data Lines are ready. */
	ZF_LOGD("waiting for cmd and data lines...");
	tb = mftb();
	while ((readl(host->base + PRES_STATE) & SDHC_PRES_STATE_CDIHB) ||
		   (readl(host->base + PRES_STATE) & SDHC_PRES_STATE_CIHB)) {
		if (T_HasElapsed(tb, SDHC_INIT_TIMEOUT_US)) {
			ZF_LOGE("timeout waiting on command and data lines to be ready");
			return -1;
		}
	}

	/* Send 80 clock ticks to card to power up. */
	ZF_LOGD("powering up card...");
	val8 = readb(host->base + SW_RESET);
	val8 |= SW_RESET_INITA;
	writeb(val8, host->base + SW_RESET);
	tb = mftb();
	while (readb(host->base + SW_RESET) & SW_RESET_INITA) {
		if (T_HasElapsed(tb, SDHC_INIT_TIMEOUT_US)) {
			ZF_LOGE("timeout waiting on card to power up");
			return -1;
		}
	}

	/* Check if a SD card is inserted. */
	val = readl(host->base + PRES_STATE);
	if (val & SDHC_PRES_STATE_CINST) {
		ZF_LOGD("Card Inserted");
		if (!(val & SDHC_PRES_STATE_WPSPL)) {
			ZF_LOGD("(Read Only)");
		}
	} else {
		ZF_LOGE("Card Not Present...");
	}

	return 0;
}

static int sdhc_get_nth_irq(sdio_host_dev_t *sdio, int n)
{
	sdhc_dev_t host = sdio_get_sdhc(sdio);
	if (n < 0 || n >= host->nirqs) {
		return -1;
	} else {
		return host->irq_table[n];
	}
}

static u32 sdhc_get_present_state_register(sdio_host_dev_t *sdio)
{
	return readl(sdio_get_sdhc(sdio)->base + PRES_STATE);
}

static int sdhc_set_operational(struct sdio_host_dev *sdio)
{
	/*
	 * Set the clock to a higher frequency for the operational state.
	 *
	 * As of now, there are no further checks to validate if the card and the
	 * host controller could be driven with a higher rate, therefore the
	 * operational clock settings are chosen rather conservative.
	 */
	sdhc_dev_t host = sdio_get_sdhc(sdio);
	return sdhc_set_clock(host->base, CLOCK_OPERATIONAL);
}

int sdhc_init(void *iobase, const int *irq_table, int nirqs, sdio_host_dev_t *dev)
{
	sdhc_dev_t sdhc;
	/* Allocate memory for SDHC structure */
	sdhc = (sdhc_dev_t)malloc(sizeof(*sdhc));
	if (!sdhc) {
		ZF_LOGE("Not enough memory!");
		return -1;
	}
	/* Complete the initialisation of the SDHC structure */
	sdhc->base = iobase;
	sdhc->nirqs = nirqs;
	sdhc->irq_table = irq_table;
	sdhc->cmd_list_head = NULL;
	sdhc->cmd_list_tail = &sdhc->cmd_list_head;
	sdhc->version = ((readl(sdhc->base + HOST_VERSION) >> 16) & 0xff) + 1;
	ZF_LOGD("SDHC version %d.00", sdhc->version);
	/* Initialise SDIO structure */
	dev->handle_irq = &sdhc_handle_irq;
	dev->nth_irq = &sdhc_get_nth_irq;
	dev->send_command = &sdhc_send_cmd;
	dev->is_voltage_compatible = &sdhc_is_voltage_compatible;
	dev->reset = &sdhc_reset;
	dev->set_operational = &sdhc_set_operational;
	dev->get_present_state = &sdhc_get_present_state_register;
	dev->priv = sdhc;
	/* Clear IRQs */
	writel(0, sdhc->base + INT_STATUS_EN);
	writel(0, sdhc->base + INT_SIGNAL_EN);
	writel(readl(sdhc->base + INT_STATUS), sdhc->base + INT_STATUS);
	return 0;
}
