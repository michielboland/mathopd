/*
 *   Copyright 1996, 1997, 1998, 1999, 2000 Michiel Boland.
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
#include <pwd.h>
#include <grp.h>
#ifdef POLL
#include <poll.h>
#endif
#include "mathopd.h"

struct tuning tuning;

char *pid_filename;
char *log_filename;
char *error_filename;

char *rootdir;
char *coredir;
struct connection *connections;
struct server *servers;

char *user_name;

#ifdef POLL
struct pollfd *pollfds;
#endif 

int log_columns;
int *log_column;

static const char *err;
static char tokbuf[STRLEN];
static int line;
static int num_servers;
static struct control *controls;

static const char c_all[] =		"*";
static const char c_accept_multi[] =	"AcceptMulti";
static const char c_access[] =		"Access";
static const char c_address[] =		"Address";
static const char c_admin[] =		"Admin";
static const char c_alias[] =		"Alias";
static const char c_allow[] =		"Allow";
static const char c_allowed_owners[] =	"AllowedOwners";
static const char c_apply[] =		"Apply";
static const char c_buf_size[] =	"BufSize";
static const char c_bytes_read[] =	"BytesRead";
static const char c_bytes_written[] =	"BytesWritten";
static const char c_child_log[] =	"ChildLog";
static const char c_clients[] =		"Clients";
static const char c_content_length[] =	"ContentLength";
static const char c_control[] =		"Control";
static const char c_core_directory[] =	"CoreDirectory";
static const char c_ctime[] =		"Ctime";
static const char c_deny[] =		"Deny";
static const char c_dns[] =		"DNSLookups";
static const char c_do_crypt[] =	"EncryptedUserFile";
static const char c_error[] =		"ErrorLog";
static const char c_error_401_file[] =	"Error401File";
static const char c_error_403_file[] =	"Error403File";
static const char c_error_404_file[] =	"Error404File";
static const char c_exact_match[] =	"ExactMatch";
static const char c_export[] =		"Export";
static const char c_external[] =	"External";
static const char c_group[] =		"Group";
static const char c_host[] =		"Host";
static const char c_index_names[] =	"IndexNames";
static const char c_input_buf_size[] =	"InputBufSize";
static const char c_location[] =	"Location";
static const char c_log[] =		"Log";
static const char c_log_format[] =	"LogFormat";
static const char c_method[] =		"Method";
static const char c_name[] =		"Name";
static const char c_noapply[] =		"NoApply";
static const char c_nohost[] =		"NoHost";
static const char c_num_connections[] =	"NumConnections";
static const char c_off[] =		"Off";
static const char c_on[] =		"On";
static const char c_path_args[] =	"PathArgs";
static const char c_pid[] =		"PIDFile";
static const char c_port[] =		"Port";
static const char c_realm[] =		"Realm";
static const char c_referer[] =		"Referer";
static const char c_root_directory[] =	"RootDirectory";
static const char c_run_scripts_as_owner[] = "RunScriptsAsOwner";
static const char c_script_user[] =	"ScriptUser";
static const char c_server[] =		"Server";
static const char c_specials[] =	"Specials";
static const char c_status[] =		"Status";
static const char c_stayroot[] =	"StayRoot";
static const char c_tcp_nodelay[] =	"TCPNoDelay";
static const char c_timeout[] =		"Timeout";
static const char c_tuning[] =		"Tuning";
static const char c_types[] =		"Types";
static const char c_virtual[] =		"Virtual";
static const char c_umask[] =		"Umask";
static const char c_uri[] =		"Uri";
static const char c_user[] =		"User";
static const char c_useragent[] =	"UserAgent";
static const char c_userfile[] =	"UserFile";
static const char c_version[] =		"Version";
static const char c_world[] =		"World";

static const char e_bad_addr[] =	"bad address";
static const char e_bad_alias[] =	"alias without matching location";
static const char e_bad_mask[] =	"mask does not match address";
static const char e_bad_network[] =	"bad network";
static const char e_help[] =		"unknown error (help)";
static const char e_inval[] =		"illegal quantity";
static const char e_keyword[] =		"unknown keyword";
static const char e_memory[] =		"out of memory";
static const char e_illegalport[] =	"Illegal port number";
static const char e_noinput[] =		"no input";
static const char e_unknown_user[] =	"unknown user";
static const char e_unknown_group[] =	"unknown group";


static const char t_close[] =		"unexpected closing brace";
static const char t_eof[] =		"unexpected end of file";
static const char t_open[] =		"unexpected opening brace";
static const char t_string[] =		"unexpected string";
static const char t_too_long[] =	"token too long";
static const char t_word[] =		"unexpected word";

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

#define ALLOC(x) if (((x) = malloc(sizeof *(x))) == 0) return e_memory
#define COPY(x, y) if (((x) = strdup(y)) == 0) return e_memory
#define GETWORD(x) if (gettoken(x) != t_word) return err
#define GETSTRING(x) if (gettoken(x) != t_string && err != t_word) return err
#define GETOPEN(x) if (gettoken(x) != t_open) return err
#define REQWORD() if (err != t_word) return err
#define REQSTRING() if (err != t_string && err != t_word) return err
#define NOTCLOSE(x) gettoken(x) != t_close
#define NOTEOF(x) gettoken(x) != t_eof

#ifdef NEED_INET_ATON
int inet_aton(const char *cp, struct in_addr *pin)
{
	unsigned long ia;

	ia = inet_addr(cp);
	if (ia == (unsigned long) -1)
		return 0;
	pin->s_addr = ia;
	return 1;
}
#endif

static const char *gettoken(FILE *f)
{
	int c;
	char w;
	int i;
	char state;

	i = 0;
	state = 1;
	err = e_help;
	do {
		w = 0;
		if ((c = getc(f)) == EOF) {
			state = 0;
			err = t_eof;
		} else if (c == '\n')
			++line;
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
				err = t_open;
				w = 1;
				state = 0;
				break;
			case '}':
				err = t_close;
				w = 1;
				state = 0;
				break;
			case '"':
				err = t_string;
				state = 3;
				break;
			default:
				err = t_word;
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
				ungetc(c, f);
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
			if (i < STRLEN - 1)
				tokbuf[i++] = c;
			else {
				state = 0;
				err = t_too_long;
			}
		}
	} while (state);
	tokbuf[i] = 0;
	return err;
}

static const char *config_string(FILE *f, char **a)
{
	GETSTRING(f);
	COPY(*a, tokbuf);
	return 0;
}

static const char *config_int(FILE *f, unsigned long *i)
{
	char *e;
	unsigned long u;

	GETWORD(f);
	u = strtoul(tokbuf, &e, 0);
	if (*e || e == tokbuf)
		return e_inval;
	*i = u;
	return 0;
}

static const char *config_flag(FILE *f, int *i)
{
	GETWORD(f);
	if (!strcasecmp(tokbuf, c_off))
		*i = 0;
	else if (!strcasecmp(tokbuf, c_on))
		*i = 1;
	else
		return e_keyword;
	return 0;
}

static const char *config_address(FILE *f, struct in_addr *b)
{
	struct in_addr ia;

	GETSTRING(f);
	if (inet_aton(tokbuf, &ia) == 0)
		return e_bad_addr;
	*b = ia;
	return 0;
}

static const char *config_list(FILE *f, struct simple_list **ls)
{
	struct simple_list *l;

	GETOPEN(f);
	while (NOTCLOSE(f)) {
		REQSTRING();
		ALLOC(l);
		COPY(l->name, tokbuf);
		l->next = *ls;
		*ls = l;
	}
	return 0;
}

static const char *config_log(FILE *f, int **colsp, int *numcolsp)
{
	int ml;
	int *cols;
	int numcols;

	ml = 0;
	cols = *colsp;
	numcols = *numcolsp;
  	GETOPEN(f);
	while (NOTCLOSE(f)) {
	  	REQWORD();
		if (!strcasecmp(tokbuf, c_ctime))
			ml = ML_CTIME;
		else if (!strcasecmp(tokbuf, c_user))
			ml = ML_USERNAME;
		else if (!strcasecmp(tokbuf, c_address))
			ml = ML_ADDRESS;
		else if (!strcasecmp(tokbuf, c_port))
			ml = ML_PORT;
		else if (!strcasecmp(tokbuf, c_server))
			ml = ML_SERVERNAME;
		else if (!strcasecmp(tokbuf, c_method))
			ml = ML_METHOD;
		else if (!strcasecmp(tokbuf, c_uri))
			ml = ML_URI;
		else if (!strcasecmp(tokbuf, c_version))
			ml = ML_VERSION;
		else if (!strcasecmp(tokbuf, c_status))
			ml = ML_STATUS;
		else if (!strcasecmp(tokbuf, c_content_length))
			ml = ML_CONTENT_LENGTH;
		else if (!strcasecmp(tokbuf, c_referer))
			ml = ML_REFERER;
		else if (!strcasecmp(tokbuf, c_useragent))
			ml = ML_USER_AGENT;
		else if (!strcasecmp(tokbuf, c_bytes_read))
			ml = ML_BYTES_READ;
		else if (!strcasecmp(tokbuf, c_bytes_written))
			ml = ML_BYTES_WRITTEN;
		else
			return e_keyword;
		++numcols;
		if (cols)
			cols = realloc(cols, sizeof *cols * numcols);
		else
			cols = malloc(sizeof *cols);
		if (cols == 0)
			return e_memory;
		cols[numcols - 1] = ml;
		*colsp = cols;
		*numcolsp = numcols;
	}
	return 0;
}

static const char *config_mime(FILE *f, struct mime **ms, int class)
{
	struct mime *m;
	char *name, *s;

	GETOPEN(f);
	while (NOTCLOSE(f)) {
		REQSTRING();
		COPY(name, tokbuf);
		GETOPEN(f);
		while (NOTCLOSE(f)) {
			REQSTRING();
			ALLOC(m);
			m->class = class;
			m->name = name;
			if (!strcasecmp(tokbuf, c_all))
				m->ext = 0;
			else {
				s = tokbuf;
				while (*s == '.')
					++s;
				COPY(m->ext, s);
			}
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

static const char *config_acccl(FILE *f, struct access **ls, int t)
{
	struct access *l;
	struct in_addr ia;
	char *sl, *e;
	unsigned long sz;

	GETOPEN(f);
	while (NOTCLOSE(f)) {
		REQWORD();
		ALLOC(l);
		l->next = *ls;
		*ls = l;
		if (t == ALLOWDENY) {
			if (!strcasecmp(tokbuf, c_allow))
				l->type = ALLOW;
			else if (!strcasecmp(tokbuf, c_deny))
				l->type = DENY;
			else
				return e_keyword;
		} else {
			if (!strcasecmp(tokbuf, c_apply))
				l->type = APPLY;
			else if (!strcasecmp(tokbuf, c_noapply))
				l->type = NOAPPLY;
			else
				return e_keyword;
		}
		GETWORD(f);
		sl = strchr(tokbuf, '/');
		if (sl == 0)
			return e_bad_network;
		*sl++ = 0;
		sz = strtoul(sl, &e, 0);
		if (*e || e == sl || sz > 32)
			return e_inval;
		l->mask = htonl(masks[sz]);
		if (inet_aton(tokbuf, &ia) == 0)
			return e_bad_addr;
		l->addr = ia.s_addr;
		if ((l->mask | l->addr) != l->mask)
			return e_bad_mask;
	}
	return 0;
}

static const char *config_access(FILE *f, struct access **ls)
{
	return config_acccl(f, ls, ALLOWDENY);
}

static const char *config_clients(FILE *f, struct access **ls)
{
	return config_acccl(f, ls, APPLYNOAPPLY);
}

static const char *config_owners(FILE *f, struct file_owner **op)
{
	struct file_owner *o;
	struct passwd *pw;
	struct group *gr;

	GETOPEN(f);
	while (NOTCLOSE(f)) {
		REQWORD();
		ALLOC(o);
		o->next = *op;
		*op = o;
		if (!strcasecmp(tokbuf, c_user)) {
			o->type = FO_USER;
			GETSTRING(f);
			pw = getpwnam(tokbuf);
			if (pw == 0)
				return e_unknown_user;
			o->user = pw->pw_uid;
		} else if (!strcasecmp(tokbuf, c_group)) {
			o->type = FO_GROUP;
			GETSTRING(f);
			gr = getgrnam(tokbuf);
			if (gr == 0)
				return e_unknown_group;
			o->group = gr->gr_gid;
		} else if (!strcasecmp(tokbuf, c_world))
			o->type = FO_WORLD;
		else
			return e_keyword;
	}
	return 0;
}

static void chopslash(char *s)
{
	char *t;

	t = s + strlen(s);
	while (--t >= s && *t == '/')
		*t = 0;
}

static const char *config_control(FILE *f, struct control **as)
{
	const char *t = 0;
	struct control *a, *b;
	struct simple_list *l;

	b = *as;
	while (b && b->locations)
		b = b->next;
	ALLOC(a);
	a->locations = 0;
	a->alias = 0;
	a->clients = 0;
	a->exact_match = 0;
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
		a->allowed_owners = b->allowed_owners;
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
		a->allowed_owners = 0;
	}
	a->next = *as;
	*as = a;
	GETOPEN(f);
	while (NOTCLOSE(f)) {
		REQWORD();
		if (!strcasecmp(tokbuf, c_location)) {
			ALLOC(l);
			GETSTRING(f);
			chopslash(tokbuf);
			COPY(l->name, tokbuf);
			if (a->locations) {
				l->next = a->locations->next;
				a->locations->next = l;
			} else {
				l->next = l;
				a->locations = l;
			}
		} else if (!strcasecmp(tokbuf, c_alias)) {
			GETSTRING(f);
			chopslash(tokbuf);
			COPY(a->alias, tokbuf);
		} else if (!strcasecmp(tokbuf, c_path_args))
			t = config_flag(f, &a->path_args_ok);
		else if (!strcasecmp(tokbuf, c_index_names))
			t = config_list(f, &a->index_names);
		else if (!strcasecmp(tokbuf, c_access))
			t = config_access(f, &a->accesses);
		else if (!strcasecmp(tokbuf, c_clients))
			t = config_clients(f, &a->clients);
		else if (!strcasecmp(tokbuf, c_types))
			t = config_mime(f, &a->mimes, CLASS_FILE);
		else if (!strcasecmp(tokbuf, c_specials))
			t = config_mime(f, &a->mimes, CLASS_SPECIAL);
		else if (!strcasecmp(tokbuf, c_external))
			t = config_mime(f, &a->mimes, CLASS_EXTERNAL);
		else if (!strcasecmp(tokbuf, c_admin))
			t = config_string(f, &a->admin);
		else if (!strcasecmp(tokbuf, c_realm))
			t = config_string(f, &a->realm);
		else if (!strcasecmp(tokbuf, c_userfile))
			t = config_string(f, &a->userfile);
		else if (!strcasecmp(tokbuf, c_error_401_file))
			t = config_string(f, &a->error_401_file);
		else if (!strcasecmp(tokbuf, c_error_403_file))
			t = config_string(f, &a->error_403_file);
		else if (!strcasecmp(tokbuf, c_error_404_file))
			t = config_string(f, &a->error_404_file);
		else if (!strcasecmp(tokbuf, c_do_crypt))
			t = config_flag(f, &a->do_crypt);
		else if (!strcasecmp(tokbuf, c_child_log))
			t = config_string(f, &a->child_filename);
		else if (!strcasecmp(tokbuf, c_dns))
			t = config_flag(f, &a->dns);
		else if (!strcasecmp(tokbuf, c_export))
			t = config_list(f, &a->exports);
		else if (!strcasecmp(tokbuf, c_exact_match))
			t = config_flag(f, &a->exact_match);
		else if (!strcasecmp(tokbuf, c_script_user))
			t = config_string(f, &a->script_user);
		else if (!strcasecmp(tokbuf, c_run_scripts_as_owner))
			t = config_flag(f, &a->run_scripts_as_owner);
		else if (!strcasecmp(tokbuf, c_allowed_owners))
			t = config_owners(f, &a->allowed_owners);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	if (a->alias && (a->locations == 0))
		return e_bad_alias;
	return 0;
}

static const char *config_vhost(struct virtual **vs, struct vserver *s, const char *host)
{
	struct virtual *v;

	ALLOC(v);
	if (host == 0)
		v->host = 0;
	else {
		COPY(v->host, host);
	}
	v->fullname = 0;
	v->parent = s->server;
	v->controls = 0; /* filled in later */
	v->nrequests = 0;
	v->nread = 0;
	v->nwritten = 0;
	v->vserver = s;
	v->next = *vs;
	*vs = v;
	return 0;
}

