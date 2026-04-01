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
#define IPC_MINI_CODE_JUMP          0x00000001
#define IPC_MINI_CODE_GETVERS       0x00000002
#define IPC_MINI_CODE_WRITE32       0x01000100
#define IPC_MINI_CODE_SET32         0x01000106

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

enum MINI_Err {
	MINI_OK,
	MINI_BAD_INFOHDR_PTR,
	MINI_BAD_INFOHDR_MAGIC,
	MINI_BAD_INFOHDR_VERSION,
	MINI_TIMEOUT,
	MINI_NOT_INIT
};

/*
 * Check if a valid infohdr is present in MEM2.
 * Does not require MINI_Init.
 */
enum MINI_Err MINI_ValidInfoHdr(void);

/*
 * Initialize MINI IPC interface, only meant to be called by
 * the IPC core.
 */
enum MINI_Err MINI_Init(void);

/*
 * Post a message w/ va_args.  See MINI_IPCPost.
 */
enum MINI_Err MINI_IPCVpost(u32 code, u32 tag, uint num_args, va_list args);

/*
 * Post a message to Starlet.
 *
 * Returns MINI_TIMEOUT if Starlet is stuck for too long.
 * Returns MINI_NOT_INIT if not using MINI, or IPC not initialized.
 * Returns 0 on success.
 */
enum MINI_Err MINI_IPCPost(u32 code, u32 tag, uint num_args, ...);

/*
 * Receive a message from Starlet, filling in 'req' with
 * the message that has been received.
 *
 * Returns MINI_TIMEOUT if Starlet is stuck for too long.
 * Returns MINI_NOT_INIT if not using MINI, or IPC not initialized.
 * Returns 0 on success.
 */
enum MINI_Err MINI_IPCRecv(struct ipc_request_mini *req, uint max_attempts);

/*
 * Receive a message from Starlet, whose code and tag match
 * the provided parameters, filling in 'req' with the
 * message that has been received.
 *
 * 'max_recv_attempts' is the 'max_attempts' of the ipc_receive_mini call.
 * 'max_attempts' is how many times we wait for the message, if we ended up
 * receiving some other message.
 *
 * Returns MINI_TIMEOUT if Starlet is stuck for too long.
 * Returns MINI_NOT_INIT if not using MINI, or IPC not initialized.
 * Returns 0 on success.
 */
enum MINI_Err MINI_IPCRecvTagged(struct ipc_request_mini *req, u32 code, u32 tag, uint max_recv_attempts, uint max_attempts);

/*
 * Exchange a message with Starlet, filling in 'req' with
 * the message that has been received.
 *
 * Returns MINI_TIMEOUT if Starlet is stuck for too long,
 *   for either receiving the message, or replying.
 * Returns MINI_NOT_INIT if not using MINI, or IPC not initialized.
 * Returns 0 on success for both send and receive.
 */
enum MINI_Err MINI_IPCExchange(struct ipc_request_mini *req, u32 code, uint max_recv_attempts, uint max_attempts, uint num_args, ...);


#endif
