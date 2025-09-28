/*
 * NPLL - PowerPC Exception handling
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _EXCEPTIONS_H
#define _EXCEPTIONS_H

#include <stdbool.h>
#include <npll/types.h>

extern void E_Init(void);
extern void E_Handler(int exception);

#endif /* _EXCEPTIONS_H */
