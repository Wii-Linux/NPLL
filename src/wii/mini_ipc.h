/*
 * NPLL - Wii - Mini IPC
 * Copyright (C) 2026 Techflash
 *
 * Derived from Wii-Linux arch/powerpc/include/asm/hlwd-ipc-mini.h:
 * Copyright (C) 2025 Michael "Techflash" Garofalo <officialTechflashYT@gmail.com>
 *
 * Derived from BootMii ppcskel's 'ipc.h':
 * Copyright (C) 2008, 2009	Haxx Enterprises <bushing@gmail.com>
 * Copyright (C) 2008, 2009	Hector Martin "marcan" <marcan@marcansoft.com>
 * Copyright (C) 2009		Andre Heider "dhewg" <dhewg@wiibrew.org>
 * Copyright (C) 2009		John Kelley <wiidev@kelley.ca>
 * Copyright (C) 2008, 2009	Sven Peter <svenpeter@gmail.com>
 */

#ifndef __MINI_IPC_H
#define __MINI_IPC_H
#include <npll/types.h>
#include <stdarg.h>

/*
 * IPC codes
 */
#define IPC_MINI_CODE_SLWPING       0x00000000
#define IPC_MINI_CODE_PING          0x01000000
#define IPC_MINI_CODE_GETVERS       0x00000002

#define IPC_MINI_CODE_PPC_BOOT_MEM  0x00060000
#define IPC_MINI_CODE_PPC_BOOT_FILE 0x00060001

/* only supported in downstream versions of MINI */
#define IPC_MINI_CODE_PPC_CLK_SUPP  0x00068000
#define IPC_MINI_CODE_PPC_CLK_LO    0x00068001
#define IPC_MINI_CODE_PPC_CLK_HI    0x00068002

#define IPC_MINI_CODE_BOOT2_RUN     0x00050000


/*
 * IPC request structure
 */
struct ipc_request_mini {
	union {
		struct {
			u8 flags;
			u8 device;
			u16 req;
		};
		u32 code;
	};
	u32 tag;
	u32 args[6];
};


/*
 * Initialize MINI IPC interface, only meant to be called by
 * the IPC core.
 */
void MINI_Init(void);

/*
 * Post a message w/ va_args.  See MINI_IPCPost.
 */
int MINI_IPCVpost(u32 code, u32 tag, int num_args, va_list args);

/*
 * Post a message to Starlet.
 *
 * Returns -ETIMEDOUT if Starlet is stuck for too long.
 * Returns -EINVAL if not using MINI, or IPC not initialized.
 * Returns 0 on success.
 */
int MINI_IPCPost(u32 code, u32 tag, int num_args, ...);

/*
 * Receive a message from Starlet, filling in 'req' with
 * the message that has been received.
 *
 * Returns -ETIMEDOUT if Starlet is stuck for too long.
 * Returns -EINVAL if not using MINI, or IPC not initialized.
 * Returns 0 on success.
 */
int MIPC_IPCRecv(struct ipc_request_mini *req, int max_attempts);

/*
 * Receive a message from Starlet, whose code and tag match
 * the provided parameters, filling in 'req' with the
 * message that has been received.
 *
 * 'max_recv_attempts' is the 'max_attempts' of the ipc_receive_mini call.
 * 'max_attempts' is how many times we wait for the message, if we ended up
 * receiving some other message.
 *
 * Returns -ETIMEDOUT if Starlet is stuck for too long.
 * Returns -EINVAL if not using MINI, or IPC not initialized.
 * Returns 0 on success.
 */
int MINI_IPCRecvTagged(struct ipc_request_mini *req, u32 code, u32 tag, int max_recv_attempts, int max_attempts);

/*
 * Exchange a message with Starlet, filling in 'req' with
 * the message that has been received.
 *
 * Returns -ETIMEDOUT if Starlet is stuck for too long,
 *   for either receiving the message, or replying.
 * Returns -EINVAL if not using MINI, or IPC not initialized.
 * Returns 0 on success for both send and receive.
 */
int MINI_IPCExchange(struct ipc_request_mini *req, u32 code, int max_recv_attempts, int max_attempts, int num_args, ...);


#endif
