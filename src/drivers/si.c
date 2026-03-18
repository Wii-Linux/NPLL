/*
 * NPLL - Flipper/Hollywood/Latte Hardware - Serial Interface
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "SI"

#include <assert.h>
#include <stdio.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/input.h>
#include <npll/log.h>
#include <npll/timer.h>

struct si_channel_regs {
	vu32 outbuf;
	vu32 inbufh;
	vu32 inbufl;
};

struct si_regs {
	volatile struct si_channel_regs chan[4];
	vu32 poll;
	vu32 comcsr;
	vu32 sr;
	vu32 exilk;
	vu32 pad[0x10];
	vu32 buf[0x20];
};

/* https://www.gc-forever.com/yagcd/chap5.html#sec5.8 */
/* COMCSR bits */
#define SI_COMCSR_TSTART         (1 << 0)
#define SI_COMCSR_CHAN_SHIFT     1
#define SI_COMCSR_CHAN           (3 << SI_COMCSR_CHAN_SHIFT)
#define SI_COMCSR_CALLBACK_EN    (1 << 6)
#define SI_COMCSR_CMD_EN         (1 << 7)
#define SI_COMCSR_INLEN_SHIFT    8
#define SI_COMCSR_INLEN          (127 << SI_COMCSR_INLEN_SHIFT)
#define SI_COMCSR_OUTLEN_SHIFT   16
#define SI_COMCSR_OUTLEN         (127 << SI_COMCSR_OUTLEN_SHIFT)
#define SI_COMCSR_CHAN_EN        (1 << 24)
#define SI_COMCSR_CHAN_NUM_SHIFT 25
#define SI_COMCSR_CHAN_NUM       (3 << SI_COMCSR_CHAN_NUM_SHIFT)
#define SI_COMCSR_RDSTINTMSK     (1 << 27)
#define SI_COMCSR_RDSTINT        (1 << 28)
#define SI_COMCSR_COMERR         (1 << 29)
#define SI_COMCSR_TCINTMSK       (1 << 30)
#define SI_COMCSR_TCINT          (1 << 31)

/* POLL bits */
#define SI_POLL_VBCPY_SHIFT      0
#define SI_POLL_VBCPY3           (1 << 0)
#define SI_POLL_VBCPY2           (1 << 1)
#define SI_POLL_VBCPY1           (1 << 2)
#define SI_POLL_VBCPY0           (1 << 3)
#define SI_POLL_EN_SHIFT         4
#define SI_POLL_EN3              (1 << 4)
#define SI_POLL_EN2              (1 << 5)
#define SI_POLL_EN1              (1 << 6)
#define SI_POLL_EN0              (1 << 7)
#define SI_POLL_Y_SHIFT          8
#define SI_POLL_Y                (0xff << SI_POLL_Y_SHIFT)
#define SI_POLL_X_SHIFT          16
#define SI_POLL_X                (0x3ff << SI_POLL_X_SHIFT)

/* SR bits */
#define SI_SR_WR                 (1 << 31)
#define SI_RDST(n)               (1 << (5 + ((3-n) * 8)))
#define SI_WRST(n)               (1 << (4 + ((3-n) * 8)))
#define SI_NOREP(n)              (1 << (3 + ((3-n) * 8)))
#define SI_COLL(n)               (1 << (2 + ((3-n) * 8)))
#define SI_OVRUN(n)              (1 << (1 + ((3-n) * 8)))
#define SI_UNRUN(n)              (1 << ((3-n) * 8))


/* https://github.com/dolphin-emu/dolphin/blob/de44626d23a85aa3cc07260f6f97e64f36600652/Source/Core/Core/HW/SI/SI_Device.h#L51 */
/* only including the ones that might actually be used */
#define JOYBUS_CMD_STATUS 0x00
#define JOYBUS_CMD_READ_GBA 0x14
#define JOYBUS_CMD_WRITE_GBA 0x15
#define JOYBUS_CMD_DIRECT 0x40
#define JOYBUS_CMD_DIRECT_KB 0x54
#define JOYBUS_CMD_RESET 0xff

