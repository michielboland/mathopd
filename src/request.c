/*
 * request.c - parse and process an HTTP request
 *
 * Copyright 1996, 1997, 1998, Michiel Boland
 */

/* Mysterons */

#include "mathopd.h"

extern int cern; /* aargh */

#ifdef BROKEN_SPRINTF
#define SPRINTF2(x,y) (sprintf(x,y), strlen(x))
#define SPRINTF3(x,y,z) (sprintf(x,y,z), strlen(x))
#define SPRINTF4(x,y,z,w) (sprintf(x,y,z,w), strlen(x))
#else
#define SPRINTF2 sprintf
#define SPRINTF3 sprintf
#define SPRINTF4 sprintf
#endif

static STRING(br_empty) =		"empty request";
static STRING(br_bad_method) =		"bad method";
static STRING(br_bad_url) =		"bad or missing url";
static STRING(br_bad_protocol) =	"bad protocol";
static STRING(br_bad_date) =		"bad date";
static STRING(br_bad_path_name) =	"bad path name";
static STRING(fb_not_plain) =		"file not plain";
static STRING(fb_symlink) =		"symlink spotted";
static STRING(fb_active) =		"actively forbidden";
static STRING(fb_access) =		"no permission";
static STRING(fb_post_file) =		"POST to file";
static STRING(ni_not_implemented) =	"method not implemented";
static STRING(se_alias) =		"cannot resolve pathname";
static STRING(se_get_path_info) =	"cannot determine path argument";
static STRING(se_no_control) =		"out of control";
static STRING(se_no_mime) =		"no MIME type";
static STRING(se_no_specialty) =	"unconfigured specialty";
static STRING(se_no_virtual) =		"no virtual server";
static STRING(se_open) =		"open failed";
static STRING(su_open) =		"too many open files";
static STRING(se_unknown) =		"unknown error (help!)";
static STRING(ni_version_not_supp) =	"version not supported";

static STRING(m_get) =			"GET";
static STRING(m_head) =			"HEAD";
static STRING(m_post) =			"POST";

static time_t timerfc(char *s)
{
	static int daytab[2][12] = {
		{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 },
		{0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 }
	};
	unsigned sec = 60, min = 60, hour = 24,
		day = 32, mon, year = 1969;
	char month[3];
	register char c;
	register unsigned n;
	register char flag;
	register char state;
	register char isctime = 0;
	enum { D_START, D_END, D_MON, D_DAY, D_YEAR, D_HOUR, D_MIN, D_SEC };

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
			}
			else if (c == ',') state = D_DAY;
			break;
		case D_MON:
			if (isalpha(c)) {
				if (n < 3) month[n++] = c;
			}
			else {
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
			}
			else {
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
	return (sec + 60L *
		(min + 60L *
		 (hour + 24L *
		  (day + daytab[year % 4 == 0][mon] +
		   365L * (year - 1970L) + ((year - 1969L) >> 2)))));
}

static char *rfctime(time_t t)
{
	static char buf[32];
	struct tm *tp;

	if ((tp = gmtime(&t)) != 0)
		strftime(buf, 31, "%a, %d %b %Y %H:%M:%S GMT", tp);
	else
		buf[0] = 0;
	return buf;
}

static char *cerntime(time_t t)
{
	static char buf[32];
	struct tm *tp;

	if ((tp = gmtime(&t)) != 0)
		strftime(buf, 27, "%d/%b/%Y:%H:%M:%S +0000", tp);
	else
		buf[0] = 0;
	return buf;
}

