/*
 * NPLL - internal BCM2045A Bluetooth / Wii Remote input
 *
 * Intentionally small BT stack, no pairing, etc.  Good enough
 * only to connect to known Wiimotes.
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "usb-bt"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <npll/console.h>
#include <npll/drivers.h>
#include <npll/endian.h>
#include <npll/input.h>
#include <npll/log.h>
#include <npll/timer.h>
#include <npll/usb.h>
#include <npll/wiimote.h>

#define BT_VENDOR_NINTENDO  0x057eu
#define BT_PRODUCT_INTERNAL 0x0305u

#define BT_POLL_US            10000u
#define BT_CONNECT_POLL_US    1000000u
#define BT_CONNECT_WINDOW_US  15000000u
#define BT_EVENT_SIZE         260u
#define BT_ACL_SIZE           1024u
#define BT_MAX_REMOTES        4u
#define BT_COMMAND_QUEUE      8u
#define BT_COMMAND_TIMEOUT_US 2000000u

#define HCI_COMMAND_PKT 0x01u
#define HCI_ACL_PKT     0x02u

#define HCI_OP(ogf, ocf) ((u16)(((ogf) << 10) | (ocf)))
#define HCI_RESET                 HCI_OP(3, 0x0003)
#define HCI_SET_EVENT_MASK        HCI_OP(3, 0x0001)
#define HCI_WRITE_SCAN_ENABLE     HCI_OP(3, 0x001a)
#define HCI_READ_STORED_LINK_KEY  HCI_OP(3, 0x000d)
#define HCI_READ_BD_ADDR          HCI_OP(4, 0x0009)
#define HCI_CREATE_CONNECTION     HCI_OP(1, 0x0005)
#define HCI_ACCEPT_CONNECTION     HCI_OP(1, 0x0009)
#define HCI_REJECT_CONNECTION     HCI_OP(1, 0x000a)
#define HCI_LINK_KEY_REPLY        HCI_OP(1, 0x000b)
#define HCI_LINK_KEY_NEG_REPLY    HCI_OP(1, 0x000c)
#define HCI_PIN_CODE_NEG_REPLY    HCI_OP(1, 0x000e)
#define HCI_AUTH_REQUESTED        HCI_OP(1, 0x0011)
#define HCI_SET_CONN_ENCRYPT      HCI_OP(1, 0x0013)
#define HCI_DISCONNECT            HCI_OP(1, 0x0006)

#define HCI_EV_CONNECTION_COMPLETE    0x03u
#define HCI_EV_CONNECTION_REQUEST     0x04u
#define HCI_EV_DISCONNECTION_COMPLETE 0x05u
#define HCI_EV_AUTH_COMPLETE          0x06u
#define HCI_EV_ENCRYPT_CHANGE         0x08u
#define HCI_EV_COMMAND_COMPLETE       0x0eu
#define HCI_EV_COMMAND_STATUS         0x0fu
#define HCI_EV_RETURN_LINK_KEYS       0x15u
#define HCI_EV_PIN_CODE_REQUEST       0x16u
#define HCI_EV_LINK_KEY_REQUEST       0x17u

#define L2CAP_CID_SIGNALING  0x0001u
#define L2CAP_PSM_HID_CTRL   0x0011u
#define L2CAP_PSM_HID_INTR   0x0013u
#define L2CAP_CONN_REQ       0x02u
#define L2CAP_CONN_RSP       0x03u
#define L2CAP_CONFIG_REQ     0x04u
#define L2CAP_CONFIG_RSP     0x05u
#define L2CAP_DISCONN_REQ    0x06u
#define L2CAP_DISCONN_RSP    0x07u
#define L2CAP_INFO_REQ       0x0au
#define L2CAP_INFO_RSP       0x0bu

#define WM_BTN_A     0x0008u
#define WM_BTN_HOME  0x0080u
#define WM_BTN_LEFT  0x0100u
#define WM_BTN_RIGHT 0x0200u
#define WM_BTN_DOWN  0x0400u
#define WM_BTN_UP    0x0800u
#define WM_REPEAT_DELAY_US  400000u
#define WM_REPEAT_PERIOD_US 100000u

enum btInitState {
	BT_INIT_RESET,
	BT_INIT_EVENT_MASK,
	BT_INIT_SCAN,
	BT_INIT_ADDRESS,
	BT_INIT_KEYS,
	BT_INIT_READY
};

struct btLinkKey {
	u8 bdaddr[6];
	u8 key[16];
};

struct btChannel {
	u16 localCID;
	u16 remoteCID;
	bool connected;
	bool configured;
};

struct btRemote {
	bool used;
	u8 bdaddr[6];
	u16 handle;
	struct btChannel control;
	struct btChannel interrupt;
	u16 buttons;
	u16 repeatButton;
	u64 repeatStarted;
	u64 lastRepeat;
	u8 signalId;
	u8 acl[BT_ACL_SIZE];
	u16 aclExpected;
	u16 aclLength;
};

struct btAdapter {
	struct usbInterface *interface;
	struct usbEndpoint *eventIn, *aclIn, *aclOut;
	enum btInitState init;
	u16 pendingOpcode;
	u64 pendingAt;
	struct {
		u16 opcode;
		u8 length;
		u8 parameters[32];
	} commandQueue[BT_COMMAND_QUEUE];
	u8 commandHead, commandCount;
	u8 localAddress[6];
	struct btLinkKey linkKeys[WIIMOTE_MAX_PAIRINGS];
	uint linkKeyCount;
	u64 readyAt;
	u64 lastConnectPoll;
	uint nextPairing;
	bool pagePending;
	u8 pageAddress[6];
	struct btRemote remotes[BT_MAX_REMOTES];
};

struct hciCommandHeader {
	u16 opcode;
	u8 length;
} __attribute__((packed));

struct hciACLHeader {
	u16 handleFlags;
	u16 length;
} __attribute__((packed));

struct l2capHeader {
	u16 length;
	u16 cid;
} __attribute__((packed));

static REGISTER_DRIVER(btTopDriver);
static struct btAdapter adapter;
static bool pairingKnown(const u8 *address);
static void hciServiceQueue(void);

static u16 getLE16(const void *ptr) {
	const u8 *p = ptr;
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static void putLE16(void *ptr, u16 value) {
	u8 *p = ptr;

	p[0] = (u8)value;
	p[1] = (u8)(value >> 8);
}

static bool sameAddress(const u8 *a, const u8 *b) {
	return !memcmp(a, b, 6);
}

/* BT.DINF stores the conventional MSB-first spelling; HCI uses LSB first. */
static void pairingHCIAddress(const struct wiimotePairing *pairing, u8 address[6]) {
	uint i;

	for (i = 0; i < 6; i++)
		address[i] = pairing->bdaddr[5u - i];
}

