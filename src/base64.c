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

/* Aja */

static const char rcsid[] = "$Id$";

#include <string.h>
#include <stdio.h>
#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include "mathopd.h"

#define B64(x) ( \
	(x) >= 'A' ? ( \
		(x) <= 'Z' ? \
		(x) - 'A' : \
		(x) >= 'a' ? ( \
			x <= 'z' ? \
			(x) - 'a' + 26 : \
			64 \
		) : 64 \
	) : \
	(x) <= '9' ? ( \
		(x) >= '0' ? \
		(x) - '0' + 52 : \
		(x) == '+' ? \
		62 : \
		(x) == '/' ? \
		63 : \
		64 \
	) : \
	(x) == '=' ? \
	0 : \
	64 \
)

static int base64decode(const char *encoded, char *decoded)
{
	char c, t1, t2, u1, u2, u3;

	while (1) {
		c = *encoded++;
		if (c == 0) {
			*decoded = 0;
			return 0;
		}
		t1 = B64(c);
		if (t1 == 64)
			return -1;
		c = *encoded++;
		if (c == 0)
			return -1;
		t2 = B64(c);
		if (t2 == 64)
			return -1;
		u1 = t1 << 2 | t2 >> 4;
		c = *encoded++;
		if (c == 0)
			return -1;
		t1 = B64(c);
		if (t1 == 64)
			return -1;
		u2 = (t2 & 0xf) << 4 | t1 >> 2;
		c = *encoded++;
		if (c == 0)
			return -1;
		t2 = B64(c);
		if (t2 == 64)
			return -1;
		u3 = (t1 & 0x3) << 6 | t2;
		*decoded++ = u1;
		if (u1 == 0)
			return 0;
		*decoded++ = u2;
		if (u2 == 0)
			return 0;
		*decoded++ = u3;
		if (u3 == 0)
			return 0;
	}
}

static int pwok(const char *good, const char *guess, int do_crypt)
{
	char *cs;

	if (do_crypt == 0)
		return strcmp(good, guess) == 0;
	cs = crypt(guess, good);
	if (cs)
		return strcmp(good, cs) == 0;
	return 0;
}

static int f_webuserok(const char *authorization, FILE *fp, char *username, int len, int do_crypt)
{
	char buf[128], tmp[128], *p, *q;
	int c, gotcr;
	size_t bp;

	if (strlen(authorization) >= sizeof tmp) {
		log_d("authorization string too long");
		return 0;
	}
	if (base64decode(authorization, tmp) == -1) {
		log_d("could not decode authorization");
		return 0;
	}
	q = strchr(tmp, ':');
	if (q == 0) {
		log_d("no colon in decoded authorization");
		return 0;
	}
	if (username && q >= tmp + len) {
		log_d("supplied username too long");
		return 0;
	}
	*q++ = 0;
	bp = 0;
	gotcr = 0;
	while ((c = getc(fp)) != EOF)
		if (c == '\n') {
			if (bp < sizeof buf) {
				buf[bp] = 0;
				p = strchr(buf, ':');
				if (p) {
					*p++ = 0;
					if (strcmp(tmp, buf) == 0 && pwok(p, q, do_crypt)) {
						if (username)
							strcpy(username, buf);
						return 1;
					}
				}
			}
			bp = 0;
			gotcr = 0;
		} else {
			if (gotcr && bp < sizeof buf)
				buf[bp++] = '\r';
			gotcr = c == '\r';
			if (gotcr == 0 && bp < sizeof buf)
				buf[bp++] = c;
		}
	return 0;
}

int webuserok(const char *authorization, const char *userfilename, char *username, int len, int do_crypt)
{
	FILE *f;
	int fd;
	int retval;

	fd = open(userfilename, O_RDONLY | O_NONBLOCK);
	if (fd == -1) {
		log_d("cannot open userfile %s", userfilename);
		return 0;
	}
	f = fdopen(fd, "r");
	if (f == 0) {
		log_d("webuserok: fdopen failed");
		close(fd);
		return 0;
	}
	retval = f_webuserok(authorization, f, username, len, do_crypt);
	fclose(f);
	return retval;
}
