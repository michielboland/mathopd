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

/* Mysterons */

static const char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <fcntl.h>
#include <pwd.h>
#include <time.h>
#include <unistd.h>
#include "mathopd.h"

static const char m_get[] =			"GET";
static const char m_head[] =			"HEAD";
static const char m_post[] =			"POST";

static time_t timerfc(const char *s)
{
	static const int daytab[2][12] = {
		{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 },
		{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 }
	};
	unsigned sec, min, hour, day, mon, year;
	char month[3];
	int c;
	unsigned n;
	char flag;
	char state;
	char isctime;
	enum { D_START, D_END, D_MON, D_DAY, D_YEAR, D_HOUR, D_MIN, D_SEC };

	sec = 60;
	min = 60;
	hour = 24;
	day = 32;
	year = 1969;
	isctime = 0;
	month[0] = 0;
	state = D_START;
	n = 0;
	flag = 1;
	do {
		c = *s++;
		switch (state) {
		case D_START:
			if (c == ' ') {
				state = D_MON;
				isctime = 1;
			} else if (c == ',')
				state = D_DAY;
			break;
		case D_MON:
			if (isalpha(c)) {
				if (n < 3)
					month[n++] = c;
			} else {
				if (n < 3)
					return -1;
				n = 0;
				state = isctime ? D_DAY : D_YEAR;
			}
			break;
		case D_DAY:
			if (c == ' ' && flag)
				;
			else if (isdigit(c)) {
				flag = 0;
				n = 10 * n + (c - '0');
			} else {
				day = n;
				n = 0;
				state = isctime ? D_HOUR : D_MON;
			}
			break;
		case D_YEAR:
			if (isdigit(c))
				n = 10 * n + (c - '0');
			else {
				year = n;
				n = 0;
				state = isctime ? D_END : D_HOUR;
			}
			break;
		case D_HOUR:
			if (isdigit(c))
				n = 10 * n + (c - '0');
			else {
				hour = n;
				n = 0;
				state = D_MIN;
			}
			break;
		case D_MIN:
			if (isdigit(c))
				n = 10 * n + (c - '0');
			else {
				min = n;
				n = 0;
				state = D_SEC;
			}
			break;
		case D_SEC:
			if (isdigit(c))
				n = 10 * n + (c - '0');
			else {
				sec = n;
				n = 0;
				state = isctime ? D_YEAR : D_END;
			}
			break;
		}
	} while (state != D_END && c);
	switch (month[0]) {
	case 'A':
		mon = (month[1] == 'p') ? 4 : 8;
		break;
	case 'D':
		mon = 12;
		break;
	case 'F':
		mon = 2;
		break;
	case 'J':
		mon = (month[1] == 'a') ? 1 : ((month[2] == 'l') ? 7 : 6);
		break;
	case 'M':
		mon = (month[2] == 'r') ? 3 : 5;
		break;
	case 'N':
		mon = 11;
		break;
	case 'O':
		mon = 10;
		break;
	case 'S':
		mon = 9;
		break;
	default:
		return -1;
	}
	if (year <= 100)
		year += (year < 70) ? 2000 : 1900;
	--mon;
	--day;
	if (sec >= 60 || min >= 60 || hour >= 60 || day >= 31)
		return -1;
	if (year < 1970)
		return 0;
	return sec + 60L * (min + 60L * (hour + 24L * ( day +
	    daytab[year % 4 == 0 && (year % 100 || year % 400 == 0)][mon] +
	    365L * (year - 1970L) + ((year - 1969L) >> 2))));
}

char *rfctime(time_t t, char *buf)
{
	struct tm *tp;

	tp = gmtime(&t);
	if (tp == 0) {
		log_d("gmtime failed!?!?!?");
		strcpy(buf, "?");
		return buf;
	}
	strftime(buf, 31, "%a, %d %b %Y %H:%M:%S GMT", tp);
	return buf;
}

