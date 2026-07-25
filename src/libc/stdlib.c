/*
 * NPLL - libc - stdlib
 *
 * Copyright (C) 2026 Techflash
 *
 * qsort() based on code from uClibc-ng 1.0.58:
 * https://uclibc-ng.org/
 * Copyright (C) 2002     Manuel Novoa III
 */

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <npll/utils.h>

/*
 * not trying to be perfectly standards compliant here,
 * this is good enough...
 */

static void setup(const char **_str, int *_base, bool *_negative) {
	const char *str = *_str;
	int base = *_base;
	bool negative = *_negative;

	/* skip leading whitespace */
	while (isspace(*str))
		str++;

	/* get sign */
	if (*str == '-') {
		negative = true;
		str++;
	}
	else if (*str == '+') {
		negative = false;
		str++;
	}

	/* guess base */
	if (base == 0) {
		if (*str == '0' && str[1] == 'x') { /* hexadecial */
			str += 2;
			base = 16;
		}
		else if (*str == '0' && isdigit(str[1])) { /* just loading 0, octal */
			str++;
			base = 8;
		}
		else /* decimal */
			base = 10;
	}

	assert_msg(base == 8 || base == 10 || base == 16, "weird base in number conversion");

	*_str = str;
	*_base = base;
	*_negative = negative;
}

long strtol(const char *str, char **endPtr, int base) {
	bool negative = false;
	long val = 0;
	int digit, tmp;
	setup(&str, &base, &negative);

	/* try to convert */
	while (*str) {
		tmp = *str;

		/* is it even valid? */
		if (!isalnum(tmp))
			break;

		/* is it valid digit? */
		if (!isdigit(tmp) && base <= 10)
			break;

		/* normalize ASCII characters into an integer */
		digit = tmp;
		if (isupper(tmp))
			digit -= 'A' - 10;
		if (islower(tmp))
			digit -= 'a' - 10;
		if (isdigit(tmp))
			digit -= '0';

		/* is it valid at our base? */
		if (digit > base)
			break;

		/* add it to our value and move on */
		val *= base;
		val += digit;
		str++;
	}

	if (endPtr)
		*endPtr = (char *)str;

	if (negative)
		val *= -1;

	return val;
}

unsigned long strtoul(const char *str, char **endPtr, int base) {
	/* strtoul is supposed to be able to handle negatives too */
	return (unsigned long)strtol(str, endPtr, base);
}

long long strtoll(const char *str, char **endPtr, int base) {
	bool negative = false;
	long long val = 0;
	int digit, tmp;
	setup(&str, &base, &negative);

	/* try to convert */
	while (*str) {
		tmp = *str;

		/* is it even valid? */
		if (!isalnum(tmp))
			break;

		/* is it valid digit? */
		if (!isdigit(tmp) && base <= 10)
			break;

		/* normalize ASCII characters into an integer */
		digit = tmp;
		if (isupper(tmp))
			digit -= 'A' - 10;
		if (islower(tmp))
			digit -= 'a' - 10;
		if (isdigit(tmp))
			digit -= '0';

		/* is it valid at our base? */
		if (digit > base)
			break;

		/* add it to our value and move on */
		val *= base;
		val += digit;
		str++;
	}

	if (endPtr)
		*endPtr = (char *)str;

	if (negative)
		val *= -1;

	return val;
}

unsigned long long strtoull(const char *str, char **endPtr, int base) {
	/* strtoull is supposed to be able to handle negatives too */
	return (unsigned long long)strtoll(str, endPtr, base);
}

void qsort(void *base, size_t nel, size_t width, qsortCmpFn_t comp) {
	size_t wgap, i, j, k;
	char tmp;

	if ((nel > 1) && (width > 0)) {
		assert(nel <= ((size_t)(-1)) / width); /* check for overflow */
		wgap = 0;
		do {
			wgap = 3 * wgap + 1;
		} while (wgap < (nel-1)/3);
		/* From the above, we know that either wgap == 1 < nel or */
		/* ((wgap-1)/3 < (int) ((nel-1)/3) <= (nel-1)/3 ==> wgap <  nel. */
		wgap *= width;			/* So this can not overflow if wnel doesn't. */
		nel *= width;			/* Convert nel to 'wnel' */
		do {
			i = wgap;
			do {
				j = i;
				do {
					register char *a;
					register char *b;

					j -= wgap;
					a = j + ((char *)base);
					b = a + wgap;
					if ((*comp)(a, b) <= 0) {
						break;
					}
					k = width;
					do {
						tmp = *a;
						*a++ = *b;
						*b++ = tmp;
					} while (--k);
				} while (j >= wgap);
				i += width;
			} while (i < nel);
			wgap = (wgap - width)/3;
		} while (wgap);
	}
}
