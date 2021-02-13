/*
 *   Copyright 1996 - 2004 Michiel Boland.
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

/* Peg */

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include "mathopd.h"

struct cgi_parameters {
	char **cgi_envp;
	char **cgi_argv;
	int cgi_envc;
	int cgi_argc;
};

static int add(const char *name, const char *value, size_t choplen, struct cgi_parameters *cp)
{
	char *tmp;
	size_t namelen, valuelen;
	char **e;

	if (name && value == 0)
		return 0;
	e = realloc(cp->cgi_envp, (cp->cgi_envc + 1) * sizeof *cp->cgi_envp);
	if (e == 0)
		return -1;
	cp->cgi_envp = e;
	if (name == 0) {
		if (value == 0)
			cp->cgi_envp[cp->cgi_envc] = 0;
		else if ((cp->cgi_envp[cp->cgi_envc] = strdup(value)) == 0)
			return -1;
	} else {
		namelen = strlen(name);
		valuelen = strlen(value);
		if (choplen) {
			if (choplen < valuelen)
				valuelen -= choplen;
			else
				valuelen = 0;
		}
		tmp = malloc(namelen + valuelen + 2);
		if (tmp == 0) {
			cp->cgi_envp[cp->cgi_envc] = 0;
			return -1;
		}
		memcpy(tmp, name, namelen);
		tmp[namelen] = '=';
		memcpy(tmp + namelen + 1, value, valuelen);
		tmp[namelen + valuelen + 1] = 0;
		cp->cgi_envp[cp->cgi_envc] = tmp;
	}
	++cp->cgi_envc;
	return 0;
}

static int add_argv(const char *a, const char *b, int decode, struct cgi_parameters *cp)
{
	char *tmp;
	size_t s;
	char **e;

	e = realloc(cp->cgi_argv, (cp->cgi_argc + 1) * sizeof *cp->cgi_argv);
	if (e == 0)
		return -1;
	cp->cgi_argv = e;
	if (a == 0)
		cp->cgi_argv[cp->cgi_argc] = 0;
	else {
		s = b ? (size_t) (b - a) : strlen(a);
		tmp = malloc(s + 1);
		if (tmp == 0) {
			cp->cgi_argv[cp->cgi_argc] = 0;
			return -1;
		}
		if (decode) {
			if (unescape_url_n(a, tmp, s)) {
				free(tmp);
				cp->cgi_argv[cp->cgi_argc] = 0;
				return -1;
			}
		} else {
			memcpy(tmp, a, s);
			tmp[s] = 0;
		}
		cp->cgi_argv[cp->cgi_argc] = tmp;
	}
	++cp->cgi_argc;
	return 0;
}

static int add_http_vars(struct request *r, struct cgi_parameters *cp)
{
	size_t i, j, k, l, n;
	int c, *seen;
	char *tmp, *b, **e;
	const char *name;

	n = r->nheaders;
	if (n == 0)
		return 0;
	seen = malloc(n * sizeof *seen);
	if (seen == 0)
		return -1;
	for (i = 0; i < n; i++)
		seen[i] = 0;
	for (i = 0; i < n; i++) {
		if (seen[i])
			continue;
		name = r->headers[i].rh_name;
		if (strcasecmp(name, "Authorization") == 0 && r->user[0])
			continue;
		l = strlen(name) + strlen(r->headers[i].rh_value) + 7;
		for (j = i + 1; j < n; j++) {
			if (seen[j])
				continue;
			if (strcasecmp(r->headers[j].rh_name, name) == 0) {
				seen[j] = 1;
				l += strlen(r->headers[j].rh_value) + 1;
			}
		}
		tmp = malloc(l);
		if (tmp == 0) {
			free(seen);
			return -1;
		}
		memcpy(tmp, "HTTP_", 5);
		b = tmp + 5;
		l = strlen(name);
		for (k = 0; k < l; k++) {
			c = toupper(name[k]);
			if (c == '-')
				c = '_';
			*b++ = c;
		}
		b += sprintf(b, "=%s", r->headers[i].rh_value);
		for (j = i + 1; j < n; j++) {
			if (seen[j] == 1) {
				b += sprintf(b, ",%s", r->headers[j].rh_value);
				seen[j] = 2;
			}
		}
		e = realloc(cp->cgi_envp, (cp->cgi_envc + 1) * sizeof *cp->cgi_envp);
		if (e == 0) {
			free(tmp);
			free(seen);
			return -1;
		}
		cp->cgi_envp = e;
		cp->cgi_envp[cp->cgi_envc] = tmp;
		++cp->cgi_envc;
	}
	free(seen);
	return 0;
}