static struct btRemote *remoteByHandle(u16 handle) {
	uint i;

	for (i = 0; i < BT_MAX_REMOTES; i++) {
		if (adapter.remotes[i].used && adapter.remotes[i].handle == handle)
			return &adapter.remotes[i];
	}

	return NULL;
}

static struct btRemote *remoteByAddress(const u8 *address) {
	uint i;

	for (i = 0; i < BT_MAX_REMOTES; i++) {
		if (adapter.remotes[i].used && sameAddress(adapter.remotes[i].bdaddr, address))
			return &adapter.remotes[i];
	}

	return NULL;
}

static struct btRemote *allocateRemote(const u8 *address, u16 handle) {
	struct btRemote *remote = remoteByAddress(address);
	uint i;

	if (!remote) {
		for (i = 0; i < BT_MAX_REMOTES; i++) {
			if (!adapter.remotes[i].used) {
				remote = &adapter.remotes[i];
				memset(remote, 0, sizeof(*remote));
				remote->used = true;
				memcpy(remote->bdaddr, address, 6);
				remote->control.localCID = (u16)(0x0040u + (i * 2u));
				remote->interrupt.localCID = (u16)(remote->control.localCID + 1u);
				remote->signalId = 1;
				break;
			}
		}
	}

	if (remote)
		remote->handle = handle;

	return remote;
}

static int hciCommandNow(u16 opcode, const void *parameters, u8 length) {
	u8 packet[35] ALIGN(32);
	struct hciCommandHeader *header = (struct hciCommandHeader *)packet;
	int ret;

	if (!adapter.interface || adapter.pendingOpcode || length > 32u)
		return -EBUSY;

	header->opcode = npll_cpu_to_le16(opcode);
	header->length = length;
	if (length)
		memcpy(packet + sizeof(*header), parameters, length);

	ret = USB_ControlTransfer(adapter.interface->device,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE, 0, 0, 0,
		packet, (u16)(sizeof(*header) + length), 1000000u
	);
	if (ret < 0)
		return ret;
	if (ret != (int)(sizeof(*header) + length))
		return -EIO;

	adapter.pendingOpcode = opcode;
	adapter.pendingAt = mftb();
	return 0;
}

static int hciCommand(u16 opcode, const void *parameters, u8 length) {
	uint tail;

	if (length > sizeof(adapter.commandQueue[0].parameters))
		return -EINVAL;

	if (!adapter.pendingOpcode)
		return hciCommandNow(opcode, parameters, length);

	if (adapter.commandCount >= BT_COMMAND_QUEUE)
		return -ENOSPC;

	tail = ((uint)adapter.commandHead + adapter.commandCount) % BT_COMMAND_QUEUE;
	adapter.commandQueue[tail].opcode = opcode;
	adapter.commandQueue[tail].length = length;
	if (length)
		memcpy(adapter.commandQueue[tail].parameters, parameters, length);
	adapter.commandCount++;
	return 0;
}

