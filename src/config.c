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

/* House of Games */

#include "mathopd.h"

struct tuning tuning;

char *pid_filename;
char *log_filename;
char *error_filename;

char *rootdir;
char *coredir;
struct connection *connections;
struct server *servers;
struct simple_list *exports;

char *user_name;

static const char *err;
static char *fqdn;
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
static const char c_apply[] =		"Apply";
static const char c_buf_size[] =	"BufSize";
static const char c_child_log[] =	"ChildLog";
static const char c_clients[] =		"Clients";
static const char c_control[] =		"Control";
static const char c_core_directory[] =	"CoreDirectory";
static const char c_default_name[] =	"DefaultName";
static const char c_deny[] =		"Deny";
static const char c_do_crypt[] =	"EncryptedUserFile";
static const char c_error[] =		"ErrorLog";
static const char c_error_401_file[] =	"Error401File";
static const char c_error_403_file[] =	"Error403File";
static const char c_error_404_file[] =	"Error404File";
static const char c_export[] =		"Export";
static const char c_external[] =	"External";
static const char c_host[] =		"Host";
static const char c_index_names[] =	"IndexNames";
static const char c_location[] =	"Location";
static const char c_log[] =		"Log";
static const char c_name[] =		"Name";
static const char c_num_connections[] =	"NumConnections";
static const char c_off[] =		"Off";
static const char c_on[] =		"On";
static const char c_path_args[] =	"PathArgs";
static const char c_pid[] =		"PIDFile";
static const char c_port[] =		"Port";
static const char c_realm[] =		"Realm";
static const char c_refresh[] =		"Refresh";
static const char c_root_directory[] =	"RootDirectory";
static const char c_server[] =		"Server";
static const char c_specials[] =	"Specials";
static const char c_stayroot[] =	"StayRoot";
static const char c_symlinks[] =	"Symlinks";
static const char c_timeout[] =		"Timeout";
static const char c_tuning[] =		"Tuning";
static const char c_types[] =		"Types";
static const char c_virtual[] =		"Virtual";
static const char c_umask[] =		"Umask";
static const char c_user[] =		"User";
static const char c_userfile[] =	"UserFile";

static const char e_addr_set[] =	"address already set";
static const char e_bad_addr[] =	"bad address";
static const char e_bad_alias[] =	"alias without matching location";
static const char e_bad_mask[] =	"mask does not match address";
static const char e_bad_network[] =	"bad network";
static const char e_help[] =		"unknown error (help)";
static const char e_inval[] =		"illegal quantity";
static const char e_keyword[] =		"unknown keyword";
static const char e_memory[] =		"out of memory";
static const char e_nodefault[] =	"DefaultName not set";
static const char e_unknown_host[] =	"unknown host";

static const char t_close[] =		"unexpected closing brace";
static const char t_eof[] =		"unexpected end of file";
static const char t_open[] =		"unexpected opening brace";
static const char t_string[] =		"unexpected string";
static const char t_too_long[] =	"token too long";
static const char t_word[] =		"unexpected word";

#define ALLOC(x) if (((x) = malloc(sizeof *(x))) == 0) return e_memory
#define COPY(x, y) if (((x) = strdup(y)) == 0) return e_memory
#define GETWORD() if (gettoken() != t_word) return err
#define GETSTRING() if (gettoken() != t_string && err != t_word) return err
#define GETOPEN() if (gettoken() != t_open) return err
#define REQWORD() if (err != t_word) return err
#define REQSTRING() if (err != t_string && err != t_word) return err
#define NOTCLOSE() gettoken() != t_close
#define NOTEOF() gettoken() != t_eof

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

