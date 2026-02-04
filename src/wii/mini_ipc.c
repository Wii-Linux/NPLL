/*
 * NPLL - Wii - Mini IPC
 * Copyright (C) 2026 Techflash
 *
 * Derived from Wii-Linux arch/powerpc/platforms/embedded6xx/hlwd-ipc-mini.c:
 * Copyright (C) 2025 Michael "Techflash" Garofalo <officialTechflashYT@gmail.com>
 *
 * Derived from BootMii ppcskel's 'ipc.c':
 * Copyright (C) 2008, 2009	Hector Martin "marcan" <marcan@marcansoft.com>
 * Copyright (C) 2009		Andre Heider "dhewg" <dhewg@wiibrew.org>
 * Copyright (C) 2009		John Kelley <wiidev@kelley.ca>
 */

#define MODULE "MINI"
#include <assert.h>
#include <string.h>
#include <npll/console.h>
#include <npll/log.h>
#include <npll/panic.h>
#include <npll/regs.h>
#include <npll/timer.h>
#include <npll/types.h>
#include <npll/utils.h>
#include "mini_ipc.h"

struct infohdr {
	char magic[3];
	u8 version;
	void *mem2_boundary;
	volatile struct ipc_request_mini *ipc_in;
	u32 ipc_in_size;
	volatile struct ipc_request_mini *ipc_out;
	u32 ipc_out_size;
};

struct mini_state {
	struct infohdr *infohdr;                      /* MINI infohdr */
	u16 out_head;                                 /* Out head */
	u16 in_tail;                                  /* In tail */
	int in_size;                                  /* In queue size */
	int out_size;                                 /* Out queue size */
	volatile struct ipc_request_mini *in_queue;   /* In queue pointer */
	volatile struct ipc_request_mini *out_queue;  /* Out queue pointer */
	u32 cur_tag;                                  /* Current request number ("tag") */
};

static struct mini_state state;
static bool initialized = false;

void MINI_Init(void) {
	u32 infohdr_ptr, ppcmsg;
	struct infohdr *infohdr;
	struct ipc_request_mini req;
	int ret;

	/* read the infohdr pointer from the region we just mapped */
	infohdr_ptr = *(u32 *)(MEM2_UNCACHED_BASE + MEM2_SIZE_WII - 4);

	if (infohdr_ptr < MEM2_PHYS_BASE || infohdr_ptr >= (MEM2_PHYS_BASE + MEM2_SIZE_WII)) {
		log_printf("bogus infohdr ptr 0x%08x\r\n", infohdr_ptr);
		panic("bogus infohdr ptr");
	}

	infohdr_ptr = (u32)physToCached(infohdr_ptr);
	infohdr = (struct infohdr *)infohdr_ptr;

	/* valid infohdr? */
	if (memcmp(infohdr->magic, "IPC", 3) != 0) {
		log_printf("invalid IPC magic: %c%c%c\r\n", infohdr->magic[0], infohdr->magic[1], infohdr->magic[2]);
		panic("invalid IPC magic");
	}

	if (infohdr->version != 1) {
		log_printf("unknown IPC version %d\r\n", infohdr->version);
		panic("unknown MINI IPC version");
	}

	/* set up our internal state */
	memset(&state, 0, sizeof(state));

	state.infohdr = infohdr;
	state.in_size = infohdr->ipc_in_size;
	state.out_size = infohdr->ipc_out_size;

	/* map the in queue */
	state.in_queue = physToUncached((u32)infohdr->ipc_in);

	/* map the out queue */
	state.out_queue = physToUncached((u32)infohdr->ipc_out);

	/* read in_tail and out_head from IPC hardware registers */
	ppcmsg = HW_IPC_PPCMSG;
	state.in_tail = ppcmsg & 0xffff;
	state.out_head = ppcmsg >> 16;

	state.cur_tag = 1;

	log_printf("initial in tail: %d, out head: %d\r\n", state.in_tail, state.out_head);

	log_puts("running trivial tests:");
	initialized = true;
	ret = MINI_IPCExchange(&req, IPC_MINI_CODE_PING, 3, 1, 0);
	log_printf(" * fast ping: %d\r\n", ret);
	ret = MINI_IPCExchange(&req, IPC_MINI_CODE_SLWPING, 3, 1, 0);
	log_printf(" * slow ping: %d\r\n", ret);
	ret = MINI_IPCExchange(&req, IPC_MINI_CODE_GETVERS, 3, 1, 0);
	log_printf(" * getvers: %d (version: 0x%08x)\r\n", ret, req.args[0]);

	H_WiiMEM2Top = infohdr->mem2_boundary;

	return;
}

static u16 peek_outtail(void) {
	u32 val = HW_IPC_ARMMSG;
	assert(initialized);
	return (u16)(val & 0xffff);
}

