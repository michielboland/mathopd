/*
 *   Copyright 1996, 1997, 1998, 1999, 2000, 2001 Michiel Boland.
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
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include "mathopd.h"

static const char m_get[] =			"GET";
static const char m_head[] =			"HEAD";
static const char m_post[] =			"POST";

static time_t timerfc(char *s)
{
	static const int daytab[2][12] = {
		{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 },
		{0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 }
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
	if (sec >= 60 || min >= 60 || hour >= 60 || day >= 31 || year < 1970)
		return -1;
	return sec + 60L * (min + 60L * (hour + 24L * (
		day + daytab[year % 4 == 0][mon] + 365L * (year - 1970L) + ((year - 1969L) >> 2))));
}

static char *rfctime(time_t t, char *buf)
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

static char *getline(struct pool *p)
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
			if (s == end || (*s != ' ' && *s != '\t')) {
				if (f)
					*sp = 0;
				else
					s[-1] = 0;
				p->start = s;
				return olds;
			}
		case '\r':
		case '\t':
			if (f == 0) {
				f = 1;
				sp = s - 1;
			}
			s[-1] = ' ';
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
	unsigned long port;
	const char *protocol;

	if (r->cn->assbackwards)
		return 0;
	b = tmp_outbuf;
	b += sprintf(b, "HTTP/%d.%d %s\r\n"
		"Server: %s\r\n"
		"Date: %s\r\n",
		r->protocol_major, r->protocol_minor, r->status_line,
		server_version,
		rfctime(current_time, gbuf));
	if (r->allowedmethods)
		b += sprintf(b, "Allow: %s\r\n", r->allowedmethods);
	if (r->c && r->c->realm && r->status == 401)
		b += sprintf(b, "WWW-Authenticate: Basic realm=\"%s\"\r\n", r->c->realm);
	if (r->num_content >= 0) {
		b += sprintf(b, "Content-type: %s\r\n", r->content_type);
		cl = r->content_length;
		if (cl >= 0)
			b += sprintf(b, "Content-length: %ld\r\n", cl);
		if (r->last_modified)
			b += sprintf(b, "Last-Modified: %s\r\n", rfctime(r->last_modified, gbuf));
	}
	if (r->location && r->status == 302) {
		if (r->location[0] == '/' && r->servername) {
			port = r->cn->s->port;
			protocol = r->cn->s->protocol;
			if (protocol == 0)
				protocol = "http";
			if (port == 80)
				b += sprintf(b, "Location: %s://%s%.512s\r\n", protocol, r->servername, r->location);
			else
				b += sprintf(b, "Location: %s://%s:%lu%.512s\r\n", protocol, r->servername, port, r->location);
		} else
			b += sprintf(b, "Location: %.512s\r\n", r->location);
	}
	if (r->cn->keepalive) {
		if (r->protocol_minor == 0)
			b += sprintf(b, "Connection: Keep-Alive\r\n");
	} else if (r->protocol_minor)
		b += sprintf(b, "Connection: Close\r\n");
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
		}
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
		s_forbidden,
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

	p = r->path_translated;
	q = p + strlen(p);
	r->isindex = 1;
	i = r->c->index_names;
	while (i) {
		strcpy(q, i->name);
		if (stat(p, &r->finfo) != -1)
			break;
		i = i->next;
	}
	if (i == 0) {
		*q = 0;
		r->error_file = r->c->error_404_file;
		return 404;
	}
	return 0;
}

static int process_external(struct request *r)
{
	r->num_content = -1;
	return process_cgi(r);
}

static int process_dummy(struct request *r)
{
	r->error_file = r->c->error_404_file;
	return 404;
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
	if (!strcasecmp(ct, DUMMY_MAGIC_TYPE))
		return process_dummy(r);
	log_d("don't know how to process '%s' specialties", ct);
	return 500;
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
	if (r->method == M_GET) {
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		r->cn->rfd = fd;
	} else
		close(fd);
	return 200;
}

static int add_fd(struct request *r, const char *filename)
{
	int fd;
	struct stat s;

	if (filename == 0)
		return -1;
	if (get_mime(r, filename) == -1)
		return -1;
	fd = open(filename, O_RDONLY);
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

static int hostmatch(const char *s, const char *t)
{
	size_t l;

	l = strlen(t);
	if (strncasecmp(s, t, l))
		return 0;
	switch (s[l]) {
	case 0:
	case ':':
		return 1;
	case '.':
		if (s[l + 1] == 0)
			return 1;
	}
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
				if (hostmatch(r->host, v->host))
					break;
			} else if (v->anyhost)
				d = v;
			v = v->next;
		}
	else
		while (v) {
			if (v->host == 0) {
				if (v->anyhost)
					d = v;
				else
					break;
			}
			v = v->next;
		}
	if (v == 0) {
		if (d == 0)
			return 1;
		v = d;
	}
	r->vs = v;
	if (v->host)
		r->servername = v->host;
	else
		r->servername = r->cn->s->s_name;
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
	if (strncasecmp(a, "basic", 5))
		return -1;
	a += 5;
	while (*a == ' ')
		++a;
	if (webuserok(a, r->c->userfile, r->user, sizeof r->user, r->c->do_crypt))
		return 0;
	return -1;
}

struct control *faketoreal(char *x, char *y, struct request *r, int update)
{
	unsigned long ip;
	struct control *c;
	char *s;

	if (r->vs == 0) {
		log_d("virtualhost not initialized!");
		return 0;
	}
	ip = r->cn->peer.sin_addr.s_addr;
	s = 0;
	c = r->vs->controls;
	while (c) {
		if (c->locations && c->alias) {
			s = c->exact_match ? exactmatch(x, c->alias) : dirmatch(x, c->alias);
			if (s && (c->clients == 0 || evaluate_access(ip, c->clients) == APPLY))
				break;
		}
		c = c->next;
	}
	if (c) {
		if (update)
			c->locations = c->locations->next;
		strcpy(y, c->locations->name);
		r->location_length = strlen(y);
		if (c->locations->name[0] == '/' || !c->path_args_ok)
			strcat(y, s);
	}
	return c;
}

static int process_path(struct request *r)
{
	int rv;

	switch (find_vs(r)) {
	case -1:
		return 500;
	case 1:
		return 404;
	}
	if ((r->c = faketoreal(r->path, r->path_translated, r, 1)) == 0)
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
	if (get_path_info(r) == -1)
		return 500;
	if (S_ISDIR(r->finfo.st_mode)) {
		if (r->path_args[0] != '/')
			return makedir(r);
		rv = append_indexes(r);
		if (rv)
			return rv;
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

static int process_headers(struct request *r)
{
	char *l, *u, *s, *t;
	unsigned long x, y;
	time_t i;

	while (1) {
		l = getline(r->cn->input);
		if (l == 0) {
			return -1;
		}
		while (*l == ' ')
			++l;
		u = strchr(l, ' ');
		if (u)
			break;
		log_d("%s: ignoring garbage \"%s\"", inet_ntoa(r->cn->peer.sin_addr), l);
	}
	r->method_s = l;
	*u++ = 0;
	while (*u == ' ')
		++u;
	s = strrchr(u, ' ');
	if (s) {
		r->version = s + 1;
		do {
			*s-- = 0;
		} while (*s == ' ');
	}
	r->url = u;
	s = strchr(u, '?');
	if (s) {
		r->args = s + 1;
		*s = 0;
	}
	while ((l = getline(r->cn->input)) != 0) {
		s = strchr(l, ':');
		if (s == 0)
			continue;
		*s++ = 0;
		while (*s == ' ')
			++s;
		if (*s == 0)
			continue;
		if (!strcasecmp(l, "User-agent"))
			r->user_agent = s;
		else if (!strcasecmp(l, "Referer"))
			r->referer = s;
		else if (!strcasecmp(l, "From"))
			r->from = s;
		else if (!strcasecmp(l, "Authorization"))
			r->authorization = s;
		else if (!strcasecmp(l, "Cookie"))
			r->cookie = s;
		else if (!strcasecmp(l, "Host"))
			r->host = s;
		else if (!strcasecmp(l, "Connection"))
			r->connection = s;
		else if (!strcasecmp(l, "If-modified-since"))
			r->ims_s = s;
		else if (!strcasecmp(l, "Content-type"))
			r->in_content_type = s;
		else if (!strcasecmp(l, "Content-length"))
			r->in_content_length = s;
	}
	s = r->method_s;
	if (s == 0) {
		log_d("method_s == 0 !?");
		return -1;
	}
	if (strcmp(s, m_get) == 0) {
		r->method = M_GET;
	} else {
		if (r->cn->assbackwards) {
			log_d("method \"%s\" not implemented for old-style connections", s);
			return 501;
		}
		if (strcmp(s, m_head) == 0)
			r->method = M_HEAD;
		else if (strcmp(s, m_post) == 0)
			r->method = M_POST;
		else {
			log_d("method \"%s\" not implemented", s);
			return 501;
		}
	}
	s = r->url;
	if (s == 0) {
		log_d("url == 0 !?");
		return -1;
	}
	if (strlen(s) > STRLEN)
		return 400;
	if (unescape_url(s, r->path) == -1)
		return 400;
	if (r->path[0] != '/')
		return 400;
	if (r->cn->assbackwards) {
		r->protocol_major = 0;
		r->protocol_minor = 9;
	} else {
		s = r->version;
		if (s == 0) {
			log_d("version == 0 !?");
			return -1;
		}
		if (strncmp(s, "HTTP/", 5)) {
			log_d("%s: unsupported version \"%s\"", inet_ntoa(r->cn->peer.sin_addr), s);
			return 400;
		}
		t = strchr(s + 5, '.');
		if (t == 0) {
			log_d("%s: unsupported version \"%s\"", inet_ntoa(r->cn->peer.sin_addr), s);
			return 400;
		}
		*t = 0;
		x = atoi(s + 5);
		y = atoi(t + 1);
		*t = '.';
		if (x != 1 || y > 1) {
			log_d("%s: unsupported version \"%s\"", inet_ntoa(r->cn->peer.sin_addr), s);
			return 505;
		}
		r->protocol_major = x;
		r->protocol_minor = y;
		s = r->connection;
		if (y) {
			r->cn->keepalive = !(s && strcasecmp(s, "Close") == 0);
		} else {
			r->cn->keepalive = s && strcasecmp(s, "Keep-Alive") == 0;
		}
	}
	if (r->method == M_GET) {
		s = r->ims_s;
		if (s) {
			i = timerfc(s);
			if (i == (time_t) -1) {
				log_d("illegal date \"%s\" in If-Modified-Since", s);
				return 400;
			}
			r->ims = i;
		}
	}
	return 0;
}

int prepare_reply(struct request *r)
{
	struct pool *p;
	char *b, buf[PATHLEN];
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
		default:
			b += sprintf(b, "An internal server error has occurred.\n");
			break;
		}
		if (r->c && r->c->admin)
			b += sprintf(b, "<p>Please contact the site administrator at <i>%s</i>.\n", r->c->admin);
		r->content_length = strlen(buf);
		r->num_content = 0;
		r->content_type = "text/html";
	}
	if (r->status >= 400 && r->method != M_GET && r->method != M_HEAD)
		r->cn->keepalive = 0;
	p = r->cn->output;
	return (output_headers(p, r) == -1 || (send_message && putstring(p, buf) == -1)) ? -1 : 0;
}

static void init_request(struct request *r)
{
	r->vs = 0;
	r->user_agent = 0;
	r->referer = 0;
	r->from = 0;
	r->authorization = 0;
	r->cookie = 0;
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
	r->servername = 0;
	r->allowedmethods = 0;
	r->location_length = 0;
}

int process_request(struct request *r)
{
	init_request(r);
	if ((r->status = process_headers(r)) == 0)
		r->status = process_path(r);
	if (r->status > 0 && prepare_reply(r) == -1) {
		log_d("cannot prepare reply for client");
		return -1;
	}
	if (r->status_line && r->c)
		r->processed = 1;
	return r->status > 0 ? 0 : -1;
}
