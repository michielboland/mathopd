/*
 *   Copyright 1996, 1997, 1998 Michiel Boland.
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
 *   3. All advertising materials mentioning features or use of
 *      this software must display the following acknowledgement:
 *
 *   This product includes software developed by Michiel Boland.
 *
 *   4. The name of the author may not be used to endorse or promote
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

#include "mathopd.h"

static const char br_empty[] =			"empty request";
static const char br_bad_method[] =		"bad method";
static const char br_bad_url[] =		"bad or missing url";
static const char br_bad_protocol[] =		"bad protocol";
static const char br_bad_date[] =		"bad date";
static const char br_bad_path_name[] =		"bad path name";
static const char fb_not_plain[] =		"file not plain";
static const char fb_symlink[] =		"symlink spotted";
static const char fb_active[] =			"actively forbidden";
static const char fb_access[] =			"no permission";
static const char fb_post_file[] =		"POST to file";
static const char ni_not_implemented[] =	"method not implemented";
static const char se_alias[] =			"cannot resolve pathname";
static const char se_get_path_info[] =		"cannot determine path args";
static const char se_no_class[] =		"unknown class!?";
static const char se_no_control[] =		"out of control";
static const char se_no_mime[] =		"no MIME type";
static const char se_no_specialty[] =		"unconfigured specialty";
static const char se_no_virtual[] =		"virtual server does not exist";
static const char se_open[] =			"open failed";
static const char su_open[] =			"too many open files";
static const char se_unknown[] =		"unknown error (help!)";
static const char ni_version_not_supp[] =	"version not supported";

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
	register char c;
	register unsigned n;
	register char flag;
	register char state;
	register char isctime;
	enum { D_START, D_END, D_MON, D_DAY, D_YEAR, D_HOUR, D_MIN, D_SEC };

	sec = 60;
	min = 60;
	hour = 24;
	day = 32;
	year = 1969;
	isctime = 0;
	month[0] = '\0';
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
			} else if (c == ',') state = D_DAY;
			break;
		case D_MON:
			if (isalpha(c)) {
				if (n < 3) month[n++] = c;
			} else {
				if (n < 3) return -1;
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
		log(L_ERROR, "gmtime failed!?!?!?");
		return 0;
	}
	strftime(buf, 31, "%a, %d %b %Y %H:%M:%S GMT", tp);
	return buf;
}

static char *getline(struct pool *p)
{
	register char *s;
	char *olds, *sp, *end;
	register int f;

	end = p->end;
	s = olds = sp = p->start;
	f = 0;
	while (s < end) {
		switch (*s++) {
		case '\n':
			if (s == end || (*s != ' ' && *s != '\t')) {
				if (f)
					*sp = '\0';
				else
					s[-1] = '\0';
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
	return 0;
}

static int putstring(struct pool *p, char *s)
{
	int l;

	l = strlen(s);
	if (l > p->ceiling - p->end) {
		log(L_ERROR, "no more room to put string!?!?");
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

	if (r->cn->assbackwards) {
		log(L_WARNING, "old-style connection from %s", r->cn->ip);
		return 0;
	}
	b = tmp_outbuf;
	b += sprintf(b, "HTTP/%d.%d %s\r\n"
		"Server: %s\r\n"
		"Date: %s\r\n",
		r->protocol_major, r->protocol_minor, r->status_line,
		server_version,
		rfctime(current_time, gbuf));
	if (r->c) {
		if (r->c->refresh)
			b += sprintf(b, "Refresh: %d\r\n", r->c->refresh);
		if (r->status == 401 && r->c->realm)
			b += sprintf(b, "WWW-Authenticate: Basic realm=\"%s\"\r\n", r->c->realm);
	}
	if (r->num_content >= 0) {
		b += sprintf(b, "Content-type: %s\r\n", r->content_type);
		cl = r->content_length;
		if (cl >= 0)
			b += sprintf(b, "Content-length: %ld\r\n", cl);
		if (r->last_modified)
			b += sprintf(b, "Last-Modified: %s\r\n", rfctime(r->last_modified, gbuf));
	}
	if (r->location)
		b += sprintf(b, "Location: %.512s\r\n", r->location);
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
	int n;

	n = strlen(t);
	if (n == 0)
		return s;
	return !strncmp(s, t, n) && (s[n] == '/' || s[n] == '\0' || s[n-1] == '~') ? s + n : 0;
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
	char *p, *pa, *end, *cp;
	struct stat *s;
	int rv;

	s = &r->finfo;
	p = r->path_translated;
	end = p + strlen(p);
	pa = r->path_args;
	*pa = '\0';
	cp = end;
	while (cp > p && cp[-1] == '/')
		--cp;
	while (cp > p) {
		if (cp != end)
			*cp = '\0';
		rv = stat(p, s);
		if (debug) {
			if (rv == -1)
				log(L_DEBUG, "get_path_info: stat(\"%s\") = %d", p, rv, s->st_mode);
			else
				log(L_DEBUG, "get_path_info: stat(\"%s\") = %d; st_mode = %#o", p, rv, s->st_mode);
		}
		if (cp != end)
			*cp = '/';
		if (rv != -1) {
			strcpy(pa, cp);
			if (S_ISDIR(s->st_mode))
				*cp++ = '/';
			*cp = '\0';
			return 0;
		}
		while (--cp > p && *cp != '/')
			;
	}
	return -1;
}

static int check_path(struct request *r)
{
	char *p;

	p = r->path;
	if (*p != '/')
		return -1;
	while (1)
		switch (*p++) {
		case '\0':
			return 0;
		case '/':
			switch (*p) {
			case '.':
			case '/':
				return -1;
			}
		}
}

static int check_symlinks(struct request *r)
{
	char *p, *s, *t, b[PATHLEN];
	struct control *c;
	struct stat buf;
	int flag, rv;

	p = r->path_translated;
	c = r->c;
	if (c->symlinksok)
		return 0;
	strcpy(b, p);
	t = b + (c->locations->name ? strlen(c->locations->name) : 0);
	s = b + strlen(b);
	flag = 1;
	while (--s > t) {
		if (*s == '/') {
			*s = '\0';
			flag = 1;
		} else if (flag) {
			flag = 0;
			rv = lstat(b, &buf);
			if (debug) {
				if (rv == -1)
					log(L_DEBUG, "check_symlinks: lstat(\"%s\") = %d", b, rv, buf.st_mode);
				else
					log(L_DEBUG, "check_symlinks: lstat(\"%s\") = %d; st_mode = %#o", b, rv, buf.st_mode);
			}
			if (rv == -1) {
				lerror("lstat");
				return -1;
			}
			if (S_ISLNK(buf.st_mode)) {
				log(L_WARNING, "%s is a symbolic link", b);
					return -1;
			}
		}
	}
	return 0;
}

static int makedir(struct request *r)
{
	char *buf, *e;

	buf = r->newloc;
	construct_url(buf, r->url, r);
	e = buf + strlen(buf);
	*e++ = '/';
	*e = '\0';
	r->location = buf;
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
		if (debug)
			log(L_DEBUG, "append_indexes: stat(\"%s\") = %d", p, rv);
		if (rv != -1)
			break;
		i = i->next;
	}
	if (i == 0) {
		*q = '\0';
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
	r->error = se_no_specialty;
	return 500;
}

static int process_fd(struct request *r)
{
	int fd, rv;

	if (r->path_args[0] && r->c->path_args_ok == 0 && (r->path_args[1] || r->isindex == 0)) {
		r->error_file = r->c->error_404_file;
		return 404;
	}
	if (r->method == M_POST) {
		r->error = fb_post_file;
		return 405;
	}
	r->content_length = r->finfo.st_size;
	r->last_modified = r->finfo.st_mtime;
	if (r->last_modified <= r->ims) {
		r->num_content = -1;
		return 304;
	}
	if (r->method == M_GET) {
		fd = open(r->path_translated, O_RDONLY);
		if (debug)
			log(L_DEBUG, "process_fd: open(\"%s\") = %d", r->path_translated, fd);
		if (fd == -1) {
			switch (errno) {
			case EACCES:
				r->error = fb_access;
				r->error_file = r->c->error_403_file;
				return 403;
			case EMFILE:
				r->error = su_open;
				return 503;
			default:
				lerror("open");
				r->error = se_open;
				return 500;
			}
		}
		rv = fcntl(fd, F_SETFD, FD_CLOEXEC);
		if (debug)
			log(L_DEBUG, "process_fd: fcntl(%d, F_SETFD, FD_CLOEXEC) = %d", fd, rv);
		r->cn->rfd = fd;
	}
	return 200;
}

static int add_fd(struct request *r, const char *filename)
{
	int fd, rv;
	struct stat s;

	if (filename == 0)
		return -1;
	if (get_mime(r, filename) == -1)
		return -1;
	fd = open(filename, O_RDONLY);
	if (debug)
		log(L_DEBUG, "add_fd: open(\"%s\") = %d", filename, fd);
	if (fd == -1)
		return -1;
	rv = fstat(fd, &s);
	if (debug) {
		if (rv == -1)
			log(L_DEBUG, "add_fd: fstat(%d) = %d", fd, rv, s.st_mode);
		else
			log(L_DEBUG, "add_fd: fstat(%d) = %d; st_mode = %#o", fd, rv, s.st_mode);
	}
	if (!S_ISREG(s.st_mode)) {
		log(L_WARNING, "non-plain file %s", filename);
		rv = close(fd);
		if (debug)
			log(L_DEBUG, "add_fd: close(%d) = rv", fd);
		return -1;
	}
	rv = fcntl(fd, F_SETFD, FD_CLOEXEC);
	if (debug)
		log(L_DEBUG, "add_fd: fcntl(%d, F_SETFD, FD_CLOEXEC) = %d", fd, rv);
	r->cn->rfd = fd;
	r->content_length = s.st_size;
	return 0;
}

static int hostmatch(const char *s, const char *t)
{
	register char c, d;

	while (c = *s++, d = *t++, c && c != ':' && d) {
		if (toupper(c) != toupper(d))
			return 0;
	}
	switch (c) {
	case '\0':
	case ':':
	case '.':
		switch (d) {
		case '\0':
		case '.':
			return 1;
		}
	default:
		return 0;
	}
}

static int find_vs(struct request *r)
{
	struct virtual *v, *gv;

	gv = 0;
	v = r->cn->s->children;
	while (v) {
		if (v->host == 0)
			gv = v;
		else if (r->host && hostmatch(r->host, v->host))
			break;
		v = v->next;
	}
	if (v == 0) {
		v = gv;
		if (v == 0)
			return -1;
	}
	r->vs = v;
	v->nrequests++;
	if (v->host)
		r->servername = v->host;
	else if (v->parent->s_name)
		r->servername = v->parent->s_name;
	if (r->host && v->host == 0) {
		log(L_ERROR, "No such virtual server: %.50s", r->host);
		return 1;
	}
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
	while (isspace(*a))
		++a;
	if (webuserok(a, r->c->userfile, r->user, sizeof r->user - 1))
		return 0;
	return -1;
}

static int process_path(struct request *r)
{
	int rv;

	switch (find_vs(r)) {
	case -1:
		r->error = se_no_virtual;
		return 500;
	case 1:
		r->error = se_no_virtual;
		return 404;
	}
	if ((r->c = faketoreal(r->path, r->path_translated, r, 1)) == 0) {
		r->error = se_alias;
		return 500;
	}
	if (r->path_translated[0] == 0) {
		r->error = se_alias;
		return 500;
	}
	if (r->path_translated[0] != '/') {
		escape_url(r->path_translated, r->newloc);
		r->location = r->newloc;
		return 302;
	}
	if (evaluate_access(r->cn->peer.sin_addr.s_addr, r->c->accesses) == DENY) {
		r->error = fb_active;
		r->error_file = r->c->error_403_file;
		return 403;
	}
	if (check_realm(r) == -1) {
		r->error_file = r->c->error_401_file;
		return 401;
	}
	if (check_path(r) == -1) {
		r->error = br_bad_path_name;
		return 400;
	}
	if (get_path_info(r) == -1) {
		r->error = se_get_path_info;
		return 500;
	}
	if (r->c->locations == 0) {
		log(L_ERROR, "raah... no locations found");
		r->error_file = r->c->error_404_file;
		return 404;
	}
	if (S_ISDIR(r->finfo.st_mode)) {
		if (r->path_args[0] != '/')
			return makedir(r);
		rv = append_indexes(r);
		if (rv)
			return rv;
	}
	if (!S_ISREG(r->finfo.st_mode)) {
		r->error = fb_not_plain;
		r->error_file = r->c->error_403_file;
		return 403;
	}
	if (check_symlinks(r) == -1) {
		r->error = fb_symlink;
		r->error_file = r->c->error_403_file;
		return 403;
	}
	if (get_mime(r, r->path_translated) == -1) {
		r->error = se_no_mime;
		return 500;
	}
	switch (r->class) {
	case CLASS_FILE:
		return process_fd(r);
	case CLASS_SPECIAL:
		return process_special(r);
	case CLASS_EXTERNAL:
		return process_external(r);
	}
	r->error = se_no_class;
	return 500;
}

static int get_method(char *p, struct request *r)
{
	if (p == 0)
		return -1;
	if (!strcmp(p, m_get)) {
		r->method = M_GET;
		r->method_s = m_get;
		return 0;
	}
	if (!r->cn->assbackwards) {
		if (!strcmp(p, m_head)) {
			r->method = M_HEAD;
			r->method_s = m_head;
			return 0;
		}
		if (!strcmp(p, m_post)) {
			r->method = M_POST;
			r->method_s = m_post;
			return 0;
		}
	}
	log(L_ERROR, "unknown method \"%.32s\" from %s", p, r->cn->ip);
	return -1;
}

static int get_url(char *p, struct request *r)
{
	char *s;

	if (p == 0)
		return -1;
	if (strlen(p) > STRLEN) {
		log(L_ERROR, "url too long from %s", r->cn->ip);
		return -1;
	}
	r->url = p;
	s = strchr(p, '?');
	if (s) {
		r->args = s+1;
		*s = '\0';
	}
	s = strchr(p, ';');
	if (s) {
		r->params = s + 1;
		*s = '\0';
	}
	if (unescape_url(r->url, r->path) == -1) {
		log(L_ERROR, "badly encoded url from %s", r->cn->ip);
		return -1;
	}
	if (r->path[0] != '/')
		return -1; /* this is wrong */
	return 0;
}

