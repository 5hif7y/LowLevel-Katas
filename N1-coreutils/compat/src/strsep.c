/*
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 */

#include <string.h>
#include <bsd/string.h>

char *
strsep(char **stringp, const char *delim)
{
	char *s;
	const char *spanp;
	int c, sc;
	char *tok;

	if ((s = *stringp) == NULL)
		return (NULL);
	for (tok = s;;) {
		c = *s++;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0)
					s = NULL;
				else
					s[-1] = 0;
				*stringp = s;
				return (tok);
			}
		} while (sc != 0);
	}
	/* NOTREACHED */
}
