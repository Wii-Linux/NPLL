/*
 * NPLL - Drivers - Hollywood SDHC interface
 *
 * Copyright (C) 2025 Techflash
 *
 * Derived from mini's sdhc.c:
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 * Copyright (c) 2009 Sven Peter <svenpeter@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * SD Host Controller driver based on the SD Host Controller Standard
 * Simplified Specification Version 1.00 (www.sdcard.com).
 */

#define MODULE "sdhc"

#include <string.h>
#include <errno.h>
#include <npll/cache.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/utils.h>
#include <npll/drivers/hollywood_sdmmc.h>
#include <npll/drivers/hollywood_sdhc.h>

struct sdhc_host sc_host;

#define SDHC_COMMAND_TIMEOUT	500
#define SDHC_TRANSFER_TIMEOUT	5000

#define sdhc_wait_intr(a,b,c) sdhc_wait_intr_debug(__func__, __LINE__, a, b, c)

/* some stuff for compatibility with MINI's code */
#define read32(x) (*(vu32 *)(x))
#define write32(x, y) *(vu32 *)(x) = (y)
static inline u32 mask32(u32 addr, u32 clear, u32 set) {
	u32 data = read32(addr);
	data |= set;
	data &= ~clear;
	return data;
}

#define bzero(mem, size) memset(mem, 0, size)

#define ISSET(var, mask) (((var) & (mask)) ? 1 : 0)
#define SET(var, mask) ((var) |= (mask))
#define MIN(a, b) (((a)>(b))?(b):(a))

static inline u32 bus_space_read_4(bus_space_handle_t ioh, u32 reg) {
	return read32(ioh + reg);
}

static inline u16 bus_space_read_2(bus_space_handle_t ioh, u32 reg) {
	if(reg & 3)
		return (read32((ioh + reg) & ~3) & 0xffff0000) >> 16;
	else
		return (read32(ioh + reg) & 0xffff);
}

static inline u8 bus_space_read_1(bus_space_handle_t ioh, u32 reg) {
	u32 mask;
	u32 addr;
	u8 shift;

	shift = (reg & 3) * 8;
	mask = (0xFF << shift);
	addr = ioh + reg;

	return (read32(addr & ~3) & mask) >> shift;
}

static inline void bus_space_write_4(bus_space_handle_t ioh, u32 r, u32 v) {
	write32(ioh + r, v);
}

static inline void bus_space_write_2(bus_space_handle_t ioh, u32 r, u16 v) {
	if(r & 3)
		mask32((ioh + r) & ~3, 0xffff0000, v << 16);
	else
		mask32((ioh + r), 0xffff, ((u32)v));
}

static inline void bus_space_write_1(bus_space_handle_t ioh, u32 r, u8 v) {
	u32 mask;
	u32 addr;
	u8 shift;

	shift = (r & 3) * 8;
	mask = (0xFF << shift);
	addr = ioh + r;

	mask32(addr & ~3, mask, v << shift);
}

/* flag values */
#define SHF_USE_DMA		0x0001

#define HREAD1(hp, reg)							\
	(bus_space_read_1((hp)->ioh, (reg)))
#define HREAD2(hp, reg)							\
	(bus_space_read_2((hp)->ioh, (reg)))
#define HREAD4(hp, reg)							\
	(bus_space_read_4((hp)->ioh, (reg)))
#define HWRITE1(hp, reg, val)						\
	bus_space_write_1((hp)->ioh, (reg), (val))
#define HWRITE2(hp, reg, val)						\
	bus_space_write_2((hp)->ioh, (reg), (val))
#define HWRITE4(hp, reg, val)						\
	bus_space_write_4((hp)->ioh, (reg), (val))
#define HCLR1(hp, reg, bits)						\
	HWRITE1((hp), (reg), HREAD1((hp), (reg)) & ~(bits))