static char *getline(struct pool *p, int fold)
{
	char *s, *olds, *sp, *end;
	int f;

	end = p->end;
	s = p->start;
	if (s >= end)
		return 0;
	olds = s;
	sp = s;
	f = 0;
	while (s < end) {
		switch (*s++) {
		case '\n':
			if (s == end || fold == 0 || (*s != ' ' && *s != '\t')) {
				if (f)
					*sp = 0;
				else
					s[-1] = 0;
				p->start = s;
				return olds;
			}
		case '\r':
		case '\t':
			s[-1] = ' ';
		case ' ':
			if (f == 0) {
				f = 1;
				sp = s - 1;
			}
			break;
		default:
			f = 0;
			break;
		}
	}
	log_d("getline: fallen off the end");
	return 0;
}

static int putstring(struct pool *p, char *s)
{
	size_t l;

	l = strlen(s);
	if (p->end + l > p->ceiling) {
		log_d("no more room to put string!?!?");
		return -1;
	}
	memcpy(p->end, s, l);
	p->end += l;
	return 0;
}

static int output_headers(struct pool *p, struct request *r)
{
	long cl;
	char tmp_outbuf[2048], gbuf[40], *b;
	struct simple_list *h;

	if (r->cn->assbackwards)
		return 0;
	b = tmp_outbuf;
	b += sprintf(b, "HTTP/1.1 %s\r\n"
		"Server: %s\r\n"
		"Date: %s\r\n",
		r->status_line,
		server_version,
		rfctime(current_time, gbuf));
	if (r->allowedmethods)
		b += sprintf(b, "Allow: %s\r\n", r->allowedmethods);
	if (r->c && r->c->realm && r->status == 401)
		b += sprintf(b, "WWW-Authenticate: Basic realm=\"%s\"\r\n", r->c->realm);
	if (r->num_content >= 0) {
		b += sprintf(b, "Content-Type: %s\r\n", r->content_type);
		cl = r->content_length;
		if (cl >= 0)
			b += sprintf(b, "Content-Length: %ld\r\n", cl);
		if (r->last_modified)
			b += sprintf(b, "Last-Modified: %s\r\n", rfctime(r->last_modified, gbuf));
	}
	if (r->location && r->status == 302)
		b += sprintf(b, "Location: %.512s\r\n", r->location);
	if (r->status == 416)
		b += sprintf(b, "Content-Range: bytes */%lu\r\n", r->range_total);
	if (r->status == 206)
		b += sprintf(b, "Content-Range: bytes %lu-%lu/%lu\r\n", r->range_floor, r->range_ceiling, r->range_total);
	if (r->cn->keepalive) {
		if (r->protocol_minor == 0)
			b += sprintf(b, "Connection: keep-alive\r\n");
	} else if (r->protocol_minor)
		b += sprintf(b, "Connection: close\r\n");
	if (r->c && (r->status == 200 || r->status == 206))
		for (h = r->c->extra_headers; h; h = h->next)
			b += sprintf(b, "%s\r\n", h->name);
	b += sprintf(b, "\r\n");
	return putstring(p, tmp_outbuf);
}

static char *dirmatch(char *s, char *t)
{
	size_t n;

	n = strlen(t);
	if (n == 0)
		return s;
	return !strncmp(s, t, n) && (s[n] == '/' || s[n] == 0 || s[n - 1] == '~') ? s + n : 0;
}

static char *exactmatch(char *s, char *t)
{
	size_t n;

	n = strlen(t);
	return !strncmp(s, t, n) && s[n] == '/' && s[n + 1] == 0 ? s + n : 0;
}

static int evaluate_access(unsigned long ip, struct access *a)
{
	while (a && ((ip & a->mask) != a->addr))
		a = a->next;
	return a ? a->type : ALLOW;
}

static int get_mime(struct request *r, const char *s)
{
	struct mime *m;
	char *saved_type;
	int saved_class;
	int l, le, lm;

	saved_type = 0;
	saved_class = 0;
	lm = 0;
	l = strlen(s);
	m = r->c->mimes;
	while (m) {
		if (m->ext) {
			le = strlen(m->ext);
			if (le > lm && le <= l && !strcasecmp(s + l - le, m->ext)) {
				lm = le;
				saved_type = m->name;
				saved_class = m->class;
			}
		} else if (saved_type == 0) {
			saved_type = m->name;
			saved_class = m->class;
		}
		m = m->next;
	}
	if (saved_type) {
		r->content_type = saved_type;
		r->class = saved_class;
		r->num_content = lm;
		return 0;
	}
	log_d("don't know how to handle %s", s);
	return -1;
}

