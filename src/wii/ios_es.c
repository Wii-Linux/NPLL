/*
 * NPLL - Wii - IOS /dev/es
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "IOS-ES"

#include "ios_ipc.h"
#include "ios_es.h"
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/log.h>
#include <npll/panic.h>
#include <npll/timer.h>
#include <npll/utils.h>

/* ES-related stuff for booting MINI */
static int esFd = -1;

/* these __assume()s probably don't help in practice since they're just getting passed to IOS_Ioctlv anyways but eh, why not */
int ES_Init(void) {
	uint retries = 100;
	int ret;
	bool success = false;
	u64 titleID ALIGN(32) = 0;
	u64 iosVer;
	tikview_t tikviews[4] ALIGN(32);
	u32 num_tikviews ALIGN(32);
	__assume(esFd == -1);

	while (retries--) {
		esFd = IOS_Open("/dev/es", IOS_MODE_NONE);
		log_printf("/dev/es fd: %d\r\n", esFd);
		if (esFd != 0) /* not just < 0, because /dev/es should be our first fd */
			goto failed;

		/* success opening it, but can we do trivial commands to actually use it? */
		#if 0 /* FIXME: always fails with -1017 (Invalid Argument) */
		ret = ES_GetTitleID(&titleID);
		log_printf("ES_GetTitleID ret=%d, titleID=0x%016llx, &titleID=0x%08x\r\n", ret, titleID, &titleID);
		if (ret != 0)
			goto failed;
		#endif

		/* make sure the path we're going to use with ABN works */
		iosVer = (u64)H_WiiBootIOS | TITLE_ID_IOS;
		ret = ES_GetTikViewsCount(iosVer, &num_tikviews);
		log_printf("ES_GetTikViewsCount ret=%d, num=%u\r\n", ret, num_tikviews);
		if (ret != 0)
			goto failed;
		ret = ES_GetTikViews(iosVer, tikviews, num_tikviews);
		log_printf("ES_GetTikViews ret=%d\r\n", ret);
		if (ret != 0)
			goto failed;

		/* success! */
		H_WiiBootTitleID = titleID;
		success = true;
		break;

	failed:
		/* failed, reset IPC state and retry */
		IOS_Reset();
		udelay(50 * 1000);
		continue;
	}
	if (!success)
		panic("Failed to open /dev/es");

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

i32 ES_GetTitleID(u64 *title_id) {
	ios_ioctlv_t vec ALIGN(32);
	__assume(esFd <= 0);

	dcache_flush_invalidate(title_id, sizeof(*title_id));
	vec.data = virtToPhys(title_id);
	vec.size = 8;

	return IOS_Ioctlv(esFd, ES_GET_TITLE_ID, 0, 1, &vec);

}