#define HCLR2(hp, reg, bits)						\
	HWRITE2((hp), (reg), HREAD2((hp), (reg)) & ~(bits))
#define HSET1(hp, reg, bits)						\
	HWRITE1((hp), (reg), HREAD1((hp), (reg)) | (bits))
#define HSET2(hp, reg, bits)						\
	HWRITE2((hp), (reg), HREAD2((hp), (reg)) | (bits))

static int	sdhc_start_command(struct sdhc_host *, struct sdmmc_command *);
static int	sdhc_wait_state(struct sdhc_host *, u32, u32);
static int	sdhc_soft_reset(struct sdhc_host *, int);
static void sdhc_reset_intr_status(struct sdhc_host *hp);
static int	sdhc_wait_intr_debug(const char *func, int line, struct sdhc_host *, int, int);
static void	sdhc_transfer_data(struct sdhc_host *, struct sdmmc_command *);
static void	sdhc_read_data(struct sdhc_host *, u8 *, int);
static void	sdhc_write_data(struct sdhc_host *, u8 *, int);
       void	sdhc_dump_regs(struct sdhc_host *hp);


/*
 * FIXME: These drivers must have some kind of race condition that only
 * manifests when running on Broadway since it's so much faster.  I've
 * found that ironically the DPRINTF()s are in the perfect spots to win
 * the race consistently, but that'd mean that we need to actually log them.
 * So, as a workaround, if we wouldn't log a message in this case, then we
 * delay for 1ms.
 */
//#define SDHC_DEBUG 1
#ifdef SDHC_DEBUG
int sdhcdebug = 0;
#define DPRINTF(n,s)	do { if ((n) <= sdhcdebug) log_printf s; else udelay(1 * 1000); } while (0)
#else
#define DPRINTF(n,s)	do {udelay(1 * 1000);} while(0)
#endif

/*
 * Called by attachment driver.  For each SD card slot there is one SD
 * host controller standard register set. (1.3)
 */
static int sdhc_host_found(bus_space_tag_t iot, bus_space_handle_t ioh, int usedma) {
	u32 caps;
	int error = 1;

#ifdef SDHC_DEBUG
	u16 version;

	version = bus_space_read_2(ioh, SDHC_HOST_CTL_VERSION);
	log_printf("SD Host Specification/Vendor Version ");

	switch(SDHC_SPEC_VERSION(version)) {
	case 0x00:
		log_printf("1.0/%u\r\n", SDHC_VENDOR_VERSION(version));
		break;
	default:
		log_printf(">1.0/%u\r\n", SDHC_VENDOR_VERSION(version));
		break;
	}
#endif

	memset(&sc_host, 0, sizeof(sc_host));

	/* Fill in the new host structure. */
	sc_host.iot = iot;
	sc_host.ioh = ioh;
	sc_host.data_command = 0;

	/*
	 * Reset the host controller and enable interrupts.
	 */
	(void)sdhc_host_reset(&sc_host);

	/* Determine host capabilities. */
	caps = HREAD4(&sc_host, SDHC_CAPABILITIES);

	/* Use DMA if the host system and the controller support it. */
	if (usedma && ISSET(caps, SDHC_DMA_SUPPORT))
		SET(sc_host.flags, SHF_USE_DMA);

	/*
	 * Determine the base clock frequency. (2.2.24)
	 */
	if (SDHC_BASE_FREQ_KHZ(caps) != 0)
		sc_host.clkbase = SDHC_BASE_FREQ_KHZ(caps);
	if (sc_host.clkbase == 0) {
		/* The attachment driver must tell us. */
		log_printf("base clock frequency unknown\r\n");
		goto err;
	} else if (sc_host.clkbase < 10000 || sc_host.clkbase > 63000) {
		/* SDHC 1.0 supports only 10-63 MHz. */
		log_printf("base clock frequency out of range: %u MHz\r\n",
		    sc_host.clkbase / 1000);
		goto err;
	}

	/*
	 * XXX Set the data timeout counter value according to
	 * capabilities. (2.2.15)
	 */

	SET(sc_host.ocr, MMC_OCR_3_2V_3_3V | MMC_OCR_3_3V_3_4V);

	/*
	 * Attach the generic SD/MMC bus driver.  (The bus driver must
	 * not invoke any chipset functions before it is attached.)
	 */
	sdmmc_attach(&sc_host);

	return 0;

err:
	return (error);
}

