/*
 * NPLL - Wii - IOS IPC
 *
 * Copyright (C) 2025 Techflash
 *
 * Based on the IPC code of The Homebrew Channel Reloader Stub:
 *  Copyright (C) 2008 dhewg, #wiidev efnet
 *  Copyright (C) 2008 marcan, #wiidev efnet
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

extern int IOS_Open(const char *filename, u32 mode);
extern int IOS_Close(int fd);
extern i32 IOS_Read(i32 fd, void *buf, u32 len);
extern i32 IOS_Write(i32 fd, const void *buf, u32 len);
extern i32 IOS_Seek(i32 fd, i32 offset, u32 whence);
extern int IOS_Ioctlv(int fd, u32 cmd, u32 in_count, u32 out_count, ios_ioctlv_t *vec);
extern int IOS_IoctlvReboot(int fd, u32 cmd, u32 in_count, u32 out_count, ios_ioctlv_t *vec);
extern void IOS_Reset(void);
#endif /* _IOS_IPC_H */