static void hciServiceQueue(void) {
	uint head;
	int ret;

	if (adapter.pendingOpcode || !adapter.commandCount)
		return;

	head = adapter.commandHead;
	ret = hciCommandNow(adapter.commandQueue[head].opcode, adapter.commandQueue[head].parameters, adapter.commandQueue[head].length);
	if (ret)
		return;

	adapter.commandHead = (u8)((head + 1u) % BT_COMMAND_QUEUE);
	adapter.commandCount--;
}

static int hciAddressCommand(u16 opcode, const u8 *address) {
	return hciCommand(opcode, address, 6);
}

static const struct btLinkKey *findLinkKey(const u8 *address) {
	uint i;

	for (i = 0; i < adapter.linkKeyCount; i++) {
		if (sameAddress(adapter.linkKeys[i].bdaddr, address))
			return &adapter.linkKeys[i];
	}

	return NULL;
}

static int replyLinkKey(const u8 *address, const u8 *key) {
	u8 parameters[22];

	memcpy(parameters, address, 6);
	memcpy(parameters + 6, key, 16);
	return hciCommand(HCI_LINK_KEY_REPLY, parameters, sizeof(parameters));
}

static int aclSend(struct btRemote *remote, u16 cid, const void *payload, u16 length) {
	u8 packet[sizeof(struct hciACLHeader) + sizeof(struct l2capHeader) + 128] ALIGN(32);
	struct hciACLHeader *acl = (struct hciACLHeader *)packet;
	struct l2capHeader *l2 = (struct l2capHeader *)(packet + sizeof(*acl));
	u32 actual;

	if (!remote || !adapter.aclOut || length > 128u)
		return -EINVAL;

	acl->handleFlags = npll_cpu_to_le16((u16)(remote->handle | 0x2000u));
	acl->length = npll_cpu_to_le16((u16)(sizeof(*l2) + length));

	l2->length = npll_cpu_to_le16(length);
	l2->cid = npll_cpu_to_le16(cid);

	if (length)
		memcpy(packet + sizeof(*acl) + sizeof(*l2), payload, length);

	return USB_BulkTransfer(adapter.interface->device, adapter.aclOut, packet, (u32)(sizeof(*acl) + sizeof(*l2) + length), &actual, 1000000u);
}

static int signalSend(struct btRemote *remote, u8 code, u8 id,
	const void *parameters, u16 length) {
	u8 packet[28];

	if (length > 24u)
		return -EINVAL;
	packet[0] = code;
	packet[1] = id;
	putLE16(packet + 2, length);
	if (length)
		memcpy(packet + 4, parameters, length);

	return aclSend(remote, L2CAP_CID_SIGNALING, packet, (u16)(4u + length));
}

static void sendConfigRequest(struct btRemote *remote, struct btChannel *channel) {
	u8 params[4];

	putLE16(params, channel->remoteCID);
	putLE16(params + 2, 0);
	signalSend(remote, L2CAP_CONFIG_REQ, remote->signalId++, params, sizeof(params));
}

static void connectChannel(struct btRemote *remote, struct btChannel *channel, u16 psm) {
	u8 params[4];

	if (channel->remoteCID)
		return;

	putLE16(params, psm);
	putLE16(params + 2, channel->localCID);
	signalSend(remote, L2CAP_CONN_REQ, remote->signalId++, params, sizeof(params));
}

static void wiimoteConfigure(struct btRemote *remote) {
	u8 mode[] = { 0x52, 0x12, 0x00, 0x30 };
	u8 leds[] = { 0x52, 0x11, 0x10 };
	uint slot = (uint)(remote - adapter.remotes);

	if (!remote->control.configured || !remote->interrupt.configured)
		return;

	leds[2] = (u8)(0x10u << slot);
	aclSend(remote, remote->control.remoteCID, leds, sizeof(leds));
	aclSend(remote, remote->control.remoteCID, mode, sizeof(mode));
}

static inputEvent_t buttonAction(u16 button) {
	switch (button) {
	case WM_BTN_UP: return INPUT_EV_UP;
	case WM_BTN_DOWN: return INPUT_EV_DOWN;
	case WM_BTN_LEFT: return INPUT_EV_LEFT;
	case WM_BTN_RIGHT: return INPUT_EV_RIGHT;
	case WM_BTN_A: return INPUT_EV_SELECT;
	case WM_BTN_HOME: return INPUT_EV_SCREENSHOT;
	default: return 0;
	}
}

static u16 firstButton(u16 buttons) {
	static const u16 order[] = { WM_BTN_UP, WM_BTN_DOWN, WM_BTN_LEFT, WM_BTN_RIGHT, WM_BTN_A, WM_BTN_HOME };
	uint i;

	for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		if (buttons & order[i])
			return order[i];
	}

	return 0;
}