/* https://www.gc-forever.com/yagcd/chap5.html#sec5.8 */
#define SI_MKOUTBUF(cmd, output0, output1) ((((cmd) & 0xff) << 16) | (((output0) & 0xff) << 8) | ((output1) & 0xff))
#define SI_MKPOLL(vbcpy, en, y, x) ((((vbcpy) & 0b1111) << SI_POLL_VBCPY_SHIFT) | (((en) & 0b1111) << SI_POLL_EN_SHIFT) | (((y) & 0xff) << SI_POLL_Y_SHIFT) | (((x) & 0x3ff) << SI_POLL_X_SHIFT))


/*
 * https://www.gc-forever.com/yagcd/chap9.html#sec9.1
 * https://github.com/dolphin-emu/dolphin/blob/de44626d23a85aa3cc07260f6f97e64f36600652/Source/Core/Core/HW/SI/SI_Device.h#L35
 */
enum si_device_id {
	SI_DEVICE_ID_N64_MIC = 0x0001,
	SI_DEVICE_ID_N64_KBD = 0x0002,
	SI_DEVICE_ID_GBA = 0x0004,
	SI_DEVICE_ID_N64_MOUSE = 0x0200,
	SI_DEVICE_ID_N64_CONTROLLER = 0x0500,
	SI_DEVICE_ID_GBA_2 = 0x0800,
	SI_DEVICE_ID_GCN_KBD = 0x0820,
	SI_DEVICE_ID_STANDARD = 0x0900,
	SI_DEVICE_ID_WAVEBIRD_1 = 0xa800,
	SI_DEVICE_ID_WAVEBIRD_2 = 0xe960,
	SI_DEVICE_ID_WAVEBIRD_3 = 0xe9a0,
	SI_DEVICE_ID_WAVEBIRD_4 = 0xebb0
};

enum si_device_type {
	SI_DEVICE_TYPE_NONE = -1,
	SI_DEVICE_TYPE_GCN_CONTROLLER,
	SI_DEVICE_TYPE_N64_CONTROLLER,
	SI_DEVICE_TYPE_GCN_KBD,
	SI_DEVICE_TYPE_N64_KBD,
	SI_DEVICE_TYPE_N64_MOUSE,
	SI_DEVICE_TYPE_N64_MIC,
	SI_DEVICE_TYPE_GBA
};


/* GCN controller direct report */
/* https://www.gc-forever.com/yagcd/chap9.html#sec9.2.2 */
/* 1st word */
#define GCN_CONTROLLER_DIRECT_LSTICK_Y_SHIFT  0
#define GCN_CONTROLLER_DIRECT_LSTICK_Y        (0xff << GCN_CONTROLLER_DIRECT_LSTICK_Y_SHIFT)
#define GCN_CONTROLLER_DIRECT_LSTICK_X_SHIFT  8
#define GCN_CONTROLLER_DIRECT_LSTICK_X        (0xff << GCN_CONTROLLER_DIRECT_LSTICK_X_SHIFT)
#define GCN_CONTROLLER_DIRECT_DPAD_LEFT       (1 << 16)
#define GCN_CONTROLLER_DIRECT_DPAD_RIGHT      (1 << 17)
#define GCN_CONTROLLER_DIRECT_DPAD_DOWN       (1 << 18)
#define GCN_CONTROLLER_DIRECT_DPAD_UP         (1 << 19)
#define GCN_CONTROLLER_DIRECT_Z               (1 << 20)
#define GCN_CONTROLLER_DIRECT_R_TRIG_DIG      (1 << 21)
#define GCN_CONTROLLER_DIRECT_L_TRIG_DIG      (1 << 22)
#define GCN_CONTROLLER_DIRECT_A               (1 << 24)
#define GCN_CONTROLLER_DIRECT_B               (1 << 25)
#define GCN_CONTROLLER_DIRECT_X               (1 << 26)
#define GCN_CONTROLLER_DIRECT_Y               (1 << 27)
#define GCN_CONTROLLER_DIRECT_START           (1 << 28)
#define GCN_CONTROLLER_DIRECT_ERRLATCH        (1 << 30)
#define GCN_CONTROLLER_DIRECT_ERRSTAT         (1 << 31)

