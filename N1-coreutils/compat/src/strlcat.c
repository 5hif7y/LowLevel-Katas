/*
 * Copyright (c) 1998, 2015 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <stddef.h>
#include <string.h>
#include <bsd/string.h>

size_t
strlcat(char *dst, const char *src, size_t dsize)
{
	const char *odst = dst;
	const char *osrc = src;
	size_t nleft = dsize;
	size_t dlen;

	/* Find the end of dst and adjust bytes left but don't go past end. */
	while (nleft-- != 0 && *dst != '\0')
		dst++;
	dlen = (size_t)(dst - odst);
	nleft = dsize - dlen;

	if (nleft-- == 0)
		return (dlen + strlen(src));

	while (*src != '\0') {
		if (nleft != 0) {
			*dst++ = *src;
			nleft--;
		}
		src++;
	}
	*dst = '\0';

	return (size_t)(dlen + (src - osrc));
}
