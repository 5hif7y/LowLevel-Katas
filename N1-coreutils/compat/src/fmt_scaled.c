/*
 * Copyright (c) 2001, 2002, 2003 Ian F. Darwin.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include <util.h>

typedef enum {
	NONE = 0, KILO = 1, MEGA = 2, GIGA = 3, TERA = 4, PETA = 5, EXA = 6
} unit_type;

static const unit_type units[] = { NONE, KILO, MEGA, GIGA, TERA, PETA, EXA };
static const char scale_chars[] = "BKMGTPE";
static const long long scale_factors[] = {
	1LL,
	1024LL,
	1024LL*1024,
	1024LL*1024*1024,
	1024LL*1024*1024*1024,
	1024LL*1024*1024*1024*1024,
	1024LL*1024*1024*1024*1024*1024,
};
#define	SCALE_LENGTH (sizeof(units)/sizeof(units[0]))
#define MAX_DIGITS (SCALE_LENGTH * 3)

int
scan_scaled(char *scaled, long long *result)
{
	char *p = scaled;
	int sign = 0;
	unsigned int i, ndigits = 0, fract_digits = 0, muls = 0, divs = 0;
	long long scale_fact = 1, whole = 0, fpart = 0;

	while (isascii((unsigned char)*p) && isspace((unsigned char)*p))
		++p;

	while (*p == '-' || *p == '+') {
		if (*p == '-') {
			if (sign) {
				errno = EINVAL;
				return -1;
			}
			sign = -1;
			++p;
		} else if (*p == '+') {
			if (sign) {
				errno = EINVAL;
				return -1;
			}
			sign = +1;
			++p;
		}
	}

	for (; isascii((unsigned char)*p) &&
	    (isdigit((unsigned char)*p) || *p=='.'); ++p) {
		if (*p == '.') {
			if (fract_digits > 0) {
				errno = EINVAL;
				return -1;
			}
			fract_digits = 1;
			continue;
		}

		i = (*p) - '0';
		if (fract_digits > 0) {
			if (fract_digits >= MAX_DIGITS-1)
				continue;
			fract_digits++;
			if (fpart > LLONG_MAX / 10) {
				errno = ERANGE;
				return -1;
			}
			fpart *= 10;
			if (i > LLONG_MAX - fpart) {
				errno = ERANGE;
				return -1;
			}
			fpart += i;
		} else {
			if (++ndigits >= MAX_DIGITS) {
				errno = ERANGE;
				return -1;
			}
			if (whole > LLONG_MAX / 10) {
				errno = ERANGE;
				return -1;
			}
			whole *= 10;
			if (i > LLONG_MAX - whole) {
				errno = ERANGE;
				return -1;
			}
			whole += i;
		}
	}

	if (sign)
		whole *= sign;

	if (!*p) {
		*result = whole;
		return 0;
	}

	for (i = 0; i < SCALE_LENGTH; i++) {
		if (*p == scale_chars[i] ||
			*p == tolower((unsigned char)scale_chars[i])) {

			if (isalnum((unsigned char)*(p+1))) {
				errno = EINVAL;
				return -1;
			}
			scale_fact = scale_factors[i];

			if (whole > LLONG_MAX / scale_fact ||
			    whole < LLONG_MIN / scale_fact) {
				errno = ERANGE;
				return -1;
			}

			whole *= scale_fact;

			muls = i;
			divs = fract_digits > 0 ?  fract_digits - 1 : 0;
			while (muls > 0 && divs > 0) {
				if (fpart <= LLONG_MAX / 1024) {
					fpart *= 1024;
					muls--;
				} else {
					fpart /= 10;
					divs--;
				}
			}
			while (muls > 0) {
				fpart *= 1024;
				muls--;
			}
			while (divs > 0) {
				fpart /= 10;
				divs--;
			}
			if (sign == -1) {
				if (whole < LLONG_MIN + fpart) {
					errno = ERANGE;
					return -1;
				}
				whole -= fpart;
			} else {
				if (whole > LLONG_MAX - fpart) {
					errno = ERANGE;
					return -1;
				}
				whole += fpart;
			}
			*result = whole;
			return 0;
		}
	}

	errno = EINVAL;
	return -1;
}

int
fmt_scaled(long long number, char *result)
{
	long long abval, fract = 0;
	unsigned int i;
	unit_type unit = NONE;

	if (number == LLONG_MIN) {
		errno = ERANGE;
		return -1;
	}

	abval = llabs(number);

	if (abval / 1024 >= scale_factors[SCALE_LENGTH-1]) {
		errno = ERANGE;
		return -1;
	}

	for (i = 0; i < SCALE_LENGTH; i++) {
		if (abval/1024 < scale_factors[i]) {
			unit = units[i];
			fract = (i == 0) ? 0 : abval % scale_factors[i];
			number /= scale_factors[i];
			if (i > 0)
				fract /= scale_factors[i - 1];
			break;
		}
	}

	fract = (10 * fract + 512) / 1024;
	if (fract >= 10) {
		if (number >= 0)
			number++;
		else
			number--;
		fract = 0;
	} else if (fract < 0) {
		fract = 0;
	}

	if (number == 0)
		strlcpy(result, "0B", FMT_SCALED_STRSIZE);
	else if (unit == NONE || number >= 100 || number <= -100) {
		if (fract >= 5) {
			if (number >= 0)
				number++;
			else
				number--;
		}
		(void)snprintf(result, FMT_SCALED_STRSIZE, "%lld%c",
			number, scale_chars[unit]);
	} else
		(void)snprintf(result, FMT_SCALED_STRSIZE, "%lld.%1lld%c",
			number, fract, scale_chars[unit]);

	return 0;
}

char *
getbsize(int *headerlenp, long *blocksizep)
{
	static char header[32] = "512-blocks";
	long sz = 512;
	char *p = getenv("BLOCKSIZE");
	if (p != NULL && *p != '\0') {
		long val = strtol(p, NULL, 10);
		if (val > 0)
			sz = val;
	}
	if (blocksizep != NULL)
		*blocksizep = sz;
	if (headerlenp != NULL)
		*headerlenp = (int)strlen(header);
	return header;
}
