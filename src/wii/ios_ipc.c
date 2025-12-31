/*
 * NPLL - Wii - IOS IPC
 *
 * Copyright (C) 2025 Techflash
 *
 * Loosely based on the IPC code on PowerBlocks:
 * Copyright (C) 2025 Samuel Fitzsimons (rainbain)
 */

#include "ios_ipc.h"
#include <npll/types.h>
#include <npll/regs.h>
#include <npll/cpu.h>
#include <npll/cache.h>
#include <npll/utils.h>
#include <npll/timer.h>
#include <stdio.h>
#include <string.h>

/* IOS IPC request */
typedef struct IOSRequest {
	u32 command;
	i32 result;
	u32 fd;
	u32 arg0;
	u32 arg1;
	u32 arg2;
	u32 arg3;
	u32 arg4;
	u32 arg5;
	u32 pad[6];
} IOSRequest;

/*  IOS command IDs */
typedef enum {
	IOS_OPEN = 1,
	IOS_CLOSE = 2,
	IOS_READ = 3,
	IOS_WRITE = 4,
	IOS_SEEK = 5,
	IOS_IOCTL = 6,
	IOS_IOCTLV = 7
} ios_cmd_t;

#define ipc_clear() HW_IPC_PPCCTRL = HW_IPC_PPCCTRL_Y1 | HW_IPC_PPCCTRL_Y2

static inline void ipc_send(IOSRequest *req) {
	dcache_flush(req, sizeof(IOSRequest));
	sync();

	HW_IPC_PPCMSG = (u32)virtToPhys(req);
	barrier();

	HW_IPC_PPCCTRL |= HW_IPC_PPCCTRL_X1; /* signal execute */
}

static i32 ipc_wait(IOSRequest *req, u32 timeout_usec) {
	u64 start = mftb();

	while (!(HW_IPC_PPCCTRL & HW_IPC_PPCCTRL_Y1)) {
		if (timeout_usec && T_HasElapsed(start, timeout_usec)) {
			puts("IOS: Timeout waiting for Starlet to handle IPC, is it OK?");
			return -1; /* timeout */
		}
	}

	barrier();
	ipc_clear();
	sync();

	return req->result;
}

static i32 ipc_call(IOSRequest *req) {
	ipc_send(req);
	return ipc_wait(req, 1000 * 1000);
}

/*
 * public APIs
 */

i32 IOS_Open(const char *path, u32 mode) {
	IOSRequest req = {0};

	req.command = IOS_OPEN;
	req.arg0 = (u32)virtToPhys((void *)path);
	req.arg1 = mode;

	dcache_flush(path, 256); /* conservative */

	return ipc_call(&req);
}

i32 IOS_Close(i32 fd) {
	IOSRequest req = {0};

	req.command = IOS_CLOSE;
	req.fd = fd;

	return ipc_call(&req);
}

i32 IOS_Read(i32 fd, void *buf, u32 len) {
	IOSRequest req = {0};

	req.command = IOS_READ;
	req.fd = fd;
	req.arg0 = (u32)virtToPhys(buf);
	req.arg1 = len;

	dcache_flush(buf, len);

	return ipc_call(&req);
}

i32 IOS_Write(i32 fd, const void *buf, u32 len) {
	IOSRequest req = {0};

	req.command = IOS_WRITE;
	req.fd = fd;
	req.arg0 = (u32)virtToPhys((void *)buf);
	req.arg1 = len;

	dcache_flush(buf, len);

	return ipc_call(&req);
}

i32 IOS_Seek(i32 fd, i32 offset, u32 whence) {
	IOSRequest req = {0};

	req.command = IOS_SEEK;
	req.fd = fd;
	req.arg0 = offset;
	req.arg1 = whence;

	return ipc_call(&req);
}

i32 IOS_Ioctl(i32 fd, u32 cmd, void *inbuf, u32 inlen,
	      void *outbuf, u32 outlen) {
	IOSRequest req = {0};

	req.command = IOS_IOCTL;
	req.fd = fd;
	req.arg0 = cmd;
	req.arg1 = (u32)virtToPhys(inbuf);
	req.arg2 = inlen;
	req.arg3 = (u32)virtToPhys(outbuf);
	req.arg4 = outlen;

	if (inbuf && inlen)
		dcache_flush(inbuf, inlen);
	if (outbuf && outlen)
		dcache_flush(outbuf, outlen);

	return ipc_call(&req);
}

i32 IOS_Ioctlv(i32 fd, u32 cmd, u32 in_cnt, u32 out_cnt, ios_ioctlv_t *vec) {
	IOSRequest req = {0};

	req.command = IOS_IOCTLV;
	req.fd = fd;
	req.arg0 = cmd;
	req.arg1 = in_cnt;
	req.arg2 = out_cnt;
	req.arg3 = (u32)virtToPhys(vec);

	/* caller must ensure vectors + buffers are cache coherent */
	dcache_flush(vec, (in_cnt + out_cnt) * 8);

	return ipc_call(&req);
}