/*
 * Shutdown hook established by or called from attachment driver.
 */
void sdhc_shutdown(void) {
	/* XXX chip locks up if we don't disable it before reboot. */
	(void)sdhc_host_reset(&sc_host);
}

/*
 * Reset the host controller.  Called during initialization, when
 * cards are removed, upon resume, and during error recovery.
 */
int sdhc_host_reset(struct sdhc_host *hp) {
	u16 imask;
	int error;

	/* Disable all interrupts. */
	HWRITE2(hp, SDHC_NINTR_SIGNAL_EN, 0);

	/*
	 * Reset the entire host controller and wait up to 100ms for
	 * the controller to clear the reset bit.
	 */
	if ((error = sdhc_soft_reset(hp, SDHC_RESET_ALL)) != 0) {
		return (error);
	}

	/* Set data timeout counter value to max for now. */
	HWRITE1(hp, SDHC_TIMEOUT_CTL, SDHC_TIMEOUT_MAX);

	/* Enable interrupts. */
	imask =
	    SDHC_CARD_REMOVAL | SDHC_CARD_INSERTION |
	    SDHC_BUFFER_READ_READY | SDHC_BUFFER_WRITE_READY |
	    SDHC_DMA_INTERRUPT | SDHC_BLOCK_GAP_EVENT |
	    SDHC_TRANSFER_COMPLETE | SDHC_COMMAND_COMPLETE;

	HWRITE2(hp, SDHC_NINTR_STATUS_EN, imask);
	HWRITE2(hp, SDHC_EINTR_STATUS_EN, SDHC_EINTR_STATUS_MASK);
	HWRITE2(hp, SDHC_NINTR_SIGNAL_EN, imask);
	HWRITE2(hp, SDHC_EINTR_SIGNAL_EN, SDHC_EINTR_SIGNAL_MASK);

	return 0;
}

/*
 * Return non-zero if the card is currently inserted.
 */
int sdhc_card_detect(struct sdhc_host *hp) {
	return ISSET(HREAD4(hp, SDHC_PRESENT_STATE), SDHC_CARD_INSERTED) ?
	    1 : 0;
}

/*
 * Set or change SD bus voltage and enable or disable SD bus power.
 * Return zero on success.
 */
int sdhc_bus_power(struct sdhc_host *hp, u32 ocr) {
	log_printf("sdhc_bus_power(%u)\r\n", ocr);
	/* Disable bus power before voltage change. */
	HWRITE1(hp, SDHC_POWER_CTL, 0);

	/* If power is disabled, reset the host and return now. */
	if (ocr == 0) {
		(void)sdhc_host_reset(hp);
		return 0;
	}

	/*
	 * Enable bus power.  Wait at least 1 ms (or 74 clocks) plus
	 * voltage ramp until power rises.
	 */
	HWRITE1(hp, SDHC_POWER_CTL, (SDHC_VOLTAGE_3_3V << SDHC_VOLTAGE_SHIFT) |
	    SDHC_BUS_POWER);
	udelay(10000);

	/*
	 * The host system may not power the bus due to battery low,
	 * etc.  In that case, the host controller should clear the
	 * bus power bit.
	 */
	if (!ISSET(HREAD1(hp, SDHC_POWER_CTL), SDHC_BUS_POWER)) {
		log_printf("Host controller failed to enable bus power\r\n");
		return ENXIO;
	}

	return 0;
}