static void wiimoteInput(struct btRemote *remote, const u8 *data, u16 length) {
	u16 buttons, pressed, button;
	u64 now = mftb();

	if (length < 4 || data[0] != 0xa1)
		return;

	/* every input report begins with report ID then the two button bytes */
	buttons = (u16)(((u16)data[2] << 8) | data[3]);
	pressed = (u16)(buttons & (u16)~remote->buttons);
	button = firstButton(pressed);
	#if 0
	if (pressed)
		log_printf("pressed=%04x button=%04x\r\n", pressed, button);
	#endif
	if (button) {
		IN_NewEvent(buttonAction(button));
		remote->repeatButton = button;
		remote->repeatStarted = now;
		remote->lastRepeat = now;
	}
	else if (!(buttons & remote->repeatButton)) {
		remote->repeatButton = firstButton((u16)(buttons & (WM_BTN_UP | WM_BTN_DOWN | WM_BTN_LEFT | WM_BTN_RIGHT)));
		remote->repeatStarted = now;
		remote->lastRepeat = now;
	}
	else if (T_HasElapsed(remote->repeatStarted, WM_REPEAT_DELAY_US) && T_HasElapsed(remote->lastRepeat, WM_REPEAT_PERIOD_US)) {
		remote->lastRepeat = now;
		IN_NewEvent(buttonAction(remote->repeatButton));
	}

	remote->buttons = buttons;
}

static struct btChannel *channelByLocal(struct btRemote *remote, u16 cid) {
	if (remote->control.localCID == cid)
		return &remote->control;

	if (remote->interrupt.localCID == cid)
		return &remote->interrupt;

	return NULL;
}

static struct btChannel *channelForPSM(struct btRemote *remote, u16 psm) {
	if (psm == L2CAP_PSM_HID_CTRL)
		return &remote->control;

	if (psm == L2CAP_PSM_HID_INTR)
		return &remote->interrupt;

	return NULL;
}

static void l2capSignal(struct btRemote *remote, const u8 *data, u16 length) {
	u8 code, id, response[12];
	const u8 *p;
	u16 commandLength, psm, remoteCID;
	struct btChannel *channel;

	while (length >= 4u) {
		code = data[0];
		id = data[1];
		commandLength = getLE16(data + 2);
		p = data + 4;

		if (commandLength > (u16)(length - 4u))
			return;

		switch (code) {
		case L2CAP_CONN_REQ: {
			if (commandLength >= 4u) {
				psm = getLE16(p);
				remoteCID = getLE16(p + 2);
				channel = channelForPSM(remote, psm);

				putLE16(response, channel ? channel->localCID : 0);
				putLE16(response + 2, remoteCID);
				putLE16(response + 4, channel ? 0 : 2);
				putLE16(response + 6, 0);
				signalSend(remote, L2CAP_CONN_RSP, id, response, 8);

				if (channel) {
					channel->remoteCID = remoteCID;
					channel->connected = true;
					sendConfigRequest(remote, channel);
				}
			}
			break;
		}
		case L2CAP_CONN_RSP: {
			if (commandLength >= 8 && !getLE16(p + 4)) {
				channel = channelByLocal(remote, getLE16(p + 2));
				if (channel) {
					channel->remoteCID = getLE16(p);
					channel->connected = true;
					sendConfigRequest(remote, channel);
				}
			}
			break;
		}
		case L2CAP_CONFIG_REQ: {
			if (commandLength >= 4) {
				channel = channelByLocal(remote, getLE16(p));
				putLE16(response, channel ? channel->remoteCID : 0);
				putLE16(response + 2, 0);
				putLE16(response + 4, channel ? 0 : 2);
				signalSend(remote, L2CAP_CONFIG_RSP, id, response, 6);
				if (channel) {
					channel->configured = true;
					wiimoteConfigure(remote);
				}
			}
			break;
		}
		case L2CAP_CONFIG_RSP: {
			if (commandLength >= 6 && !getLE16(p + 4)) {
				channel = channelByLocal(remote, getLE16(p));
				if (channel) {
					channel->configured = true;
					wiimoteConfigure(remote);
				}
			}
			break;
		}
		case L2CAP_DISCONN_REQ: {
			if (commandLength >= 4) {
				signalSend(remote, L2CAP_DISCONN_RSP, id, p, 4);
				channel = channelByLocal(remote, getLE16(p));
				if (channel)
					memset(channel, 0, sizeof(*channel));
			}
			break;
		}
		case L2CAP_INFO_REQ: {
			if (commandLength >= 2u) {
				memcpy(response, p, 2);
				putLE16(response + 2, 1); /* not supported */
				signalSend(remote, L2CAP_INFO_RSP, id, response, 4);
			}
			break;
		}
		default:
			break;
		}
		data += 4 + commandLength;
		length = (u16)(length - 4 - commandLength);
	}
}

