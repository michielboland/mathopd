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

/* House of Games */

static const char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include "mathopd.h"

#ifndef TOKEN_LENGTH
#define TOKEN_LENGTH 500
#endif

struct tuning tuning;

char *pid_filename;
char *log_filename;
char *error_filename;

char *rootdir;
char *coredir;
struct connection *connections;
struct server *servers;

char *user_name;

int log_columns;
int *log_column;
int log_gmt;

struct configuration {
	FILE *config_file;
	char *tokbuf;
	size_t size;
	int line;
};

static int num_servers;
static struct control *controls;

static const char c_all[] =			"*";
static const char c_accept_multi[] =		"AcceptMulti";
static const char c_access[] =			"Access";
static const char c_address[] =			"Address";
static const char c_admin[] =			"Admin";
static const char c_alias[] =			"Alias";
static const char c_allow[] =			"Allow";
static const char c_allow_dotfiles[] =		"AllowDotfiles";
static const char c_any_host[] =		"AnyHost";
static const char c_apply[] =			"Apply";
static const char c_buf_size[] =		"BufSize";
static const char c_bytes_read[] =		"BytesRead";
static const char c_bytes_written[] =		"BytesWritten";
static const char c_child_log[] =		"ChildLog";
static const char c_clients[] =			"Clients";
static const char c_content_length[] =		"ContentLength";
static const char c_control[] =			"Control";
static const char c_core_directory[] =		"CoreDirectory";
static const char c_ctime[] =			"Ctime";
static const char c_deny[] =			"Deny";
static const char c_dns_lookups[] =		"DNSLookups";
static const char c_encrypted_user_file[] =	"EncryptedUserFile";
static const char c_error_log[] =		"ErrorLog";
static const char c_error_401_file[] =		"Error401File";
static const char c_error_403_file[] =		"Error403File";
static const char c_error_404_file[] =		"Error404File";
static const char c_exact_match[] =		"ExactMatch";
static const char c_export[] =			"Export";
static const char c_external[] =		"External";
static const char c_host[] =			"Host";
static const char c_index_names[] =		"IndexNames";
static const char c_input_buf_size[] =		"InputBufSize";
static const char c_location[] =		"Location";
static const char c_log[] =			"Log";
static const char c_log_format[] =		"LogFormat";
static const char c_log_gmt[] =			"LogGMT";
static const char c_method[] =			"Method";
static const char c_name[] =			"Name";
static const char c_no_apply[] =		"NoApply";
static const char c_no_host[] =			"NoHost";
static const char c_num_connections[] =		"NumConnections";
static const char c_off[] =			"Off";
static const char c_on[] =			"On";
static const char c_path_args[] =		"PathArgs";
static const char c_pid_file[] =		"PIDFile";
static const char c_port[] =			"Port";
static const char c_query_string[] =		"QueryString";
static const char c_realm[] =			"Realm";
static const char c_referer[] =			"Referer";
static const char c_remote_address[] =		"RemoteAddress";
static const char c_remote_port[] =		"RemotePort";
static const char c_remote_user[] =		"RemoteUser";
static const char c_root_directory[] =		"RootDirectory";
static const char c_run_scripts_as_owner[] =	"RunScriptsAsOwner";
static const char c_script_user[] =		"ScriptUser";
static const char c_server[] =			"Server";
static const char c_server_name[] =		"ServerName";
static const char c_specials[] =		"Specials";
static const char c_status[] =			"Status";
static const char c_stay_root[] =		"StayRoot";
static const char c_timeout[] =			"Timeout";
static const char c_tuning[] =			"Tuning";
static const char c_types[] =			"Types";
static const char c_virtual[] =			"Virtual";
static const char c_umask[] =			"Umask";
static const char c_uri[] =			"Uri";
static const char c_user[] =			"User";
static const char c_user_agent[] =		"UserAgent";
static const char c_user_directory[] =		"UserDirectory";
static const char c_user_file[] =		"UserFile";
static const char c_version[] =			"Version";