/*
 * Return the smallest possible base clock frequency divisor value
 * for the CLOCK_CTL register to produce `freq' (KHz).
 */
static int sdhc_clock_divisor(struct sdhc_host *hp, u32 freq) {
	int div;

	for (div = 1; div <= 256; div *= 2)
		if ((hp->clkbase / div) <= freq)
			return (div / 2);
	/* No divisor found. */
	return -1;
}

/*
 * Set or change SDCLK frequency or disable the SD clock.
 * Return zero on success.
 */
int sdhc_bus_clock(struct sdhc_host *hp, int freq) {
	int div;
	int timo;

	log_printf("%s(%d)\r\n", __FUNCTION__, freq);
#ifdef DIAGNOSTIC
	/* Must not stop the clock if commands are in progress. */
	if (ISSET(HREAD4(hp, SDHC_PRESENT_STATE), SDHC_CMD_INHIBIT_MASK) &&
	    sdhc_card_detect(hp))
		log_printf("sdhc_sdclk_frequency_select: command in progress\r\n");
#endif

	/* Stop SD clock before changing the frequency. */
	HWRITE2(hp, SDHC_CLOCK_CTL, 0);
	if (freq == SDMMC_SDCLK_OFF)
		return 0;

	/* Set the minimum base clock frequency divisor. */
	if ((div = sdhc_clock_divisor(hp, freq)) < 0) {
		/* Invalid base clock frequency or `freq' value. */
		return EINVAL;
	}
	HWRITE2(hp, SDHC_CLOCK_CTL, div << SDHC_SDCLK_DIV_SHIFT);

	/* Start internal clock.  Wait 10ms for stabilization. */
	HSET2(hp, SDHC_CLOCK_CTL, SDHC_INTCLK_ENABLE);
	for (timo = 1000; timo > 0; timo--) {
		if (ISSET(HREAD2(hp, SDHC_CLOCK_CTL), SDHC_INTCLK_STABLE))
			break;
		udelay(10);
	}
	if (timo == 0) {
		log_printf("internal clock never stabilized\r\n");
		return ETIMEDOUT;
	}

	/* Enable SD clock. */
	HSET2(hp, SDHC_CLOCK_CTL, SDHC_SDCLK_ENABLE);

	return 0;
}

static int sdhc_wait_state(struct sdhc_host *hp, u32 mask, u32 value) {
	u32 state;
	int timeout;

	for (timeout = 500; timeout > 0; timeout--) {
		if (((state = HREAD4(hp, SDHC_PRESENT_STATE)) & mask)
		    == value)
			return 0;
		udelay(10000);
	}
	DPRINTF(0,("timeout waiting for %x (state=%d)\r\n", value, state));
	return ETIMEDOUT;
}

