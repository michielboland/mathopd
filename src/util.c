/*
 *   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002 Michiel Boland.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *
 *   1. Redistributions of source code must retain the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials
 *      provided with the distribution.
 *
 *   3. The name of the author may not be used to endorse or promote
 *      products derived from this software without specific prior
 *      written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY
 *   EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 *   THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *   PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR
 *   BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *   IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *   THE POSSIBILITY OF SUCH DAMAGE.
 */

/* San Jacinto */

static const char rcsid[] = "$Id$";

#include <ctype.h>
#include "mathopd.h"

#define HEXDIGIT(x) (((x) <= '9') ? (x) - '0' : ((x) & 7) + 9)

int unescape_url(const char *from, char *to)
{
	char c;
	int x1, x2;

	while ((c = *from++) != 0)
		if (c == '%') {
			x1 = *from++;
			if (!isxdigit(x1))
				return -1;
			x2 = *from++;
			if (!isxdigit(x2))
				return -1;
			*to++ = (HEXDIGIT(x1) << 4) + HEXDIGIT(x2);
		} else
			*to++ = c;
	*to = 0;
	return 0;
}

int unescape_url_n(const char *from, char *to, size_t n)
{
	char c;
	int x1, x2;

	while (n-- && (c = *from++) != 0)
		if (c == '%') {
			x1 = *from++;
			if (!isxdigit(x1))
				return -1;
			x2 = *from++;
			if (!isxdigit(x2))
				return -1;
			*to++ = (HEXDIGIT(x1) << 4) + HEXDIGIT(x2);
		} else
			*to++ = c;
	*to = 0;
	return 0;
}

void sanitize_host(char *s)
{
	int c, l;
	int inside_ipv6_literal = 0;

	l = 0;
	if (s[0] == '[') {
		inside_ipv6_literal = 1;
	}
	while ((c = *s) != 0) {
		/*
		 * Strip off the colon and the remaining port
		 * number, unless the colon was part of an
		 * IPv6 literal.
		 */
		if (c == ':' && inside_ipv6_literal == 0) {
			*s = 0;
			break;
		}
		if (c == ']') {
			inside_ipv6_literal = 0;
		}
		*s++ = tolower(c);
		l = c;
	}
	if (l == '.')
		s[-1] = 0;
}
