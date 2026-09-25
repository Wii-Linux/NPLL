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
#include <npll/wii_configs.h>
#include <npll/log.h>
#include <npll/wiimote.h>

#define BT_DINF_DEVICE_SIZE (6u + WIIMOTE_NAME_LENGTH)
#define BT_DINF_MIN_SIZE (1u + (16u * BT_DINF_DEVICE_SIZE))

static struct wiimotePairing pairings[WIIMOTE_MAX_PAIRINGS];
static uint pairingCount;

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
	const u8 *payload, *device;
	struct wcValue value;
	size_t payloadLength;
	u8 c;
	uint wanted, i, j;
	int ret;

	WM_ClearPairings();
	ret = WC_Find(data, length, "BT.DINF", &value);
	if (ret) return ret;
	if (value.type != WC_BIG_ARRAY || value.length < BT_DINF_MIN_SIZE)
		return -EINVAL;
	payload = value.data;
	payloadLength = value.length;
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
	size_t size;
	int ret;

	WM_ClearPairings();
	ret = WC_ReadFile(WC_SYSCONF_PATH, WC_SYSCONF_MAX_SIZE, &buffer, &size);
	if (!ret)
		ret = WM_ParsePairings(buffer, size);

	if (buffer)
		free(buffer);

	if (ret >= 0)
		log_printf("cached %d Wii Remote pairing(s) from SFFS\r\n", ret);
	else
		log_printf("could not cache Wii Remote pairings: %d\r\n", ret);

	return ret;
}
