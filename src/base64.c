/*
 *   Copyright 1996, 1997, 1998, 1999 Michiel Boland.
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

#include "mathopd.h"

static unsigned char b64[256];

void base64initialize(void)
{
	int i;

	memset(b64, 64, sizeof b64);
	for (i = 'A'; i <= 'Z'; i++)
		b64[i] = i - 'A';
	for (i = 'a'; i <= 'z'; i++)
		b64[i] = i - 'a' + 26;
	for (i = '0'; i <= '9'; i++)
		b64[i] = i - '0' + 52;
	b64['+'] = 62;
	b64['/'] = 63;
	b64['='] = 0;
}

static int base64decode(const unsigned char *encoded, unsigned char *decoded)
{
	register int c;
	register unsigned char t1, t2, u1, u2, u3;

	while (1) {
		c = *encoded++;
		if (c == 0) {
			*decoded = 0;
			return 0;
		}
		t1 = b64[c];
		if (t1 == 64)
			return -1;
		c = *encoded++;
		if (c == 0)
			return -1;
		t2 = b64[c];
		if (t2 == 64)
			return -1;
		u1 = t1 << 2 | t2 >> 4;
		c = *encoded++;
		if (c == 0)
			return -1;
		t1 = b64[c];
		if (t1 == 64)
			return -1;
		u2 = (t2 & 0xf) << 4 | t1 >> 2;
		c = *encoded++;
		if (c == 0)
			return -1;
		t2 = b64[c];
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

static int f_webuserok(const char *authorization, FILE *fp, char *username, int len)
{
	char buf[128], tmp[128], *p;
	register int c, bp, skipline;

	if (strlen(authorization) >= sizeof tmp)
		return 0;
	if (base64decode(authorization, tmp) == -1)
		return 0;
	bp = 0;
	skipline = 0;
	while ((c = getc(fp)) != EOF) {
		if (c == '\n') {
			if (skipline == 0) {
				buf[bp] = 0;
				if (strcmp(tmp, buf) == 0) {
					if (username && len) {
						p = strchr(buf, ':');
						if (p)
							*p = 0;
						if (buf + len - 1 > p)
							strcpy(username, buf);
						else
							return 0;
					}
					return 1;
				}
			}
			bp = 0;
			skipline = 0;
		} else if (skipline == 0) {
			buf[bp++] = c;
			if (bp == sizeof buf)
				skipline = 1;
		}
	}
	return 0;
}

int webuserok(const char *authorization, const char *userfilename, char *username, int len)
{
	FILE *f;
	int retval;

	f = fopen(userfilename, "r");
	if (f == 0) {
		log_d("cannot open userfile %s", userfilename);
		return 0;
	}
	retval = f_webuserok(authorization, f, username, len);
	fclose(f);
	return retval;
}
