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
	cgi_argv[cgi_argc] = tmp;
	++cgi_argc;
	return 0;
}

#define ADD_ARGV(x, y, z) if (add_argv(x, y, z) == -1) return -1

static int make_cgi_envp(struct request *r)
{
	char t[16];
	struct simple_list *e;
	unsigned long ia;
	struct hostent *hp;
	char *addr;
	int i;
	char path_translated[PATHLEN];

	faketoreal(r->path_args, path_translated, r, 0);
	i = strlen(r->path) - strlen(r->path_args);
	if (i >= 0)
		r->path[i] = '\0';
	cgi_envc = 0;
	cgi_envp = 0;
	sprintf(t, "%d", r->cn->s->port);
	addr = r->cn->ip;
	ia = r->cn->peer.sin_addr.s_addr;
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
	hp = gethostbyaddr((char *) &ia, sizeof ia, AF_INET);
	if (hp)
		ADD("REMOTE_HOST", hp->h_name);
	ADD("REQUEST_METHOD", r->method_s);
	ADD("SCRIPT_NAME", r->path);
	ADD("SERVER_NAME", r->servername);
	ADD("SERVER_PORT", t);
	ADD("SERVER_SOFTWARE", server_version);
	if (r->protocol_major) {
		sprintf(t, "HTTP/%d.%d", r->protocol_major, r->protocol_minor);
		ADD("SERVER_PROTOCOL", t);
	} else
		ADD("SERVER_PROTOCOL", "HTTP/0.9");
	e = exports;
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
		write(STDOUT_FILENO, p->start, p->end - p->start);
	}
	return -1;
}

static int init_cgi_env(struct request *r)
{
	char *dir, *base;

	dir = r->path_translated;
	base = strrchr(dir, '/');
	if (base == '\0')
		return -1;
	*base++ = '\0';
	if (make_cgi_envp(r) == -1)
		return -1;
	if (make_cgi_argv(r, base) == -1)
		return -1;
	if (chdir(dir) == -1)
		return -1;
	return 0;
}

static int exec_cgi(struct request *r)
{
	if (init_cgi_env(r) == -1)
		return cgi_error(r, 500, "could not initialize CGI environment");
	if (execve(cgi_argv[0], (char **) cgi_argv, cgi_envp) == -1) {
		log(L_ERROR, "could not execute %s", cgi_argv[0]);
		lerror("execve");
		return cgi_error(r, 500, "could not execute CGI program");
	}
	return 0;
}

int process_cgi(struct request *r)
{
	return fork_request(r, exec_cgi);
}