static char *getline(struct pool *p)
{
	register char *s;
	char *olds, *sp;
	char *end = p->end;
	register int f;

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

static int putstrings(struct pool *p, int n, ...)
{
	int bl, sl;
	char *s;
	va_list ap;

	va_start(ap, n);
	bl = p->ceiling - p->end;
	while (--n >= 0) {
		s = va_arg(ap, char *);
		if (s) {
			sl = strlen(s);
			if (sl > bl) {
				log(L_ERROR, "output buffer overflow");
				break;
			}
			memcpy(p->end, s, sl);
			p->end += sl;
			bl -= sl;
		}
		else {
			log(L_ERROR, "null pointer in putstrings");
			break;
		}
	}
	va_end(ap);
	return n >= 0 ? -1 : 0;
}

static int out3(struct pool *p, const char *a, const char *b, const char *c)
{
	return b ? putstrings(p, 3, a, b, c) : 0;
}

static int output_headers(struct pool *p, struct request *r)
{
	long l;
	static STRING(crlf) = "\r\n";
	char t[16];

#define OUT(y, z) if (out3(p, (y), (z), crlf) == -1) return -1

	if (r->cn->assbackwards)
		return 0;

	sprintf(t, "HTTP/%d.%d ",
		r->protocol_major,
		r->protocol_minor);

	OUT(t, r->status_line);

	OUT("Server: ", server_version);

	OUT("Date: ", rfctime(current_time));

	if (r->c && r->c->refresh) {
		char buf[20];

		sprintf(buf, "%d", r->c->refresh);
		OUT("Refresh: ", buf);
	}

	if (r->num_content >= 0) {

		OUT("Content-type: ", r->content_type);

		if ((l = r->content_length) >= 0) {
			char buf[20];

			sprintf(buf, "%ld", l);
			OUT("Content-length: ", buf);
		}

		if (r->last_modified) {
			OUT("Last-modified: ", rfctime(r->last_modified));
		}
	}

	OUT("Location: ", r->location);

	if (r->cn->keepalive) {
		if (r->protocol_minor == 0)
			OUT("Connection: ", "Keep-Alive");
	} else if (r->protocol_minor)
		OUT("Connection: ", "Close");

	return putstrings(p, 1, crlf);
}

static char *dirmatch(char *s, char *t)
{
	int n;

	log(L_DEBUG, "dirmatch(\"%s\", \"%s\")", s, t);
	if ((n = strlen(t)) == 0)
		return s;
	return strneq(s, t, n) &&
		(s[n] == '/' || s[n] == '\0' || s[n-1] == '~') ? s + n : 0;
}

static int evaluate_access(unsigned long ip, struct access *a)
{
	while (a && ((ip & a->mask) != a->addr))
		a = a->next;
	return a ? a->type : ALLOW;
}

static int get_mime(struct request *r)
{
	char *s = r->path_translated;
	struct mime *m = r->c->mimes;
	char *saved_type = 0;
	int saved_s = 0;
	int l, le, lm;

	lm = 0;
	l = strlen(s);
	while (m) {
		if (m->ext) {
			le = strlen(m->ext);
			if (le > lm && le <= l && strceq(s + l - le, m->ext)) {
				lm = le;
				saved_type = m->name;
				saved_s = m->type == M_SPECIAL;
			}
		}
		else if (saved_type == 0) {
			saved_type = m->name;
			saved_s = m->type == M_SPECIAL;
		}
		m = m->next;
	}
	if (saved_type) {
		r->content_type = saved_type;
		r->special = saved_s;
		r->num_content = lm;
		return 0;
	}
	else
		return -1;
}

static int get_path_info(struct request *r)
{
	char *p = r->path_translated;
	char *pa = r->path_args;
	struct stat *s = &r->finfo;
	char *end = p + strlen(p);
	char *cp;
	int rv;

	*pa = '\0';
	cp = end;

	while (cp > p && cp[-1] == '/')
		--cp;

	while (cp > p) {
		*cp = '\0';
		log(L_DEBUG, "  stat(\"%s\")", p);
		rv = stat(p, s);
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
	char *p = r->path;

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
	char *p = r->path_translated;
	struct control *c = r->c;
	char b[PATHLEN];
	struct stat buf;
	char *s, *t;
	int flag = 1;

	if (c->symlinksok)
		return 0;
	strcpy(b, p);
	t = b + (c->locations->name ? strlen(c->locations->name) : 0);
	s = b + strlen(b);
	while (--s > t) {
		if (*s == '/') {
			*s = '\0';
			flag = 1;
		}
		else if (flag) {
			flag = 0;
			log(L_DEBUG, "  lstat(\"%s\")", b);
			if (lstat(b, &buf) == -1) {
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
	static char buf[PATHLEN];
	char *e;

	construct_url(buf, r->url, r->vs);
	e = buf+strlen(buf);
	*e++ = '/';
	*e = '\0';
	r->location = buf;
	return 302;
}

static int append_indexes(struct request *r)
{
	char *p = r->path_translated;
	struct simple_list *i = r->c->index_names;
	char *q = p + strlen(p);

	r->isindex = 1;
	while (i) {
		strcpy(q, i->name);
		log(L_DEBUG, "  stat(\"%s\")", p);
		if (stat(p, &r->finfo) != -1)
			break;
		i = i->next;
	}
	if (i == 0) {
		*q = '\0';
		return 404;
	}
	return 0;
}

static int process_special(struct request *r)
{
	const char *ct;

	ct = r->content_type;
	r->num_content = -1;
	if (strceq(ct, CGI_MAGIC_TYPE))
		return process_cgi(r);
	if (strceq(ct, IMAP_MAGIC_TYPE))
		return process_imap(r);
	r->error = se_no_specialty;
	return 500;
}

static int process_fd(struct request *r)
{
	if (r->path_args[0] && r->c->path_args_ok == 0) {
		if (r->path_args[1] || r->isindex == 0)
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
		int fd;

		if ((fd = open(r->path_translated, O_RDONLY)) == -1) {
			switch (errno) {
			case EACCES:
				r->error = fb_access;
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
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		r->cn->rfd = fd;
		if (r->content_length <= 65)
			r->cn->keepalive = 0;
	}
	return 200;
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
	struct virtual *v = r->cn->s->children;
	struct virtual *gv = 0;

	while (v) {
		if (v->host == 0)
			gv = v;
		else if (r->host && hostmatch(r->host, v->host))
			break;
		v = v->next;
	}
	if (v == 0 && gv)
		v = gv;
	if (v) {
		r->vs = v;
		v->nrequests++;
		return 0;
	}
	return 1;
}

static int process_path(struct request *r)
{
	log(L_DEBUG, "process_path starting: find_vs,");
	if (find_vs(r)) {
		r->error = se_no_virtual;
		return 500;
	}
	log(L_DEBUG, " faketoreal,");
	if ((r->c = faketoreal(r->path, r->path_translated, r, 1)) == 0) {
		r->error = se_alias;
		return 500;
	}
	log(L_DEBUG, " empty path check,");
	if (r->path_translated[0] == 0) {
		r->error = se_alias;
		return 500;
	}
	log(L_DEBUG, " redirect check,");
	if (r->path_translated[0] != '/') {
		escape_url(r->path_translated);
		r->location = r->path_translated;
		return 302;
	}
	log(L_DEBUG, " evaluate_access,");
	if (evaluate_access(r->cn->peer.s_addr, r->c->accesses) == DENY) {
		r->error = fb_active;
		return 403;
	}
	log(L_DEBUG, " check_path,");
	if (check_path(r) == -1) {
		r->error = br_bad_path_name;
		return 400;
	}
	log(L_DEBUG, " get_path_info,");
	if (get_path_info(r) == -1) {
		r->error = se_get_path_info;
		return 500;
	}
	log(L_DEBUG, " sanity check,");
	if (r->c->locations == 0) {
		log(L_ERROR, "raah... no locations found");
		return 404;
	}
	log(L_DEBUG, " ISDIR check,");
	if (S_ISDIR(r->finfo.st_mode)) {
		int rv;

		if (r->path_args[0] != '/')
			return makedir(r);
		if ((rv = append_indexes(r)) != 0)
			return rv;
	}
	log(L_DEBUG, " ISREG check,");
	if (S_ISREG(r->finfo.st_mode) == 0) {
		r->error = fb_not_plain;
		return 403;
	}
	log(L_DEBUG, " check_symlinks,");
	if (check_symlinks(r) == -1) {
		r->error = fb_symlink;
		return 403;
	}
	log(L_DEBUG, " get_mime,");
	if (get_mime(r) == -1) {
		r->error = se_no_mime;
		return 500;
	}
	log(L_DEBUG, " done.");
	return r->special ? process_special(r) : process_fd(r);
}

static int get_method(char *p, struct request *r)
{
	if (p == 0)
		return -1;

	if (streq(p, m_get)) {
		r->method = M_GET;
		r->method_s = m_get;
		return 0;
	}

	if (!r->cn->assbackwards) {
		if (streq(p, m_head)) {
			r->method = M_HEAD;
			r->method_s = m_head;
			return 0;
		}
		if (streq(p, m_post)) {
			r->method = M_POST;
			r->method_s = m_post;
			return 0;
		}
	}

	log(L_ERROR, "unknown method \"%s\" from %s", p, r->cn->ip);

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
	if (s == 0)
		return -1;

	*s++ = '\0';
	x = atoi(p);
	y = atoi(s);
	if (x != 1 || y > 1) {
		log(L_ERROR, "unsupported HTTP version (%d.%d) from %s",
		    x, y, r->cn->ip);
		return -1;
	}

	r->protocol_major = x;
	r->protocol_minor = y;
	return 0;
}

static int process_headers(struct request *r)
{
	static STRING(whitespace) = " \t";
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
	r->special = 0;
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
			log(L_ERROR, "bad protocol string \"%.32s\" from %s",
			    p, r->cn->ip);
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
			if (strceq(l, "User-agent"))
				r->user_agent = s;
			else if (strceq(l, "Referer"))
				r->referer = s;
			else if (strceq(l, "From"))
				r->from = s;
			else if (strceq(l, "Authorization"))
				r->authorization = s;
			else if (strceq(l, "Cookie"))
				r->cookie = s;
			else if (strceq(l, "Host"))
				r->host = s;
			else if (strceq(l, "Connection")) {
				if (r->protocol_minor) {
					if (strceq(s, "Close"))
						r->cn->keepalive = 0;
				} else if (strceq(s, "Keep-Alive"))
					r->cn->keepalive = 1;
			}
			else if (r->method == M_GET) {
				if (strceq(l, "If-modified-since")) {
					r->ims = timerfc(s);
					if (r->ims == (time_t) -1) {
						r->error = br_bad_date;
						return 400;
					}
				}
			}
			else if (r->method == M_POST) {
				if (strceq(l, "Content-type"))
					r->in_content_type = s;
				else if (strceq(l, "Content-length"))
					r->in_content_length = s;
			}
		}
	}
	return 0;
}

int prepare_reply(struct request *r)
{
	struct pool *p = r->cn->output;
	static char buf[PATHLEN];
	int send_message = r->method != M_HEAD;

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
		log(L_WARNING, "  peer:  %s\n", r->cn->ip);
	}
	if (send_message) {
		char *b = buf;

		b += SPRINTF3(b, "<title>%s</title>\n", r->status_line);

		switch (r->status) {
		case 302:
			b += SPRINTF4(b, "This document has moved to URL "
				      "<a href=\"%s\">%s</a>.\n",
				      r->location, r->location);
			break;
		case 400:
		case 405:
		case 501:
		case 505:
			b += SPRINTF2(b, "Your request was not understood "
				      "or not allowed by this server.\n");
			break;
		case 403:
			b += SPRINTF2(b, "Access to this resource has been "
				      "denied to you.\n");
			break;
		case 404:
			b += SPRINTF2(b, "The resource requested could not be "
				      "found on this server.\n");
			break;
		case 503:
			b += SPRINTF2(b, "The server is temporarily busy.\n");
			break;
		default:
			b += SPRINTF2(b, "An internal server error has "
				      "occurred.\n");
			break;
		}

		if (r->c && r->c->admin) {
			b += SPRINTF3(b,
				      "<p>Please contact the site "
				      "administrator at <i>%s</i>.\n",
				      r->c->admin);
		}
		r->content_length = strlen(buf);
		r->num_content = 0;
		r->content_type = "text/html";
	}

	if (r->status >= 400 && r->method != M_GET && r->method != M_HEAD)
		r->cn->keepalive = 0;

	return (output_headers(p, r) == -1
		|| (send_message && putstrings(p, 1, buf) == -1)) ? -1 : 0;
}

static void log_request(struct request *r)
{
	struct connection *cn = r->cn;
	char *ti;
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

	if (cern) {
		ti = cerntime(current_time);
		log(L_TRANS, "%s - - [%s] \"%s %s HTTP/%d.%d\" %.3s %ld",
		    cn->ip,
		    ti,
		    r->method_s,
		    r->path,
		    r->protocol_major,
		    r->protocol_minor,
		    r->status_line,
		    cl);
	} else {
		ti = ctime(&current_time);
		log(L_TRANS, "%.24s\t-\t%s\t-\t%s\t%s\t%s\t%.3s\t%ld\t%.128s\t%.128s",
		    ti ? ti : "???",
		    cn->ip,
		    r->vs->fullname,
		    r->method_s,
		    r->path,
		    r->status_line,
		    cl,
		    r->referer ? r->referer : "-",
		    r->user_agent ? r->user_agent : "-");
	}
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
	unsigned long ip = r->cn->peer.s_addr;
	struct control *c;
	char *s = 0;

	if (r->vs == 0) {
		log(L_ERROR, "virtualhost not initialized!");
		return 0;
	}
	c = r->vs->controls;
	while (c) {
		if (c->locations
		    && c->alias
		    && (s = dirmatch(x, c->alias)) != 0
		    && (c->clients == 0 ||
			evaluate_access(ip, c->clients) == APPLY))
			break;
		c = c->next;
	}
	if (c) {
		if (update)
			c->locations = c->locations->next;
		strcpy(y, c->locations->name);
		if (c->locations->name[0] == '/'
		    || !c->path_args_ok)
			strcat(y, s);
	}
	return c;
}

void construct_url(char *d, char *s, struct virtual *v)
{
	sprintf(d, "http://%s%s", v->fullname, s);
}

void escape_url(char *url)
{
	static STRING(hex) = "0123456789abcdef";
	char scratch[PATHLEN];
	char *s;
	register char c;

	s = strcpy(scratch, url);
	while ((c = *s++) != '\0') {
		switch (c) {
		case '%':
		case ' ':
		case '?':
		case '+':
		case '&':
			*url++ = '%';
			*url++ = hex[(c >> 4) & 15];
			*url++ = hex[c & 15];
			break;
		default:
			*url++ = c;
			break;
		}
	}
	*url = 0;
}

int unescape_url(char *s, char *t)
{
	char c, x1, x2;

#define hexdigit(x) (((x) <= '9') ? (x) - '0' : ((x) & 7) + 9)

	while ((c = *s++) != '\0')
		if (c == '%') {
			if (isxdigit(x1 = *s++) && isxdigit(x2 = *s++))
				*t++ = ((hexdigit(x1) << 4) + hexdigit(x2));
			else
				return -1;
		}
		else *t++ = c;
	*t = '\0';
	return 0;
}