/* 2nd word */
#define GCN_CONTROLLER_DIRECT_R_TRIG_AN_SHIFT 0
#define GCN_CONTROLLER_DIRECT_R_TRIG_AN       (0xff << GCN_CONTROLLER_DIRECT_R_TRIG_AN_SHIFT)
#define GCN_CONTROLLER_DIRECT_L_TRIG_AN_SHIFT 8
#define GCN_CONTROLLER_DIRECT_L_TRIG_AN       (0xff << GCN_CONTROLLER_DIRECT_L_TRIG_AN_SHIFT)
#define GCN_CONTROLLER_DIRECT_CSTICK_Y_SHIFT  16
#define GCN_CONTROLLER_DIRECT_CSTICK_Y        (0xff << GCN_CONTROLLER_DIRECT_CSTICK_Y_SHIFT)
#define GCN_CONTROLLER_DIRECT_CSTICK_X_SHIFT  24
#define GCN_CONTROLLER_DIRECT_CSTICK_X        (0xff << GCN_CONTROLLER_DIRECT_CSTICK_X_SHIFT)


static volatile struct si_regs *regs;
static REGISTER_DRIVER(siDrv);
static u64 lastConnectedCheck;
static u16 padIDs[4] = { 0, 0, 0, 0 };
static u32 padButtons[4] = { 0, 0, 0, 0 };
static enum si_device_type deviceTypes[4] = { SI_DEVICE_TYPE_NONE, SI_DEVICE_TYPE_NONE, SI_DEVICE_TYPE_NONE, SI_DEVICE_TYPE_NONE };


static const char *siTypeToStr(enum si_device_type type) {
	switch (type) {
	case SI_DEVICE_TYPE_NONE: return "None";
	case SI_DEVICE_TYPE_GCN_CONTROLLER: return "GameCube Controller";
	case SI_DEVICE_TYPE_GCN_KBD: return "GameCube ASCII Keyboard";
	case SI_DEVICE_TYPE_N64_CONTROLLER: return "N64 Controller";
	case SI_DEVICE_TYPE_N64_MIC: return "N64 Microphone";
	case SI_DEVICE_TYPE_N64_KBD: return "N64 Keyboard";
	case SI_DEVICE_TYPE_N64_MOUSE: return "N64 Mouse";
	default: assert_unreachable(); /* should never be hit, since we control the type directly (it's not the ID) */
	}
}

static void drainInBuf(int chan) {
	vu32 resp;
	resp = regs->chan[chan].inbufh;
	resp = regs->chan[chan].inbufl;
	(void)resp;
}