static const char e_bad_addr[] =	"bad address";
static const char e_bad_alias[] =	"alias without matching location";
static const char e_bad_mask[] =	"mask does not match address";
static const char e_bad_network[] =	"bad network";
static const char e_help[] =		"unknown error (help)";
static const char e_inval[] =		"illegal quantity";
static const char e_keyword[] =		"unknown keyword";
static const char e_memory[] =		"out of memory";
static const char e_illegalport[] =	"illegal port number";
static const char e_noinput[] =		"no input";

static const char t_close[] =		"unexpected closing brace";
static const char t_eof[] =		"unexpected end of file";
static const char t_open[] =		"unexpected opening brace";
static const char t_string[] =		"unexpected string";
static const char t_too_long[] =	"token too long";

static int default_log_column[] = {
	ML_CTIME,
	ML_USERNAME,
	ML_ADDRESS,
	ML_PORT,
	ML_SERVERNAME,
	ML_METHOD,
	ML_URI,
	ML_STATUS,
	ML_CONTENT_LENGTH,
	ML_REFERER,
	ML_USER_AGENT,
	ML_BYTES_READ,
	ML_BYTES_WRITTEN
};


#ifdef NEED_INET_ATON
int inet_aton(const char *cp, struct in_addr *pin)
{
	unsigned long ia;

	ia = inet_addr(cp);
	if (ia == -1)
		return 0;
	pin->s_addr = ia;
	return 1;
}
#endif

static const char *gettoken(struct configuration *p)
{
	int c;
	char w;
	size_t i;
	char state;
	const char *t;

	i = 0;
	state = 1;
	t = e_help;
	do {
		w = 0;
		if ((c = getc(p->config_file)) == EOF) {
			state = 0;
			t = t_eof;
		} else if (c == '\n')
			++p->line;
		switch (state) {
		case 1:
			switch (c) {
			case ' ':
			case '\t':
			case '\r':
			case '\n':
				break;
			case '#':
				state = 2;
				break;
			case '{':
				t = t_open;
				w = 1;
				state = 0;
				break;
			case '}':
				t = t_close;
				w = 1;
				state = 0;
				break;
			case '"':
				t = t_string;
				state = 3;
				break;
			default:
				t = t_string;
				w = 1;
				state = 4;
				break;
			}
			break;
		case 2:
			if (c == '\n')
				state = 1;
			break;
		case 3:
			if (c == '\\')
				state = 5;
			else if (c == '"')
				state = 0;
			else
				w = 1;
			break;
		case 4:
			switch (c) {
			case ' ':
			case '\t':
			case '\r':
			case '\n':
				state = 0;
				break;
			case '#':
			case '"':
			case '{':
			case '}':
				ungetc(c, p->config_file);
				state = 0;
				break;
			default:
				w = 1;
				break;
			}
			break;
		case 5:
			w = 1;
			state = 3;
			break;
		}
		if (w) {
			if (i + 1 < p->size)
				p->tokbuf[i++] = c;
			else {
				state = 0;
				t = t_too_long;
			}
		}
	} while (state);
	p->tokbuf[i] = 0;
	return t;
}

static const char *config_string(struct configuration *p, char **a)
{
	const char *t;

	if ((t = gettoken(p)) != t_string)
		return t;
	if ((*a = strdup(p->tokbuf)) == 0)
		return e_memory;
	return 0;
}

static const char *config_int(struct configuration *p, unsigned long *i)
{
	char *e;
	unsigned long u;
	const char *t;

	if ((t = gettoken(p)) != t_string)
		return t;
	u = strtoul(p->tokbuf, &e, 0);
	if (*e || e == p->tokbuf)
		return e_inval;
	*i = u;
	return 0;
}

static const char *config_flag(struct configuration *p, int *i)
{
	const char *t;

	if ((t = gettoken(p)) != t_string)
		return t;
	if (!strcasecmp(p->tokbuf, c_off))
		*i = 0;
	else if (!strcasecmp(p->tokbuf, c_on))
		*i = 1;
	else
		return e_keyword;
	return 0;
}

static const char *config_address(struct configuration *p, struct in_addr *b)
{
	struct in_addr ia;
	const char *t;

	if ((t = gettoken(p)) != t_string)
		return t;
	if (inet_aton(p->tokbuf, &ia) == 0)
		return e_bad_addr;
	*b = ia;
	return 0;
}

