/*
 * extras.c - Mathopd server extensions
 *
 * Copyright 1996, Michiel Boland
 */

/* Hanc marginis exiguitas non caperet */

#include "mathopd.h"

#ifdef IMAP_MAGIC_TYPE

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
	char buffer[BUFSIZ];
	char input[PATHLEN], default_url[PATHLEN];
	point testpoint, pointarray[MAXVERTS];
	long dist, mindist;
	int k, line, sawpoint, text;
	char *s, *t, *u, *v, *w, *status, *url;
	FILE *fp;
	static char *comma = ", ()\t\r\n";

	if (r->method == M_HEAD)
		return 204;
	else if (r->method != M_GET) {
		r->error = "invalid method for imagemap";
		return 501;
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
	if (setvbuf(fp, buffer, _IOFBF, BUFSIZ)) {
		r->error = "setvbuf failed";
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
			construct_url(buf, url, r->cn->s);
		escape_url(buf);
		r->location = buf;
		return 302;
	}
	return 204;
}

#endif /* IMAP_MAGIC_TYPE */

#ifdef CGI_MAGIC_TYPE

static char **cgi_envp;
static char **cgi_argv;
static int cgi_envc;
static int cgi_argc;

static int add(char *name, const char *value)
{
	if (name && value == 0)
		return 0;
	if (cgi_envc == 0)
		cgi_envp = (char **) malloc(sizeof (char *));
	else
		cgi_envp = (char **) realloc(cgi_envp, 
					     (cgi_envc + 1) * sizeof (char *));
	if (cgi_envp == 0)
		return -1;
	if (name == 0)
		cgi_envp[cgi_envc] = 0;
	else {
		if ((cgi_envp[cgi_envc] =
		     (char *) malloc(strlen(name) + 2 + strlen(value))) == 0)
			return -1;
		sprintf(cgi_envp[cgi_envc], "%s=%s", name, value);
	}
	++cgi_envc;
	return 0;
}

static int add_argv(char *a)
{
	if (cgi_argc == 0)
		cgi_argv = (char **) malloc(sizeof (char *));
	else cgi_argv = (char **) realloc(cgi_argv, 
					  (cgi_argc + 1) * sizeof (char *));
	if (cgi_argv == 0)
		return -1;
	cgi_argv[cgi_argc] = a;
	++cgi_argc;
	return 0;
}

static int make_cgi_envp(struct request *r)
{
	struct server *vs = r->cn->s;
	char *s;
	char t[16];
	char **e = exports;
	unsigned long ia;
	struct hostent *hp;
	char *addr;
	int i;
	char path_translated[PATHLEN];

	faketoreal(r->path_args, path_translated, r->cn->s->controls);

	i = strlen(r->path) - strlen(r->path_args);
	if (i >= 0)
		r->path[i] = '\0';

	cgi_envc = 0;
	cgi_envp = 0;
	sprintf(t, "%d", vs->port);
	addr = r->cn->ip;
	ia = r->cn->peer.s_addr;

#define ADD(x, y) if (add(x, y) == -1) return -1

	ADD("CONTENT_LENGTH", r->in_content_length);
	ADD("CONTENT_TYPE", r->in_content_type);
	ADD("HTTP_AUTHORIZATION", r->authorization);
	ADD("HTTP_COOKIE", r->cookie);
	ADD("HTTP_HOST", r->host);
	ADD("HTTP_FROM", r->from);
	ADD("HTTP_REFERER", r->referer);
	ADD("HTTP_USER_AGENT", r->user_agent);
	if (r->path_args[0]) {
		ADD("PATH_INFO", r->path_args);
		ADD("PATH_TRANSLATED", path_translated);
	}
	ADD("QUERY_STRING", r->args);
	ADD("REMOTE_ADDR", addr);
	if ((hp = gethostbyaddr((char *) &ia, sizeof ia, AF_INET)) != 0) {
		ADD("REMOTE_HOST", hp->h_name);
	}
	ADD("REQUEST_METHOD", r->method_s);
	ADD("SCRIPT_NAME", r->path);
	ADD("SERVER_NAME", vs->name);
	ADD("SERVER_PORT", t);
	ADD("SERVER_PROTOCOL", r->protocol);
	ADD("SERVER_SOFTWARE", server_version);
	if (r->cn->keepalive) {
		ADD("HTTP_CONNECTION", magic_word);
	}
	while ((s = *e++) != 0) {
		ADD(s, getenv(s));
	}
	ADD(0, 0);
	return 0;
}

#define ADD_ARGV(x) if (add_argv(x) == -1) return -1

static int make_cgi_argv(struct request *r, char *b)
{
	cgi_argc = 0;
	cgi_argv = 0;
	ADD_ARGV(b);
	if (r->args && strchr(r->args, '=') == 0) {
		char *a, *w;

		if ((a = strdup(r->args)) == 0)
			return -1;
		do {
			w = strchr(a, '+');
			if (w)
				*w = '\0';
			if (unescape_url(a, a))
				return -1;
			ADD_ARGV(a);
			if (w)
				a = w + 1;
		} while (w);
	}
	ADD_ARGV(0);
	return 0;
}

