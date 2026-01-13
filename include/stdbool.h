/*
 * NPLL - libc - stdbool
 *
 * Copyright (C) 2025-2026 Techflash
 */

#ifndef _STDBOOL_H
#define _STDBOOL_H

#if defined(__STDC_VERSION__) && __STDC_VERSION__ > 201710L
/* bool, true, and false are already defined */
#else /* __STDC_VERSION__ && __STDC_VERSION__ > 201710L */
typedef int bool;
#define true ((bool)1)
#define false ((bool)0)
#endif /* !(__STDC_VERSION__ && __STDC_VERSION__ > 201710L) */

#endif /* _STDBOOL_H */