static const char *config_list(struct configuration *p, struct simple_list **ls)
{
	struct simple_list *l;
	const char *t;

	if ((t = gettoken(p)) != t_open)
		return t;
	while ((t = gettoken(p)) != t_close) {
		if (t != t_string)
			return t;
		if ((l = malloc(sizeof *l)) == 0)
			return e_memory;
		if ((l->name = strdup(p->tokbuf)) == 0)
			return e_memory;
		l->next = *ls;
		*ls = l;
	}
	return 0;
}

static const char *config_log(struct configuration *p, int **colsp, int *numcolsp)
{
	int ml;
	int *cols;
	int numcols;
	const char *t;

	ml = 0;
	cols = *colsp;
	numcols = *numcolsp;
	if ((t = gettoken(p)) != t_open)
		return t;
	while ((t = gettoken(p)) != t_close) {
		if (t != t_string)
			return t;
		if (!strcasecmp(p->tokbuf, c_ctime))
			ml = ML_CTIME;
		else if (!strcasecmp(p->tokbuf, c_remote_user))
			ml = ML_USERNAME;
		else if (!strcasecmp(p->tokbuf, c_remote_address))
			ml = ML_ADDRESS;
		else if (!strcasecmp(p->tokbuf, c_remote_port))
			ml = ML_PORT;
		else if (!strcasecmp(p->tokbuf, c_server_name))
			ml = ML_SERVERNAME;
		else if (!strcasecmp(p->tokbuf, c_method))
			ml = ML_METHOD;
		else if (!strcasecmp(p->tokbuf, c_uri))
			ml = ML_URI;
		else if (!strcasecmp(p->tokbuf, c_version))
			ml = ML_VERSION;
		else if (!strcasecmp(p->tokbuf, c_status))
			ml = ML_STATUS;
		else if (!strcasecmp(p->tokbuf, c_content_length))
			ml = ML_CONTENT_LENGTH;
		else if (!strcasecmp(p->tokbuf, c_referer))
			ml = ML_REFERER;
		else if (!strcasecmp(p->tokbuf, c_user_agent))
			ml = ML_USER_AGENT;
		else if (!strcasecmp(p->tokbuf, c_bytes_read))
			ml = ML_BYTES_READ;
		else if (!strcasecmp(p->tokbuf, c_bytes_written))
			ml = ML_BYTES_WRITTEN;
		else if (!strcasecmp(p->tokbuf, c_query_string))
			ml = ML_QUERY_STRING;
		else
			return e_keyword;
		++numcols;
		cols = realloc(cols, sizeof *cols * numcols);
		if (cols == 0)
			return e_memory;
		cols[numcols - 1] = ml;
		*colsp = cols;
		*numcolsp = numcols;
	}
	return 0;
}

static const char *config_mime(struct configuration *p, struct mime **ms, int class)
{
	struct mime *m;
	char *name;
	const char *t;

	if ((t = gettoken(p)) != t_open)
		return t;
	while ((t = gettoken(p)) != t_close) {
		if (t != t_string)
			return t;
		if ((name = strdup(p->tokbuf)) == 0)
			return e_memory;
		if ((t = gettoken(p)) != t_open)
			return t;
		while ((t = gettoken(p)) != t_close) {
			if (t != t_string)
				return t;
			if ((m = malloc(sizeof *m)) == 0)
				return e_memory;
			m->class = class;
			m->name = name;
			if (!strcasecmp(p->tokbuf, c_all))
				m->ext = 0;
			else if ((m->ext = strdup(p->tokbuf)) == 0)
				return e_memory;
			m->next = *ms;
			*ms = m;
		}
	}
	return 0;
}

#define ALLOWDENY 0
#define APPLYNOAPPLY 1

static unsigned long masks[] = {
	0,
	0x80000000,
	0xc0000000,
	0xe0000000,
	0xf0000000,
	0xf8000000,
	0xfc000000,
	0xfe000000,
	0xff000000,
	0xff800000,
	0xffc00000,
	0xffe00000,
	0xfff00000,
	0xfff80000,
	0xfffc0000,
	0xfffe0000,
	0xffff0000,
	0xffff8000,
	0xffffc000,
	0xffffe000,
	0xfffff000,
	0xfffff800,
	0xfffffc00,
	0xfffffe00,
	0xffffff00,
	0xffffff80,
	0xffffffc0,
	0xffffffe0,
	0xfffffff0,
	0xfffffff8,
	0xfffffffc,
	0xfffffffe,
	0xffffffff
};