static int get_path_info(struct request *r)
{
	char *p, *pa, *end, *cp, *start;
	struct stat *s;
	int rv;
	size_t m;

	m = r->location_length;
	if (m == 0)
		return -1;
	s = &r->finfo;
	p = r->path_translated;
	start = p + m;
	end = p + strlen(p);
	pa = r->path_args;
	*pa = 0;
	cp = end;
	while (cp >= start && cp[-1] == '/')
		--cp;
	while (cp >= start) {
		if (cp != end)
			*cp = 0;
		rv = stat(p, s);
		if (cp != end)
			*cp = '/';
		if (rv != -1) {
			strcpy(pa, cp);
			if (S_ISDIR(s->st_mode))
				*cp++ = '/';
			*cp = 0;
			return 0;
		} else if (r->c->path_info_ok == 0)
			return -1;
		while (--cp >= start && *cp != '/')
			;
	}
	return -1;
}

static int check_path(struct request *r)
{
	char *p;
	char c;
	enum {
		s_normal,
		s_slash,
		s_slashdot,
		s_slashdotdot,
		s_forbidden
	} s;

	p = r->path;
	s = s_normal;
	do {
		c = *p++;
		switch (s) {
		case s_normal:
			if (c == '/')
				s = s_slash;
			break;
		case s_slash:
			if (c == '/')
				s = s_forbidden;
			else if (c == '.')
				s = r->c->allow_dotfiles ? s_slashdot : s_forbidden;
			else
				s = s_normal;
			break;
		case s_slashdot:
			if (c == 0 || c == '/')
				s = s_forbidden;
			else if (c == '.')
				s = s_slashdotdot;
			else
				s = s_normal;
			break;
		case s_slashdotdot:
			if (c == 0 || c == '/')
				s = s_forbidden;
			else
				s = s_normal;
			break;
		case s_forbidden:
			c = 0;
			break;
		}
	} while (c);
	return s == s_forbidden ? -1 : 0;
}

static int makedir(struct request *r)
{
	int l;

	if (r->args)
		l = snprintf(r->newloc, PATHLEN, "%s/?%s", r->url, r->args);
	else
		l = snprintf(r->newloc, PATHLEN, "%s/", r->url);
	if (l >= PATHLEN) {
		log_d("makedir: url too large");
		return 500;
	}
	r->location = r->newloc;
	return 302;
}

static int append_indexes(struct request *r)
{
	char *p, *q;
	struct simple_list *i;
	int rv;

	p = r->path_translated;
	q = p + strlen(p);
	r->isindex = 1;
	i = r->c->index_names;
	while (i) {
		strcpy(q, i->name);
		rv = stat(p, &r->finfo);
		if (rv != -1)
			break;
		i = i->next;
	}
	if (i == 0) {
		*q = 0;
		return -1;
	}
	return 0;
}

static int process_cgi(struct request *r)
{
	return fork_request(r, exec_cgi);
}

static int process_external(struct request *r)
{
	r->num_content = -1;
	return process_cgi(r);
}

static int process_special(struct request *r)
{
	const char *ct;

	ct = r->content_type;
	r->num_content = -1;
	if (!strcasecmp(ct, CGI_MAGIC_TYPE))
		return process_cgi(r);
	if (!strcasecmp(ct, IMAP_MAGIC_TYPE))
		return process_imap(r);
	if (!strcasecmp(ct, REDIRECT_MAGIC_TYPE))
		return process_redirect(r);
	if (!strcasecmp(ct, DUMP_MAGIC_TYPE))
		return process_dump(r);
	r->error_file = r->c->error_404_file;
	return 404;
}