static int get_version(char *p, struct request *r)
{
	unsigned int x, y;
	char *s;

	s = strchr(p, '.');
	if (s == 0) {
		log(L_ERROR, "illegal HTTP version (%.32s) from %s", p, r->cn->ip);
		return -1;
	}
	*s++ = '\0';
	x = atoi(p);
	y = atoi(s);
	if (x != 1 || y > 1) {
		log(L_ERROR, "unsupported HTTP version (%.32s.%.32s) from %s", p, s, r->cn->ip);
		return -1;
	}
	r->protocol_major = x;
	r->protocol_minor = y;
	return 0;
}

static int process_headers(struct request *r)
{
	static const char whitespace[] = " \t";
	char *l, *m, *p, *s;

	r->vs = 0;
	r->user_agent = 0;
	r->referer = 0;
	r->from = 0;
	r->authorization = 0;
	r->cookie = 0;
	r->host = 0;
	r->in_content_type = 0;
	r->in_content_length = 0;
	r->path[0] = '\0';
	r->path_translated[0] = '\0';
	r->path_args[0] = '\0';
	r->num_content = -1;
	r->class = 0;
	r->content_length = -1;
	r->last_modified = 0;
	r->ims = 0;
	r->location = 0;
	r->status_line = 0;
	r->error = 0;
	r->method_s = 0;
	r->url = 0;
	r->args = 0;
	r->params = 0;
	r->protocol_major = 0;
	r->protocol_minor = 0;
	r->method = 0;
	r->status = 0;
	r->isindex = 0;
	r->c = 0;
	r->error_file = 0;
	r->user[0] = '\0';
	r->servername = 0;
	if ((l = getline(r->cn->input)) == 0) {
		r->error = br_empty; /* can this happen? */
		return 400;
	}
	m = strtok(l, whitespace);
	if (get_method(m, r) == -1) {
		r->error = ni_not_implemented;
		return 501;
	}
	p = strtok(0, whitespace);
	if (get_url(p, r) == -1) {
		r->error = br_bad_url;
		return 400;
	}
	if (r->cn->assbackwards)
		r->protocol_minor = 9;
	else {
		p = strtok(0, whitespace);
		if (p == 0 || strncmp(p, "HTTP/", 5)) {
			log(L_ERROR, "bad protocol string \"%.32s\" from %s", p, r->cn->ip);
			r->error = br_bad_protocol;
			return 400;
		}
		if (get_version(p + 5, r) == -1) {
			r->error = ni_version_not_supp;
			return 505;
		}
		if (r->protocol_minor)
			r->cn->keepalive = 1;
		while ((l = getline(r->cn->input)) != 0) {
			if ((s = strchr(l, ':')) == 0)
				continue;
			*s++ = '\0';
			while (isspace(*s))
				++s;
			if (*s == '\0')
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
			else if (!strcasecmp(l, "Connection")) {
				if (r->protocol_minor) {
					if (!strcasecmp(s, "Close"))
						r->cn->keepalive = 0;
				} else if (!strcasecmp(s, "Keep-Alive"))
					r->cn->keepalive = 1;
			} else if (r->method == M_GET) {
				if (!strcasecmp(l, "If-modified-since")) {
					r->ims = timerfc(s);
					if (r->ims == (time_t) -1) {
						r->error = br_bad_date;
						return 400;
					}
				}
			} else if (r->method == M_POST) {
				if (!strcasecmp(l, "Content-type"))
					r->in_content_type = s;
				else if (!strcasecmp(l, "Content-length"))
					r->in_content_length = s;
			}
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
	if (r->error) {
		log(L_WARNING, "* %s (%s)", r->status_line, r->error);
		if (r->url)
			log(L_WARNING, "  url:   %s", r->url);
		if (r->vs && r->vs->fullname)
			log(L_WARNING, "  host:  %s", r->vs->fullname);
		if (r->user_agent)
			log(L_WARNING, "  agent: %s", r->user_agent);
		if (r->referer)
			log(L_WARNING, "  ref:   %s", r->referer);
		log(L_WARNING, "  peer:  %s", r->cn->ip);
	}
	if (r->error_file) {
		if (add_fd(r, r->error_file) != -1)
			send_message = 0;
	}
	if (send_message) {
		b = buf;
		b += sprintf(b, "<title>%s</title>\n", r->status_line);
		switch (r->status) {
		case 302:
			b += sprintf(b, "This document has moved to URL " "<a href=\"%s\">%s</a>.\n", r->location, r->location);
			break;
		case 401:
			b += sprintf(b, "You need proper authorization to " "use this resource.\n");
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

static void log_request(struct request *r)
{
	struct connection *cn;
	long cl;

	if (r->path[0] == '\0') {
		r->path[0] = '?';
		r->path[1] = '\0';
	}
	cl = r->num_content;
	if (cl >= 0)
		cl = r->content_length;
	if (cl < 0)
		cl = 0;
	cn = r->cn;
	log(L_TRANS, "%.15s\t%s\t%hu\t%s\t%s\t%s\t%.3s\t%ld\t%.128s\t%.128s",
		r->user[0] ? r->user : "-",
		cn->ip,
		htons(cn->peer.sin_port),
		r->vs->fullname,
		r->method_s,
		r->path,
		r->status_line,
		cl,
		r->referer ? r->referer : "-",
		r->user_agent ? r->user_agent : "-");
}

int process_request(struct request *r)
{
	if ((r->status = process_headers(r)) == 0)
		r->status = process_path(r);
	if (r->status > 0 && prepare_reply(r) == -1) {
		log(L_ERROR, "cannot prepare reply for client");
		return -1;
	}
	if (r->status_line && r->c) {
		switch(r->c->loglevel) {
		case 1:
			break;
		case 2:
			if (r->status != 200)
				break;
		default:
			log_request(r);
		}
	}
	return r->status > 0 ? 0 : -1;
}

struct control *faketoreal(char *x, char *y, struct request *r, int update)
{
	unsigned long ip;
	struct control *c;
	char *s;

	if (r->vs == 0) {
		log(L_ERROR, "virtualhost not initialized!");
		return 0;
	}
	ip = r->cn->peer.sin_addr.s_addr;
	s = 0;
	c = r->vs->controls;
	while (c) {
		if (c->locations && c->alias) {
			s = dirmatch(x, c->alias);
			if (s && (c->clients == 0 || evaluate_access(ip, c->clients) == APPLY))
				break;
		}
		c = c->next;
	}
	if (c) {
		if (update)
			c->locations = c->locations->next;
		strcpy(y, c->locations->name);
		if (c->locations->name[0] == '/' || !c->path_args_ok)
			strcat(y, s);
	}
	return c;
}

void construct_url(char *d, char *s, struct request *r)
{
	int port;

	port = r->cn->s->port;
	if (port == DEFAULT_PORT)
		sprintf(d, "http://%s%s", r->servername, s);
	else
		sprintf(d, "http://%s:%d%s", r->servername, port, s);
}