static void checkConnected(void) {
	u64 startTB;
	u32 resp, poll;
	u16 id;
	u8 status;
	int i, j, tries;
	enum si_device_type type;

	regs->poll = 0;
	regs->sr = SI_SR_WR; /* flush all buffers */
	while (regs->sr & SI_SR_WR);
	drainInBuf(0);
	drainInBuf(1);
	drainInBuf(2);
	drainInBuf(3);
	/* ack everything that's W1C */
	regs->comcsr = SI_COMCSR_TCINT | SI_COMCSR_RDSTINT;

	for (i = 0; i < 4; i++) {
		if (deviceTypes[i] == SI_DEVICE_TYPE_NONE)
			tries = 10; /* SI is _really_ finnicky, sometimes it takes a few tries to get an ID successfully */
		else
			tries = 1; /* don't try so hard to detect disconnect */
		while (tries > 0) {
			/*
			 * Sanitize hardware state.  If we don't do this, probing devices on
			 * sequential ports (e.g. 1 and 2, 2 and 3, 3 and 4) fails for the second
			 * device.
			 */

			/* drain input buffers */
			drainInBuf(0);
			drainInBuf(1);
			drainInBuf(2);
			drainInBuf(3);

			/* clear I/O buffer */
			for (j = 0; j < 0x20; j++)
				regs->buf[j] = 0;

			/* prepare to ask for status */
			regs->chan[i].outbuf = SI_MKOUTBUF(JOYBUS_CMD_STATUS, 0x00, 0x00);
			regs->sr = SI_SR_WR; /* write all buffers */
			while (regs->sr & SI_SR_WR);

			/* actually do the transfer */
			regs->comcsr = SI_COMCSR_TCINT | (1 << SI_COMCSR_OUTLEN_SHIFT) | (i << SI_COMCSR_CHAN_SHIFT) | SI_COMCSR_TSTART;

			/* wait for transfer complete */
			startTB = mftb();
			while (!(regs->comcsr & SI_COMCSR_TCINT)) {
				if (T_HasElapsed(startTB, 100 * 1000)) {
					log_puts("SI transfer is taking way too long, giving up");
					/*
					 * SI input will die until the next connection check, since we
					 * killed SIPOLL, but we can't really do much better since we
					 * don't actually have a coherent picture of what's on the bus
					 * anymore...
					 */
					return;
				}
			}

			/* read response and extract out useful info */
			resp = regs->buf[0];
			id = resp >> 16;
			status = resp >> 8;
			(void)status;
			padIDs[i] = id;

			/*
			 * determine what device this is based on it's ID
			 * FIXME: this kinda sucks, the ID is supposed to be
			 * a bitfield but it's defined really poorly so this is
			 * kinda the best that can be done
			 */
			switch (id) {
			case SI_DEVICE_ID_STANDARD:
			case SI_DEVICE_ID_WAVEBIRD_1:
			case SI_DEVICE_ID_WAVEBIRD_2:
			case SI_DEVICE_ID_WAVEBIRD_3:
			case SI_DEVICE_ID_WAVEBIRD_4: {
				type = SI_DEVICE_TYPE_GCN_CONTROLLER;
				break;
			}
			case SI_DEVICE_ID_GBA:
			case SI_DEVICE_ID_GBA_2: {
				type = SI_DEVICE_TYPE_GBA;
				break;
			}
			case SI_DEVICE_ID_GCN_KBD: {
				type = SI_DEVICE_TYPE_GCN_KBD;
				break;
			}
			case SI_DEVICE_ID_N64_CONTROLLER: {
				type = SI_DEVICE_TYPE_N64_CONTROLLER;
				break;
			}
			case SI_DEVICE_ID_N64_MIC: {
				type = SI_DEVICE_TYPE_N64_MIC;
				break;
			}
			case SI_DEVICE_ID_N64_KBD: {
				type = SI_DEVICE_TYPE_N64_KBD;
				break;
			}
			case SI_DEVICE_ID_N64_MOUSE: {
				type = SI_DEVICE_TYPE_N64_MOUSE;
				break;
			}
			case 0x0000:
			case 0xffff: {
				type = SI_DEVICE_TYPE_NONE;
				break;
			}
			default: {
				log_printf("Unknown device ID 0x%04x on Port %d\r\n", id, i + 1);
				type = SI_DEVICE_TYPE_NONE;
			}
			}

			/* did we get a device connection / disconnection?  if so, we're done with this port */
			if (deviceTypes[i] != SI_DEVICE_TYPE_NONE && type == SI_DEVICE_TYPE_NONE) {
				log_printf("Device disconnected from Port %d\r\n", i + 1);
				goto out;
			}
			else if (deviceTypes[i] == SI_DEVICE_TYPE_NONE && type != SI_DEVICE_TYPE_NONE) {
				log_printf("Device connected to Port %d: %s\r\n", i + 1, siTypeToStr(type));
				goto out;
			}

			/* nothing, continue trying */
			tries--;
			continue;

		out:
			deviceTypes[i] = type;
			/* ack everything that's W1C */
			regs->comcsr = SI_COMCSR_TCINT | SI_COMCSR_RDSTINT;
			break;
		}
	}

	/*
	 * Set up polling for all GCN controllers
	 * TODO: something for the other devices?
	 */
	poll = SI_MKPOLL(0, 0, 1, 7);
	if (deviceTypes[0] == SI_DEVICE_TYPE_GCN_CONTROLLER) {
		/*
		 * FIXME: where does the 0x03 come from here and what does it mean?
		 * YAGCD, Linux, and ppcskel/Gumboot all use it, so it must be
		 * important, but I can't find what it actually _means_.
		 */
		regs->chan[0].outbuf = SI_MKOUTBUF(JOYBUS_CMD_DIRECT, 0x03, 0x00);
		poll |= SI_POLL_EN0;
	}
	if (deviceTypes[1] == SI_DEVICE_TYPE_GCN_CONTROLLER) {
		regs->chan[1].outbuf = SI_MKOUTBUF(JOYBUS_CMD_DIRECT, 0x03, 0x00);
		poll |= SI_POLL_EN1;
	}
	if (deviceTypes[2] == SI_DEVICE_TYPE_GCN_CONTROLLER) {
		regs->chan[2].outbuf = SI_MKOUTBUF(JOYBUS_CMD_DIRECT, 0x03, 0x00);
		poll |= SI_POLL_EN2;
	}
	if (deviceTypes[3] == SI_DEVICE_TYPE_GCN_CONTROLLER) {
		regs->chan[3].outbuf = SI_MKOUTBUF(JOYBUS_CMD_DIRECT, 0x03, 0x00);
		poll |= SI_POLL_EN3;
	}

	regs->poll = poll;
	regs->sr = SI_SR_WR; /* write all buffers */
	while (regs->sr & SI_SR_WR);
}