void sdhc_exec_command(struct sdhc_host *hp, struct sdmmc_command *cmd) {
	int error;

	if (cmd->c_datalen > 0)
		hp->data_command = 1;

	if (cmd->c_timeout == 0) {
		if (cmd->c_datalen > 0)
			cmd->c_timeout = SDHC_TRANSFER_TIMEOUT;
		else
			cmd->c_timeout = SDHC_COMMAND_TIMEOUT;
	}

	hp->intr_status = 0;

	/*
	 * Start the MMC command, or mark `cmd' as failed and return.
	 */
	error = sdhc_start_command(hp, cmd);
	if (error != 0) {
		cmd->c_error = error;
		SET(cmd->c_flags, SCF_ITSDONE);
		hp->data_command = 0;
		return;
	}

	/*
	 * Wait until the command phase is done, or until the command
	 * is marked done for any other reason.
	 */

	int status = sdhc_wait_intr(hp, SDHC_COMMAND_COMPLETE, cmd->c_timeout);
	if (!ISSET(status, SDHC_COMMAND_COMPLETE)) {
		cmd->c_error = ETIMEDOUT;
		log_printf("timeout dump: error_intr: 0x%x intr: 0x%x\r\n", hp->intr_error_status, hp->intr_status);
//		sdhc_dump_regs(hp);
		SET(cmd->c_flags, SCF_ITSDONE);
		hp->data_command = 0;
		return;
	}

//	log_printf("command_complete, continuing...\r\n");

	/*
	 * The host controller removes bits [0:7] from the response
	 * data (CRC) and we pass the data up unchanged to the bus
	 * driver (without padding).
	 */
	if (cmd->c_error == 0 && ISSET(cmd->c_flags, SCF_RSP_PRESENT)) {
		if (ISSET(cmd->c_flags, SCF_RSP_136)) {
			u8 *p = (u8 *)cmd->c_resp;
			int i;

			for (i = 0; i < 15; i++)
				*p++ = HREAD1(hp, SDHC_RESPONSE + i);
		} else
			cmd->c_resp[0] = HREAD4(hp, SDHC_RESPONSE);
	}

	/*
	 * If the command has data to transfer in any direction,
	 * execute the transfer now.
	 */
	if (cmd->c_error == 0 && cmd->c_datalen > 0)
		sdhc_transfer_data(hp, cmd);

	DPRINTF(1,("cmd %u done (flags=%#x error=%d prev state=%d)\r\n",
	    cmd->c_opcode, cmd->c_flags, cmd->c_error, (cmd->c_resp[0] >> 9) & 15));
	SET(cmd->c_flags, SCF_ITSDONE);
	hp->data_command = 0;
}

static int sdhc_start_command(struct sdhc_host *hp, struct sdmmc_command *cmd) {
	u16 blksize = 0;
	u16 blkcount = 0;
	u16 mode;
	u16 command;
	int error;

	DPRINTF(1,("start cmd %u arg=%#x data=%p dlen=%d flags=%#x\r\n",
		cmd->c_opcode, cmd->c_arg, cmd->c_data, cmd->c_datalen, cmd->c_flags));

	/*
	 * The maximum block length for commands should be the minimum
	 * of the host buffer size and the card buffer size. (1.7.2)
	 */

	/* Fragment the data into proper blocks. */
	if (cmd->c_datalen > 0) {
		blksize = MIN(cmd->c_datalen, cmd->c_blklen);
		blkcount = cmd->c_datalen / blksize;
		if (cmd->c_datalen % blksize > 0) {
			/* XXX: Split this command. (1.7.4) */
			log_printf("data not a multiple of %d bytes\r\n", blksize);
			return EINVAL;
		}
	}

	/* Check limit imposed by 9-bit block count. (1.7.2) */
	if (blkcount > SDHC_BLOCK_COUNT_MAX) {
		log_printf("too much data\r\n");
		return EINVAL;
	}

	/* Prepare transfer mode register value. (2.2.5) */
	mode = 0;
	if (ISSET(cmd->c_flags, SCF_CMD_READ))
		mode |= SDHC_READ_MODE;
	if (blkcount > 0) {
		mode |= SDHC_BLOCK_COUNT_ENABLE;
//		if (blkcount > 1) {
			mode |= SDHC_MULTI_BLOCK_MODE;
			/* XXX only for memory commands? */
			mode |= SDHC_AUTO_CMD12_ENABLE;
//		}
	}
	if (ISSET(hp->flags, SHF_USE_DMA))
		mode |= SDHC_DMA_ENABLE;

	/*
	 * Prepare command register value. (2.2.6)
	 */
	command = (cmd->c_opcode & SDHC_COMMAND_INDEX_MASK) <<
	    SDHC_COMMAND_INDEX_SHIFT;

	if (ISSET(cmd->c_flags, SCF_RSP_CRC))
		command |= SDHC_CRC_CHECK_ENABLE;
	if (ISSET(cmd->c_flags, SCF_RSP_IDX))
		command |= SDHC_INDEX_CHECK_ENABLE;
	if (cmd->c_data != NULL)
		command |= SDHC_DATA_PRESENT_SELECT;

	if (!ISSET(cmd->c_flags, SCF_RSP_PRESENT))
		command |= SDHC_NO_RESPONSE;
	else if (ISSET(cmd->c_flags, SCF_RSP_136))
		command |= SDHC_RESP_LEN_136;
	else if (ISSET(cmd->c_flags, SCF_RSP_BSY))
		command |= SDHC_RESP_LEN_48_CHK_BUSY;
	else
		command |= SDHC_RESP_LEN_48;

	/* Wait until command and data inhibit bits are clear. (1.5) */
	if ((error = sdhc_wait_state(hp, SDHC_CMD_INHIBIT_MASK, 0)) != 0)
		return error;

	if (ISSET(hp->flags, SHF_USE_DMA) && cmd->c_datalen > 0) {
		cmd->c_resid = blkcount;
		cmd->c_buf = cmd->c_data;

		dcache_flush(cmd->c_data, cmd->c_datalen);
		HWRITE4(hp, SDHC_DMA_ADDR, (u32)virtToPhys(cmd->c_data));
	}

	DPRINTF(1,("cmd=%#x mode=%#x blksize=%d blkcount=%d\r\n",
	    command, mode, blksize, blkcount));

	/*
	 * Start a CPU data transfer.  Writing to the high order byte
	 * of the SDHC_COMMAND register triggers the SD command. (1.5)
	 */
	HWRITE2(hp, SDHC_BLOCK_SIZE, blksize | 7<<12);
	if (blkcount > 0)
		HWRITE2(hp, SDHC_BLOCK_COUNT, blkcount);
	HWRITE4(hp, SDHC_ARGUMENT, cmd->c_arg);
	HWRITE4(hp, SDHC_TRANSFER_MODE, ((u32)command<<16)|mode);
//	HWRITE2(hp, SDHC_COMMAND, command);
//	HWRITE2(hp, SDHC_TRANSFER_MODE, mode);

	return 0;
}

