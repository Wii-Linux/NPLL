/*
 * NPLL - Stack protector
 *
 * Copyright (C) 2025 Techflash
 */


#include <npll/panic.h>

void __attribute__((noreturn)) __stack_chk_fail(void) {
	panic("Stack smashing detected!!");
}
