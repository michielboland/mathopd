/*
 * request.c - parse and process an HTTP/1.0 request
 *
 * Copyright 1996, Michiel Boland
 */

/* Mysterons */

#include "mathopd.h"

char *magic_word = "Keep-Alive";

static char *br_empty =			"empty request";
static char *br_bad_method =		"bad method";
static char *br_bad_url =		"bad or missing url";
static char *br_bad_protocol =		"bad protocol";
static char *br_bad_date =		"bad date";
static char *br_bad_path_name =		"bad path name";
static char *fb_not_plain =		"file not plain";
static char *fb_symlink =		"symlink spotted";
static char *fb_active =		"actively forbidden";
static char *fb_access =		"no permission";
static char *ni_post =			"cannot apply POST method to URL";
static char *nf_not_found =		"not found";
static char *nf_no_index =		"no index";
static char *nf_path_info =		"path info";
static char *nf_slash =			"trailing slash";
static char *se_alias =			"cannot resolve pathname";
static char *se_get_path_info =		"cannot determine path argument";
static char *se_no_control =		"out of control";
static char *se_no_mime =		"no MIME type";
static char *se_no_specialty =		"unconfigured specialty";
static char *se_open =			"open failed";
static char *su_open =			"too many open files";
static char *se_unknown =		"unknown error (help!)";

static char *get_method =		"GET";
static char *head_method =		"HEAD";
static char *post_method =		"POST";

static char *old_protocol =		"HTTP/0.9";
static char *new_protocol =		"HTTP/1.0";

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

	if ((tp = gmtime(&t)) != 0) {
		strftime(buf, 31, "%a, %d %b %Y %H:%M:%S GMT", tp);
	}
	else
		buf[0] = '\0';
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

static int out3(struct pool *p, char *a, char *b, char *c)
{
	return b ? putstrings(p, 3, a, b, c) : 0;
}

static int output_headers(struct pool *p, struct request *r)
{
	long l;
	static char *crlf = "\r\n";

#define OUT(y, z) if (out3(p, (y), (z), crlf) == -1) return -1

	if (r->cn->assbackwards)
		return 0;

	OUT("HTTP/1.0 ", r->status_line);

	OUT("Server: ", server_version);

	OUT("Date: ", rfctime(current_time));

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
		OUT("Connection: ", magic_word);
	}

	return putstrings(p, 1, crlf);
}

static char *dirmatch(char *s, char *t)
{
	int n = strlen(t);

	return (strneq(s, t, n) && (s[n] == '/' || s[n] == '\0')) ? s + n : 0;
}

static int findcontrol(struct request *r)
{
	char *p = r->path_translated;
	struct server *s = r->cn->s;
	struct control *a, *b;
	char *m, *t;

	a = s->controls;
	b = 0;
	m = 0;

	while (a) {
		if (a->directory == 0) {
			if (b == 0)
				b = a;
		}
		else if ((t = dirmatch(p, a->directory)) != 0) {
			if (m == 0 || m < t) {
				m = t;
				b = a;
			}
		}
		a = a->next;
	}
	r->c = b;

	return b ? 0 : -1;
}

