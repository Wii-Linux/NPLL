/*
 * NPLL - Wii - IOS /dev/es
 *
 * Copyright (C) 2025-2026 Techflash
 *
 * TMD stuff taken from https://wiibrew.org/wiki/Title_metadata
 */


#ifndef _IOS_ES_H
#define _IOS_ES_H

#include "ios_ipc.h"
#include <npll/cache.h>
#include <npll/types.h>
#include <npll/utils.h>

enum esIoctls {
	ES_LAUNCH_TITLE = 0x8,
	ES_GET_TIKVIEWS_COUNT = 0x12,
	ES_GET_TIKVIEWS = 0x13,
	ES_GET_TITLE_ID = 0x20
};

typedef struct {
	/* we don't actually care what's in here */
	u8 __blah[0xd8];
} __attribute__((packed)) tikview_t;

static inline i32 IOS_GetVersion(void) {
	return (u16)(*(u32 *)(MEM1_UNCACHED_BASE + 0x3140) >> 16);
}
extern int ES_Init(void);
extern i32 ES_GetTikViewsCount(u64 title_id, u32 *num_views);
extern i32 ES_GetTikViews(u64 title_id, tikview_t *tikviews, u32 num_views);
extern i32 ES_LaunchTitle(u64 title_id, tikview_t *tikview);
extern i32 ES_GetTitleID(u64 *title_id);

#define TITLE_ID_IOS 0x100000000ull

struct tmdContentRecord {
	u32 cid;
	u16 index;
	u16 type;
	u64 size;
	u8  hash[20];
} __attribute__((packed));

struct tmd {
	u32 sigType;
	u8 sig[256];
	u8 fill1[60];
	u8 issuer[64];
	u8 version;
	u8 caCRLVersion;
	u8 signerCRLVersion;
	u8 vwii;
	u64 sysVersion;
	u64 titleID;
	u32 titleType;
	u16 groupID;
	u8 reserved[62];
	u32 accessRights;
	u16 titleVersion;
	u16 numContents;
	u16 bootIndex;
	u16 fill2;
	struct tmdContentRecord contents[];
} __attribute__((packed));

struct contentMapEntry {
	char name[8];
	u8 sha1[20];
} __attribute__((packed));

#define TMD_SIGTYPE_RSA2048 0x00010001u

#define TMD_CONTENT_NORMAL  0x0001u
#define TMD_CONTENT_DLC     0x4000u
#define TMD_CONTENT_SHARED  0x8000u

#endif /* _IOS_ES_H */
