/*
 * NPLL - libc - ctype
 *
 * Copyright (C) 2026 Techflash
 *
 * Based on code from EverythingNet:
 * Copyright (C) 2023-2025 Techflash
 */

#ifndef _CTYPE_H
#define _CTYPE_H

int isdigit(int ch);
int isxdigit(int ch);
int islower(int ch);
int isupper(int ch);
int isalpha(int ch);
int isalnum(int ch);
int isblank(int ch);
int isspace(int ch);
int ispunct(int ch);
int toupper(int ch);
int tolower(int ch);

#endif /* _CTYPE_H */
