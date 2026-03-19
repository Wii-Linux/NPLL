/*
 * NPLL - Flipper/Hollywood/Latte Hardware - Serial Interface
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "SI"

#include <assert.h>
#include <stdio.h>
#include <string.h>
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
#define SI_SR_RDST(n)            (1 << (5 + ((3-n) * 8)))
#define SI_SR_WRST(n)            (1 << (4 + ((3-n) * 8)))
#define SI_SR_NOREP(n)           (1 << (3 + ((3-n) * 8)))
#define SI_SR_COLL(n)            (1 << (2 + ((3-n) * 8)))
#define SI_SR_OVRUN(n)           (1 << (1 + ((3-n) * 8)))
#define SI_SR_UNRUN(n)           (1 << ((3-n) * 8))


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

/*
 * GCN Controller analog treshholds
 */
#define GCN_STICK_CENTER 128
#define GCN_STICK_DEADZONE 64
#define GCN_STICK_MIN_THRESH 32
#define GCN_STICK_MAX_THRES 224
#define GCN_TRIGGER_MIN_THRESH 32
#define GCN_TRIGGER_MAX_THRESH 224

struct gcn_pad_state {
	u16 buttons;
	u8 lstickX;
	u8 lstickY;
	u8 cstickX;
	u8 cstickY;
	u8 ltrig;
	u8 rtrig;
};

struct si_device_state {
	u16 id;
	enum si_device_type type;
	union {
		struct gcn_pad_state gcn_pad;
		#if 0 /* for whenever I actually implement these */
		struct gcn_kbd_state gcn_kbd;
		struct n64_pad_state n64_pad;
		struct n64_kbd_state n64_kbd;
		struct n64_mouse_state n64_mouse;
		struct gba_state gba;
		#endif
	};
};

static volatile struct si_regs *regs;
static REGISTER_DRIVER(siDrv);
static u64 lastConnectedCheck;
static struct si_device_state devices[4];

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
	/*
	 * need to drain the input buffer; see YAGCD Sect. 5.8:
	 * > SIC0INBUFH and SIC0INBUFL are double buffered to prevent inconsistent data reads due to main processor conflicting with incoming serial interface data. To insure data read from SIC0INBUFH and SIC0INFUBL are consistent, a locking mechanism prevents the double buffer from copying new data to these registers. Once SIC0INBUFH is read, both SIC0INBUFH and SIC0INBUFL are `locked` until SIC0INBUFL is read. While the buffers are `locked`, new data is not copied into the buffers. When SIC0INBUFL is read, the buffers become unlocked again.
	 */
	resp = regs->chan[chan].inbufh;
	resp = regs->chan[chan].inbufl;
	(void)resp;
}
static void drainAllInBuf(void) {
	int i;
	for (i = 0; i < 4; i++)
		drainInBuf(i);
}