static int make_cgi_envp(struct request *r, struct cgi_parameters *cp)
{
	char t[16];
	struct simple_list *e;
	char path_translated[PATHLEN];
	char *tmp;
	struct addrport a;

	if (add_http_vars(r, cp) == -1)
		return -1;
	if (add("GATEWAY_INTERFACE", "CGI/1.1", 0, cp) == -1)
		return -1;
	if (add("CONTENT_LENGTH", r->in_content_length, 0, cp) == -1)
		return -1;
	if (add("CONTENT_TYPE", r->in_content_type, 0, cp) == -1)
		return -1;
	if (r->user[0]) {
		if (add("AUTH_TYPE", "Basic", 0, cp) == -1)
			return -1;
		if (add("REMOTE_USER", r->user, 0, cp) == -1)
			return -1;
	}
	if (add("SCRIPT_FILENAME", r->path_translated, 0, cp) == -1)
		return -1;
	if (r->path_args[0]) {
		faketoreal(r->path_args, path_translated, r, 0, sizeof path_translated);
		if (add("PATH_INFO", r->path_args, 0, cp) == -1)
			return -1;
		if (add("PATH_TRANSLATED", path_translated, 0, cp) == -1)
			return -1;
	}
	if (add("QUERY_STRING", r->args, 0, cp) == -1)
		return -1;
	if (r->args) {
		tmp = malloc(strlen(r->url) + strlen(r->args) + 2);
		if (tmp == 0)
			return -1;
		sprintf(tmp, "%s?%s", r->url, r->args);
		if (add("REQUEST_URI", tmp, 0, cp) == -1) {
			free(tmp);
			return -1;
		}
		free(tmp);
	} else if (add("REQUEST_URI", r->url, 0, cp) == -1)
		return -1;
	sockaddr_to_addrport((struct sockaddr *) &r->cn->peer, &a);
	if (add("REMOTE_ADDR", a.ap_address, 0, cp) == -1)
		return -1;
	if (add("REMOTE_PORT", a.ap_port, 0, cp) == -1)
		return -1;
	if (add("REQUEST_METHOD", r->method_s, 0, cp) == -1)
		return -1;
	if (add("SCRIPT_NAME", r->path, strlen(r->path_args), cp) == -1)
		return -1;
	if (add("SERVER_NAME", r->host, 0, cp) == -1)
		return -1;
	sockaddr_to_addrport((struct sockaddr *) &r->cn->sock, &a);
	if (add("SERVER_ADDR", a.ap_address, 0, cp) == -1)
		return -1;
	if (add("SERVER_PORT", a.ap_port, 0, cp) == -1)
		return -1;
	if (add("SERVER_SOFTWARE", server_version, 0, cp) == -1)
		return -1;
	sprintf(t, "HTTP/%d.%d", r->protocol_major, r->protocol_minor);
	if (add("SERVER_PROTOCOL", t, 0, cp) == -1)
		return -1;
	e = r->c->exports;
	while (e) {
		if (add(e->name, getenv(e->name), 0, cp) == -1)
			return -1;
		e = e->next;
	}
	e = r->c->putenvs;
	while (e) {
		if (add(0, e->name, 0, cp) == -1)
			return -1;
		e = e->next;
	}
	if (add(0, 0, 0, cp) == -1)
		return -1;
	return 0;
}