static void l2capPacket(struct btRemote *remote, const u8 *data, u16 length) {
	u16 payloadLength, cid;

	if (length < sizeof(struct l2capHeader))
		return;

	payloadLength = getLE16(data);
	cid = getLE16(data + 2);

	if (payloadLength > (u16)(length - sizeof(struct l2capHeader)))
		return;
	data += sizeof(struct l2capHeader);

	if (cid == L2CAP_CID_SIGNALING)
		l2capSignal(remote, data, payloadLength);
	else if (cid == remote->interrupt.localCID)
		wiimoteInput(remote, data, payloadLength);
}

static void aclPacket(const u8 *packet, u32 length) {
	struct btRemote *remote;
	u16 handleFlags, payloadLength, handle, pb;
	const u8 *payload;

	if (length < sizeof(struct hciACLHeader))
		return;

	handleFlags = getLE16(packet);
	payloadLength = getLE16(packet + 2);
	if (payloadLength > length - sizeof(struct hciACLHeader))
		return;

	handle = handleFlags & 0x0fffu;
	pb = (handleFlags >> 12) & 3u;
	remote = remoteByHandle(handle);
	if (!remote)
		return;

	payload = packet + sizeof(struct hciACLHeader);

	if (pb == 2 || pb == 0) {
		if (payloadLength < 4)
			return;

		remote->aclExpected = (u16)(getLE16(payload) + 4);
		remote->aclLength = 0;
	}
	if (!remote->aclExpected || payloadLength > sizeof(remote->acl) - remote->aclLength) {
		remote->aclExpected = remote->aclLength = 0;
		return;
	}

	memcpy(remote->acl + remote->aclLength, payload, payloadLength);

	remote->aclLength = (u16)(remote->aclLength + payloadLength);
	if (remote->aclLength >= remote->aclExpected) {
		l2capPacket(remote, remote->acl, remote->aclExpected);
		remote->aclExpected = remote->aclLength = 0;
	}
}

static void aclPackets(const u8 *packet, u32 length) {
	u32 packetLength;

	while (length >= sizeof(struct hciACLHeader)) {
		packetLength = (u32)getLE16(packet + 2) + sizeof(struct hciACLHeader);
		if (packetLength > length)
			return;

		aclPacket(packet, packetLength);
		packet += packetLength;
		length -= packetLength;
	}
}

static void initAdvance(u16 opcode, const u8 *returnParams, u8 length) {
	static const u8 eventMask[8] = { 0xff, 0xff, 0xfb, 0xff, 0x07, 0xf8, 0xbf, 0x3d };
	u8 readKeys[7] = { 0, 0, 0, 0, 0, 0, 1 };
	u8 scan = 2;

	if (adapter.init == BT_INIT_RESET && opcode == HCI_RESET) {
		adapter.init = BT_INIT_EVENT_MASK;
		hciCommand(HCI_SET_EVENT_MASK, eventMask, sizeof(eventMask));
	}
	else if (adapter.init == BT_INIT_EVENT_MASK && opcode == HCI_SET_EVENT_MASK) {
		adapter.init = BT_INIT_SCAN;
		hciCommand(HCI_WRITE_SCAN_ENABLE, &scan, sizeof(scan));
	}
	else if (adapter.init == BT_INIT_SCAN && opcode == HCI_WRITE_SCAN_ENABLE) {
		adapter.init = BT_INIT_ADDRESS;
		hciCommand(HCI_READ_BD_ADDR, NULL, 0);
	}
	else if (adapter.init == BT_INIT_ADDRESS && opcode == HCI_READ_BD_ADDR && length >= 7 && !returnParams[0]) {
		memcpy(adapter.localAddress, returnParams + 1, 6);
		adapter.init = BT_INIT_KEYS;
		hciCommand(HCI_READ_STORED_LINK_KEY, readKeys, sizeof(readKeys));
	}
	else if (adapter.init == BT_INIT_KEYS && opcode == HCI_READ_STORED_LINK_KEY) {
		adapter.init = BT_INIT_READY;
		adapter.readyAt = adapter.lastConnectPoll = mftb();
		log_printf("internal Bluetooth adapter ready with %u stored link key(s)\r\n", adapter.linkKeyCount);
	}
}

static void returnLinkKeys(const u8 *parameters, u8 length) {
	uint count, i;
	struct btLinkKey *key;

	if (!length)
		return;

	count = parameters[0];
	parameters++;
	length--;
	if (count > (uint)length / 22)
		return;

	for (i = 0; i < count && adapter.linkKeyCount < WIIMOTE_MAX_PAIRINGS; i++) {
		key = &adapter.linkKeys[adapter.linkKeyCount++];
		memcpy(key->bdaddr, parameters, 6);
		memcpy(key->key, parameters + 6, 16);
		parameters += 22;
	}
	log_printf("Bluetooth controller returned %u stored link key(s)\r\n", count);
}

