/*
 * NPLL - Wii system configs (SYSCONF/setting.txt)
 *
 * Copyright (C) 2026 Techflash
 */
#define MODULE "wii-configs"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/wiimote.h>
#include <npll/wii_configs.h>

static u16 readBE16(const u8 *p) {
	return (u16)(((u16)p[0] << 8) | p[1]);
}

int WC_Find(const void *data, size_t length, const char *name, struct wcValue *value) {
	const u8 *buf = data;
	size_t count, tableEnd, i, off, end, nlen, pos, size;
	unsigned type;

	if (!buf || !name || !value || length < 8 || memcmp(buf, "SCv0", 4))
		return -EINVAL;

	count = readBE16(buf + 4);
	if (count + 1 > (length - 6) / 2)
		return -EINVAL;

	tableEnd = 6 + 2 * (count + 1);
	for (i = 0; i < count; i++) {
		off = readBE16(buf + 6 + 2 * i);
		end = readBE16(buf + 8 + 2 * i);
		if (off < tableEnd || end <= off || end > length)
			return -EINVAL;

		nlen = (buf[off] & 0x1fu) + 1u;
		pos = off + 1 + nlen;
		if (pos > end)
			return -EINVAL;

		type = buf[off] >> 5;
		switch (type) {
		case WC_BIG_ARRAY: {
			if (end - pos < 2) return -EINVAL;
			size = (size_t)readBE16(buf + pos) + 1;
			pos += 2;
			break;
		}
		case WC_SMALL_ARRAY: {
			if (pos == end) return -EINVAL;
			size = (size_t)buf[pos++] + 1;
			break;
		}
		case WC_BYTE:
		case WC_BOOL: {
			size = 1;
			break;
		}
		case WC_SHORT: {
			size = 2;
			break;
		}
		case WC_LONG: {
			size = 4;
			break;
		}
		case WC_LONG_LONG: {
			size = 8;
			break;
		}
		default:
			return -EINVAL;
		}

		if (size > end - pos)
			return -EINVAL;

		if (strlen(name) == nlen && !memcmp(buf + off + 1, name, nlen)) {
			value->type = (enum wcType)type;
			value->data = buf + pos;
			value->length = size;
			return 0;
		}
	}
	return -ENOENT;
}

int WC_GetSetting(const void *data, size_t length, const char *name, char *out, size_t capacity) {
	char text[256];
	const u8 *buf = data;
	u32 key = 0x73b5dbfa;
	size_t i, start, end, nlen;

	if (!buf || length != sizeof(text) || !name || !out || !capacity)
		return -EINVAL;

	for (i = 0; i < length; i++) {
		text[i] = (char)(buf[i] ^ (u8)key);
		key = (key << 1) | (key >> 31);
	}

	nlen = strlen(name);
	for (start = 0; start < length && text[start]; start = end + 1) {
		for (end = start; end < length && text[end] && text[end] != '\r' && text[end] != '\n'; end++);

		if (end - start > nlen && !memcmp(text + start, name, nlen) && text[start + nlen] == '=') {
			i = end - start - nlen - 1;
			if (i >= capacity) return -ENOSPC;
			memcpy(out, text + start + nlen + 1, i);
			out[i] = 0;
			return 0;
		}

		if (end == length || !text[end])
			break;
	}
	return -ENOENT;
}

static bool preference(const void *data, size_t length, const char *name) {
	struct wcValue value;
	return !WC_Find(data, length, name, &value) &&
		(value.type == WC_BYTE || value.type == WC_BOOL) &&
		value.data[0] == 1;
}

int WC_GetVideoMode(const void *settings, size_t settingsLength,
	const void *sysconf, size_t sysconfLength, enum viMode *mode) {
	char standard[5];
	bool progressive = preference(sysconf, sysconfLength, "IPL.PGS");
	int ret = WC_GetSetting(settings, settingsLength, "VIDEO", standard, sizeof(standard));

	if (ret)
		return ret;
	if (!mode)
		return -EINVAL;

	if (!strcmp(standard, "PAL")) {
		*mode = progressive ? VI_MODE_640X480_PAL60_PROG :
			preference(sysconf, sysconfLength, "IPL.E60") ? VI_MODE_640X480_PAL60_INT : VI_MODE_640X576_PAL50_INT;
	}
	else if (!strcmp(standard, "NTSC") || !strcmp(standard, "MPAL")) {
		/* No MPAL support yet */
		*mode = progressive ? VI_MODE_640X480_NTSC_PROG : VI_MODE_640X480_NTSC_INT;
	}
	else
		return -EINVAL;

	return 0;
}

int WC_ReadFile(const char *path, size_t maxSize, void **data, size_t *length) {
	int fd = FS_Open(path);
	ssize_t size, got;
	void *buf;

	*data = NULL;
	*length = 0;

	if (fd < 0)
		return fd;

	size = FS_GetSize(fd);
	if (size <= 0 || (size_t)size > maxSize) {
		FS_Close(fd);
		return -EINVAL;
	}

	buf = malloc((size_t)size);
	if (!buf) {
		FS_Close(fd);
		return -ENOMEM;
	}

	got = FS_Read(fd, buf, (size_t)size);
	FS_Close(fd);
	if (got != size) {
		free(buf);
		return got < 0 ? (int)got : -EIO;
	}

	*data = buf;
	*length = (size_t)size;
	return 0;
}

void WC_LoadFromSFFS(void) {
	void *sysconf = NULL, *settings = NULL;
	size_t sysconfLength = 0, settingsLength = 0;
	enum viMode mode;
	int ret;

	WM_ClearPairings();
	ret = WC_ReadFile(WC_SYSCONF_PATH, WC_SYSCONF_MAX_SIZE, &sysconf, &sysconfLength);
	if (!ret)
		ret = WM_ParsePairings(sysconf, sysconfLength);

	log_printf("Wii Remote pairing cache result: %d\r\n", ret);
	ret = WC_ReadFile("/title/00000001/00000002/data/setting.txt", 256, &settings, &settingsLength);
	if (!ret)
		ret = WC_GetVideoMode(settings, settingsLength, sysconf, sysconfLength, &mode);
	if (!ret)
		ret = H_VISetModeTier(VI_MODE_CHOICE_SYS, mode);
	if (ret)
		log_printf("System video mode selection failed: %d\r\n", ret);

	if (settings)
		free(settings);
	if (sysconf)
		free(sysconf);
}
