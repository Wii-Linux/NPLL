/*
 * NPLL - Wii - IOS IPC
 *
 * Copyright (C) 2025 Techflash
 *
 * Based on the IPC code of The Homebrew Channel Reloader Stub:
 *  Copyright (C) 2008 dhewg, #wiidev efnet
 *  Copyright (C) 2008 marcan, #wiidev efnet
 */

#include "ios_ipc.h"
#include <npll/types.h>
#include <npll/regs.h>
#include <npll/cpu.h>
#include <npll/cache.h>
#include <npll/utils.h>
#include <npll/timer.h>
#include <npll/log.h>
#include <string.h>

#define IPC_TIMEOUT (250 * 1000)

static void ipc_bell(u32 w) {
	HW_IPC_PPCCTRL = (HW_IPC_PPCCTRL & 0x30) | w;
}

static int ipc_wait_ack(void) {
	u64 tb = mftb();
	while ((HW_IPC_PPCCTRL & 0x22) != 0x22) {
		if (T_HasElapsed(tb, IPC_TIMEOUT)) {
			log_printf("IOS: Timeout waiting for IOS reply\r\n");
			return -1;
		}
	}
	udelay(100);

	return 0;
}

static int ipc_wait_reply(void) {
	u64 tb = mftb();
	while ((HW_IPC_PPCCTRL & 0x14) != 0x14) {
		if (T_HasElapsed(tb, IPC_TIMEOUT)) {
			log_printf("IOS: Timeout waiting for IOS reply\r\n");
			return -1;
		}
	}
	udelay(100);

	return 0;
}

// Mid-level IPC access.

struct ipc {
	u32 cmd;
	int result;
	int fd;
	u32 arg[5];

	u32 user[8];
};

static struct ipc ipc __attribute__((aligned(64)));

static int ipc_send_request(void) {
	int ret;

	dcache_flush(&ipc, 0x40);

	HW_IPC_PPCMSG = (u32)virtToPhys(&ipc);
	ipc_bell(1);

	ret = ipc_wait_ack();
	if (ret)
		return ret;

	ipc_bell(2);

	return 0;
}

static int ipc_send_twoack(void) {
	int ret;

	dcache_flush(&ipc, 0x40);

	HW_IPC_PPCMSG = (u32)virtToPhys(&ipc);
	ipc_bell(1);

	ret = ipc_wait_ack();
	if (ret)
		return ret;

	ipc_bell(2);

	ret = ipc_wait_ack();
	if (ret)
		return ret;

	ipc_bell(2);
	ipc_bell(8);

	return 0;
}

static int ipc_recv_reply(void) {
	int ret;

	for (;;) {
		u32 reply;

		ret = ipc_wait_reply();
		if (ret)
			return ret;

		reply = HW_IPC_ARMMSG;
		ipc_bell(4);

		ipc_bell(8);

		if (((u32 *)reply) == virtToPhys(&ipc))
			break;

		log_printf("Ignoring unexpected IPC reply @ 0x%08x\r\n", reply);
	}

	dcache_invalidate(&ipc, sizeof ipc);

	return 0;
}


// High-level IPC access.

int IOS_Open(const char *filename, u32 mode) {
	int ret;

	dcache_flush((void*)filename, strlen(filename) + 1);
	memset(&ipc, 0, sizeof ipc);

	ipc.cmd = 1;
	ipc.fd = 0;
	ipc.arg[0] = (u32)virtToPhys((void *)filename);
	ipc.arg[1] = mode;

	ret = ipc_send_request();
	if (ret)
		return ret;

	ret = ipc_recv_reply();
	if (ret)
		return ret;

	return ipc.result;
}

int IOS_Close(int fd) {
	int ret;

	memset(&ipc, 0, sizeof ipc);

	ipc.cmd = 2;
	ipc.fd = fd;

	ret = ipc_send_request();
	if (ret)
		return ret;

	ret = ipc_recv_reply();
	if (ret)
		return ret;


	return ipc.result;
}

static int _ios_ioctlv(int fd, u32 cmd, u32 in_count, u32 out_count, ios_ioctlv_t *vec, int reboot) {
	u32 i;
	int ret;

	memset(&ipc, 0, sizeof ipc);

	for (i = 0; i < in_count + out_count; i++) {
		if (addrIsValidCached(vec[i].data)) {
			dcache_flush(vec[i].data, vec[i].size);
			vec[i].data = (void *)virtToPhys(vec[i].data);
		}
	}

	dcache_flush(vec, (in_count + out_count) * sizeof *vec);

	ipc.cmd = 7;
	ipc.fd = fd;
	ipc.arg[0] = cmd;
	ipc.arg[1] = in_count;
	ipc.arg[2] = out_count;
	ipc.arg[3] = (u32)virtToPhys(vec);

	if(reboot)
		return ipc_send_twoack();
	else {
		ret = ipc_send_request();
		if (ret)
			return ret;

		ret = ipc_recv_reply();
		if (ret)
			return ret;


		for (i = in_count; i < in_count + out_count; i++) {
			if (vec[i].data) {
				vec[i].data = physToCached(vec[i].data);
				dcache_invalidate(vec[i].data, vec[i].size);
			}
		}
		return ipc.result;
	}
}

int IOS_Ioctlv(int fd, u32 cmd, u32 in_count, u32 out_count, ios_ioctlv_t *vec) {
	return _ios_ioctlv(fd, cmd, in_count, out_count, vec, 0);
}

int IOS_IoctlvReboot(int fd, u32 cmd, u32 in_count, u32 out_count, ios_ioctlv_t *vec) {
	return _ios_ioctlv(fd, cmd, in_count, out_count, vec, 1);
}

// Cleanup any old state.

static void ipc_cleanup_reply(void) {
	if ((HW_IPC_PPCCTRL & 0x14) != 0x14)
		return;

	HW_IPC_ARMMSG;
	ipc_bell(4);

	ipc_bell(8);
}

static void ipc_cleanup_request(void) {
	if ((HW_IPC_PPCCTRL & 0x22) == 0x22)
		ipc_bell(2);
}

void IOS_Reset(void) {
	int i;

	log_printf("Flushing IPC transactions");
	for (i = 0; i < 10; i++) {
		ipc_cleanup_request();
		ipc_cleanup_reply();
		udelay(1000);
		log_printf(".");
	}
	log_printf(" Done.\r\n");

	log_printf("Closing file descriptors");
	for (i = 0; i < 32; i++) {
		IOS_Close(i);
		log_printf(".");
	}
	log_printf(" Done.\r\n");
}