static void probePad(int chan) {
	u32 inbufh, inbufl, buttons, held, pressed, released, idle;

	inbufh = regs->chan[chan].inbufh; /* buttons, main stick */
	inbufl = regs->chan[chan].inbufl; /* c stick, L trigger, R trigger */
	(void)inbufl;
	buttons = inbufh & 0xffff0000; /* ignore lstick */

	/* determine state transitions of buttons */
	held = buttons & padButtons[chan];
	pressed = buttons & ~padButtons[chan];
	released = padButtons[chan] & ~buttons;
	idle = ~(buttons | padButtons[chan]);

	/* TODO: do something with idle/released/held */
	(void)idle;
	(void)released;
	(void)held;

	/* save state for next time */
	padButtons[chan] = buttons;

	if (pressed & GCN_CONTROLLER_DIRECT_A)
		IN_NewEvent(INPUT_EV_SELECT);
	if (pressed & GCN_CONTROLLER_DIRECT_DPAD_UP)
		IN_NewEvent(INPUT_EV_UP);
	if (pressed & GCN_CONTROLLER_DIRECT_DPAD_DOWN)
		IN_NewEvent(INPUT_EV_DOWN);
	if (pressed & GCN_CONTROLLER_DIRECT_DPAD_LEFT)
		IN_NewEvent(INPUT_EV_LEFT);
	if (pressed & GCN_CONTROLLER_DIRECT_DPAD_RIGHT)
		IN_NewEvent(INPUT_EV_RIGHT);

}

static void siCallback(void) {
	int i;

	/* re-probe the ports to find connections/disconnections periodically */
	if (T_HasElapsed(lastConnectedCheck, 250 * 1000)) {
		checkConnected();
		lastConnectedCheck = mftb();
	}

	/* Probe inputs */
	for (i = 0; i < 4; i++) {
		if (deviceTypes[i] == SI_DEVICE_TYPE_GCN_CONTROLLER)
			probePad(i);
	}
}

static void siInit(void) {
	int i;

	/* figure out where the SI regs are actually at on this hardware */
	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE: {
		regs = (struct si_regs *)0xcc006400;
		break;
	}
	case CONSOLE_TYPE_WII:
	case CONSOLE_TYPE_WII_U: {
		regs = (struct si_regs *)0xcd806400;
		break;
	}
	default:
		break;
	}

	/* register our callback */
	D_AddCallback(siCallback);

	/*
	 * Reset and sanitize everything
	 */
	regs->chan[0].outbuf = 0;
	regs->chan[1].outbuf = 0;
	regs->chan[2].outbuf = 0;
	regs->chan[3].outbuf = 0;
	regs->poll = 0;
	regs->sr = 0;
	/* ack everything that's W1C and clear the rest */
	regs->comcsr = SI_COMCSR_TCINT | SI_COMCSR_RDSTINT;
	/* clear the I/O buffer */
	for (i = 0; i < 0x20; i++)
		regs->buf[i] = 0;
	/* drain input buffers */
	drainInBuf(0);
	drainInBuf(1);
	drainInBuf(2);
	drainInBuf(3);

	/* initial check which controllers are connected */
	checkConnected();
	lastConnectedCheck = mftb();

	/* we're all good */
	siDrv.state = DRIVER_STATE_READY;
}

static REGISTER_DRIVER(siDrv) = {
	.name = "Serial Interface",
	.mask = DRIVER_ALLOW_ALL,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_INPUT,
	.init = siInit,
	.cleanup = NULL
};