static u16 peek_inhead(void) {
	u32 val = HW_IPC_ARMMSG;
	assert(initialized);
	return (u16)(val >> 16);
}

static void poke_intail(void) {
	u32 val;
	assert(initialized);

	val = HW_IPC_PPCMSG;
	val &= 0xffff0000;
	val |= state.in_tail;
	HW_IPC_PPCMSG = val;
}

static void poke_outhead(void) {
	u32 val;
	assert(initialized);

	val = HW_IPC_PPCMSG;
	val &= 0x0000ffff;
	val |= (state.out_head << 16);
	HW_IPC_PPCMSG = val;
}

static int inqueue_full(void) {
	return peek_inhead() == ((state.in_tail + 1) & (state.in_size - 1));
}

int MINI_IPCVpost(u32 code, u32 tag, int num_args, va_list args) {
	int i = 0, arg = 0;
	assert(initialized);

	if (inqueue_full()) {
		log_puts("in queue full, this might be bad...");
		while (inqueue_full()) {
			udelay(5000);
			i++;
			if (i > 200 && i % 200 == 0) {
				log_printf("Starlet is probably stuck!  Still waiting on inhead %d != %d\r\n",
						peek_inhead(), ((state.in_tail + 1) & (state.in_size - 1)));
			}
			/* if Starlet can't process 1 IPC message in 10 entire seconds, it's definetly hosed */
			if (i > 10000) {
				log_puts("abandoning all hope for submitting this request, "
					"Starlet is locked up; please reboot the system to restore functionality.");
				return -1;
			}
		}
	}
	/* prepare our message */
	state.in_queue[state.in_tail].code = code;
	state.in_queue[state.in_tail].tag = tag;
	while (num_args--)
		state.in_queue[state.in_tail].args[arg++] = va_arg(args, u32);

	state.in_tail = (state.in_tail + 1) & (state.in_size - 1);

	/* send it off */
	poke_intail();
	HW_IPC_PPCCTRL = HW_IPC_PPCCTRL_X1;

	/* success, Starlet is processing it */
	return 0;
}

int MINI_IPCPost(u32 code, u32 tag, int num_args, ...) {
	va_list args;
	int ret;
	assert(initialized);

	if (num_args > 0)
		va_start(args, num_args);

	ret = MINI_IPCVpost(code, tag, num_args, args);

	if (num_args > 0)
		va_end(args);

	return ret;
}

/* TODO: IRQs? */
int MINI_IPCRecv(struct ipc_request_mini *req, int max_attempts) {
	int i = 0;
	assert(initialized);

	while (peek_outtail() == state.out_head) {
		udelay(5000);
		i++;
		if (i > 200 && i % 200 == 0) {
			log_printf("Starlet might be stuck!  Still waiting on outtail %d == %d\r\n",
				peek_outtail(), state.out_head);
		}
		if (i > 10000) {
			log_puts("abandoning all hope for receiving this request, "
				"Starlet is locked up; please reboot the system to restore functionality.");
			memset(req, 0, sizeof(struct ipc_request_mini));
			return -1;
		}
		if (i > max_attempts && max_attempts > 1) {
			memset(req, 0, sizeof(struct ipc_request_mini));
			return -1;
		}
	}

	/* we got a message! */
	memcpy(req, (void *)&state.out_queue[state.out_head], sizeof(struct ipc_request_mini));

	/* update our state and hardware state accordingly */
	state.out_head = (state.out_head + 1) & (state.out_size - 1);
	poke_outhead();

	/* success, read your message */
	return 0;
}

int MINI_IPCRecvTagged(struct ipc_request_mini *req, u32 code, u32 tag, int max_recv_attempts, int max_attempts) {
	int error = 0;
	assert(initialized);

	error = MINI_IPCRecv(req, max_recv_attempts);
	if (error)
		return error;
	while (req->code != code || req->tag != tag) {
		log_printf("Got response with wrong info!  Expecting: code=%d, tag=%d; Received: code=%d, tag=%d\r\n", code, tag, req->code, req->tag);
		error = -1;

		max_attempts--;
		if (max_attempts <= 0)
			break;
		error = MINI_IPCRecv(req, max_recv_attempts);
		if (error)
			break;
	}

	return error;
}


int MINI_IPCExchange(struct ipc_request_mini *req, u32 code, int max_recv_attempts, int max_attempts, int num_args, ...) {
	va_list args;
	int error = 0;

	assert(initialized);

	if (num_args > 0)
		va_start(args, num_args);

	/* send the message */
	error = MINI_IPCVpost(code, state.cur_tag, num_args, args);
	if (error)
		goto out;

	/* get the reply */
	error = MINI_IPCRecvTagged(req, code, state.cur_tag, max_recv_attempts, max_attempts);
	if (error)
		goto out;

	/* increment our tag */
	state.cur_tag++;

	return 0;

out:
	if (num_args > 0)
		va_end(args);

	return error;
}
