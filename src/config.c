/*
 * config.c - startup and configuration of Mathopd
 *
 * Copyright 1996, 1997, 1998, Michiel Boland
 */

/* House of Games */

#include "mathopd.h"

int buf_size = DEFAULT_BUF_SIZE;
int input_buf_size = INPUT_BUF_SIZE;
int num_connections = DEFAULT_NUM_CONNECTIONS;
int timeout = DEFAULT_TIMEOUT;
int cern = 0;

char *pid_filename;
char *log_filename;
char *error_filename;
char *child_filename;

char *rootdir;
char *coredir;
struct connection *connections;
struct server *servers;
struct simple_list *exports;

char *user_name;
uid_t user_id;
gid_t group_id;

#ifdef POLL
struct pollfd *pollfds;
#endif

static const char *err;
static char *fqdn;
static char tokbuf[STRLEN];
static int line = 1;
static int num_servers = 0;
static struct control *controls;

static STRING(c_access) =		"Access";
static STRING(c_address) =		"Address";
static STRING(c_admin) =		"Admin";
static STRING(c_alias) =		"Alias";
static STRING(c_allow) =		"Allow";
static STRING(c_apply) =		"Apply";
static STRING(c_buf_size) =		"BufSize";
static STRING(c_cern) =			"CERNStyle";
static STRING(c_child_log) =		"ChildLog";
static STRING(c_clients) =		"Clients";
static STRING(c_control) =		"Control";
static STRING(c_core_directory) =	"CoreDirectory";
static STRING(c_default_name) =		"DefaultName";
static STRING(c_deny) =			"Deny";
static STRING(c_error) =		"ErrorLog";
static STRING(c_exact) =		"Exact";
static STRING(c_export) =		"Export";
static STRING(c_group) =		"Group";
static STRING(c_host) =			"Host";
static STRING(c_index_names) =		"IndexNames";
static STRING(c_location) =		"Location";
static STRING(c_log) =			"Log";
static STRING(c_loglevel) =		"LogLevel";
static STRING(c_name) =			"Name";
static STRING(c_num_connections) =	"NumConnections";
static STRING(c_off) =			"Off";
static STRING(c_on) =			"On";
static STRING(c_path_args) =		"PathArgs";
static STRING(c_pid) =			"PIDFile";
static STRING(c_port) =			"Port";
static STRING(c_refresh) =		"Refresh";
static STRING(c_root_directory) =	"RootDirectory";
static STRING(c_server) =		"Server";
static STRING(c_specials) =		"Specials";
static STRING(c_symlinks) =		"Symlinks";
static STRING(c_timeout) =		"Timeout";
static STRING(c_types) =		"Types";
static STRING(c_virtual) =		"Virtual";
static STRING(c_umask) =		"Umask";
static STRING(c_user) =			"User";

static STRING(e_addr_set) =		"address already set";
static STRING(e_bad_addr) =		"bad address";
static STRING(e_bad_alias) =		"alias without matching location";
static STRING(e_bad_group) =		"bad group name";
static STRING(e_bad_user) =		"bad user name";
static STRING(e_help) =			"unknown error (help)";
static STRING(e_inval) =		"illegal quantity";
static STRING(e_keyword) =		"unknown keyword";
static STRING(e_memory) =		"out of memory";
static STRING(e_unknown_host) =		"no default hostname";

static STRING(t_close) =		"unexpected closing brace";
static STRING(t_eof) =			"unexpected end of file";
static STRING(t_open) =			"unexpected opening brace";
static STRING(t_string) =		"unexpected string";
static STRING(t_too_long) =		"token too long";
static STRING(t_word) =			"unexpected word";

static const char *gettoken(void)
{
	int c;
	char w;
	int i = 0;
	char state = 1;

	err = e_help;
	do {
		w = 0;
		if ((c = getc(stdin)) == EOF) {
			state = 0;
			err = t_eof;
		}
		else if (c == '\n')
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
	tokbuf[i] = '\0';
	return err;
}

#ifdef NEED_STRDUP
char *strdup(const char *s)
{
	char *t;

	return (t = (char *) malloc(strlen(s) + 1)) ? strcpy(t,s) : 0;
}
#endif

#define NEW(x) (x *) malloc(sizeof (x))
#define MAKE(x, y) if (((x) = (y *) malloc(sizeof (y))) == 0) return e_memory
#define COPY(x, y) if (((x) = strdup(y)) == 0) return e_memory
#define GETWORD() if (gettoken() != t_word) return err
#define GETSTRING() if (gettoken() != t_string && err != t_word) return err
#define GETOPEN() if (gettoken() != t_open) return err
#define REQWORD() if (err != t_word) return err
#define REQSTRING() if (err != t_string && err != t_word) return err
#define NOTCLOSE() gettoken() != t_close
#define NOTEOF() gettoken() != t_eof

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
	if (strceq(tokbuf, c_off))
		*i = 0;
	else if (strceq(tokbuf, c_on))
		*i = 1;
	else
		return e_keyword;
	return 0;
}
	