static int satisfy_range(struct request *r)
{
	r->range_total = r->content_length;
	switch(r->range) {
	case -1:
		if (r->range_suffix == 0)
			return -1;
		r->range_ceiling = r->range_total - 1;
		if (r->range_suffix < r->range_total)
			r->range_floor = r->range_total - r->range_suffix;
		else
			r->range_floor = 0;
		break;
	case 1:
		if (r->range_floor >= r->range_total)
			return -1;
		r->range_ceiling = r->range_total - 1;
		break;
	case 2:
		if (r->range_floor >= r->range_total)
			return -1;
		if (r->range_ceiling >= r->range_total)
			r->range_ceiling = r->range_total - 1;
		break;
	}
	if (r->range_floor == 0 && r->range_ceiling == r->range_total - 1) {
		r->range = 0;
		return 0;
	}
	if (r->if_range_s && r->last_modified > r->if_range) {
		r->range = 0;
		return 0;
	}
	return 0;
}

static int process_fd(struct request *r)
{
	int fd;

	if (r->path_args[0] && r->c->path_args_ok == 0 && (r->path_args[1] || r->isindex == 0)) {
		r->error_file = r->c->error_404_file;
		return 404;
	}
	if (r->method == M_POST)
		return 405;
	fd = open(r->path_translated, O_RDONLY | O_NONBLOCK);
	if (fd == -1) {
		log_d("cannot open %s", r->path_translated);
		lerror("open");
		r->error_file = r->c->error_404_file;
		return 404;
	}
	if (fstat(fd, &r->finfo) == -1) {
		lerror("fstat");
		close(fd);
		return 500;
	}
	if (!S_ISREG(r->finfo.st_mode)) {
		log_d("process_fd: non-regular file %s", r->path_translated);
		close(fd);
		r->error_file = r->c->error_404_file;
		return 404;
	}
	r->content_length = r->finfo.st_size;
	r->last_modified = r->finfo.st_mtime;
	if (r->last_modified <= r->ims) {
		close(fd);
		r->num_content = -1;
		return 304;
	}
	if (r->ius && r->last_modified > r->ius) {
		close(fd);
		return 412;
	}
	if (r->range) {
		if (satisfy_range(r) == -1) {
			close(fd);
			return 416;
		}
		if (r->range) {
			if (r->range_floor)
				lseek(fd, r->range_floor, SEEK_SET);
			r->content_length = r->range_ceiling - r->range_floor + 1;
		}
	}
	if (r->method == M_GET) {
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		r->cn->rfd = fd;
	} else
		close(fd);
	return r->range ? 206 : 200;
}