static void connectionComplete(const u8 *p, u8 length) {
	struct btRemote *remote;
	u16 handle;
	u8 auth[2], disconnect[3];

	if (length < 11u)
		return;

	if (sameAddress(p + 3, adapter.pageAddress))
		adapter.pagePending = false;

	log_printf("Bluetooth connection complete status %02x from %02x:%02x:%02x:%02x:%02x:%02x\r\n",
		p[0], p[8], p[7], p[6], p[5], p[4], p[3]
	);

	if (p[0])
		return;

	handle = getLE16(p + 1) & 0x0fffu;
	remote = allocateRemote(p + 3, handle);
	if (!remote) {
		putLE16(disconnect, handle);
		disconnect[2] = 0x0du;
		hciCommand(HCI_DISCONNECT, disconnect, sizeof(disconnect));
		return;
	}
	putLE16(auth, handle);
	hciCommand(HCI_AUTH_REQUESTED, auth, sizeof(auth));
}

static void hciEvent(const u8 *event, u32 length) {
	u8 code, plen, params[7];
	const u8 *p;
	u16 opcode, handle;
	struct btRemote *remote;
	const struct btLinkKey *key;

	if (length < 2)
		return;

	code = event[0];
	plen = event[1];
	if ((u32)plen > length - 2)
		return;

	p = event + 2;

	switch (code) {
	case HCI_EV_COMMAND_COMPLETE: {
		if (plen >= 3u) {
			opcode = getLE16(p + 1);
			if (adapter.pendingOpcode == opcode)
				adapter.pendingOpcode = 0;
			initAdvance(opcode, p + 3, (u8)(plen - 3));
		}
		break;
	}
	case HCI_EV_COMMAND_STATUS: {
		if (plen >= 4u) {
			opcode = getLE16(p + 2);
			if (p[0])
				log_printf("Bluetooth HCI command %04x status %02x\r\n", opcode, p[0]);

			if (adapter.pendingOpcode == opcode)
				adapter.pendingOpcode = 0;
			if (opcode == HCI_CREATE_CONNECTION && p[0])
				adapter.pagePending = false;
		}
		break;
	}
	case HCI_EV_CONNECTION_COMPLETE: {
		connectionComplete(p, plen);
		break;
	}
	case HCI_EV_CONNECTION_REQUEST: {
		if (plen >= 10u) {
			log_printf("Bluetooth connection request from %02x:%02x:%02x:%02x:%02x:%02x\r\n",
				p[5], p[4], p[3], p[2], p[1], p[0]);

			memcpy(params, p, 6);
			if (pairingKnown(p)) {
				params[6] = 0; /* become master */
				hciCommand(HCI_ACCEPT_CONNECTION, params, sizeof(params));
			}
			else {
				params[6] = 0x0fu; /* unacceptable address */
				hciCommand(HCI_REJECT_CONNECTION, params, sizeof(params));
			}
		}
		break;
	}
	case HCI_EV_DISCONNECTION_COMPLETE: {
		if (plen >= 4u) {
			remote = remoteByHandle(getLE16(p + 1) & 0x0fffu);
			if (remote)
				memset(remote, 0, sizeof(*remote));
		}
		break;
	}
	case HCI_EV_AUTH_COMPLETE: {
		if (plen >= 3u) {
			log_printf("Bluetooth authentication complete status %02x handle %03x\r\n",
				p[0], getLE16(p + 1) & 0x0fffu);

			if (!p[0]) {
				memcpy(params, p + 1, 2);
				params[2] = 1;
				hciCommand(HCI_SET_CONN_ENCRYPT, params, sizeof(params));
			}
		}
		break;
	}
	case HCI_EV_ENCRYPT_CHANGE: {
		if (plen >= 4u) {
			handle = getLE16(p + 1) & 0x0fffu;
			remote = remoteByHandle(handle);
			log_printf("Bluetooth encryption change status %02x handle %03x enabled %u\r\n",
				p[0], handle, p[3]);

			if (!p[0] && p[3] && remote) {
				connectChannel(remote, &remote->control, L2CAP_PSM_HID_CTRL);
				connectChannel(remote, &remote->interrupt, L2CAP_PSM_HID_INTR);
			}
		}
		break;
	}
	case HCI_EV_RETURN_LINK_KEYS: {
		returnLinkKeys(p, plen);
		break;
	}
	case HCI_EV_PIN_CODE_REQUEST: {
		if (plen >= 6) {
			log_puts("rejecting Bluetooth PIN request (pairing is disabled)");
			hciAddressCommand(HCI_PIN_CODE_NEG_REPLY, p);
		}
		break;
	}
	case HCI_EV_LINK_KEY_REQUEST: {
		if (plen >= 6u) {
			key = findLinkKey(p);
			if (key) {
				log_puts("replying with stored Bluetooth link key");
				replyLinkKey(p, key->key);
			}
			else {
				log_puts("rejecting unknown Bluetooth link-key request");
				hciAddressCommand(HCI_LINK_KEY_NEG_REPLY, p);
			}
		}
		break;
	}
	default:
		break;
	}
	hciServiceQueue();
}

