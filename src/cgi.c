/*
 * extras.c - Mathopd server extensions
 *
 * Copyright 1996, 1997, Michiel Boland
 */

/* Peg */

#include "mathopd.h"

static char **cgi_envp;
static char **cgi_argv;
static int cgi_envc;
static int cgi_argc;

static int add(const char *name, const char *value)
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
	struct server *sv = r->cn->s;
	char t[16];
	struct simple_list *e = exports;
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
	sprintf(t, "%d", sv->port);
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
	ADD("SERVER_NAME", r->vs->host ? r->vs->host : sv->name);
	ADD("SERVER_PORT", t);
	ADD("SERVER_SOFTWARE", server_version);

	if (r->protocol_major) {
		sprintf(t, "HTTP/%d.%d",
			r->protocol_major,
			r->protocol_minor);
		ADD("SERVER_PROTOCOL", t);
	} else
		ADD("SERVER_PROTOCOL", "HTTP/0.9");

	while (e) {
		ADD(e->name, getenv(e->name));
		e = e->next;
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

static int cgi_error(struct request *r, int code, const char *error)
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