static int add_fd(struct request *r, const char *filename)
{
	int fd;
	struct stat s;

	if (filename == 0)
		return -1;
	if (get_mime(r, filename) == -1)
		return -1;
	if (r->class != CLASS_FILE)
		return -1;
	fd = open(filename, O_RDONLY | O_NONBLOCK);
	if (fd == -1)
		return -1;
	if (fstat(fd, &s) == -1) {
		lerror("fstat");
		close(fd);
		return -1;
	}
	if (!S_ISREG(s.st_mode)) {
		log_d("non-plain file %s", filename);
		close(fd);
		return -1;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	r->cn->rfd = fd;
	r->content_length = s.st_size;
	return 0;
}

static int find_vs(struct request *r)
{
	struct virtual *v, *d;

	d = 0;
	v = r->cn->s->children;
	if (r->host)
		while (v) {
			if (v->host) {
				if (strcmp(r->host, v->host) == 0)
					break;
			} else if (v->anyhost)
				d = v;
			v = v->next;
		}
	else
		while (v) {
			if (v->host == 0 && v->anyhost == 0)
				break;
			v = v->next;
		}
	if (v == 0) {
		if (d == 0)
			return 1;
		v = d;
	}
	r->vs = v;
	return 0;
}

static int check_realm(struct request *r)
{
	char *a;

	if (r == 0 || r->c == 0 || r->c->realm == 0 || r->c->userfile == 0)
		return 0;
	a = r->authorization;
	if (a == 0)
		return -1;
	if (strncasecmp(a, "basic ", 6))
		return -1;
	a += 6;
	while (*a == ' ')
		++a;
	if (webuserok(a, r->c->userfile, r->user, sizeof r->user, r->c->do_crypt))
		return 0;
	return -1;
}

static size_t expand_hostname(char *dest, const char *source, const char *host, int m)
{
	int c, l, n;

	if (m <= 0) /* should never happen */
		return 0;
	n = m;
	do {
		c = *source++;
		switch (c) {
		case '*':
			dest += l = sprintf(dest, "%.*s", n, host);
			n -= l;
			break;
		default:
			*dest++ = c;
			--n;
			break;
		case 0:
			*dest = 0;
			break;
		}
	} while (n && c);
	return m - n;
}

struct control *faketoreal(char *x, char *y, struct request *r, int update, int maxlen)
{
	struct control *c;
	char *s, *t;
	struct passwd *p;
	int l;

	if (r->vs == 0) {
		log_d("virtualhost not initialized!");
		return 0;
	}
	c = r->vs->vserver->controls;
	while (c) {
		if (c->locations && c->alias) {
			s = c->exact_match ? exactmatch(x, c->alias) : dirmatch(x, c->alias);
			if (s && (c->clients == 0 || evaluate_access(r->cn->peer.sin_addr.s_addr, c->clients) == APPLY)) {
				if (c->user_directory == 0) {
					if (r->host)
						l = expand_hostname(y, c->locations->name, r->host, maxlen - 1);
					else
						l = sprintf(y, "%.*s", maxlen - 1, c->locations->name);
					r->location_length = l;
					if (c->locations->name[0] == '/' || !c->path_args_ok)
						sprintf(y + l, "%.*s", maxlen - (l + 1), s);
					break;
				} else {
					t = strchr(s, '/');
					if (t)
						*t = 0;
					p = getpwnam(s);
					if (t)
						*t = '/';
					if (p == 0 || p->pw_dir == 0)
						return 0;
					l = strlen(p->pw_dir);
					if (l + 2 > maxlen) {
						log_d("overflow in faketoreal");
						return 0;
					}
					l = sprintf(y, "%s/%.*s", p->pw_dir, maxlen - (l + 2), c->locations->name);
					r->location_length = l;
					maxlen -= l + 1;
					if (t && (c->locations->name[0] == '/' || !c->path_args_ok))
						sprintf(y + l, "%.*s", maxlen, t);
					break;
				}
			}
		}
		c = c->next;
	}
	if (c && update)
		c->locations = c->locations->next;
	return c;
}

static int process_path(struct request *r)
{
	switch (find_vs(r)) {
	case -1:
		return 500;
	case 1:
		return 400;
	}
	if ((r->c = faketoreal(r->path, r->path_translated, r, 1, sizeof r->path_translated)) == 0)
		return 500;
	if (r->c->accesses && evaluate_access(r->cn->peer.sin_addr.s_addr, r->c->accesses) == DENY) {
		r->error_file = r->c->error_403_file;
		return 403;
	}
	if (r->c->realm && check_realm(r) == -1) {
		r->error_file = r->c->error_401_file;
		return 401;
	}
	if (r->path_translated[0] == 0)
		return 500;
	if (r->path_translated[0] != '/') {
		r->location = r->path_translated;
		return 302;
	}
	if (check_path(r) == -1) {
		r->error_file = r->c->error_404_file;
		return 404;
	}
	if (get_path_info(r) == -1) {
		r->error_file = r->c->error_404_file;
		return 404;
	}
	if (S_ISDIR(r->finfo.st_mode)) {
		if (r->path_args[0] != '/')
			return makedir(r);
		if (append_indexes(r) == -1) {
			if (r->path_args[1] == 0 && r->c->auto_index_command && *r->c->auto_index_command == '/') {
				if (r->method == M_POST)
					return 405;
				r->content_type = r->c->auto_index_command;
				r->class = CLASS_EXTERNAL;
				return fork_request(r, exec_cgi);
			}
			r->error_file = r->c->error_404_file;
			return 404;
		}
		
	}
	if (!S_ISREG(r->finfo.st_mode)) {
		log_d("%s is not a regular file", r->path_translated);
		r->error_file = r->c->error_404_file;
		return 404;
	}
	if (get_mime(r, r->path_translated) == -1) {
		r->error_file = r->c->error_404_file;
		return 404;
	}
	switch (r->class) {
	case CLASS_FILE:
		return process_fd(r);
	case CLASS_SPECIAL:
		return process_special(r);
	case CLASS_EXTERNAL:
		return process_external(r);
	}
	log_d("unknown class!?");
	return 500;
}

static int parse_range_header(struct request *r, const char *s)
{
	char *t;
	int suffix;
	unsigned long u, v;

	if (strncasecmp(s, "bytes", 5))
		return -1;
	s += 5;
	while (*s == ' ')
		++s;
	if (*s != '=')
		return -1;
	do
		++s;
	while (*s == ' ');
	suffix = *s == '-';
	if (suffix) {
		do
			++s;
		while (*s == ' ');
		if (*s == '-')
			return -1;
	}
	u = strtoul(s, &t, 10);
	if (t == s)
		return -1;
	s = t;
	while (*s == ' ')
		++s;
	if (suffix) {
		if (*s)
			return -1;
		r->range = -1;
		r->range_suffix = u;
		return 0;
	}
	if (*s != '-')
		return -1;
	do
		++s;
	while (*s == ' ');
	if (*s == 0) {
		r->range = 1;
		r->range_floor = u;
		return 0;
	}
	if (*s == '-')
		return -1;
	v = strtoul(s, &t, 10);
	if (t == s)
		return -1;
	s = t;
	while (*s == ' ')
		++s;
	if (*s)
		return -1;
	if (v < u)
		return -1;
	r->range = 2;
	r->range_floor = u;
	r->range_ceiling = v;
	return 0;
}

static int parse_http_version(struct request *r)
{
	const char *v;
	char *e;
	unsigned long ma, mi;

	v = r->version;
	if (v == 0)
		return 0;
	if (strncmp(v, "HTTP", 4))
		return -1;
	v += 4;
	while (*v == ' ')
		++v;
	if (*v != '/')
		return -1;
	do
		++v;
	while (*v == ' ');
	if (*v == '-')
		return -1;
	ma = strtoul(v, &e, 10);
	if (e == v)
		return -1;
	v = e;
	while (*v == ' ')
		++v;
	if (*v != '.')
		return -1;
	do
		++v;
	while (*v == ' ');
	if (*v == '-')
		return -1;
	mi = strtoul(v, &e, 10);
	if (e == v || *e)
		return -1;
	if (ma == 0 || ma > INT_MAX || mi > INT_MAX)
		return -1;
	r->protocol_major = ma;
	r->protocol_minor = mi;
	return 0;
}

static int process_headers(struct request *r)
{
	char *l, *u, *s;
	time_t i;
	size_t n;
	int multiple_range;
	unsigned long cl;

	do {
		l = getline(r->cn->input, 0);
		if (l == 0)
			return -1;
	} while (*l == 0);
	u = strchr(l, ' ');
	if (u == 0)
		return -1;
	r->method_s = l;
	*u++ = 0;
	if (r->cn->assbackwards)
		r->protocol_minor = 9;
	else {
		s = strrchr(u, 'H');
		if (s == 0 || s == u || s[-1] != ' ') {
			log_d("no HTTP-Version in Request-Line");
			return -1;
		}
		r->version = s;
		s[-1] = 0;
	}
	r->url = u;
	s = strchr(u, '?');
	if (s) {
		r->args = s + 1;
		*s = 0;
	}
	if (parse_http_version(r) == -1)
		return 400;
	n = 0;
	multiple_range = 0;
	while ((l = getline(r->cn->input, 1)) != 0) {
		s = strchr(l, ':');
		if (s == 0)
			continue;
		*s++ = 0;
		while (*s == ' ')
			++s;
		if (*s == 0)
			continue;
		if (n < tuning.num_headers) {
			r->headers[n].rh_name = l;
			r->headers[n++].rh_value = s;
		}
		if (!strcasecmp(l, "User-agent"))
			r->user_agent = s;
		else if (!strcasecmp(l, "Referer"))
			r->referer = s;
		else if (!strcasecmp(l, "Authorization"))
			r->authorization = s;
		else if (!strcasecmp(l, "Host")) {
			sanitize_host(s);
			r->host = s;
		} else if (!strcasecmp(l, "Connection"))
			r->connection = s;
		else if (!strcasecmp(l, "If-Modified-Since"))
			r->ims_s = s;
		else if (!strcasecmp(l, "If-Unmodified-Since"))
			r->ius_s = s;
		else if (!strcasecmp(l, "Content-Type"))
			r->in_content_type = s;
		else if (!strcasecmp(l, "Content-Length"))
			r->in_content_length = s;
		else if (!strcasecmp(l, "Range")) {
			if (r->range_s)
				multiple_range = 1;
			else
				r->range_s = s;
		} else if (!strcasecmp(l, "If-Range"))
			r->if_range_s = s;
		else if (!strcasecmp(l, "Transfer-Encoding"))
			r->in_transfer_encoding = s;
	}
	r->nheaders = n;
	s = r->method_s;
	if (strcmp(s, m_get) == 0)
		r->method = M_GET;
	else {
		if (r->cn->assbackwards)
			return 501;
		if (strcmp(s, m_head) == 0)
			r->method = M_HEAD;
		else if (strcmp(s, m_post) == 0)
			r->method = M_POST;
		else
			return 501;
	}
	s = r->url;
	if (strlen(s) > STRLEN)
		return 414;
	if (*s != '/') {
		if (r->cn->assbackwards)
			return 400;
		u = strchr(s, '/');
		if (u == 0 || u[1] != '/' || u[2] == 0 || u[2] == '/')
			return 400;
		u += 2;
		s = strchr(u, '/');
		if (s == 0)
			return 400;
		memcpy(r->rhost, u, s - u);
		r->rhost[s - u] = 0;
		r->host = r->rhost;
		sanitize_host(r->host);
	}
	if (unescape_url(s, r->path) == -1)
		return 400;
	if (r->protocol_major > 1 || (r->protocol_major == 1 && r->protocol_minor > 1)) {
		log_d("%s: unsupported version HTTP/%d.%d", inet_ntoa(r->cn->peer.sin_addr), r->protocol_major, r->protocol_minor);
			return 505;
	}
	if (r->protocol_major) {
		s = r->connection;
		if (r->protocol_minor)
			r->cn->keepalive = !(s && strcasecmp(s, "close") == 0);
		else
			r->cn->keepalive = s && strcasecmp(s, "keep-alive") == 0;
	}
	if (r->in_transfer_encoding) {
		if (strcasecmp(r->in_transfer_encoding, "chunked")) {
			log_d("unimplemented transfer-coding \"%s\"", r->in_transfer_encoding);
			return 501;
		}
		if (r->in_content_length) {
			log_d("ignoring Content-Length header from client");
			r->in_content_length = 0;
		}
	}
	s = r->in_content_length;
	if (s) {
		if (*s == '-' || (cl = strtoul(s, &u, 10), u == s || *u || cl >= UINT_MAX)) {
			log_d("bad Content-Length from client: \"%s\"", s);
			return 400;
		}
		r->in_mblen = cl;
	}
	if (r->method == M_GET) {
		s = r->ims_s;
		if (s) {
			i = timerfc(s);
			if (i != -1)
				r->ims = i;
		}
		s = r->ius_s;
		if (s) {
			i = timerfc(s);
			if (i != -1)
				r->ius = i;
		}
		s = r->if_range_s;
		if (s) {
			i = timerfc(s);
			if (i != -1)
				r->if_range = i;
		}
		if (multiple_range)
			log_d("multiple Range headers!?");
		else {
			s = r->range_s;
			if (s) {
				if (parse_range_header(r, s) == -1)
					log_d("ignoring Range header \"%s\"", s);
			}
		}
	} else if (r->method == M_POST) {
		if (r->in_content_length == 0)
			return 411;
	}
	return 0;
}

static int prepare_reply(struct request *r)
{
	struct pool *p;
	char *b, buf[200];
	int send_message;

	send_message = r->method != M_HEAD;
	if (r->status >= 400)
		r->last_modified = 0;
	switch (r->status) {
	case 200:
		r->status_line = "200 OK";
		send_message = 0;
		break;
	case 204:
		r->status_line = "204 No Content";
		send_message = 0;
		break;
	case 206:
		r->status_line = "206 Partial Content";
		send_message = 0;
		break;
	case 302:
		r->status_line = "302 Moved";
		break;
	case 304:
		r->status_line = "304 Not Modified";
		send_message = 0;
		break;
	case 400:
		r->status_line = "400 Bad Request";
		break;
	case 401:
		r->status_line = "401 Not Authorized";
		break;
	case 403:
		r->status_line = "403 Forbidden";
		break;
	case 404:
		r->status_line = "404 Not Found";
		break;
	case 405:
		r->status_line = "405 Method Not Allowed";
		r->allowedmethods = "GET, HEAD";
		break;
	case 411:
		r->status_line = "411 Length Required";
		break;
	case 412:
		r->status_line = "412 Precondition Failed";
		break;
	case 414:
		r->status_line = "414 Request-URI Too Long";
		break;
	case 416:
		r->status_line = "416 Requested Range Not Satisfiable";
		break;
	case 501:
		r->status_line = "501 Not Implemented";
		break;
	case 503:
		r->status_line = "503 Service Unavailable";
		break;
	case 505:
		r->status_line = "505 HTTP Version Not Supported";
		break;
	default:
		r->status_line = "500 Internal Server Error";
		break;
	}
	if (r->error_file) {
		if (send_message && add_fd(r, r->error_file) != -1)
			send_message = 0;
	}
	if (send_message) {
		b = buf;
		b += sprintf(b, "<title>%s</title>\n"
			"<h1>%s</h1>\n", r->status_line, r->status_line);
		switch (r->status) {
		case 302:
			b += sprintf(b, "This document has moved to URL <a href=\"%s\">%s</a>.\n", r->location, r->location);
			break;
		case 401:
			b += sprintf(b, "You need proper authorization to use this resource.\n");
			break;
		case 400:
		case 405:
		case 501:
		case 505:
			b += sprintf(b, "Your request was not understood or not allowed by this server.\n");
			break;
		case 403:
			b += sprintf(b, "Access to this resource has been denied to you.\n");
			break;
		case 404:
			b += sprintf(b, "The resource requested could not be found on this server.\n");
			break;
		case 503:
			b += sprintf(b, "The server is temporarily busy.\n");
			break;
		}
		if (r->c && r->c->admin)
			b += sprintf(b, "<p>Please contact the site administrator at <i>%.100s</i>.\n", r->c->admin);
		r->content_length = strlen(buf);
		r->num_content = 0;
		r->content_type = "text/html";
	}
	if (r->status >= 400 && r->method != M_GET && r->method != M_HEAD)
		r->cn->keepalive = 0;
	p = r->cn->output;
	return (output_headers(p, r) == -1 || (send_message && putstring(p, buf) == -1)) ? -1 : 0;
}

void init_request(struct request *r)
{
	r->vs = 0;
	r->user_agent = 0;
	r->referer = 0;
	r->authorization = 0;
	r->host = 0;
	r->in_content_type = 0;
	r->in_content_length = 0;
	r->connection = 0;
	r->ims_s = 0;
	r->path[0] = 0;
	r->path_translated[0] = 0;
	r->path_args[0] = 0;
	r->num_content = -1;
	r->class = 0;
	r->content_length = -1;
	r->last_modified = 0;
	r->ims = 0;
	r->location = 0;
	r->status_line = 0;
	r->method_s = 0;
	r->url = 0;
	r->args = 0;
	r->version = 0;
	r->protocol_major = 0;
	r->protocol_minor = 0;
	r->method = 0;
	r->status = 0;
	r->isindex = 0;
	r->c = 0;
	r->error_file = 0;
	r->user[0] = 0;
	r->allowedmethods = 0;
	r->location_length = 0;
	r->nheaders = 0;
	r->range_s = 0;
	r->if_range_s = 0;
	r->if_range = 0;
	r->range = 0;
	r->range_floor = 0;
	r->range_ceiling = 0;
	r->range_suffix = 0;
	r->range_total = 0;
	r->ius_s = 0;
	r->ius = 0;
	r->rhost[0] = 0;
	r->in_transfer_encoding = 0;
	r->in_mblen = 0;
}

int process_request(struct request *r)
{
	if ((r->status = process_headers(r)) == 0)
		r->status = process_path(r);
	if (r->status > 0 && prepare_reply(r) == -1) {
		log_d("cannot prepare reply for client");
		return -1;
	}
	return r->status >= 0 ? 0 : -1;
}

int cgi_error(struct request *r)
{
	r->status = 500;
	return prepare_reply(r);
}
