/*
 * NPLL - Wii - IOS /dev/es
 *
 * Copyright (C) 2025 Techflash
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
	ES_GET_TIKVIEWS = 0x13
};

typedef struct {
	/* we don't actually care what's in here */
	u8 __blah[0xd8];
} __attribute__((packed)) tikview_t;

static inline i32 IOS_GetVersion(void) {
	return *(u32 *)(MEM1_UNCACHED_BASE + 0x3140) >> 16;
}
extern int ES_Init(void);
extern i32 ES_GetTikViewsCount(u64 title_id, u32 *num_views);
extern i32 ES_GetTikViews(u64 title_id, tikview_t *tikviews, u32 num_views);
extern i32 ES_LaunchTitle(u64 title_id, tikview_t *tikview);

#define TITLE_ID_IOS 0x100000000

#endif /* _IOS_ES_H */