static const char *config_acccl(struct configuration *p, struct access **ls, int accltype)
{
	struct access *l;
	struct in_addr ia;
	char *sl, *e;
	unsigned long sz;
	const char *t;

	if ((t = gettoken(p)) != t_open)
		return t;
	while ((t = gettoken(p)) != t_close) {
		if (t != t_string)
			return t;
		if ((l = malloc(sizeof *l)) == 0)
			return e_memory;
		l->next = *ls;
		*ls = l;
		if (accltype == ALLOWDENY) {
			if (!strcasecmp(p->tokbuf, c_allow))
				l->type = ALLOW;
			else if (!strcasecmp(p->tokbuf, c_deny))
				l->type = DENY;
			else
				return e_keyword;
		} else {
			if (!strcasecmp(p->tokbuf, c_apply))
				l->type = APPLY;
			else if (!strcasecmp(p->tokbuf, c_no_apply))
				l->type = NOAPPLY;
			else
				return e_keyword;
		}
		if ((t = gettoken(p)) != t_string)
			return t;
		sl = strchr(p->tokbuf, '/');
		if (sl == 0)
			return e_bad_network;
		*sl++ = 0;
		sz = strtoul(sl, &e, 0);
		if (*e || e == sl || sz > 32)
			return e_inval;
		l->mask = htonl(masks[sz]);
		if (inet_aton(p->tokbuf, &ia) == 0)
			return e_bad_addr;
		l->addr = ia.s_addr;
		if ((l->mask | l->addr) != l->mask)
			return e_bad_mask;
	}
	return 0;
}

static const char *config_access(struct configuration *p, struct access **ls)
{
	return config_acccl(p, ls, ALLOWDENY);
}

static const char *config_clients(struct configuration *p, struct access **ls)
{
	return config_acccl(p, ls, APPLYNOAPPLY);
}

static void chopslash(char *s)
{
	char *t;

	t = s + strlen(s);
	while (--t >= s && *t == '/')
		*t = 0;
}

