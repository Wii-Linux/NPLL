/*
 * NPLL - Drivers - EXI
 *
 * Copyright (C) 2025 Techflash
 *
 * Derived in part from the Linux USB Gecko udbg driver:
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 */

#ifndef _DRIVERS_EXI_H
#define _DRIVERS_EXI_H

#include <npll/types.h>
#include <npll/drivers.h>

#define EXI_CLK_32MHZ           5

#define   EXI_CSR_CLKMASK       (0x7<<4)
#define     EXI_CSR_CLK_32MHZ   (EXI_CLK_32MHZ<<4)
#define   EXI_CSR_CSMASK        (0x7<<7)
#define     EXI_CSR_CS_0        (0x1<<7)  /* Chip Select 001 */

#define   EXI_CR_TSTART         (1<<0)
#define   EXI_CR_WRITE		(1<<2)
#define   EXI_CR_RW             (2<<2)
#define   EXI_CR_TLEN(len)      (((len)-1)<<4)

extern vu32 *EXI_CSR(int chan);
extern vu32 *EXI_CR(int chan);
extern vu32 *EXI_DATA(int chan);

DECLARE_DRIVER(exiDrv);
#endif /* _DRIVERS_EXI_H */

