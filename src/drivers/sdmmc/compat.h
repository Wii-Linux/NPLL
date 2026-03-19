/*
 * NPLL - Drivers - SDMMC - Compatibility with seL4
 *
 * Derived from seL4 projects_libs libsdhcdrivers:
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _INTERNAL_SDMMC_COMPAT_H
#define _INTERNAL_SDMMC_COMPAT_H

#define UNUSED __attribute__((unused))
#define ZF_LOGD(...) _log_printf("sdhc: debug: " __VA_ARGS__); _log_puts("");
#define ZF_LOGE(...) _log_printf("sdhc: error: " __VA_ARGS__); _log_puts("");

#endif /* _INTERNAL_SDMMC_COMPAT_H */
