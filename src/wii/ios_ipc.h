/*
 * NPLL - Wii - IOS IPC
 *
 * Copyright (C) 2025 Techflash
 *
 * Loosely based on the IPC code on PowerBlocks:
 * Copyright (C) 2025 Samuel Fitzsimons (rainbain)
 */

#ifndef _IOS_IPC_H
#define _IOS_IPC_H

#include <npll/types.h>

typedef enum {
	IOS_MODE_NONE = 0,
	IOS_MODE_READ = 1,
	IOS_MODE_WRITE = 2
} ios_open_mode_t;

typedef struct {
	void* data;
	u32 size;
} ios_ioctlv_t;

extern i32 IOS_Open(const char *path, u32 mode);
extern i32 IOS_Close(i32 fd);
extern i32 IOS_Read(i32 fd, void *buf, u32 len);
extern i32 IOS_Write(i32 fd, const void *buf, u32 len);
extern i32 IOS_Seek(i32 fd, i32 offset, u32 whence);
extern i32 IOS_Ioctl(i32 fd, u32 cmd, void *inbuf, u32 inlen, void *outbuf, u32 outlen);
extern i32 IOS_Ioctlv(i32 fd, u32 cmd, u32 in_cnt, u32 out_cnt, ios_ioctlv_t *vec);
#endif /* _IOS_IPC_H */