static const char *config_control(struct configuration *p, struct control **as)
{
	struct control *a, *b;
	struct simple_list *l;
	const char *t;

	b = *as;
	while (b && b->locations)
		b = b->next;
	if ((a = malloc(sizeof *a)) == 0)
		return e_memory;
	a->locations = 0;
	a->alias = 0;
	a->clients = 0;
	a->exact_match = 0;
	a->user_directory = 0;
	if (b) {
		a->index_names = b->index_names;
		a->accesses = b->accesses;
		a->mimes = b->mimes;
		a->path_args_ok = b->path_args_ok;
		a->admin = b->admin;
		a->realm = b->realm;
		a->userfile = b->userfile;
		a->error_401_file = b->error_401_file;
		a->error_403_file = b->error_403_file;
		a->error_404_file = b->error_404_file;
		a->do_crypt = b->do_crypt;
		a->child_filename = b->child_filename;
		a->dns = b->dns;
		a->exports = b->exports;
		a->script_user = b->script_user;
		a->run_scripts_as_owner = b->run_scripts_as_owner;
		a->allow_dotfiles = b->allow_dotfiles;
	} else {
		a->index_names = 0;
		a->accesses = 0;
		a->mimes = 0;
		a->path_args_ok = 0;
		a->admin = 0;
		a->realm = 0;
		a->userfile = 0;
		a->error_401_file = 0;
		a->error_403_file = 0;
		a->error_404_file = 0;
		a->do_crypt = 0;
		a->child_filename = 0;
		a->dns = 1;
		a->exports = 0;
		a->script_user = 0;
		a->run_scripts_as_owner = 0;
		a->allow_dotfiles = 0;
	}
	a->next = *as;
	*as = a;
	if ((t = gettoken(p)) != t_open)
		return t;
	while ((t = gettoken(p)) != t_close) {
		if (t != t_string)
			return t;
		if (!strcasecmp(p->tokbuf, c_location)) {
			if ((l = malloc(sizeof *l)) == 0)
				return e_memory;
			if ((t = gettoken(p)) != t_string)
				return t;
			chopslash(p->tokbuf);
			if ((l->name = strdup(p->tokbuf)) == 0)
				return e_memory;
			if (a->locations) {
				l->next = a->locations->next;
				a->locations->next = l;
			} else {
				l->next = l;
				a->locations = l;
			}
			continue;
		} else if (!strcasecmp(p->tokbuf, c_alias)) {
			if ((t = gettoken(p)) != t_string)
				return t;
			chopslash(p->tokbuf);
			if ((a->alias = strdup(p->tokbuf)) == 0)
				return e_memory;
			continue;
		} else if (!strcasecmp(p->tokbuf, c_path_args))
			t = config_flag(p, &a->path_args_ok);
		else if (!strcasecmp(p->tokbuf, c_index_names))
			t = config_list(p, &a->index_names);
		else if (!strcasecmp(p->tokbuf, c_access))
			t = config_access(p, &a->accesses);
		else if (!strcasecmp(p->tokbuf, c_clients))
			t = config_clients(p, &a->clients);
		else if (!strcasecmp(p->tokbuf, c_types))
			t = config_mime(p, &a->mimes, CLASS_FILE);
		else if (!strcasecmp(p->tokbuf, c_specials))
			t = config_mime(p, &a->mimes, CLASS_SPECIAL);
		else if (!strcasecmp(p->tokbuf, c_external))
			t = config_mime(p, &a->mimes, CLASS_EXTERNAL);
		else if (!strcasecmp(p->tokbuf, c_admin))
			t = config_string(p, &a->admin);
		else if (!strcasecmp(p->tokbuf, c_realm))
			t = config_string(p, &a->realm);
		else if (!strcasecmp(p->tokbuf, c_user_file))
			t = config_string(p, &a->userfile);
		else if (!strcasecmp(p->tokbuf, c_error_401_file))
			t = config_string(p, &a->error_401_file);
		else if (!strcasecmp(p->tokbuf, c_error_403_file))
			t = config_string(p, &a->error_403_file);
		else if (!strcasecmp(p->tokbuf, c_error_404_file))
			t = config_string(p, &a->error_404_file);
		else if (!strcasecmp(p->tokbuf, c_encrypted_user_file))
			t = config_flag(p, &a->do_crypt);
		else if (!strcasecmp(p->tokbuf, c_child_log))
			t = config_string(p, &a->child_filename);
		else if (!strcasecmp(p->tokbuf, c_dns_lookups))
			t = config_flag(p, &a->dns);
		else if (!strcasecmp(p->tokbuf, c_export))
			t = config_list(p, &a->exports);
		else if (!strcasecmp(p->tokbuf, c_exact_match))
			t = config_flag(p, &a->exact_match);
		else if (!strcasecmp(p->tokbuf, c_script_user))
			t = config_string(p, &a->script_user);
		else if (!strcasecmp(p->tokbuf, c_run_scripts_as_owner))
			t = config_flag(p, &a->run_scripts_as_owner);
		else if (!strcasecmp(p->tokbuf, c_allow_dotfiles))
			t = config_flag(p, &a->allow_dotfiles);
		else if (!strcasecmp(p->tokbuf, c_user_directory))
			t = config_flag(p, &a->user_directory);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	if (a->alias && (a->locations == 0))
		return e_bad_alias;
	return 0;
}

static const char *config_vhost(struct virtual **vs, struct vserver *s, const char *host, int anyhost)
{
	struct virtual *v;

	if ((v = malloc(sizeof *v)) == 0)
		return e_memory;
	if (host == 0)
		v->host = 0;
	else {
		if ((v->host = strdup(host)) == 0)
			return e_memory;
	}
	v->fullname = 0;
	v->parent = s->server;
	v->controls = 0; /* filled in later */
	v->vserver = s;
	v->next = *vs;
	v->anyhost = anyhost;
	*vs = v;
	return 0;
}

static const char *config_virtual(struct configuration *p, struct vserver **vs, struct server *parent)
{
	struct vserver *v;
	int nameless;
	const char *t;

	if ((v = malloc(sizeof *v)) == 0)
		return e_memory;
	v->server = parent;
	v->controls = parent->controls;
	nameless = 1;
	v->next = *vs;
	*vs = v;
	if ((t = gettoken(p)) != t_open)
		return t;
	while ((t = gettoken(p)) != t_close) {
		if (t != t_string)
			return t;
		if (!strcasecmp(p->tokbuf, c_host)) {
			if ((t = gettoken(p)) != t_string)
				return t;
			nameless = 0;
			t = config_vhost(&parent->children, v, p->tokbuf, 0);
		} else if (!strcasecmp(p->tokbuf, c_no_host)) {
			nameless = 0;
			t = config_vhost(&parent->children, v, 0, 0);
		} else if (!strcasecmp(p->tokbuf, c_control))
			t = config_control(p, &v->controls);
		else if (!strcasecmp(p->tokbuf, c_any_host)) {
			t = config_vhost(&parent->children, v, 0, 1);
			continue;
		} else
			t = e_keyword;
		if (t)
			return t;
	}
	return 0;
}

static const char *config_server(struct configuration *p, struct server **ss)
{
	struct server *s;
	const char *t;

	if ((s = malloc(sizeof *s)) == 0)
		return e_memory;
	s->port = 80;
	s->addr.s_addr = 0;
	s->s_name = 0;
	s->children = 0;
	s->vservers= 0;
	s->controls = controls;
	s->naccepts = 0;
	s->nhandled = 0;
	if ((t = gettoken(p)) != t_open)
		return t;
	while ((t = gettoken(p)) != t_close) {
		if (t != t_string)
			return t;
		if (!strcasecmp(p->tokbuf, c_port))
			t = config_int(p, &s->port);
		else if (!strcasecmp(p->tokbuf, c_name))
			t = config_string(p, &s->s_name);
		else if (!strcasecmp(p->tokbuf, c_address))
			t = config_address(p, &s->addr);
		else if (!strcasecmp(p->tokbuf, c_virtual))
			t = config_virtual(p, &s->vservers, s);
		else if (!strcasecmp(p->tokbuf, c_control))
			t = config_control(p, &s->controls);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	if (s->port == 0 || s->port > 0xffff)
		return e_illegalport;
	num_servers++;
	s->next = *ss;
	*ss = s;
	return 0;
}

static const char *fill_servernames(void)
{
	struct server *s;
	struct virtual *v;
	char buf[8];
	char *name;

	s = servers;
	while (s) {
		v = s->children;
		while (v) {
			v->controls = v->vserver->controls;
			name = v->host ? v->host : s->s_name;
			if (name) {
				if (s->port == 80)
					v->fullname = name;
				else {
					v->fullname = malloc(strlen(name) + sprintf(buf, ":%lu", s->port) + 1);
					if (v->fullname == 0)
						return e_memory;
					strcpy(v->fullname, name);
					strcat(v->fullname, buf);
				}
			}
			v = v->next;
		}
		s = s->next;
	}
	return 0;
}

static const char *config_tuning(struct configuration *p, struct tuning *tp)
{
	const char *t;

	if ((t = gettoken(p)) != t_open)
		return t;
	while ((t = gettoken(p)) != t_close) {
		if (t != t_string)
			return t;
		if (!strcasecmp(p->tokbuf, c_timeout))
			t = config_int(p, &tp->timeout);
		else if (!strcasecmp(p->tokbuf, c_buf_size))
			t = config_int(p, &tp->buf_size);
		else if (!strcasecmp(p->tokbuf, c_input_buf_size))
			t = config_int(p, &tp->input_buf_size);
		else if (!strcasecmp(p->tokbuf, c_num_connections))
			t = config_int(p, &tp->num_connections);
		else if (!strcasecmp(p->tokbuf, c_accept_multi))
			t = config_flag(p, &tp->accept_multi);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	return 0;
}

static const char *config_main(struct configuration *p)
{
	const char *t;

	while ((t = gettoken(p)) != t_eof) {
		if (t != t_string)
			return t;
		if (!strcasecmp(p->tokbuf, c_root_directory))
			t = config_string(p, &rootdir);
		else if (!strcasecmp(p->tokbuf, c_core_directory))
			t = config_string(p, &coredir);
		else if (!strcasecmp(p->tokbuf, c_umask))
			t = config_int(p, &fcm);
		else if (!strcasecmp(p->tokbuf, c_stay_root))
			t = config_flag(p, &stayroot);
		else if (!strcasecmp(p->tokbuf, c_user))
			t = config_string(p, &user_name);
		else if (!strcasecmp(p->tokbuf, c_pid_file))
			t = config_string(p, &pid_filename);
		else if (!strcasecmp(p->tokbuf, c_log))
			t = config_string(p, &log_filename);
		else if (!strcasecmp(p->tokbuf, c_error_log))
			t = config_string(p, &error_filename);
		else if (!strcasecmp(p->tokbuf, c_tuning))
			t = config_tuning(p, &tuning);
		else if (!strcasecmp(p->tokbuf, c_control))
			t = config_control(p, &controls);
		else if (!strcasecmp(p->tokbuf, c_server))
			t = config_server(p, &servers);
		else if (!strcasecmp(p->tokbuf, c_log_format))
			t = config_log(p, &log_column, &log_columns);
		else if (!strcasecmp(p->tokbuf, c_log_gmt))
			t = config_flag(p, &log_gmt);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	return 0;
}

static struct pool *new_pool(size_t s)
{
	char *t;
	struct pool *p;

	p = malloc(sizeof *p);
	if (p == 0)
		return 0;
	t = malloc(s);
	if (t == 0) {
		free(p);
		return 0;
	}
	p->floor = t;
	p->ceiling = t + s;
	return p;
}

const char *config(const char *config_filename)
{
	const char *s;
	unsigned long n;
	struct connection *cn;
	struct configuration *p;

	p = malloc(sizeof *p);
	if (p == 0)
		return e_memory;
	p->size = TOKEN_LENGTH;
	p->tokbuf = malloc(p->size);
	if (p->tokbuf == 0) {
		free(p);
		return e_memory;
	}
	if (config_filename) {
		p->config_file = fopen(config_filename, "r");
		if (p->config_file == 0) {
			fprintf(stderr, "Cannot open configuration file %s\n", config_filename);
			free(p->tokbuf);
			free(p);
			return e_noinput;
		}
	} else
		p->config_file = stdin;
	tuning.buf_size = DEFAULT_BUF_SIZE;
	tuning.input_buf_size = INPUT_BUF_SIZE;
	tuning.num_connections = DEFAULT_NUM_CONNECTIONS;
	tuning.timeout = DEFAULT_TIMEOUT;
	tuning.accept_multi = 1;
	fcm = DEFAULT_UMASK;
	stayroot = 0;
	log_columns = 0;
	log_column = 0;
	log_gmt = 0;
	p->line = 1;
	s = config_main(p);
	if (config_filename)
		fclose(p->config_file);
	if (s) {
		if (config_filename)
			fprintf(stderr, "In configuration file: %s\n", config_filename);
		fprintf(stderr, "Error at token '%s' around line %d\n", p->tokbuf, p->line);
		free(p->tokbuf);
		free(p);
		return s;
	}
	free(p->tokbuf);
	free(p);
	if (log_column == 0) {
		log_column = default_log_column;
		log_columns = sizeof default_log_column / sizeof default_log_column[0];
	}
	s = fill_servernames();
	if (s)
		return s;
	if (init_pollfds(tuning.num_connections + num_servers) == -1)
		return e_memory;
	for (n = 0; n < tuning.num_connections; n++) {
		if ((cn = malloc(sizeof *cn)) == 0)
			return e_memory;
		if ((cn->r = malloc(sizeof *cn->r)) == 0)
			return e_memory;
		if ((cn->input = new_pool(tuning.input_buf_size)) == 0)
			return e_memory;
		if ((cn->output = new_pool(tuning.buf_size)) == 0)
			return e_memory;
		cn->r->cn = cn;
		cn->next = connections;
		cn->state = HC_FREE;
		connections = cn;
	}
	if (init_log_buffer(tuning.input_buf_size + 1000) == -1)
		return e_memory;
	return 0;
}
