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

/* Peg */

#include "mathopd.h"

static char **cgi_envp;
static char **cgi_argv;
static int cgi_envc;
static int cgi_argc;

static int add(const char *name, const char *value)
{
	char *tmp;

	if (name && value == 0)
		return 0;
	if (cgi_envc == 0)
		cgi_envp = malloc(sizeof *cgi_envp);
	else
		cgi_envp = realloc(cgi_envp, (cgi_envc + 1) * sizeof *cgi_envp);
	if (cgi_envp == 0)
		return -1;
	if (name == 0)
		cgi_envp[cgi_envc] = 0;
	else {
		tmp = malloc(strlen(name) + 2 + strlen(value));
		if (tmp == 0)
			return -1;
		sprintf(tmp, "%s=%s", name, value);
		cgi_envp[cgi_envc] = tmp;
	}
	++cgi_envc;
	return 0;
}

#define ADD(x, y) if (add(x, y) == -1) return -1

static int add_argv(const char *a, const char *b, int decode)
{
	char *tmp;
	size_t s;

	if (cgi_argc == 0)
		cgi_argv = malloc(sizeof *cgi_argv);
	else
		cgi_argv = realloc(cgi_argv, (cgi_argc + 1) * sizeof *cgi_argv);
	if (cgi_argv == 0)
		return -1;
	if (a) {
		s = b ? b - a : strlen(a);
		tmp = malloc(s + 1);
		if (tmp == 0)
			return -1;
		if (decode) {
			if (unescape_url_n(a, tmp, s))
				return -1;
		} else {
			memcpy(tmp, a, s);
			tmp[s] = 0;
		}
	} else
		tmp = 0;
	cgi_argv[cgi_argc] = tmp;
	++cgi_argc;
	return 0;
}

#define ADD_ARGV(x, y, z) if (add_argv(x, y, z) == -1) return -1

static char *dnslookup(struct in_addr ia, int level)
{
	int hostok;
	char **al;
	struct hostent *h;
	const char *message;
	char *tmp;

	if (level == 0)
		return 0;
	if (debug)
		log_d("dnslookup: gethostbyaddr(%s)", inet_ntoa(ia));
	h = gethostbyaddr((char *) &ia, sizeof ia, AF_INET);
	if (h == 0 || h->h_name == 0)
		return 0;
	tmp = strdup(h->h_name);
	if (tmp == 0)
		return 0;
	if (level <= 1)
		return tmp;
	hostok = 0;
	message = 0;
	if (debug)
		log_d("dnslookup: gethostbyname(\"%s\")", tmp);
	h = gethostbyname(tmp);
	if (h == 0)
		message = "host not found";
	else if (h->h_name == 0)
		message = "h_name == 0";
	else if (level > 2 && strcasecmp(h->h_name, tmp))
		message = "name not canonical";
	else if (h->h_addrtype != AF_INET)
		message = "h_addrtype != AF_INET";
	else if (h->h_length != sizeof ia)
		message = "h_length != sizeof (struct in_addr)";
	else {
		for (al = h->h_addr_list; *al; al++) {
			if (memcmp(*al, &ia, sizeof ia) == 0) {
				hostok = 1;
				break;
			}
		}
	}
	if (hostok == 0) {
		free(tmp);
		log_d("dnslookup: %s != %s%s%s", tmp, inet_ntoa(ia), message ? ": " : "", message ? message : "");
		return 0;
	}
	return tmp;
}

