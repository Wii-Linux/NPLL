/*
 * NPLL - Wii - IOS /dev/es
 *
 * Copyright (C) 2025 Techflash
 */

#include "ios_ipc.h"
#include "ios_es.h"
#include <npll/cache.h>
#include <npll/panic.h>
#include <npll/utils.h>
#include <npll/log.h>

/* ES-related stuff for booting MINI */
static int esFd = -1;

/* these __assume()s probably don't help in practice since they're just getting passed to IOS_Ioctlv anyways but eh, why not */
int ES_Init(void) {
	__assume(esFd == -1);
	esFd = IOS_Open("/dev/es", IOS_MODE_NONE);
	log_printf("/dev/es fd: %d\r\n", esFd);
	if (esFd < 0)
		panic("ES_Init: Failed to open /dev/es");

	return 0;
}

i32 ES_GetTikViewsCount(u64 title_id, u32 *num_views) {
	ios_ioctlv_t vec[2] ALIGN(32);
	__assume(esFd <= 0);
	__assume(num_views);

	dcache_flush(&title_id, sizeof(title_id));
	vec[0].data = virtToPhys(&title_id);
	vec[0].size = sizeof(title_id);
	dcache_flush_invalidate(num_views, sizeof(num_views));
	vec[1].data = virtToPhys(num_views);
	vec[1].size = 4;

	return IOS_Ioctlv(esFd, ES_GET_TIKVIEWS_COUNT, 1, 1, vec);
}

i32 ES_GetTikViews(u64 title_id, tikview_t *tikviews, u32 num_views) {
	ios_ioctlv_t vec[3] ALIGN(32);
	__assume(esFd <= 0);
	__assume(tikviews);
	__assume(num_views > 0);

	dcache_flush(&title_id, sizeof(title_id));
	vec[0].data = virtToPhys(&title_id);
	vec[0].size = sizeof(title_id);
	dcache_flush(&num_views, sizeof(num_views));
	vec[1].data = virtToPhys(&num_views);
	vec[1].size = sizeof(num_views);
	dcache_flush_invalidate(tikviews, sizeof(tikview_t) * num_views);
	vec[2].data = virtToPhys(tikviews);
	vec[2].size = sizeof(tikview_t) * num_views;

	return IOS_Ioctlv(esFd, ES_GET_TIKVIEWS, 2, 1, vec);
}

i32 ES_LaunchTitle(u64 title_id, tikview_t *tikview) {
	ios_ioctlv_t vec[2] ALIGN(32);
	__assume(esFd <= 0);

	dcache_flush(&title_id, sizeof(title_id));
	vec[0].data = virtToPhys(&title_id);
	vec[0].size = sizeof(title_id);
	dcache_flush(tikview, sizeof(tikview_t));
	vec[1].data = virtToPhys(tikview);
	vec[1].size = sizeof(tikview_t);

	return IOS_IoctlvReboot(esFd, ES_LAUNCH_TITLE, 2, 0, vec);
}