static const char *config_user(uid_t *u)
{
	struct passwd *pwent;

	GETSTRING();
	COPY(user_name, tokbuf);
	*u = ((pwent = getpwnam(tokbuf)) == 0) ? 0 : pwent->pw_uid;
	return *u ? 0 : e_bad_user;
}

static const char *config_group(gid_t *g)
{
	struct group *grent;

	GETSTRING();
	*g = ((grent = getgrnam(tokbuf)) == 0) ? 0 : grent->gr_gid;
	return *g ? 0 : e_bad_group;
}

static const char *config_address(char **a, struct in_addr *b)
{
	if (*a)
		return e_addr_set;
	GETSTRING();
	COPY(*a, tokbuf);
	if ((b->s_addr = inet_addr(tokbuf)) == (unsigned long) -1)
		return e_bad_addr;
	return 0;
}

static const char *config_name(char **a, struct in_addr *b)
{
	struct hostent *h;

	if (*a)
		return e_addr_set;
	GETSTRING();
	COPY(*a, tokbuf);
	if ((h = gethostbyname(tokbuf)) == 0)
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
		MAKE(l, struct simple_list);
		COPY(l->name, tokbuf);
		l->next = *ls;
		*ls = l;
	}
	return 0;
}

static const char *config_mime(struct mime **ms, int type)
{
	struct mime *m;
	char *name;

	GETOPEN();
	while (NOTCLOSE()) {
		REQSTRING();
		COPY(name, tokbuf);
		GETOPEN();
		while (NOTCLOSE()) {
			REQSTRING();
			MAKE(m, struct mime);
			m->type = type;
			m->name = name;
			if (*tokbuf == '*')
				m->ext = 0;
			else {
				char buf[32];
				char *s = tokbuf;

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

	GETOPEN();
	while (NOTCLOSE()) {
		REQWORD();
		MAKE(l, struct access);
		l->next = *ls;
		*ls = l;
		if (strceq(tokbuf, c_allow))
			l->type = ALLOW;
		else if (strceq(tokbuf, c_deny))
			l->type = DENY;
		else if (strceq(tokbuf, c_apply))
			l->type = APPLY;
		else
			return e_keyword;
		GETWORD();
		if (*tokbuf == '*')
			l->mask = l->addr = 0;
		else {
			if (strceq(tokbuf, c_exact))
				l->mask = (unsigned long) -1;
			else if ((l->mask = inet_addr(tokbuf))
				 == (unsigned long) -1)
				return e_bad_addr;
			GETWORD();
			if ((l->addr = inet_addr(tokbuf))
			    == (unsigned long) -1)
				return e_bad_addr;
		}
	}
	return 0;
}
	
static void chopslash(char *s)
{
	char *t;

	t = s + strlen(s);
	while (--t >= s && *t == '/')
		*t = '\0';
}

static const char *config_control(struct control **as)
{
	const char *t = 0;
	struct control *a, *b;
	struct simple_list *l;

	b = *as;
	while (b && b->locations)
		b = b->next;
	MAKE(a, struct control);
	a->locations = 0;
	a->alias = 0;
	a->clients = 0;
	if (b) {
		a->index_names = b->index_names;
		a->accesses = b->accesses;
		a->mimes = b->mimes;
		a->symlinksok = b->symlinksok;
		a->path_args_ok = b->path_args_ok;
		a->loglevel = b->loglevel;
		a->admin = b->admin;
		a->refresh = b->refresh;
	}
	else {
		a->index_names = 0;
		a->accesses = 0;
		a->mimes = 0;
		a->symlinksok = 0;
		a->path_args_ok = 0;
		a->loglevel = 0;
		a->admin = 0;
		a->refresh = 0;
	}
	a->next = *as;
	*as = a;
	GETOPEN();
	while (NOTCLOSE()) {
		REQWORD();
		if (strceq(tokbuf, c_location)) {
			MAKE(l, struct simple_list);
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
		}
		else if (strceq(tokbuf, c_alias)) {
			GETSTRING();
			chopslash(tokbuf);
			COPY(a->alias, tokbuf);
		}
		else if (strceq(tokbuf, c_symlinks))
			t = config_flag(&a->symlinksok);
		else if (strceq(tokbuf, c_path_args))
			t = config_flag(&a->path_args_ok);
		else if (strceq(tokbuf, c_loglevel))
			t = config_int(&a->loglevel);
		else if (strceq(tokbuf, c_index_names))
			t = config_list(&a->index_names);
		else if (strceq(tokbuf, c_access))
			t = config_access(&a->accesses);
		else if (strceq(tokbuf, c_clients))
			t = config_access(&a->clients);
		else if (strceq(tokbuf, c_types))
			t = config_mime(&a->mimes, M_TYPE);
		else if (strceq(tokbuf, c_specials))
			t = config_mime(&a->mimes, M_SPECIAL);
		else if (strceq(tokbuf, c_admin))
			t = config_string(&a->admin);
		else if (strceq(tokbuf, c_refresh))
			t = config_int(&a->refresh);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	if (a->alias && (a->locations == 0))
		return e_bad_alias;
	return 0;
}

static const char *config_virtual(struct virtual **vs, struct server *parent,
				  int trivial)
{
	const char *t = 0;
	struct virtual *v;

	MAKE(v, struct virtual);
	v->host = 0;
	v->parent = parent;
	v->controls = parent->controls;
	v->nrequests = 0;
	v->nread = 0;
	v->nwritten = 0;
	v->next = *vs;
	*vs = v;
	if (trivial)
		return 0;
	GETOPEN();
	while (NOTCLOSE()) {
		REQWORD();
		if (strceq(tokbuf, c_host))
			t = config_string(&v->host);
		else if (strceq(tokbuf, c_control))
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
	struct virtual *v;

	MAKE(s, struct server);
	num_servers++;
	s->port = 0;
	s->addr.s_addr = htonl(INADDR_ANY);
	s->name = 0;
	s->children = 0;
	s->controls = controls;
	s->next = *ss;
	s->naccepts = 0;
	s->nhandled = 0;
	*ss = s;
	GETOPEN();
	while (NOTCLOSE()) {
		REQWORD();
		if (strceq(tokbuf, c_port))
			t = config_int(&s->port);
		else if (strceq(tokbuf, c_name))
			t = config_name(&s->name, &s->addr);
		else if (strceq(tokbuf, c_address))
			t = config_address(&s->name, &s->addr);
		else if (strceq(tokbuf, c_virtual))
			t = config_virtual(&s->children, s, 0);
		else if (strceq(tokbuf, c_control))
			t = config_control(&s->controls);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	v = s->children;
	while (v) {
		if (v->host == 0)
			break;
		v = v->next;
	}
	return (v == 0) ? config_virtual(&s->children, s, 1) : 0;
}

static const char *fill_servernames(void)
{
	struct server *s = servers;
	struct virtual *v;
	char buf[256];
	char *name;

	while (s) {
		if (s->port == 0)
			s->port = DEFAULT_PORT;
		if (s->name == 0) {
			if (fqdn == 0)
				return e_unknown_host;
			s->name = fqdn;
		}
		v = s->children;
		while (v) {
			name = v->host ? v->host : s->name;
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

static const char *config_main(void)
{
	const char *t = 0;

	while (NOTEOF()) {
		REQWORD();
		if (strceq(tokbuf, c_root_directory))
			t = config_string(&rootdir);
		else if (strceq(tokbuf, c_core_directory))
			t = config_string(&coredir);
		else if (strceq(tokbuf, c_default_name))
			t = config_string(&fqdn);
		else if (strceq(tokbuf, c_cern))
			t = config_flag(&cern);
		else if (strceq(tokbuf, c_umask))
			t = config_int(&fcm);
		else if (strceq(tokbuf, c_timeout))
			t = config_int(&timeout);
		else if (strceq(tokbuf, c_buf_size))
			t = config_int(&buf_size);
		else if (strceq(tokbuf, c_num_connections))
			t = config_int(&num_connections);
		else if (strceq(tokbuf, c_user))
			t = config_user(&user_id);
		else if (strceq(tokbuf, c_group))
			t = config_group(&group_id);
		else if (strceq(tokbuf, c_pid))
			t = config_string(&pid_filename);
		else if (strceq(tokbuf, c_log))
			t = config_string(&log_filename);
		else if (strceq(tokbuf, c_error))
			t = config_string(&error_filename);
		else if (strceq(tokbuf, c_child_log))
			t = config_string(&child_filename);
		else if (strceq(tokbuf, c_export))
			t = config_list(&exports);
		else if (strceq(tokbuf, c_control))
			t = config_control(&controls);
		else if (strceq(tokbuf, c_server))
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

	p = NEW(struct pool);
	if (p) {
		t = (char *) malloc(s);
		if (t) {
			p->floor = t;
			p->ceiling = t + s;
		}
		else
			return 0;
	}
	return p;
}

void config(void)
{
	const char *s;
	int n;
	struct connection *cn;

	s = config_main();

	if (s)
		die(0,
		    "An error occurred at token `%s' around line %d:\n"
		    "*** %s ***",
		    tokbuf,
		    line,
		    s);

	s = fill_servernames();
	if (s)
		die(0, "%s", s);

#ifdef POLL
	pollfds = (struct pollfd *) malloc((num_connections + num_servers)
					   * sizeof (struct pollfd));
	if (pollfds == 0)
		die(0, e_memory);
#endif

	for (n = 0; n < num_connections; n++) {
		if ((cn = NEW(struct connection)) == 0
			|| (cn->r = NEW(struct request)) == 0
			|| (cn->input = new_pool(INPUT_BUF_SIZE)) == 0
			|| (cn->output = new_pool(buf_size)) == 0)
			die(0, e_memory);
		else {
			cn->ip[15] = '\0';
			cn->r->cn = cn;
			cn->next = connections;
			cn->state = HC_FREE;
			connections = cn;
		}
	}
}