static void sdhc_transfer_data(struct sdhc_host *hp, struct sdmmc_command *cmd) {
	int error;
	int status;

	error = 0;

	DPRINTF(1,("resp=%#x datalen=%d\r\n", MMC_R1(cmd->c_resp), cmd->c_datalen));
	if (ISSET(hp->flags, SHF_USE_DMA)) {
		for(;;) {
			status = sdhc_wait_intr(hp, SDHC_TRANSFER_COMPLETE |
					SDHC_DMA_INTERRUPT,
					SDHC_TRANSFER_TIMEOUT);
			if (!status) {
				log_printf("DMA timeout %08x\r\n", status);
				error = ETIMEDOUT;
				break;
			}

			if (ISSET(status, SDHC_TRANSFER_COMPLETE)) {
//				log_printf("got a TRANSFER_COMPLETE: %08x\r\n", status);
				break;
			}
			if (ISSET(status, SDHC_DMA_INTERRUPT)) {
				DPRINTF(2,("dma left:%#x\r\n", HREAD2(hp, SDHC_BLOCK_COUNT)));
				HWRITE4(hp, SDHC_DMA_ADDR,
						(u32)virtToPhys(HREAD4(hp, SDHC_DMA_ADDR)));
				continue;
			}
		}
	} else
		log_printf("fail.\r\n");



#ifdef SDHC_DEBUG
	/* XXX I forgot why I wanted to know when this happens :-( */
	if ((cmd->c_opcode == 52 || cmd->c_opcode == 53) &&
	    ISSET(MMC_R1(cmd->c_resp), 0xcb00))
		log_printf("CMD52/53 error response flags %#x\r\n",
		    MMC_R1(cmd->c_resp) & 0xff00);
#endif

	if (error != 0)
		cmd->c_error = error;
	SET(cmd->c_flags, SCF_ITSDONE);

	DPRINTF(1,("data transfer done (error=%d)\r\n", cmd->c_error));
	return;
}