static void checkConnected(void) {
	u64 startTB;
	u32 resp, poll, sr;
	u16 id;
	u8 status;
	int i, j, tries;
	enum si_device_type type;

	regs->poll = 0;
	regs->sr = SI_SR_WR; /* flush all buffers */
	while (regs->sr & SI_SR_WR);
	drainAllInBuf();

	for (i = 0; i < 4; i++) {
		if (devices[i].type == SI_DEVICE_TYPE_NONE)
			tries = 10; /* SI is _really_ finnicky, especially when a controller has just be connected - sometimes it takes a few tries to get an ID successfully */
		else
			tries = 1; /* only briefly try to detect disconnect - if a device is connected, it's far more likely that it is still connected than not */
		while (tries > 0) {
			/*
			 * Sanitize hardware state.  If we don't do this, probing devices on
			 * sequential ports (e.g. 1 and 2, 2 and 3, 3 and 4) fails for the second
			 * device.
			 */

			/* ack everything that's W1C */
			regs->comcsr = SI_COMCSR_TCINT | SI_COMCSR_RDSTINT;

			/* drain input buffers */
			drainAllInBuf();

			/* clear I/O buffer */
			for (j = 0; j < 0x20; j++)
				regs->buf[j] = 0;

			/* prepare to ask for status */
			regs->chan[i].outbuf = SI_MKOUTBUF(JOYBUS_CMD_STATUS, 0x00, 0x00);
			regs->sr = SI_SR_WR; /* write all buffers */
			while (regs->sr & SI_SR_WR);

			/* actually do the transfer */
			regs->comcsr = (1 << SI_COMCSR_OUTLEN_SHIFT) | (3 << SI_COMCSR_INLEN_SHIFT) | (i << SI_COMCSR_CHAN_SHIFT) | SI_COMCSR_TSTART;

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
					 *
					 * Not worth retrying immediately, it'll certainly just fail again.
					 * Need to at least give it until the next check to try again.
					 * TODO: Maybe even give up trying alltogether if it just keeps failing
					 */
					return;
				}
			}

			if (regs->comcsr & SI_COMCSR_COMERR) {
				sr = regs->sr;
				/*
				 * NOREP ocurrs every time to try to talk to an empty port,
				 * so don't log the condition unless we have some other error.
				 */
				if ((sr & SI_SR_COLL(i)) || (sr & SI_SR_OVRUN(i)) || (sr & SI_SR_UNRUN(i))) {
					log_printf("error probing: [%c] No Response; [%c] Collision; [%c] OverRun; [%c] UnderRun\r\n",
						(sr & SI_SR_NOREP(i)) ? 'x' : ' ',
						(sr & SI_SR_COLL(i)) ? 'x' : ' ',
						(sr & SI_SR_OVRUN(i)) ? 'x' : ' ',
						(sr & SI_SR_UNRUN(i)) ? 'x' : ' ');

					/* ack all errors and carry on */
					regs->sr = SI_SR_NOREP(i) | SI_SR_COLL(i) | SI_SR_OVRUN(i) | SI_SR_UNRUN(i);
					goto fail;
				}
				else {
					/* ack all errors and carry on */
					regs->sr = SI_SR_NOREP(i) | SI_SR_COLL(i) | SI_SR_OVRUN(i) | SI_SR_UNRUN(i);

					/*
					 * Either getting an empty ID or NOREP means the device disconnected,
					 * so try to handle them in the same path
					 */
					type = SI_DEVICE_TYPE_NONE;
					id = 0xffff;
					goto gotType;
				}
			}


			/* read response and extract out useful info */
			resp = regs->buf[0];
			id = resp >> 16;
			status = resp >> 8;
			(void)status;
			devices[i].id = id;

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

		gotType:
			/* did we get a device connection / disconnection?  if so, we're done with this port */
			if (devices[i].type != SI_DEVICE_TYPE_NONE && type == SI_DEVICE_TYPE_NONE) {
				log_printf("Device disconnected from Port %d\r\n", i + 1);
				goto out;
			}
			else if (devices[i].type == SI_DEVICE_TYPE_NONE && type != SI_DEVICE_TYPE_NONE) {
				log_printf("Device connected to Port %d: %s\r\n", i + 1, siTypeToStr(type));
				goto out;
			}

		fail:
			/* nothing, continue trying */
			tries--;
			continue;

		out:
			devices[i].type = type;
			break;
		}
	}

	/*
	 * Set up polling for all GCN controllers, don't send anything to other devices (yet)
	 * TODO: figure out how to get input from the other devices?
	 */
	poll = SI_MKPOLL(0, 0, 1, 7);
	for (i = 0; i < 4; i++) {
		if (devices[i].type == SI_DEVICE_TYPE_GCN_CONTROLLER) {
			/*
			 * FIXME: where does the 0x03 come from here and what does it mean?
			 * YAGCD, Linux, and ppcskel/Gumboot all use it, so it must be
			 * important, but I can't find what it actually _means_.
			 */
			regs->chan[i].outbuf = SI_MKOUTBUF(JOYBUS_CMD_DIRECT, 0x03, 0x00);
			poll |= (1 << (SI_POLL_EN_SHIFT + (3 - i)));
		}
	}

	regs->poll = poll;
	regs->sr = SI_SR_WR; /* write all buffers */
	while (regs->sr & SI_SR_WR);

	/* ack everything that's W1C */
	regs->comcsr = SI_COMCSR_TCINT | SI_COMCSR_RDSTINT;
}