static int make_cgi_envp(struct request *r)
{
	char t[16];
	struct simple_list *e;
	char path_translated[PATHLEN];
	char *tmp;

	cgi_envc = 0;
	cgi_envp = 0;
	ADD("CONTENT_LENGTH", r->in_content_length);
	ADD("CONTENT_TYPE", r->in_content_type);
	ADD("HTTP_AUTHORIZATION", r->authorization);
	ADD("HTTP_COOKIE", r->cookie);
	ADD("HTTP_HOST", r->host);
	ADD("HTTP_FROM", r->from);
	ADD("HTTP_REFERER", r->referer);
	ADD("HTTP_USER_AGENT", r->user_agent);
	if (r->path_args[0]) {
		faketoreal(r->path_args, path_translated, r, 0);
		ADD("PATH_INFO", r->path_args);
		ADD("PATH_TRANSLATED", path_translated);
	}
	ADD("QUERY_STRING", r->args);
	ADD("REMOTE_ADDR", r->cn->ip);
	tmp = dnslookup(r->cn->peer.sin_addr, r->c->dns);
	if (tmp) {
		ADD("REMOTE_HOST", tmp);
		free(tmp);
	}
	ADD("REQUEST_METHOD", r->method_s);
	ADD("SCRIPT_NAME", r->path);
	ADD("SERVER_NAME", r->servername);
	sprintf(t, "%d", r->cn->s->port);
	ADD("SERVER_PORT", t);
	ADD("SERVER_SOFTWARE", server_version);
	if (r->protocol_major) {
		sprintf(t, "HTTP/%d.%d", r->protocol_major, r->protocol_minor);
		ADD("SERVER_PROTOCOL", t);
	} else
		ADD("SERVER_PROTOCOL", "HTTP/0.9");
	e = r->c->exports;
	while (e) {
		ADD(e->name, getenv(e->name));
		e = e->next;
	}
	ADD(0, 0);
	return 0;
}

static int make_cgi_argv(struct request *r, char *b)
{
	char *a, *w;

	cgi_argc = 0;
	cgi_argv = 0;
	if (r->class == CLASS_EXTERNAL)
		ADD_ARGV(r->content_type, 0, 0);
	ADD_ARGV(b, 0, 0);
	a = r->args;
	if (a && strchr(a, '=') == 0) {
		do {
			w = strchr(a, '+');
			ADD_ARGV(a, w, 1);
			if (w)
				a = w + 1;
		} while (w);
	}
	ADD_ARGV(0, 0, 0);
	return 0;
}

static int cgi_error(struct request *r, int code, const char *error)
{
	struct pool *p;

	r->status = code;
	r->error = error;
	if (prepare_reply(r) != -1) {
		p = r->cn->output;
		write(1, p->start, p->end - p->start);
	}
	return -1;
}

static int init_cgi_env(struct request *r)
{
	char *p, *b;
	int rv;

	p = r->path_translated;
	b = strrchr(p, '/');
	if (b == 0)
		return -1;
	*b = 0;
	rv = chdir(p);
	if (debug)
		log_d("init_cgi_env: chdir(\"%s\") = %d", p, rv);
	*b = '/';
	if (rv == -1)
		return -1;
	if (make_cgi_envp(r) == -1)
		return -1;
	if (make_cgi_argv(r, p) == -1)
		return -1;
	return 0;
}

static int set_uids(uid_t uid, gid_t gid)
{
	int rv;

	if (uid < 100) {
		log_d("refusing to set uid to %d", uid);
		return -1;
	}
	rv = setgroups(1, &gid);
	if (debug)
		log_d("set_uids: setgroups(1, [%d]) = %d", gid, rv);
	if (rv == -1) {
		lerror("setgroups");
		return -1;
	}
	rv = setgid(gid);
	if (debug)
		log_d("set_uids: setgid(%d) = %d", gid, rv);
	if (rv == -1) {
		lerror("setgid");
		return -1;
	}
	rv = setuid(uid);
	if (debug)
		log_d("set_uids: setuid(%d) = %d", uid, rv);
	if (rv == -1) {
		lerror("setuid");
		return -1;
	}
	log_d("set_uids: uid set to %d, gid set to %d", uid, gid);
	return 0;
}

static int exec_cgi(struct request *r)
{
	if (init_cgi_env(r) == -1)
		return cgi_error(r, 500, "could not initialize CGI environment");
	setuid(getuid());
	if (geteuid() == 0) {
		if (set_uids(r->finfo.st_uid, r->finfo.st_gid) == -1)
			return cgi_error(r, 403, "cannot set uids");
	}
	log_d("executing %s", cgi_argv[0]);
	if (execve(cgi_argv[0], (char **) cgi_argv, cgi_envp) == -1) {
		log_d("could not execute %s", cgi_argv[0]);
		lerror("execve");
		return cgi_error(r, 500, "could not execute CGI program");
	}
	return 0;
}

int process_cgi(struct request *r)
{
	return fork_request(r, exec_cgi);
}