/* Prepare for another command. */
static int sdhc_soft_reset(struct sdhc_host *hp, int mask) {
	int timo;

	DPRINTF(1,("software reset reg=%#x\r\n", mask));

	HWRITE1(hp, SDHC_SOFTWARE_RESET, mask);
	for (timo = 10; timo > 0; timo--) {
		if (!ISSET(HREAD1(hp, SDHC_SOFTWARE_RESET), mask))
			break;
		udelay(10000);
		HWRITE1(hp, SDHC_SOFTWARE_RESET, 0);
	}
	if (timo == 0) {
		DPRINTF(1,("timeout reg=%#x\r\n", HREAD1(hp, SDHC_SOFTWARE_RESET)));
		HWRITE1(hp, SDHC_SOFTWARE_RESET, 0);
		return (ETIMEDOUT);
	}
	return (0);
}

/*
 * Established by attachment driver at interrupt priority IPL_SDMMC.
 */
int sdhc_intr(void) {
	u16 status;

	DPRINTF(1,("shdc_intr():\n"));
//	sdhc_dump_regs(&sc_host);

	/* Find out which interrupts are pending. */
	status = HREAD2(&sc_host, SDHC_NINTR_STATUS);
	if (!ISSET(status, SDHC_NINTR_STATUS_MASK)) {
		DPRINTF(1, ("unknown interrupt\n"));
		return 0;
	}

	/* Acknowledge the interrupts we are about to handle. */
	HWRITE2(&sc_host, SDHC_NINTR_STATUS, status);
	DPRINTF(2,("interrupt status=%d\n", status));

	/* Service error interrupts. */
	if (ISSET(status, SDHC_ERROR_INTERRUPT)) {
		u16 error;
		u16 signal;

		/* Acknowledge error interrupts. */
		error = HREAD2(&sc_host, SDHC_EINTR_STATUS);
		signal = HREAD2(&sc_host, SDHC_EINTR_SIGNAL_EN);
		HWRITE2(&sc_host, SDHC_EINTR_SIGNAL_EN, 0);
		(void)sdhc_soft_reset(&sc_host, SDHC_RESET_DAT|SDHC_RESET_CMD);
		if (sc_host.data_command == 1) {
			sc_host.data_command = 0;

		// TODO: add a way to send commands from irq
		// context and uncomment this
//		sdmmc_abort();
		}
		HWRITE2(&sc_host, SDHC_EINTR_STATUS, error);
		HWRITE2(&sc_host, SDHC_EINTR_SIGNAL_EN, signal);

		DPRINTF(2,("error interrupt, status=%d\n", error));

		if (ISSET(error, SDHC_CMD_TIMEOUT_ERROR|
	 	    SDHC_DATA_TIMEOUT_ERROR)) {
			sc_host.intr_error_status |= error;
			sc_host.intr_status |= status;
		}
	}

	/* Wake up the sdmmc event thread to scan for cards. */
	if (ISSET(status, SDHC_CARD_REMOVAL|SDHC_CARD_INSERTION))
		log_puts("card changed!");

	/*
	 * Wake up the blocking process to service command
	 * related interrupt(s).
	 */
	if (ISSET(status, SDHC_BUFFER_READ_READY|
	    SDHC_BUFFER_WRITE_READY|SDHC_COMMAND_COMPLETE|
	     SDHC_TRANSFER_COMPLETE|SDHC_DMA_INTERRUPT)) {
		sc_host.intr_status |= status;
	}

	/* Service SD card interrupts. */
	if  (ISSET(status, SDHC_CARD_INTERRUPT)) {
		DPRINTF(0,("card interrupt\n"));
		HCLR2(&sc_host, SDHC_NINTR_STATUS_EN, SDHC_CARD_INTERRUPT);
	}
	return 1;
}