static void probeGCNPad(int chan) {
	u32 inbufh, inbufl, prevButtons, buttons, pressed;
	u8 lstickX, lstickY, prevLstickX, prevLstickY, cstickX, cstickY, ltrig, rtrig;

	inbufh = regs->chan[chan].inbufh; /* buttons, main stick */
	inbufl = regs->chan[chan].inbufl; /* c stick, L trigger, R trigger */
	prevButtons = devices[chan].gcn_pad.buttons << 16;
	prevLstickX = devices[chan].gcn_pad.lstickX;
	prevLstickY = devices[chan].gcn_pad.lstickY;

	/* unpack report */
	buttons = inbufh & 0xffff0000; /* ignore lstick */
	lstickX = (inbufh & GCN_CONTROLLER_DIRECT_LSTICK_X) >> GCN_CONTROLLER_DIRECT_LSTICK_X_SHIFT;
	lstickY = (inbufh & GCN_CONTROLLER_DIRECT_LSTICK_Y) >> GCN_CONTROLLER_DIRECT_LSTICK_Y_SHIFT;
	cstickX = (inbufl & GCN_CONTROLLER_DIRECT_CSTICK_X) >> GCN_CONTROLLER_DIRECT_CSTICK_X_SHIFT;
	cstickY = (inbufl & GCN_CONTROLLER_DIRECT_CSTICK_Y) >> GCN_CONTROLLER_DIRECT_CSTICK_Y_SHIFT;
	ltrig = (inbufl & GCN_CONTROLLER_DIRECT_L_TRIG_AN) >> GCN_CONTROLLER_DIRECT_L_TRIG_AN_SHIFT;
	rtrig = (inbufl & GCN_CONTROLLER_DIRECT_R_TRIG_AN) >> GCN_CONTROLLER_DIRECT_R_TRIG_AN_SHIFT;

	/* determine state transitions of buttons */
	pressed = buttons & ~prevButtons;
	#if 0 /* if we ever have a need for these */
	held = buttons & prevButtons;
	released = prevButtons & ~buttons;
	idle = ~(buttons | prevButtons);
	#endif

	/* TODO: repeat if held for a certain amount of time */
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

	if (lstickY > GCN_STICK_CENTER + GCN_STICK_DEADZONE && prevLstickY < GCN_STICK_CENTER + GCN_STICK_DEADZONE)
		IN_NewEvent(INPUT_EV_UP);
	else if (lstickY < GCN_STICK_CENTER - GCN_STICK_DEADZONE && prevLstickY > GCN_STICK_CENTER - GCN_STICK_DEADZONE)
		IN_NewEvent(INPUT_EV_DOWN);
	if (lstickX > GCN_STICK_CENTER + GCN_STICK_DEADZONE && prevLstickX < GCN_STICK_CENTER + GCN_STICK_DEADZONE)
		IN_NewEvent(INPUT_EV_RIGHT);
	else if (lstickX < GCN_STICK_CENTER - GCN_STICK_DEADZONE && prevLstickX > GCN_STICK_CENTER - GCN_STICK_DEADZONE)
		IN_NewEvent(INPUT_EV_LEFT);

	/* save state for next time */
	devices[chan].gcn_pad.buttons = (u16)(buttons >> 16);
	devices[chan].gcn_pad.lstickX = lstickX;
	devices[chan].gcn_pad.lstickY = lstickY;
	devices[chan].gcn_pad.cstickX = cstickX;
	devices[chan].gcn_pad.cstickY = cstickY;
	devices[chan].gcn_pad.ltrig = ltrig;
	devices[chan].gcn_pad.rtrig = rtrig;
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
		switch (devices[i].type) {
		case SI_DEVICE_TYPE_GCN_CONTROLLER: {
			probeGCNPad(i);
			break;
		}
		default:
			break;
		}
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
		assert_unreachable();
	}

	/* register our callback */
	D_AddCallback(siCallback);

	/*
	 * Reset and sanitize everything
	 */
	for (i = 0; i < 4; i++)
		regs->chan[i].outbuf = 0;
	regs->poll = 0;
	/* ack everything that's W1C and clear the rest */
	regs->sr = SI_SR_NOREP(0) | SI_SR_COLL(0) | SI_SR_OVRUN(0) | SI_SR_UNRUN(0) |
		   SI_SR_NOREP(1) | SI_SR_COLL(1) | SI_SR_OVRUN(1) | SI_SR_UNRUN(1) |
		   SI_SR_NOREP(2) | SI_SR_COLL(2) | SI_SR_OVRUN(2) | SI_SR_UNRUN(2) |
		   SI_SR_NOREP(3) | SI_SR_COLL(3) | SI_SR_OVRUN(3) | SI_SR_UNRUN(3);
	regs->comcsr = SI_COMCSR_TCINT | SI_COMCSR_RDSTINT;
	/* clear the I/O buffer */
	for (i = 0; i < 0x20; i++)
		regs->buf[i] = 0;
	/* drain input buffers */
	drainAllInBuf();

	/* cleanup device state */
	memset(devices, 0, sizeof(devices));
	for (i = 0; i < 4; i++)
		devices[i].type = SI_DEVICE_TYPE_NONE;

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
