/*
 * NPLL - Wii Remote pairing cache
 *
 * BT.DINF describes the remotes known by IOS.  Link keys are held by the
 * Bluetooth controller, not in this record.
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "wiimote-pairing"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/wiimote.h>

#define SYSCONF_PATH "/shared2/sys/SYSCONF"
#define SYSCONF_MAX_SIZE 0x4000u
#define SYSCONF_BIG_ARRAY 1u
#define BT_DINF_DEVICE_SIZE (6u + WIIMOTE_NAME_LENGTH)
#define BT_DINF_MIN_SIZE (1u + (16u * BT_DINF_DEVICE_SIZE))

static struct wiimotePairing pairings[WIIMOTE_MAX_PAIRINGS];
static uint pairingCount;

static u16 readBE16(const u8 *p) {
	return (u16)(((u16)p[0] << 8) | p[1]);
}

static bool allZero(const u8 *p, size_t length) {
	size_t i;

	for (i = 0; i < length; i++)
		if (p[i])
			return false;
	return true;
}

void WM_ClearPairings(void) {
	memset(pairings, 0, sizeof(pairings));
	pairingCount = 0;
}

const struct wiimotePairing *WM_GetPairings(uint *count) {
	if (count)
		*count = pairingCount;

	return pairings;
}

int WM_ParsePairings(const void *data, size_t length) {
	const u8 *sysconf = data, *entry, *payload, *device;
	u8 c;
	size_t entryOffset, headerLength, nameLength, payloadLength;
	u16 entryCount, offset;
	uint wanted, i, j;

	WM_ClearPairings();
	if (!sysconf || length < 6u || memcmp(sysconf, "SCv0", 4))
		return -EINVAL;

	entryCount = readBE16(sysconf + 4);
	if ((size_t)entryCount > (length - 6u) / 2u)
		return -EINVAL;

	entry = NULL;
	for (i = 0; i < entryCount; i++) {
		offset = readBE16(sysconf + 6u + (i * 2u));
		entryOffset = offset;

		if (entryOffset >= length)
			return -EINVAL;

		nameLength = (size_t)(sysconf[entryOffset] & 0x0fu) + 1u;
		headerLength = 1u + nameLength;
		if (headerLength > length - entryOffset)
			return -EINVAL;

		if (nameLength == sizeof("BT.DINF") - 1u && !memcmp(sysconf + entryOffset + 1u, "BT.DINF", nameLength)) {
			entry = sysconf + entryOffset;
			break;
		}
	}

	if (!entry)
		return -ENOENT;

	if ((entry[0] >> 5) != SYSCONF_BIG_ARRAY)
		return -EINVAL;

	nameLength = (size_t)(entry[0] & 0x0fu) + 1u;
	entryOffset = (size_t)(entry - sysconf);
	headerLength = 1u + nameLength + 2u;

	if (headerLength > length - entryOffset)
		return -EINVAL;

	/* SYSCONF array lengths store size minus one. */
	payloadLength = (size_t)readBE16(entry + 1u + nameLength) + 1u;
	if (payloadLength > length - entryOffset - headerLength || payloadLength < BT_DINF_MIN_SIZE)
		return -EINVAL;

	payload = entry + headerLength;
	wanted = payload[0];
	if (wanted > WIIMOTE_MAX_PAIRINGS)
		return -EINVAL;

	for (i = 0; i < wanted; i++) {
		device = payload + 1u + (i * BT_DINF_DEVICE_SIZE);

		if ((size_t)(device - payload) + BT_DINF_DEVICE_SIZE > payloadLength)
			return -EINVAL;
		if (allZero(device, 6))
			continue;

		memcpy(pairings[pairingCount].bdaddr, device, 6);
		for (j = 0; j < WIIMOTE_NAME_LENGTH && device[6u + j]; j++) {
			c = device[6u + j];
			pairings[pairingCount].name[j] =
				(c >= 0x20u && c <= 0x7eu) ? (char)c : '?';
		}

		pairings[pairingCount].name[j] = 0;
		pairingCount++;
	}

	return (int)pairingCount;
}

int WM_LoadPairingsFromSFFS(void) {
	void *buffer;
	ssize_t size, got;
	int fd, ret;

	WM_ClearPairings();
	fd = FS_Open(SYSCONF_PATH);
	if (fd < 0)
		return fd;

	size = FS_GetSize(fd);
	if (size <= 0 || size > (ssize_t)SYSCONF_MAX_SIZE) {
		FS_Close(fd);
		return -EINVAL;
	}

	buffer = malloc((size_t)size);
	if (!buffer) {
		FS_Close(fd);
		return -ENOMEM;
	}

	got = FS_Read(fd, buffer, (size_t)size);
	FS_Close(fd);

	if (got != size)
		ret = got < 0 ? (int)got : -EIO;
	else
		ret = WM_ParsePairings(buffer, (size_t)size);

	free(buffer);

	if (ret >= 0)
		log_printf("cached %d Wii Remote pairing(s) from SFFS\r\n", ret);
	else
		log_printf("could not cache Wii Remote pairings: %d\r\n", ret);

	return ret;
}
