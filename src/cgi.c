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

/* Peg */

static const char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>
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
		s = b ? b - a : strlen(a);
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

static char *dnslookup(struct in_addr ia)
{
	char **al;
	struct hostent *h;
	const char *message;
	char *tmp;

	h = gethostbyaddr((char *) &ia, sizeof ia, AF_INET);
	if (h == 0 || h->h_name == 0)
		return 0;
	tmp = strdup(h->h_name);
	if (tmp == 0) {
		log_d("dnslookup: strdup failed");
		return 0;
	}
	message = "name does not match address";
	h = gethostbyname(tmp);
	if (h == 0)
		message = "host not found";
	else if (h->h_name == 0)
		message = "h_name == 0";
	else if (strcasecmp(h->h_name, tmp))
		message = "name not canonical";
	else if (h->h_addrtype != AF_INET)
		message = "h_addrtype != AF_INET";
	else if (h->h_length != sizeof ia)
		message = "h_length != sizeof (struct in_addr)";
	else
		for (al = h->h_addr_list; *al; al++)
			if (memcmp(*al, &ia, sizeof ia) == 0) {
				message = 0;
				break;
			}
	if (message) {
		log_d("dnslookup: %s, address=%s, name=%s", message, inet_ntoa(ia), tmp);
		free(tmp);
		return 0;
	}
	return tmp;
}

static char *cgi_envar(const char *s)
{
	size_t i, j, n;
	char *t;
	int c;

	n = strlen(s);
	t = malloc(n + 6);
	if (t == 0)
		return 0;
	j = sprintf(t, "HTTP_");
	for (i = 0; i < n; i++) {
		c = toupper(s[i]);
		if (c == '-')
			c = '_';
		t[j++] = c;
	}
	t[j] = 0;
	return t;
}

static int make_cgi_envp(struct request *r, struct cgi_parameters *cp)
{
	char t[16];
	struct simple_list *e;
	char path_translated[PATHLEN];
	char *tmp;
	size_t n;

	for (n = 0; n < r->nheaders; n++) {
		switch (strlen(r->headers[n].rh_name)) {
		case 10:
			if (strcasecmp(r->headers[n].rh_name, "Connection") == 0)
				continue;
			break;
		case 12:
			if (strcasecmp(r->headers[n].rh_name, "Content-Type") == 0)
				continue;
			break;
		case 13:
			if (strcasecmp(r->headers[n].rh_name, "Authorization") == 0 && r->user && r->user[0])
				continue;
			break;
		case 14:
			if (strcasecmp(r->headers[n].rh_name, "Content-Length") == 0)
				continue;
			break;
		case 17:
			if (strcasecmp(r->headers[n].rh_name, "Transfer-Encoding") == 0)
				continue;
			break;
		}
		tmp = cgi_envar(r->headers[n].rh_name);
		if (tmp == 0)
			return -1;
		if (add(tmp, r->headers[n].rh_value, 0, cp) == -1) {
			free(tmp);
			return -1;
		}
		free(tmp);
	}
	if (add("GATEWAY_INTERFACE", "CGI/1.1", 0, cp) == -1)
		return -1;
	if (add("CONTENT_LENGTH", r->in_content_length, 0, cp) == -1)
		return -1;
	if (add("CONTENT_TYPE", r->in_content_type, 0, cp) == -1)
		return -1;
	if (r->user && r->user[0]) {
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
	sprintf(t, "%s", inet_ntoa(r->cn->peer.sin_addr));
	if (add("REMOTE_ADDR", t, 0, cp) == -1)
		return -1;
	sprintf(t, "%hu", ntohs(r->cn->peer.sin_port));
	if (add("REMOTE_PORT", t, 0, cp) == -1)
		return -1;
	if (r->c->dns) {
		tmp = dnslookup(r->cn->peer.sin_addr);
		if (tmp) {
			if (add("REMOTE_HOST", tmp, 0, cp) == -1) {
				free(tmp);
				return -1;
			}
			free(tmp);
		}
	}
	if (add("REQUEST_METHOD", r->method_s, 0, cp) == -1)
		return -1;
	if (add("SCRIPT_NAME", r->path, strlen(r->path_args), cp) == -1)
		return -1;
	if (add("SERVER_NAME", r->host, 0, cp) == -1)
		return -1;
	sprintf(t, "%s", inet_ntoa(r->cn->sock.sin_addr));
	if (add("SERVER_ADDR", t, 0, cp) == -1)
		return -1;
	sprintf(t, "%hu", ntohs(r->cn->sock.sin_port));
	if (add("SERVER_PORT", t, 0, cp) == -1)
		return -1;
	if (add("SERVER_SOFTWARE", server_version, 0, cp) == -1)
		return -1;
	if (r->protocol_major) {
		sprintf(t, "HTTP/%d.%d", r->protocol_major, r->protocol_minor);
		if (add("SERVER_PROTOCOL", t, 0, cp) == -1)
			return -1;
	} else if (add("SERVER_PROTOCOL", "HTTP/0.9", 0, cp) == -1)
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
	free(cp);
}

int process_cgi(struct request *r)
{
	struct cgi_parameters *cp;
	uid_t u;
	gid_t g;
	struct passwd *pw;
	struct pipe_params *pp;
	int p[2], efd;
	pid_t pid;
	char curdir[PATHLEN], *s;

	s = strrchr(r->path_translated, '/');
	if (s == 0)
		return 500;
	sprintf(curdir, "%.*s", s - r->path_translated, r->path_translated);
	if (r->cn->assbackwards)
		return 500;
	pp = children;
	while (pp) {
		if (pp->cn == 0)
			break;
		pp = pp->next;
	}
	if (pp == 0)
		return 503;
	if (r->c->script_user) {
		pw = getpwnam(r->c->script_user);
		if (pw == 0) {
			log_d("%s: user unknown", r->c->script_user);
			return 500;
		}
		u = pw->pw_uid;
		g = pw->pw_gid;
	} else if (r->c->run_scripts_as_owner) {
		u = r->finfo.st_uid;
		g = r->finfo.st_gid;
	} else {
		u = 0;
		g = 0;
	}
	if (geteuid() == u) {
		log_d("cannot run scripts withouth changing identity");
		return 500;
	}
	cp = malloc(sizeof *cp);
	if (cp == 0) {
		log_d("process_cgi: out of memory");
		return 500;
	}
	cp->cgi_envc = 0;
	cp->cgi_envp = 0;
	cp->cgi_argc = 0;
	cp->cgi_argv = 0;
	if (init_cgi_env(r, cp) == -1) {
		log_d("process_cgi: out of memory");
		destroy_parameters(cp);
		return 500;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, p) == -1) {
		lerror("socketpair");
		destroy_parameters(cp);
		return 500;
	}
	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	if (r->c->child_filename) {
		efd = open_log(r->c->child_filename);
		if (efd == -1) {
			close(p[0]);
			close(p[1]);
			destroy_parameters(cp);
			return 500;
		}
		fcntl(efd, F_SETFD, FD_CLOEXEC);
	} else
		efd = -1;
	pid = spawn(cp->cgi_argv[0], (char **) cp->cgi_argv, cp->cgi_envp, p[1], efd, u, g, curdir);
	if (efd != -1)
		close(efd);
	close(p[1]);
	if (pid == -1) {
		close(p[0]);
		destroy_parameters(cp);
		return 500;
	}
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	init_child(pp, r, p[0]);
	destroy_parameters(cp);
	return -1;
}