static void pollEndpoint(struct usbEndpoint *endpoint, bool event) {
	u8 packet[BT_ACL_SIZE] ALIGN(32);
	u32 actual;
	int ret;

	if (!endpoint)
		return;
	ret = USB_ResidentInPoll(adapter.interface->device, endpoint, packet,
		event ? BT_EVENT_SIZE : BT_ACL_SIZE, &actual
	);

	if (ret == 1) {
		if (event)
			hciEvent(packet, actual);
		else
			aclPackets(packet, actual);
	}
	else if (ret == -EPIPE) {
		USB_ClearHalt(adapter.interface->device, endpoint);
		USB_ResidentInArm(adapter.interface->device, endpoint, event ? BT_EVENT_SIZE : BT_ACL_SIZE);
	}
}

static bool pairingConnected(const struct wiimotePairing *pairing) {
	u8 address[6];

	pairingHCIAddress(pairing, address);
	return remoteByAddress(address) != NULL;
}

static bool pairingKnown(const u8 *address) {
	const struct wiimotePairing *pairings;
	uint count, i;
	u8 hciAddress[6];

	if (H_ConsoleType != CONSOLE_TYPE_WII)
		return true; /* no idea where the pairing list is on Wii U */

	pairings = WM_GetPairings(&count);
	for (i = 0; i < count; i++) {
		pairingHCIAddress(&pairings[i], hciAddress);
		if (sameAddress(hciAddress, address))
			return true;
	}
	return false;
}

static void tryStoredRemote(void) {
	const struct wiimotePairing *pairings, *pairing;
	u8 params[13], address[6];
	uint count, checked;

	if (H_ConsoleType != CONSOLE_TYPE_WII || adapter.pagePending ||
	    adapter.pendingOpcode || T_HasElapsed(adapter.readyAt, BT_CONNECT_WINDOW_US) ||
	    !T_HasElapsed(adapter.lastConnectPoll, BT_CONNECT_POLL_US))
		return;

	adapter.lastConnectPoll = mftb();
	pairings = WM_GetPairings(&count);
	if (!count)
		return;

	for (checked = 0; checked < count; checked++) {
		pairing = &pairings[adapter.nextPairing++ % count];
		if (pairingConnected(pairing))
			continue;

		pairingHCIAddress(pairing, address);
		memset(params, 0, sizeof(params));
		memcpy(params, address, 6);
		putLE16(params + 6, 0xcc18u); /* DM1/DH1/DM3/DH3/DM5/DH5 */
		params[8] = 1;              /* page repetition R1 */
		putLE16(params + 10, 0);
		params[12] = 1;             /* allow role switch */
		if (!hciCommand(HCI_CREATE_CONNECTION, params, sizeof(params))) {
			adapter.pagePending = true;
			memcpy(adapter.pageAddress, address, 6);
			log_printf("paging stored Wii Remote %02x:%02x:%02x:%02x:%02x:%02x\r\n",
				address[5], address[4], address[3],
				address[2], address[1], address[0]);
		}
		break;
	}
}

static void btPoll(void *data) {
	uint i;
	struct btRemote *remote;
	(void)data;

	if (!adapter.interface || !adapter.interface->device->connected)
		return;

	pollEndpoint(adapter.eventIn, true);
	pollEndpoint(adapter.aclIn, false);
	if (adapter.pendingOpcode && T_HasElapsed(adapter.pendingAt, BT_COMMAND_TIMEOUT_US)) {
		log_printf("Bluetooth HCI command %04x timed out; resetting controller\r\n", adapter.pendingOpcode);
		adapter.pendingOpcode = 0;
		adapter.commandHead = adapter.commandCount = 0;
		adapter.pagePending = false;
		adapter.init = BT_INIT_RESET;
		hciCommandNow(HCI_RESET, NULL, 0);
	}
	hciServiceQueue();
	if (adapter.init == BT_INIT_READY)
		tryStoredRemote();

	for (i = 0; i < BT_MAX_REMOTES; i++) {
		remote = &adapter.remotes[i];

		if (remote->used && remote->repeatButton &&
		    (remote->buttons & remote->repeatButton) &&
		    T_HasElapsed(remote->repeatStarted, WM_REPEAT_DELAY_US) &&
		    T_HasElapsed(remote->lastRepeat, WM_REPEAT_PERIOD_US)) {
			remote->lastRepeat = mftb();
			IN_NewEvent(buttonAction(remote->repeatButton));
		}
	}
}