static const char *gettoken(void)
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
		if ((c = getc(stdin)) == EOF) {
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
				ungetc(c, stdin);
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

static const char *config_string(char **a)
{
	GETSTRING();
	COPY(*a, tokbuf);
	return 0;
}

static const char *config_int(int *i)
{
	char *e;

	GETWORD();
	*i = (int) strtol(tokbuf, &e, 0);
	return e && *e ? e_inval : 0;
}

static const char *config_flag(int *i)
{
	GETWORD();
	if (!strcasecmp(tokbuf, c_off))
		*i = 0;
	else if (!strcasecmp(tokbuf, c_on))
		*i = 1;
	else
		return e_keyword;
	return 0;
}

static const char *config_address(char **a, struct in_addr *b)
{
	struct in_addr ia;

	if (*a)
		return e_addr_set;
	GETSTRING();
	COPY(*a, tokbuf);
	if (inet_aton(tokbuf, &ia) == 0)
		return e_bad_addr;
	*b = ia;
	return 0;
}

static const char *config_name(char **a, struct in_addr *b)
{
	struct hostent *h;

	if (*a)
		return e_addr_set;
	GETSTRING();
	COPY(*a, tokbuf);
	h = gethostbyname(tokbuf);
	if (h == 0 || h->h_addrtype != AF_INET)
		return e_unknown_host;
	b->s_addr = *(unsigned long *) h->h_addr;
	return 0;
}

static const char *config_list(struct simple_list **ls)
{
	struct simple_list *l;

	GETOPEN();
	while (NOTCLOSE()) {
		REQSTRING();
		ALLOC(l);
		COPY(l->name, tokbuf);
		l->next = *ls;
		*ls = l;
	}
	return 0;
}

static const char *config_mime(struct mime **ms, int class)
{
	struct mime *m;
	char *name, *s, buf[32];

	GETOPEN();
	while (NOTCLOSE()) {
		REQSTRING();
		COPY(name, tokbuf);
		GETOPEN();
		while (NOTCLOSE()) {
			REQSTRING();
			ALLOC(m);
			m->class = class;
			m->name = name;
			if (!strcasecmp(tokbuf, c_all))
				m->ext = 0;
			else {
				s = tokbuf;
				if (*s == '/')
					sprintf(buf, "%.30s", s);
				else {
					while (*s == '.')
						++s;
					sprintf(buf, ".%.30s", s);
				}
				COPY(m->ext, buf);
			}
			m->next = *ms;
			*ms = m;
		}
	}
	return 0;
}

static const char *config_access(struct access **ls)
{
	struct access *l;
	struct in_addr ia;
	char *sl, *e;
	unsigned long sz;

	GETOPEN();
	while (NOTCLOSE()) {
		REQWORD();
		ALLOC(l);
		l->next = *ls;
		*ls = l;
		if (!strcasecmp(tokbuf, c_allow))
			l->type = ALLOW;
		else if (!strcasecmp(tokbuf, c_deny))
			l->type = DENY;
		else if (!strcasecmp(tokbuf, c_apply))
			l->type = APPLY;
		else
			return e_keyword;
		GETWORD();
		sl = strchr(tokbuf, '/');
		if (sl == 0)
			return e_bad_network;
		*sl++ = 0;
		sz = strtoul(sl, &e, 0);
		if ((e && *e) || sz > 32)
			return e_inval;
		if (sz == 0)
			l->mask = 0;
		else
			l->mask = htonl(0xffffffff ^ ((1 << (32 - sz)) - 1));
		if (inet_aton(tokbuf, &ia) == 0)
			return e_bad_addr;
		l->addr = ia.s_addr;
		if ((l->mask | l->addr) != l->mask)
			return e_bad_mask;
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

static const char *config_control(struct control **as)
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
	if (b) {
		a->index_names = b->index_names;
		a->accesses = b->accesses;
		a->mimes = b->mimes;
		a->symlinksok = b->symlinksok;
		a->path_args_ok = b->path_args_ok;
		a->admin = b->admin;
		a->refresh = b->refresh;
		a->realm = b->realm;
		a->userfile = b->userfile;
		a->error_401_file = b->error_401_file;
		a->error_403_file = b->error_403_file;
		a->error_404_file = b->error_404_file;
		a->do_crypt = b->do_crypt;
		a->child_filename = b->child_filename;
	} else {
		a->index_names = 0;
		a->accesses = 0;
		a->mimes = 0;
		a->symlinksok = 0;
		a->path_args_ok = 0;
		a->admin = 0;
		a->refresh = 0;
		a->realm = 0;
		a->userfile = 0;
		a->error_401_file = 0;
		a->error_403_file = 0;
		a->error_404_file = 0;
		a->do_crypt = 0;
		a->child_filename = 0;
	}
	a->next = *as;
	*as = a;
	GETOPEN();
	while (NOTCLOSE()) {
		REQWORD();
		if (!strcasecmp(tokbuf, c_location)) {
			ALLOC(l);
			GETSTRING();
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
			GETSTRING();
			chopslash(tokbuf);
			COPY(a->alias, tokbuf);
		} else if (!strcasecmp(tokbuf, c_symlinks))
			t = config_flag(&a->symlinksok);
		else if (!strcasecmp(tokbuf, c_path_args))
			t = config_flag(&a->path_args_ok);
		else if (!strcasecmp(tokbuf, c_index_names))
			t = config_list(&a->index_names);
		else if (!strcasecmp(tokbuf, c_access))
			t = config_access(&a->accesses);
		else if (!strcasecmp(tokbuf, c_clients))
			t = config_access(&a->clients);
		else if (!strcasecmp(tokbuf, c_types))
			t = config_mime(&a->mimes, CLASS_FILE);
		else if (!strcasecmp(tokbuf, c_specials))
			t = config_mime(&a->mimes, CLASS_SPECIAL);
		else if (!strcasecmp(tokbuf, c_external))
			t = config_mime(&a->mimes, CLASS_EXTERNAL);
		else if (!strcasecmp(tokbuf, c_admin))
			t = config_string(&a->admin);
		else if (!strcasecmp(tokbuf, c_refresh))
			t = config_int(&a->refresh);
		else if (!strcasecmp(tokbuf, c_realm))
			t = config_string(&a->realm);
		else if (!strcasecmp(tokbuf, c_userfile))
			t = config_string(&a->userfile);
		else if (!strcasecmp(tokbuf, c_error_401_file))
			t = config_string(&a->error_401_file);
		else if (!strcasecmp(tokbuf, c_error_403_file))
			t = config_string(&a->error_403_file);
		else if (!strcasecmp(tokbuf, c_error_404_file))
			t = config_string(&a->error_404_file);
		else if (!strcasecmp(tokbuf, c_do_crypt))
			t = config_flag(&a->do_crypt);
		else if (!strcasecmp(tokbuf, c_child_log))
			t = config_string(&a->child_filename);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	if (a->alias && (a->locations == 0))
		return e_bad_alias;
	return 0;
}

static const char *config_virtual(struct virtual **vs, struct server *parent)
{
	const char *t = 0;
	struct virtual *v;

	ALLOC(v);
	v->host = 0;
	v->parent = parent;
	v->controls = parent->controls;
	v->nrequests = 0;
	v->nread = 0;
	v->nwritten = 0;
	v->next = *vs;
	*vs = v;
	GETOPEN();
	while (NOTCLOSE()) {
		REQWORD();
		if (!strcasecmp(tokbuf, c_host))
			t = config_string(&v->host);
		else if (!strcasecmp(tokbuf, c_control))
			t = config_control(&v->controls);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	return 0;
}

static const char *config_server(struct server **ss)
{
	const char *t = 0;
	struct server *s;

	ALLOC(s);
	num_servers++;
	s->port = 0;
	s->addr.s_addr = 0;
	s->s_name = 0;
	s->children = 0;
	s->controls = controls;
	s->next = *ss;
	s->naccepts = 0;
	s->nhandled = 0;
	*ss = s;
	GETOPEN();
	while (NOTCLOSE()) {
		REQWORD();
		if (!strcasecmp(tokbuf, c_port))
			t = config_int(&s->port);
		else if (!strcasecmp(tokbuf, c_name))
			t = config_name(&s->s_name, &s->addr);
		else if (!strcasecmp(tokbuf, c_address))
			t = config_address(&s->s_name, &s->addr);
		else if (!strcasecmp(tokbuf, c_virtual))
			t = config_virtual(&s->children, s);
		else if (!strcasecmp(tokbuf, c_control))
			t = config_control(&s->controls);
		else
			t = e_keyword;
		if (t)
			return t;
	}
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
		if (s->port == 0)
			s->port = DEFAULT_PORT;
		if (s->s_name == 0) {
			if (fqdn == 0)
				return e_nodefault;
			s->s_name = fqdn;
		}
		v = s->children;
		while (v) {
			name = v->host ? v->host : s->s_name;
			if (s->port == DEFAULT_PORT)
				v->fullname = name;
			else {
				sprintf(buf, "%.80s:%d", name, s->port);
				COPY(v->fullname, buf);
			}
			v = v->next;
		}
		s = s->next;
	}
	return 0;
}

static const char *config_tuning(struct tuning *tp)
{
	const char *t = 0;

	GETOPEN();
	while (NOTCLOSE()) {
		REQWORD();
		if (!strcasecmp(tokbuf, c_timeout))
			t = config_int(&tp->timeout);
		else if (!strcasecmp(tokbuf, c_buf_size))
			t = config_int(&tp->buf_size);
		else if (!strcasecmp(tokbuf, c_num_connections))
			t = config_int(&tp->num_connections);
		else if (!strcasecmp(tokbuf, c_accept_multi))
			t = config_flag(&tp->accept_multi);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	return 0;
}

static const char *config_main(void)
{
	const char *t = 0;

	while (NOTEOF()) {
		REQWORD();
		if (!strcasecmp(tokbuf, c_root_directory))
			t = config_string(&rootdir);
		else if (!strcasecmp(tokbuf, c_core_directory))
			t = config_string(&coredir);
		else if (!strcasecmp(tokbuf, c_default_name))
			t = config_string(&fqdn);
		else if (!strcasecmp(tokbuf, c_umask))
			t = config_int(&fcm);
		else if (!strcasecmp(tokbuf, c_stayroot))
			t = config_flag(&stayroot);
		else if (!strcasecmp(tokbuf, c_user))
			t = config_string(&user_name);
		else if (!strcasecmp(tokbuf, c_pid))
			t = config_string(&pid_filename);
		else if (!strcasecmp(tokbuf, c_log))
			t = config_string(&log_filename);
		else if (!strcasecmp(tokbuf, c_error))
			t = config_string(&error_filename);
		else if (!strcasecmp(tokbuf, c_export))
			t = config_list(&exports);
		else if (!strcasecmp(tokbuf, c_tuning))
			t = config_tuning(&tuning);
		else if (!strcasecmp(tokbuf, c_control))
			t = config_control(&controls);
		else if (!strcasecmp(tokbuf, c_server))
			t = config_server(&servers);
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

static const char *config2(void)
{
	const char *s;
	int n;
	struct connection *cn;

	tuning.buf_size = DEFAULT_BUF_SIZE;
	tuning.input_buf_size = INPUT_BUF_SIZE;
	tuning.num_connections = DEFAULT_NUM_CONNECTIONS;
	tuning.timeout = DEFAULT_TIMEOUT;
	tuning.accept_multi = 1;
	fcm = DEFAULT_UMASK;
	stayroot = 0;
	line = 1;
	s = config_main();
	if (s) {
		fprintf(stderr, "Error at token '%s' around line %d\n", tokbuf, line);
		return s;
	}
	s = fill_servernames();
	if (s)
		return s;
	for (n = 0; n < tuning.num_connections; n++) {
		ALLOC(cn);
		ALLOC(cn->r);
		if ((cn->input = new_pool(tuning.input_buf_size)) == 0)
			return e_memory;
		if ((cn->output = new_pool(tuning.buf_size)) == 0)
			return e_memory;
		cn->ip[15] = 0;
		cn->r->cn = cn;
		cn->next = connections;
		cn->state = HC_FREE;
		connections = cn;
	}
	return 0;
}

void config(void)
{
	const char *s;

	s = config2();
	if (s)
		die(0, "%s", s);
}