static int evaluate_access(struct request *r)
{
	unsigned long ip = r->cn->peer.s_addr;
	struct access *a = r->c->accesses;

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
	t = b + (c->directory ? strlen(c->directory) : 0);
	s = b + strlen(b);
	while (--s > t) {
		if (*s == '/') {
			*s = '\0';
			flag = 1;
		}
		else if (flag) {
			flag = 0;
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

	construct_url(buf, r->url, r->cn->s);
	e = buf+strlen(buf);
	*e++ = '/';
	*e = '\0';
	r->location = buf;
	return 302;
}

static int append_indexes(struct request *r)
{
	char *p = r->path_translated;
	char **i = r->c->index_names;
	char *s = 0;
	char *q = p + strlen(p);

	r->isindex = 1;
	if (i) {
		while ((s = *i++) != 0) {
			strcpy(q, s);
			if (stat(p, &r->finfo) != -1)
				break;
		}
	}
	if (s == 0) {
		*q = '\0';
		if (r->path_args[0] && r->path_args[1]) {
			r->error = nf_not_found;
			return 404;
		}
		else {
			r->error = nf_no_index;
			return 404;
		}
	}
	return 0;
}

static int process_special(struct request *r)
{
	char *ct;

	ct = r->content_type;
	r->num_content = -1;
#ifdef CGI_MAGIC_TYPE
	if (strceq(ct, CGI_MAGIC_TYPE))
		return process_cgi(r);
#endif
#ifdef IMAP_MAGIC_TYPE
	if (strceq(ct, IMAP_MAGIC_TYPE))
		return process_imap(r);
#endif
#ifdef DUMP_MAGIC_TYPE
	if (strceq(ct, DUMP_MAGIC_TYPE))
		return process_dump(r);
#endif
#ifdef REDIRECT_MAGIC_TYPE
	if (strceq(ct, REDIRECT_MAGIC_TYPE))
		return process_redirect(r);
#endif
	r->error = se_no_specialty;
	return 500;
}

static int process_fd(struct request *r)
{
	if (r->method == M_POST) {
		r->error = ni_post;
		return 501;
	}
	if (r->path_args[0]) {
		if (r->path_args[1]) {
			r->error = nf_path_info;
			return 404;
		}
		else if (r->isindex == 0) {
			r->error = nf_slash;
			return 404;
		}
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
	}
	return 200;
}

static int process_path(struct request *r)
{
	if (faketoreal(r->path, r->path_translated, r->cn->s->controls) == 0) {
		r->error = se_alias;
		return 500;
	}
	if (findcontrol(r) == -1) {
		r->error = se_no_control;
		return 500;
	}
	if (evaluate_access(r) == DENY) {
		r->error = fb_active;
		return 403;
	}
	if (check_path(r) == -1) {
		r->error = br_bad_path_name;
		return 400;
	}
	if (get_path_info(r) == -1) {
		r->error = se_get_path_info;
		return 500;
	}
	if (r->path_args[0] && r->path_args[1] && findcontrol(r) == -1) {
		r->error = se_no_control;
		return 500;
	}
	if (S_ISDIR(r->finfo.st_mode)) {
		int rv;

		if (r->path_args[0] != '/')
			return makedir(r);
		if ((rv = append_indexes(r)) != 0)
			return rv;
	}
	if (S_ISREG(r->finfo.st_mode) == 0) {
		r->error = fb_not_plain;
		return 403;
	}
	if (check_symlinks(r) == -1) {
		r->error = fb_symlink;
		return 403;
	}
	if (get_mime(r) == -1) {
		r->error = se_no_mime;
		return 500;
	}
	return r->special ? process_special(r) : process_fd(r);
}

static char *process_headers(struct request *r)
{
	static char *whitespace = " \t";
	char *l, *m, *p, *s;
	int a = r->cn->assbackwards;

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
	r->protocol = 0;
	r->method = 0;
	r->status = 0;
	r->isindex = 0;
	r->c = 0;

	if ((l = getline(r->cn->input)) == 0)
		return br_empty;

	m = strtok(l, whitespace);
	if (m == 0)
		return br_empty;

	if (strceq(m, get_method)) {
		r->method = M_GET;
		r->method_s = get_method;
	}
	else if (strceq(m, head_method)) {
		if (a)
			return br_bad_method;
		r->method = M_HEAD;
		r->method_s = head_method;
	}
	else if (strceq(m, post_method)) {
		if (a)
			return br_bad_method;
		r->method = M_POST;
		r->method_s = post_method;
	}
	else
		return br_bad_method;

	if ((r->url = strtok(0, whitespace)) == 0
	    || strlen(r->url) > STRLEN)
		return br_bad_url;
	if ((s = strchr(r->url, '?')) != 0) {
		r->args = s+1;
		*s = '\0';
	}
	if (unescape_url(r->url, r->path) == -1
	    || r->path[0] != '/')
		return br_bad_url;

	if (a)
		r->protocol = old_protocol;
	else {
		p = strtok(0, whitespace);
		if (p == 0 || strcasecmp(p, new_protocol))
			return br_bad_protocol;

		r->protocol = new_protocol;
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
			else if (keepalive
				 && strceq (l, "Connection")
				 && strceq(s, magic_word))
				r->cn->keepalive = 1;
			else if (r->method == M_GET) {
				if (strceq(l, "If-modified-since")) {
					r->ims = timerfc(s);
					if (r->ims == (time_t) -1)
						return br_bad_date;
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
	case 501:
		r->status_line = "501 Not Implemented";
		break;
	case 503:
		r->status_line = "503 Service Unavailable";
		break;
	default:
		r->status_line = "500 Internal Server Error";
		break;
	}
	if (r->error) {
		log(L_WARNING, "* %s (%s)", r->status_line, r->error);
		if (r->url)
			log(L_WARNING, "  url:   %s", r->url);
		if (r->host)
			log(L_WARNING, "  host:  %s", r->host);
		if (r->user_agent)
			log(L_WARNING, "  agent: %s", r->user_agent);
		if (r->referer)
			log(L_WARNING, "  ref:   %s", r->referer);
		log(L_WARNING, "  peer:  %s\n", r->cn->ip);
	}
	if (send_message) {
		char *b;

		sprintf(buf, "<TITLE>%s</TITLE>\n<H1>%s</H1><P>\n",
				r->status_line, r->status_line);
		b = buf + strlen(buf);
		switch (r->status) {
		case 302:
			sprintf(b,
				"The document has moved to URL "
				"<A HREF=\"%s\">%s</A>.\n",
				r->location, r->location);
			break;
		case 400:
			sprintf(b,
				"Your request contained the following error:\n"
				"<P><B>%s</B>\n",
				r->error);
			break;
		case 403:
			sprintf(b,
				"Access to URL %s denied on this server.\n",
				r->url);
			break;
		case 404:
			sprintf(b,
				"The URL %s was not found on this server.\n",
				r->url);
			break;
		case 501:
			sprintf(b,
				"Cannot apply %s method to URL %s\n",
				r->method_s,
				r->url);
			break;
		case 503:
			sprintf(b,
				"Server overloaded. Sorry.\n");
			break;
		default:
			sprintf(b,
				"<B>%s</B>\n",
				r->error ? r->error : se_unknown);
			if (admin)
				sprintf(b,
					"<P>Please notify %s of this error.\n",
					admin);
			break;
		}
		b += strlen(b);
		sprintf(b, "%s\n", ERROR_FOOTER);
		r->content_length = strlen(buf);
		r->num_content = 0;
		r->content_type = "text/html";
	}

	return (output_headers(p, r) == -1
		|| (send_message && putstrings(p, 1, buf) == -1)) ? -1 : 0;
}

static void log_request(struct request *r)
{
	struct connection *cn = r->cn;
	char *ti;

	if (r->path[0] == '\0') {
		r->path[0] = '?';
		r->path[1] = '\0';
	}
	ti = ctime(&current_time);
	log(L_TRANS, "%.24s - %s - - %s %s %.3s",
	    ti ? ti : "???",
	    cn->ip,
	    r->method_s,
	    r->path,
	    r->status_line
	);
	if (r->user_agent)
		log(L_AGENT, "%s", r->user_agent);
}

int process_request(struct request *r)
{
	++r->cn->s->nrequests;
	if ((r->error = process_headers(r)) == 0)
		r->status = process_path(r);
	else
		r->status = 400;

	if (r->status > 0 && prepare_reply(r) == -1) {
		log(L_ERROR, "cannot prepare reply for client");
		return -1;
	}
	if (r->status_line
		&& r->c
		&& r->c->log_level >= 2
		&& (r->c->log_level > 2 || r->status == 200 ))
		log_request(r);
	return r->status > 0 ? 0 : -1;
}

struct control *faketoreal(char *x, char *y, struct control *c)
{
	char *s;
	char *m = 0;
	struct control *cc = 0;

	while (c) {
		if (c->directory && c->alias
		    && (s = dirmatch(x, c->alias)) != 0) {
			if (m == 0 || s > m) {
				m = s;
				cc = c;
			}
		}
		c = c->next;
	}
	if (cc) {
		strcpy(y, cc->directory);
		strcat(y, m);
	}
	return cc;
}

void construct_url(char *d, char *s, struct server *sv)
{
	sprintf(d, "http://%s%s", sv->fullname, s);
}

void escape_url(char *url)
{
	static char *hex = "0123456789abcdef";
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
