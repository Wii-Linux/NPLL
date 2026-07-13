/*
 * NPLL - Stack protector
 *
 * Copyright (C) 2025 Techflash
 */


#include <npll/panic.h>
#include <npll/types.h>

/* Freestanding EABI has no libc startup to provide this object. */
uintptr_t __stack_chk_guard = 0x4e504c4cu;

void __attribute__((noreturn)) __stack_chk_fail(void) {
	panic("Stack smashing detected!!");
}
