/*
 * Copyright (c) 1987, 1993, 1994
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int	opterr = 1,		/* if error message should be printed */
	optind = 1,		/* index into parent argv vector */
	optopt,			/* character checked for validity */
	optreset;		/* reset getopt */
char	*optarg;		/* argument associated with option */

#define	BADCH	(int)'?'
#define	BADARG	(int)':'
#define	EMSG	""

/*
 * getopt --
 *	Parse argc/argv argument vector.
 */
int
getopt(int nargc, char * const nargv[], const char *ostr)
{
	static char *place = EMSG;		/* option letter processing */
	char *oli;				/* option letter list index */

	if (optreset || *place == 0) {		/* update scanning pointer */
		optreset = 0;
		place = nargv[optind];
		if (optind >= nargc || *place++ != '-') {
			/* Argument is absent or is not an option */
			place = EMSG;
			return (-1);
		}
		optopt = *place++;
		if (optopt == '-' && *place == 0) {
			/* "--" terminates options */
			optind++;
			place = EMSG;
			return (-1);
		}
		if (optopt == 0) {
			/* solitary '-', treats as non-option */
			place = EMSG;
			return (-1);
		}
	} else {
		optopt = *place++;
	}

	/* See if option letter is one the caller wanted... */
	if (optopt == ':' || (oli = strchr(ostr, optopt)) == NULL) {
		if (*place == 0)
			optind++;
		if (opterr && *ostr != ':')
			(void)fprintf(stderr,
			    "%s: unknown option -- %c\n", nargv[0], optopt);
		return (BADCH);
	}

	/* Does this option need an argument? */
	if (*++oli != ':') {
		/* don't need argument */
		optarg = NULL;
		if (*place == 0)
			optind++;
	} else {
		/* Option-argument is either the rest of this argument or the
		   entire next argument. */
		if (*place) {
			optarg = place;
		} else if (nargc > ++optind) {
			optarg = nargv[optind];
		} else {
			/* option-argument absent */
			place = EMSG;
			if (*ostr == ':')
				return (BADARG);
			if (opterr)
				(void)fprintf(stderr,
				    "%s: option requires an argument -- %c\n",
				    nargv[0], optopt);
			return (BADCH);
		}
		place = EMSG;
		optind++;
	}
	return (optopt);			/* return option letter */
}