static int cgi_error(struct request *r, int code, char *error)
{
	struct pool *p = r->cn->output;

	r->status = code;
	r->error = error;
	if (prepare_reply(r) != -1)
		write(STDOUT_FILENO, p->start, p->end - p->start);
	return -1;
}

static int exec_cgi(struct request *r)
{
	char *dir, *base;

	dir = r->path_translated;
	if ((base = strrchr(dir, '/')) == 0)
		return 1;
	*base++ = '\0';
	if (*dir == '\0') {
		dir[0] = '/';
		dir[1] = '\0';
	}
	if (make_cgi_envp(r) == -1 || make_cgi_argv(r, base) == -1)
		return cgi_error(r, 500, "out of memory");
	else if (chdir(dir) == -1) {
		lerror("chdir");
		return cgi_error(r, 500, "chdir failed");
	}
	if (execve(cgi_argv[0], cgi_argv, cgi_envp) == -1) {
		lerror("execve");
		return cgi_error(r, 500, "exec failed");
	}
	return 0;
}

int process_cgi(struct request *r)
{
	return fork_request(r, exec_cgi);
}

#endif /* CGI_MAGIC_TYPE */

#ifdef DUMP_MAGIC_TYPE

#define DUMP_BUFSIZE 4096

int process_dump(struct request *r)
{
	char name[L_tmpnam];
	char buf[DUMP_BUFSIZE];
	char *b;
	int fd, tmp_fd, l, left, t;
	int ncrd, ncwr, ncwt;
	struct connection *cn;
	struct server *s;
	long naccepts, nhandled, nrequests;

	if (r->method != M_GET) {
		r->error = "invalid method for dump";
		return 501;
	}

	tmpnam(name);

	tmp_fd = open(name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (tmp_fd == -1) {
		lerror("open");
		r->error = "cannot open temporary file";
		return 500;
	}

	if (unlink(name) == -1) {
		lerror("unlink");
		close(tmp_fd);
		r->error = "cannot unlink temporary file";
		return 500;
	}

	fd = open(r->path_translated, O_RDONLY);
	if (fd != -1) {
		while ((l = read(fd, buf, DUMP_BUFSIZE)) > 0)
		    write(tmp_fd, buf, l);
		close(fd);
	}

	left = DUMP_BUFSIZE;
	b = buf;

	ncrd = ncwr = ncwt = 0;
	cn = connections;
	while (cn) {
		if (cn->state != HC_FREE) {
			switch(cn->action) {
			case HC_READING:
				ncrd++;
				break;
			case HC_WRITING:
				ncwr++;
				break;
			case HC_WAITING:
				ncwt++;
				break;
			}
		}
		cn = cn->next;
	}
	sprintf(b,
		"Uptime: %d seconds\n"
		"Active connections: %d (Rd:%d Wr:%d Wt:%d) out of %d\n"
		"Max simultaneous connections since last dump: %d\n"
		"Number of exited children: %d\n"
		"\n"
		"Server                           "
		"Address            accepts   handled  requests\n"
		"\n",

		(int) (current_time - startuptime),
		nconnections,
		ncrd,
		ncwr,
		ncwt,
		num_connections,
		maxconnections,
		numchildren);

	maxconnections = nconnections;

	l = strlen(b);
	b += l;
	left -= l;

	s = servers;
	t = 0;
	naccepts = nhandled = nrequests = 0;

	while (s) {
		if (left > 200) {
			sprintf(b,
				"%-32s %-16s %9ld %9ld %9ld\n",
				s->fullname, 
				inet_ntoa(s->addr),
				s->naccepts,
				s->nhandled,
				s->nrequests);
			l = strlen(b);
			b += l;
			left -= l;
		}
		else if (t == 0) {
			t = 1;
			sprintf(b, "(more)\n");
			l = strlen(b);
			b += l;
			left -= l;
		}
		naccepts += s->naccepts;
		nhandled += s->nhandled;
		nrequests += s->nrequests;
		s = s->next;
	}

	sprintf(b,
		"\n"
		"   total                                         "          
		"%9ld %9ld %9ld\n",
		naccepts,
		nhandled,
		nrequests);

	write(tmp_fd, buf, strlen(buf));
	fstat(tmp_fd, &r->finfo);

	r->num_content = 0;
	r->content_type = "text/plain";
	r->cn->rfd = tmp_fd;
	r->content_length = r->finfo.st_size;
	lseek(fd, 0, SEEK_SET);
	return 200;
}

#endif /* DUMP_MAGIC_TYPE */

#ifdef REDIRECT_MAGIC_TYPE

int process_redirect(struct request *r)
{
	char **redirects = r->c->redirects;
	static char buf[PATHLEN];
	int n = r->c->redirectno;

	if (redirects[n] == 0) {
		n = 0;
		if (redirects[n] == 0) {
			r->error = "no srever to redirect to";
			return 500;
		}
	}
	r->c->redirectno = n + 1;
	if (*redirects[n] == '/')
		construct_url(buf, redirects[n], r->cn->s);
	else
		strcpy(buf, redirects[n]);
	strcat(buf, r->path_args);
	escape_url(buf);
	r->location = buf;
	return 302;
}

#endif /* REDIRECT_MAGIC_TYPE */
