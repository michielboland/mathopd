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

/* Hanc marginis exiguitas non caperet */

#include "mathopd.h"

#define MAXVERTS 100

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
	register int c;

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

static int f_process_imap(struct request *r, FILE *fp)
{
	char input[STRLEN], default_url[STRLEN];
	point testpoint, pointarray[MAXVERTS];
	long dist, mindist;
	int k, line, sawpoint, text;
	char *t, *u, *v, *w, *url;
	const char *status;
	static const char comma[] = ", ()\t\r\n";

	testpoint.x = 0;
	testpoint.y = 0;
	text = 1;
	if (r->args) {
		t = strchr(r->args, ',');
		if (t) {
			*t++ = 0;
			testpoint.x = atol(r->args);
			testpoint.y = atol(t);
			if (testpoint.x || testpoint.y)
				text = 0;
		}
	}
	line = 0;
	sawpoint = 0;
	*default_url = 0;
	mindist = 0;
	status = 0;
	url = 0;
	while (fgetline(input, STRLEN, fp) != -1) {
		++line;
		k = 0;
		t = strtok(input, comma);
		if (t == 0 || *t == 0 || *t == '#')
			continue;
		u = strtok(0, comma);
		if (u == 0 || *u == 0) {
			status = "Missing URL";
			break;
		}
		while ((v = strtok(0, comma)) != 0) {
			if (k >= MAXVERTS)
				break;
			if ((w = strtok(0, comma)) == 0) {
				k = -1;
				break;
			}
			pointarray[k].x = atol(v);
			pointarray[k++].y = atol(w);
		}
		if (k >= MAXVERTS) {
			status = "too many points";
			break;
		}
		if (k == -1) {
			status = "odd number of coords";
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
			dist = sqr(pointarray[0].x - testpoint.x) + sqr(pointarray[0].y - testpoint.y);
			if (sawpoint == 0 || dist < mindist) {
				sawpoint=1;
				mindist = dist;
				strcpy(default_url, u);
			}
		} else if (!strcmp(t, "rect")) {
			if (k < 2) {
				status = "too few rect points";
				break;
			}
			if (pointinrect(testpoint, pointarray)) {
				url = u;
				break;
			}
		} else if (!strcmp(t, "circle")) {
			if (k < 2) {
				status = "too few circle points";
				break;
			}
			if (pointincircle(testpoint, pointarray)) {
				url = u;
				break;
			}
		} else if (!strcmp(t, "spoly")) {
			if (k < 3) {
				status = "too few spoly points";
				break;
			}
			if (pointinpoly(testpoint, pointarray, k)) {
				url = u;
				break;
			}
		} else if (!strcmp(t, "poly")) {
			if (k < 3) {
				status = "too few poly points";
				break;
			}
			if (pointinpoly(testpoint, pointarray, k) & 1) {
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
	if (url) {
		escape_url(url, r->newloc);
		r->location = r->newloc;
		return 302;
	}
	return 204;
}

int process_imap(struct request *r)
{
	FILE *fp;
	int retval;

	if (r->method == M_HEAD)
		return 204;
	else if (r->method != M_GET)
		return 405;
	fp = fopen(r->path_translated, "r");
	if (fp == 0) {
		log_d("cannot open map file %.200s", r->path_translated);
		lerror("fopen");
		return 500;
	}
	retval = f_process_imap(r, fp);
	fclose(fp);
	return retval;
}