static int sdhc_wait_intr_debug(const char *funcname, int line, struct sdhc_host *hp, int mask, int timo) {
	(void) funcname;
	(void) line;

	int status;

	mask |= SDHC_ERROR_INTERRUPT;
	mask |= SDHC_ERROR_TIMEOUT;

	status = hp->intr_status & mask;

	for (; timo > 0; timo--) {
		sdhc_intr();
		if (hp->intr_status != 0) {
			status = hp->intr_status & mask;
			break;
		}
		udelay(1000);
	}

	if (timo == 0) {
		status |= SDHC_ERROR_TIMEOUT;
	}
	hp->intr_status &= ~status;

	DPRINTF(2,("funcname=%s, line=%d, timo=%d status=%#x intr status=%#x error %#x\r\n",
		funcname, line, timo, status, hp->intr_status, hp->intr_error_status));

	/* Command timeout has higher priority than command complete. */
	if (ISSET(status, SDHC_ERROR_INTERRUPT)) {
		log_printf("resetting due to error interrupt\r\n");
//		sdhc_dump_regs(hp);

		hp->intr_error_status = 0;
		(void)sdhc_soft_reset(hp, SDHC_RESET_DAT|SDHC_RESET_CMD);
		status = 0;
	}

	/* Command timeout has higher priority than command complete. */
	if (ISSET(status, SDHC_ERROR_TIMEOUT)) {
		log_printf("not resetting due to timeout\r\n");
		sdhc_dump_regs(hp);

		hp->intr_error_status = 0;
//		(void)sdhc_soft_reset(hp, SDHC_RESET_DAT|SDHC_RESET_CMD);
		status = 0;
	}

	return status;
}

void sdhc_dump_regs(struct sdhc_host *hp) {
	log_printf("0x%02x PRESENT_STATE:    %x\r\n", SDHC_PRESENT_STATE,
	    HREAD4(hp, SDHC_PRESENT_STATE));
	log_printf("0x%02x POWER_CTL:        %x\r\n", SDHC_POWER_CTL,
	    HREAD1(hp, SDHC_POWER_CTL));
	log_printf("0x%02x NINTR_STATUS:     %x\r\n", SDHC_NINTR_STATUS,
	    HREAD2(hp, SDHC_NINTR_STATUS));
	log_printf("0x%02x EINTR_STATUS:     %x\r\n", SDHC_EINTR_STATUS,
	    HREAD2(hp, SDHC_EINTR_STATUS));
	log_printf("0x%02x NINTR_STATUS_EN:  %x\r\n", SDHC_NINTR_STATUS_EN,
	    HREAD2(hp, SDHC_NINTR_STATUS_EN));
	log_printf("0x%02x EINTR_STATUS_EN:  %x\r\n", SDHC_EINTR_STATUS_EN,
	    HREAD2(hp, SDHC_EINTR_STATUS_EN));
	log_printf("0x%02x NINTR_SIGNAL_EN:  %x\r\n", SDHC_NINTR_SIGNAL_EN,
	    HREAD2(hp, SDHC_NINTR_SIGNAL_EN));
	log_printf("0x%02x EINTR_SIGNAL_EN:  %x\r\n", SDHC_EINTR_SIGNAL_EN,
	    HREAD2(hp, SDHC_EINTR_SIGNAL_EN));
	log_printf("0x%02x CAPABILITIES:     %x\r\n", SDHC_CAPABILITIES,
	    HREAD4(hp, SDHC_CAPABILITIES));
	log_printf("0x%02x MAX_CAPABILITIES: %x\r\n", SDHC_MAX_CAPABILITIES,
	    HREAD4(hp, SDHC_MAX_CAPABILITIES));
}

#define		SDHC_REG_BASE		(UNCACHED_BASE + 0x0d070000)
void sdhc_init(void) {
	sdhc_host_found(0, SDHC_REG_BASE, 1);
}
