/*
 * NPLL - Drivers - EXI
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _DRIVERS_EXI_H
#define _DRIVERS_EXI_H

/*
 * Selects the desired device (CS line) on the given
 * EXI channel, and sets the desired clock speed.
 */
extern void H_EXISelect(unsigned int channel, unsigned int cs, unsigned int clkMhz);

/*
 * Deselects any selected device (CS line) on the given
 * EXI channel.
 */
extern void H_EXIDeselect(unsigned int channel);

/*
 * Immediate transaction to channel.
 * Both the read and write will be of the same size if using both.
 * Assumes desired device is already selected.
 */
extern int H_EXIXferImm(unsigned int channel,
			unsigned int len,
			unsigned int mode,
			const void *in, void *out);

/*
 * Transfer modes
 */
#define EXI_MODE_READ  (1 << 0)
#define EXI_MODE_WRITE (1 << 1)

/* I/O shortcuts */
#define H_EXIReadImm(channel, len, out)     H_EXIXferImm(channel, len, EXI_MODE_READ, NULL, out)
#define H_EXIWriteImm(channel, len, in)     H_EXIXferImm(channel, len, EXI_MODE_WRITE, in, NULL)
#define H_EXIRdWrImm(channel, len, in, out) H_EXIXferImm(channel, len, EXI_MODE_READ | EXI_MODE_WRITE, in, out)

DECLARE_DRIVER(exiDrv);
#endif /* _DRIVERS_EXI_H */