static int btProbe(struct usbInterface *interface, const struct usbDeviceId *id) {
	struct usbEndpoint *endpoint;
	uint i;
	int ret;
	(void)id;

	if (adapter.interface)
		return -EBUSY;

	memset(&adapter, 0, sizeof(adapter));
	for (i = 0; i < interface->numEndpoints; i++) {
		endpoint = &interface->endpoints[i];

		switch (endpoint->attributes & USB_ENDPOINT_XFER_MASK) {
		case USB_ENDPOINT_XFER_INT: {
			if (endpoint->address & USB_ENDPOINT_DIR_MASK)
				adapter.eventIn = endpoint;
			break;
		}
		case USB_ENDPOINT_XFER_BULK: {
			if (endpoint->address & USB_ENDPOINT_DIR_MASK)
				adapter.aclIn = endpoint;
			else
				adapter.aclOut = endpoint;
			break;
		}
		default:
			break;
		}
	}

	if (!adapter.eventIn || !adapter.aclIn || !adapter.aclOut) {
		log_puts("internal Bluetooth endpoint topology incomplete");
		memset(&adapter, 0, sizeof(adapter));
		return -ENODEV;
	}

	adapter.interface = interface;
	interface->driverData = &adapter;

	ret = USB_ResidentInArm(interface->device, adapter.eventIn, BT_EVENT_SIZE);
	if (ret) {
		log_printf("Bluetooth event endpoint arm failed: %d\r\n", ret);
		goto fail;
	}

	ret = USB_ResidentInArm(interface->device, adapter.aclIn, BT_ACL_SIZE);
	if (ret) {
		log_printf("Bluetooth ACL endpoint arm failed: %d\r\n", ret);
		USB_ResidentInStop(interface->device, adapter.eventIn);
		goto fail;
	}

	adapter.init = BT_INIT_RESET;

	ret = hciCommand(HCI_RESET, NULL, 0);
	if (ret) {
		log_printf("Bluetooth HCI reset submission failed: %d\r\n", ret);
		USB_ResidentInStop(interface->device, adapter.aclIn);
		USB_ResidentInStop(interface->device, adapter.eventIn);
		goto fail;
	}

	log_printf("internal Bluetooth bound on bus %u address %u\r\n", interface->device->hc->bus, interface->device->address);
	return 0;
fail:
	interface->driverData = NULL;
	memset(&adapter, 0, sizeof(adapter));
	return ret;
}

static void btRemove(struct usbInterface *interface) {
	if (interface->driverData != &adapter)
		return;

	USB_ResidentInStop(interface->device, adapter.aclIn);
	USB_ResidentInStop(interface->device, adapter.eventIn);
	interface->driverData = NULL;
	memset(&adapter, 0, sizeof(adapter));
}

static const struct usbDeviceId btIds[] = {
	{
		.vendor = BT_VENDOR_NINTENDO,
		.product = BT_PRODUCT_INTERNAL,
		.interfaceClass = USB_CLASS_WIRELESS_CONTROLLER,
		.interfaceSubclass = USB_SUBCLASS_RF_CONTROLLER,
		.interfaceProtocol = USB_PROTOCOL_BLUETOOTH,
		.interfaceNumber = 0,
		.matchFlags = USB_MATCH_VENDOR_PRODUCT | USB_MATCH_INTERFACE | USB_MATCH_INTERFACE_NUMBER,
	},
	{ 0 }
};

static struct usbDriver btDriver = {
	.name = "BCM2045A Bluetooth",
	.ids = btIds,
	.probe = btProbe,
	.remove = btRemove,
};

static void btInit(void) {
	memset(&adapter, 0, sizeof(adapter));
	if (USB_RegisterDriver(&btDriver)) {
		btTopDriver.state = DRIVER_STATE_FAULTED;
		return;
	}
	T_QueueRepeatingEvent(BT_POLL_US, btPoll, NULL);
	btTopDriver.state = DRIVER_STATE_READY;
}

static void btCleanup(void) {
	T_CancelRepeatingEvent(btPoll, NULL);
	USB_UnregisterDriver(&btDriver);
	memset(&adapter, 0, sizeof(adapter));
	btTopDriver.state = DRIVER_STATE_NOT_READY;
}

static REGISTER_DRIVER(btTopDriver) = {
	.name = "BCM2045A Bluetooth",
	.mask = DRIVER_ALLOW_WII | DRIVER_ALLOW_WIIU,
	.state = DRIVER_STATE_NOT_READY,
	.type = DRIVER_TYPE_INPUT,
	.init = btInit,
	.cleanup = btCleanup,
};