static const char *config_virtual(FILE *f, struct vserver **vs, struct server *parent)
{
	const char *t = 0;
	struct vserver *v;
	int nameless;

	ALLOC(v);
	v->server = parent;
	v->controls = parent->controls;
	nameless = 1;
	v->next = *vs;
	*vs = v;
	GETOPEN(f);
	while (NOTCLOSE(f)) {
		REQWORD();
		if (!strcasecmp(tokbuf, c_host)) {
			GETSTRING(f);
			nameless = 0;
			t = config_vhost(&parent->children, v, tokbuf);
		} else if (!strcasecmp(tokbuf, c_nohost)) {
			nameless = 0;
			t = config_vhost(&parent->children, v, 0);
		} else if (!strcasecmp(tokbuf, c_control))
			t = config_control(f, &v->controls);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	return nameless ? config_vhost(&parent->children, v, 0) : 0;
}

static const char *config_server(FILE *f, struct server **ss)
{
	const char *t = 0;
	struct server *s;

	ALLOC(s);
	num_servers++;
	s->port = 80;
	s->addr.s_addr = 0;
	s->s_name = 0;
	s->children = 0;
	s->vservers= 0;
	s->s_fullname = 0;
	s->controls = controls;
	s->next = *ss;
	s->naccepts = 0;
	s->nhandled = 0;
	s->tcp_nodelay = 0;
	*ss = s;
	GETOPEN(f);
	while (NOTCLOSE(f)) {
		REQWORD();
		if (!strcasecmp(tokbuf, c_port))
			t = config_int(f, &s->port);
		else if (!strcasecmp(tokbuf, c_name))
			t = config_string(f, &s->s_name);
		else if (!strcasecmp(tokbuf, c_address))
			t = config_address(f, &s->addr);
		else if (!strcasecmp(tokbuf, c_virtual))
			t = config_virtual(f, &s->vservers, s);
		else if (!strcasecmp(tokbuf, c_control))
			t = config_control(f, &s->controls);
		else if (!strcasecmp(tokbuf, c_tcp_nodelay))
			t = config_flag(f, &s->tcp_nodelay);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	if (s->port == 0 || s->port > 0xffff)
		return e_illegalport;
	return 0;
}

static const char *fill_servernames(void)
{
	struct server *s;
	struct virtual *v;
	char buf[256];
	char *name;

	s = servers;
	while (s) {
		if (s->s_name) {
			if (s->port == 80)
				s->s_fullname = s->s_name;
			else {
				sprintf(buf, "%.200s:%lu", s->s_name, s->port);
				COPY(s->s_fullname, buf);
			}
		}
		v = s->children;
		while (v) {
			v->controls = v->vserver->controls;
			name = v->host ? v->host : s->s_name;
			if (name) {
				if (s->port == 80)
					v->fullname = name;
				else {
					sprintf(buf, "%.200s:%lu", name, s->port);
					COPY(v->fullname, buf);
				}
			}
			v = v->next;
		}
		s = s->next;
	}
	return 0;
}

static const char *config_tuning(FILE *f, struct tuning *tp)
{
	const char *t = 0;

	GETOPEN(f);
	while (NOTCLOSE(f)) {
		REQWORD();
		if (!strcasecmp(tokbuf, c_timeout))
			t = config_int(f, &tp->timeout);
		else if (!strcasecmp(tokbuf, c_buf_size))
			t = config_int(f, &tp->buf_size);
		else if (!strcasecmp(tokbuf, c_input_buf_size))
			t = config_int(f, &tp->input_buf_size);
		else if (!strcasecmp(tokbuf, c_num_connections))
			t = config_int(f, &tp->num_connections);
		else if (!strcasecmp(tokbuf, c_accept_multi))
			t = config_flag(f, &tp->accept_multi);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	return 0;
}

static const char *config_main(FILE *f)
{
	const char *t = 0;

	while (NOTEOF(f)) {
		REQWORD();
		if (!strcasecmp(tokbuf, c_root_directory))
			t = config_string(f, &rootdir);
		else if (!strcasecmp(tokbuf, c_core_directory))
			t = config_string(f, &coredir);
		else if (!strcasecmp(tokbuf, c_umask))
			t = config_int(f, &fcm);
		else if (!strcasecmp(tokbuf, c_stayroot))
			t = config_flag(f, &stayroot);
		else if (!strcasecmp(tokbuf, c_user))
			t = config_string(f, &user_name);
		else if (!strcasecmp(tokbuf, c_pid))
			t = config_string(f, &pid_filename);
		else if (!strcasecmp(tokbuf, c_log))
			t = config_string(f, &log_filename);
		else if (!strcasecmp(tokbuf, c_error))
			t = config_string(f, &error_filename);
		else if (!strcasecmp(tokbuf, c_tuning))
			t = config_tuning(f, &tuning);
		else if (!strcasecmp(tokbuf, c_control))
			t = config_control(f, &controls);
		else if (!strcasecmp(tokbuf, c_server))
			t = config_server(f, &servers);
		else if (!strcasecmp(tokbuf, c_log_format))
			t = config_log(f, &log_column, &log_columns);
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
	if (p) {
		t = malloc(s);
		if (t) {
			p->floor = t;
			p->ceiling = t + s;
		} else
			return 0;
	}
	return p;
}

const char *config(const char *config_filename)
{
	const char *s;
	unsigned long n;
	struct connection *cn;
	FILE *config_file;

	if (config_filename) {
		config_file = fopen(config_filename, "r");
		if (config_file == 0) {
			fprintf(stderr, "Cannot open configuration file %s\n", config_filename);
			return e_noinput;
		}
	} else
		config_file = stdin;
	tuning.buf_size = DEFAULT_BUF_SIZE;
	tuning.input_buf_size = INPUT_BUF_SIZE;
	tuning.num_connections = DEFAULT_NUM_CONNECTIONS;
	tuning.timeout = DEFAULT_TIMEOUT;
	tuning.accept_multi = 1;
	fcm = DEFAULT_UMASK;
	stayroot = 0;
	log_columns = 0;
	log_column = 0;
	line = 1;
	s = config_main(config_file);
	if (config_filename)
		fclose(config_file);
	if (s) {
		if (config_filename)
			fprintf(stderr, "In configuration file: %s\n", config_filename);
		fprintf(stderr, "Error at token '%s' around line %d\n", tokbuf, line);
		return s;
	}
	if (log_column == 0) {
		log_column = default_log_column;
		log_columns = sizeof default_log_column / sizeof default_log_column[0];
	}
	s = fill_servernames();
	if (s)
		return s;
#ifdef POLL
	pollfds = malloc((tuning.num_connections + num_servers) * sizeof *pollfds);
	if (pollfds == 0)
		return e_memory;
#endif
	for (n = 0; n < tuning.num_connections; n++) {
		ALLOC(cn);
		ALLOC(cn->r);
		if ((cn->input = new_pool(tuning.input_buf_size)) == 0)
			return e_memory;
		if ((cn->output = new_pool(tuning.buf_size)) == 0)
			return e_memory;
		cn->r->cn = cn;
		cn->next = connections;
		cn->state = HC_FREE;
		connections = cn;
	}
	return 0;
}
