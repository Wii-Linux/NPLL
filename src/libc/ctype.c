/*
 * NPLL - libc - ctype
 *
 * Copyright (C) 2026 Techflash
 *
 * Based on code from EverythingNet:
 * Copyright (C) 2023-2025 Techflash
 */

int isdigit(int ch) {
	return (ch >= '0') && (ch <= '9');
}

int isxdigit(int ch) {
	return isdigit(ch) ||
	       ((ch >= 'a') && (ch <= 'f')) ||
	       ((ch >= 'A') && (ch <= 'F'));
}

int islower(int ch) {
	return (ch >= 'a') && (ch <= 'z');
}

int isupper(int ch) {
	return (ch >= 'A') && (ch <= 'Z');
}

int isalpha(int ch) {
	return islower(ch) || isupper(ch);
}

int isalnum(int ch) {
	return isdigit(ch) || isalpha(ch);
}

int isblank(int ch) {
	return (ch == '\t') || (ch == ' ');
}

int isspace(int ch) {
	return (ch == '\n') || (ch == '\v') ||
	       (ch == '\f') || (ch == '\r') || isblank(ch);
}

int ispunct(int ch) {
	return ((ch >= '!') && (ch <= '\'')) ||
	       ((ch >= '(') && (ch <= '/'))  ||
	       ((ch >= ':') && (ch <= '?'))  ||
	       (ch == '@')                   ||
	       ((ch >= '[') && (ch <= '_'))  ||
	       (ch == '`')                   ||
	       ((ch >= '{') && (ch <= '~'));
}

int toupper(int ch) {
	return islower (ch) ? ch - 'a' + 'A' : ch;
}

int tolower(int ch) {
	return isupper(ch) ? ch - 'A' + 'a' : ch;
}
