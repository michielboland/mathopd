/*
 *   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003 Michiel Boland.
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

/* Hanc marginis exiguitas non caperet */

static const char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "mathopd.h"

#define MAXVERTS 100
#define MAXLINES 1000

typedef struct {
	long x;
	long y;
} point;

static int pointinrect(point p, point c[])
{
	return (((c[0].x >= p.x) != (c[1].x >= p.x)) && ((c[0].y >= p.y) != (c[1].y >= p.y)));
}

static long sqr(long x)
{
	return x * x;
}

static int pointincircle(point p, point c[])
{
	return (sqr(c[0].x - p.x) + sqr(c[0].y - p.y) <= sqr(c[0].x - c[1].x) + sqr(c[0].y - c[1].y));
}

static int pointinpoly(point t, point a[], int n)
{
	int xflag, yflag, ysign, idx;
	point *p, *q, *stop;

	if (n < 3)
		return 0;
	idx = 0;
	q = a;
	stop = a + n;
	p = stop - 1;
	while (q < stop) {
		yflag = p->y >= t.y;
		if (yflag != (q->y >= t.y)) {
			ysign = yflag ? -1 : 1;
			xflag = p->x >= t.x;
			if (xflag == (q->x >= t.x)) {
				if (xflag)
					idx += ysign;
			} else if (ysign * ((p->x - t.x) * (q->y - p->y) - (p->y - t.y) * (q->x - p->x)) >= 0)
				idx += ysign;
		}
		++q;
		if (++p == stop)
			p = a;
	}
	return idx;
}

static int fgetline(char *s, int n, FILE *stream)
{
	int c;

	do {
		if ((c = getc(stream)) == EOF)
			return -1;
		if (n > 1) {
			--n;
			*s++ = (char) c;
		}
	} while (c != '\n');
	s[-1] = 0;
	return 0;
}

struct token {
	int pos;
	int len;
};

static int separate(const char *s, struct token *t, int m)
{
	int c, i, j, n, state;

	i = 0;
	j = 0;
	n = 0;
	state = 0;
	while (n < m) {
		c = s[i];
		switch (state) {
		case 0:
			switch (c) {
			case 0:
			case '\r':
			case '\n':
			case '#':
				return n;
			case ',':
			case ' ':
			case '(':
			case ')':
			case '\t':
				break;
			default:
				j = i;
				state = 1;
				break;
			}
			break;
		case 1:
			switch (c) {
			case 0:
			case '\r':
			case '\n':
			case '#':
				t[n].pos = j;
				t[n++].len = i - j;
				return n;
			case ',':
			case ' ':
			case '(':
			case ')':
			case '\t':
				t[n].pos = j;
				t[n++].len = i - j;
				state = 0;
				break;
			}
		}
		++i;
	}
	return n;
}

static int f_process_imap(struct request *r, FILE *fp)
{
	char input[PATHLEN], default_url[PATHLEN];
	struct token tok[2 * MAXVERTS + 2];
	point testpoint, pointarray[MAXVERTS];
	long dist, mindist;
	int i, k, l, line, sawpoint, text;
	char *t, *u, *url;
	const char *status;

	testpoint.x = 0;
	testpoint.y = 0;
	text = 1;
	if (r->args) {
		t = strchr(r->args, ',');
		if (t == 0 || *++t == 0)
			return 400;
		testpoint.x = atol(r->args);
		testpoint.y = atol(t);
		if (testpoint.x || testpoint.y)
			text = 0;
	}
	line = 0;
	sawpoint = 0;
	*default_url = 0;
	mindist = 0;
	status = 0;
	url = 0;
	while (fgetline(input, PATHLEN, fp) != -1) {
		if (++line > MAXLINES) {
			status = "too many lines";
			break;
		}
		l = separate(input, tok, 2 * MAXVERTS + 2);
		if (l < 2)
			continue;
		if (l % 2) {
			status = "odd number of coords";
			break;
		}
		t = input + tok[0].pos;
		t[tok[0].len] = 0;
		u = input + tok[1].pos;
		u[tok[1].len] = 0;
		i = 2;
		k = 0;
		while (i < l) {
			pointarray[k].x = atol(input + tok[i++].pos);
			pointarray[k++].y = atol(input + tok[i++].pos);
		}
		if (k >= MAXVERTS) {
			status = "too many points";
			break;
		}
		if (!strcmp(t, "default"))
			strcpy(default_url, u);
		else if (!strcmp(t, "text")) {
			if (text) {
				url = u;
				break;
			}
		} else if (!strcmp(t, "point")) {
			if (k < 1) {
				status = "no point";
				break;
			}
			if (text == 0) {
				dist = sqr(pointarray[0].x - testpoint.x) + sqr(pointarray[0].y - testpoint.y);
				if (sawpoint == 0 || dist < mindist) {
					sawpoint = 1;
					mindist = dist;
					strcpy(default_url, u);
				}
			}
		} else if (!strcmp(t, "rect")) {
			if (k < 2) {
				status = "too few rect points";
				break;
			}
			if (text == 0 && pointinrect(testpoint, pointarray)) {
				url = u;
				break;
			}
		} else if (!strcmp(t, "circle")) {
			if (k < 2) {
				status = "too few circle points";
				break;
			}
			if (text == 0 && pointincircle(testpoint, pointarray)) {
				url = u;
				break;
			}
		} else if (!strcmp(t, "spoly")) {
			if (k < 3) {
				status = "too few spoly points";
				break;
			}
			if (text == 0 && pointinpoly(testpoint, pointarray, k)) {
				url = u;
				break;
			}
		} else if (!strcmp(t, "poly")) {
			if (k < 3) {
				status = "too few poly points";
				break;
			}
			if (text == 0 && pointinpoly(testpoint, pointarray, k) & 1) {
				url = u;
				break;
			}
		} else {
			status = "unknown keyword";
			break;
		}
	}
	if (status) {
		log_d("imagemap: error on line %d of %s", line, r->path_translated);
		log_d("imagemap: %s", status);
		return 500;
	}
	if (url == 0)
		if (*default_url)
			url = default_url;
	if (url) {
		l = snprintf(r->newloc, PATHLEN, "%s", url);
		if (l >= PATHLEN) {
			log_d("imagemap: url too large");
			return 500;
		}
		r->location = r->newloc;
		return 302;
	}
	return 204;
}

int process_imap(struct request *r)
{
	FILE *fp;
	int fd;
	int retval;

	if (r->method == M_HEAD)
		return 204;
	else if (r->method != M_GET)
		return 405;
	fd = open(r->path_translated, O_RDONLY | O_NONBLOCK);
	if (fd == -1) {
		log_d("cannot open map file %s", r->path_translated);
		lerror("open");
		return 500;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fp = fdopen(fd, "r");
	if (fp == 0) {
		log_d("process_imap: fdopen failed");
		close(fd);
		return 500;
	}
	retval = f_process_imap(r, fp);
	fclose(fp);
	return retval;
}
