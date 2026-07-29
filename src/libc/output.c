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
		O_Devices[i]->writeChar(O_Devices[i], (char)c);

	return c;
}


int puts(const char *str) {
	uint i;
	for (i = 0; i < O_NumDevices; i++) {
		O_Devices[i]->writeStr(O_Devices[i], str);
		O_Devices[i]->writeChar(O_Devices[i], '\r');
		O_Devices[i]->writeChar(O_Devices[i], '\n');
	}

	return 0;
}
