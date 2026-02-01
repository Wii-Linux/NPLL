/*
 * NPLL - Logging
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _LOG_INTERNAL_H
#define _LOG_INTERNAL_H

enum logMethod {
	LOG_METHOD_ALL_ODEV,
	LOG_METHOD_MENU_WINDOW,
	LOG_METHOD_NONE,
};

extern enum logMethod L_Method;
extern void L_Init(void);

extern void _log_puts(const char *str);
extern void _log_printf(const char *fmt, ...);

#endif /* _LOG_INTERNAL_H */