static int make_cgi_argv(struct request *r, struct cgi_parameters *cp)
{
	const char *a, *w;

	if (r->class == CLASS_EXTERNAL) {
		a = r->content_type;
		do {
			w = strchr(a, ' ');
			if (add_argv(a, w, 0, cp) == -1)
				return -1;
			if (w)
				a = w + 1;
		} while (w);
	}
	if (add_argv(r->path_translated, 0, 0, cp) == -1)
		return -1;
	a = r->args;
	if (a && strchr(a, '=') == 0)
		do {
			w = strchr(a, '+');
			if (add_argv(a, w, 1, cp) == -1)
				return -1;
			if (w)
				a = w + 1;
		} while (w);
	if (add_argv(0, 0, 0, cp) == -1)
		return -1;
	return 0;
}

static int init_cgi_env(struct request *r, struct cgi_parameters *cp)
{
	if (make_cgi_envp(r, cp) == -1)
		return -1;
	if (make_cgi_argv(r, cp) == -1)
		return -1;
	return 0;
}

static void destroy_parameters(struct cgi_parameters *cp)
{
	int i;

	if (cp->cgi_envp) {
		for (i = 0; i < cp->cgi_envc; i++)
			if (cp->cgi_envp[i])
				free(cp->cgi_envp[i]);
		free(cp->cgi_envp);
	}
	if (cp->cgi_argv) {
		for (i = 0; i < cp->cgi_argc; i++)
			if (cp->cgi_argv[i])
				free(cp->cgi_argv[i]);
		free(cp->cgi_argv);
	}
}

int process_cgi(struct request *r)
{
	struct cgi_parameters c;
	uid_t u;
	gid_t g;
	int p[2], efd;
	pid_t pid;

	if (r->curdir[0] == 0) {
		r->status = 500;
		return 0;
	}
	switch (r->c->script_identity) {
	case SI_CHANGETOFIXED:
		u = r->c->script_uid;
		g = r->c->script_gid;
		break;
	case SI_CHANGETOOWNER:
		u = r->finfo.st_uid;
		g = r->finfo.st_gid;
		break;
	default:
		u = 0;
		g = 0;
		break;
	}
	if (amroot) {
		if (u == server_uid) {
			log_d("cannot run scripts without changing identity");
			r->status = 500;
			return 0;
		}
		if (u == 0) {
			log_d("ScriptUser or RunScriptsAsOwner must be set");
			r->status = 500;
			return 0;
		}
	} else if (u) {
		log_d("root privileges are required to change identity");
		r->status = 500;
		return 0;
	}
	c.cgi_envc = 0;
	c.cgi_envp = 0;
	c.cgi_argc = 0;
	c.cgi_argv = 0;
	if (init_cgi_env(r, &c) == -1) {
		log_d("process_cgi: out of memory");
		destroy_parameters(&c);
		r->status = 500;
		return 0;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, p) == -1) {
		lerror("socketpair");
		destroy_parameters(&c);
		r->status = 500;
		return 0;
	}
	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	if (r->c->child_filename) {
		efd = open_log(r->c->child_filename);
		if (efd == -1) {
			close(p[0]);
			close(p[1]);
			destroy_parameters(&c);
			r->status = 500;
			return 0;
		}
		fcntl(efd, F_SETFD, FD_CLOEXEC);
	} else
		efd = -1;
	pid = spawn(c.cgi_argv[0], c.cgi_argv, c.cgi_envp, p[1], efd, u, g, r->curdir);
	if (efd != -1)
		close(efd);
	close(p[1]);
	if (pid == -1) {
		close(p[0]);
		destroy_parameters(&c);
		r->cn->keepalive = 0;
		r->status = 503;
		return 0;
	}
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	init_child(r->cn, p[0]);
	destroy_parameters(&c);
	if (debug)
		log_d("process_cgi: %d", p[0]);
	r->forked = 1;
	return -1;
}
