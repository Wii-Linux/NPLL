/*
 * NPLL - Logging
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _LOG_H
#define _LOG_H

#include <npll/log_internal.h>

#ifndef MODULE
#warning "MODULE is not defined, defaulting to 'UNK'"
#define MODULE "UNK"
#endif

#define log_puts(x) _log_puts(MODULE ": " x)
#define log_printf(...) _log_printf(MODULE ": " __VA_ARGS__)

#endif /* _LOG_H */
