/*
 * extras.c - Mathopd server extensions
 *
 * Copyright 1996, 1997, Michiel Boland
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
	return (((c[0].x >= p.x) != (c[1].x >= p.x))
		&& ((c[0].y >= p.y) != (c[1].y >= p.y)));
}

static long sqr(long x)
{
	return x * x;
}

static int pointincircle(point p, point c[])
{
	return (sqr(c[0].x - p.x) + sqr(c[0].y - p.y)
		<= sqr(c[0].x - c[1].x) + sqr(c[0].y - c[1].y));
}

static int pointinpoly(point t, point a[], int n)
{
	int xflag, yflag, ysign, index;
	point *p, *q, *stop;

	if (n < 3)
		return 0;
	index = 0;
	q = a;
	stop = a + n;
	p = stop - 1;

	while (q < stop) {
		if ((yflag = (p->y >= t.y)) != (q->y >= t.y)) {
			ysign = yflag ? -1 : 1;
			if ((xflag = (p->x >= t.x)) == (q->x >= t.x)) {
				if (xflag)
					index += ysign;
			}
			else if (ysign * ((p->x - t.x) * (q->y - p->y) -
					  (p->y - t.y) * (q->x - p->x)) >= 0)
				index += ysign;
		}
		++q;
		if (++p == stop)
			p = a;
	}
	return index;
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
	s[-1] = '\0';
	return 0;
}

int process_imap(struct request *r)
{
	char input[PATHLEN], default_url[PATHLEN];
	point testpoint, pointarray[MAXVERTS];
	long dist, mindist;
	int k, line, sawpoint, text;
	char *s, *t, *u, *v, *w, *url;	const char *status;
	FILE *fp;
	static STRING(comma) = ", ()\t\r\n";

	if (r->method == M_HEAD)
		return 204;
	else if (r->method != M_GET) {
		r->error = "invalid method for imagemap";
		return 405;
	}
	s = r->path_translated;
	testpoint.x = 0;
	testpoint.y = 0;
	text = 1;
	if (r->args && (t = strchr(r->args, ',')) != 0) {
		*t++ = '\0';
		testpoint.x = atol(r->args);
		testpoint.y = atol(t);
		if (testpoint.x || testpoint.y)
			text = 0;
	}
	if ((fp = fopen(s, "r")) == 0) {
		lerror("fopen");
		r->error = "cannot open map file";
		return 500;
	}

	line = 0;
	sawpoint = 0;
	*default_url = '\0';
	mindist = 0;
	status = 0;
	url = 0;

	while (fgetline(input, PATHLEN, fp) != -1) {
		++line;
		k = 0;
		t = strtok(input, comma);
		if (t == 0 || *t == '\0' || *t == '#')
			continue;
		u = strtok(0, comma);
		if (u == 0 || *u == '\0') {
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

		if (streq(t, "default"))
			strcpy(default_url, u);

		else if (streq(t, "text")) {
			if (text) {
				url = u;
				break;
			}
		}

		else if (streq(t, "point")) {
			if (k < 1) {
				status = "no point";
				break;
			}
			dist = sqr(pointarray[0].x - testpoint.x) +
				   sqr(pointarray[0].y - testpoint.y);
			if (sawpoint == 0 || dist < mindist) {
				sawpoint=1;
				mindist = dist;
				strcpy(default_url, u);
			}
		}

		else if (streq(t, "rect")) {
			if (k < 2) {
				status = "too few rect points";
				break;
			}
			if (pointinrect(testpoint, pointarray)) {
				url = u;
				break;
			}
		}

		else if (streq(t, "circle")) {
			if (k < 2) {
				status = "too few circle points";
				break;
			}
			if (pointincircle(testpoint, pointarray)) {
				url = u;
				break;
			}
		}

		else if (streq(t, "spoly")) {
			if (k < 3) {
				status = "too few spoly points";
				break;
			}
			if (pointinpoly(testpoint, pointarray, k)) {
				url = u;
				break;
			}
		}

		else if (streq(t, "poly")) {
			if (k < 3) {
				status = "too few poly points";
				break;
			}
			if (pointinpoly(testpoint, pointarray, k) & 1) {
				url = u;
				break;
			}
		}

		else {
			status = "unknown keyword";
			break;
		}
	}
	fclose(fp);
	if (status) {
		r->error = status;
		log(L_ERROR, "imagemap: %s on line %d of %s", status, line, s);
		return 500;
	}
	if (url) {
		static char buf[PATHLEN];

		strcpy(buf, url);
		if (*buf == '/')
			construct_url(buf, url, r->vs);
		escape_url(buf);
		r->location = buf;
		return 302;
	}
	return 204;
}
