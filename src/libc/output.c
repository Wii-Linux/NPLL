/*
 * NPLL - libc - putchar/puts
 *
 * Copyright (C) 2025-2026 Techflash
 */

#include <stdbool.h>
#include <npll/output.h>

int putchar(int c) {
	uint i;
	for (i = 0; i < O_NumDevices; i++)
		O_Devices[i]->writeChar((char)c);

	return c;
}


int puts(const char *str) {
	uint i;
	for (i = 0; i < O_NumDevices; i++) {
		O_Devices[i]->writeStr(str);
		O_Devices[i]->writeChar('\r');
		O_Devices[i]->writeChar('\n');
	}

	return 0;
}
