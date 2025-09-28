/*
 * NPLL - libc - putchar/puts
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdbool.h>
#include <npll/output.h>

bool __putcharNoGraphics = false;

int putchar(int c) {
	int i;
	for (i = 0; i < O_NumDevices; i++) {
		if (__putcharNoGraphics && O_Devices[i]->isGraphical)
			continue;
		O_Devices[i]->writeChar((char)c);
	}

	return c;
}


int puts(const char *str) {
	int i;
	for (i = 0; i < O_NumDevices; i++) {
		if (__putcharNoGraphics && O_Devices[i]->isGraphical)
			continue;
		O_Devices[i]->writeStr(str);
		O_Devices[i]->writeChar('\r');
		O_Devices[i]->writeChar('\n');
	}

	return 0;
}
