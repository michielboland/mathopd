/*
 * config.c - startup and configuration of Mathopd
 *
 * Copyright 1996 Michiel Boland
 */

/* House of Games */

#include "mathopd.h"

int buf_size = DEFAULT_BUF_SIZE;
int input_buf_size = INPUT_BUF_SIZE;
int log_level = L_WARNING;
int num_connections = DEFAULT_NUM_CONNECTIONS;
int timeout = DEFAULT_TIMEOUT;
static char *null;
char **exports = &null;

char *pid_filename;
char *log_filename;
char *error_filename;
char *agent_filename;

char *admin;
char *coredir;
int keepalive;
struct connection *connections;
struct server *servers;

char *user_name;
uid_t user_id;
gid_t group_id;

#ifdef POLL
struct pollfd *pollfds;
#endif

static char *err;
static char *fqdn;
static char tokbuf[STRLEN];
static int lastline;
static int line = 1;
static int num_servers = 0;
static struct control *controls;

static char *c_access =			"Access";
static char *c_address =		"Address";
static char *c_admin =			"Admin";
static char *c_agent =			"AgentLog";
static char *c_alias =			"Alias";
static char *c_allow =			"Allow";
static char *c_buf_size =		"BufSize";
static char *c_control =		"Control";
static char *c_deny =			"Deny";
static char *c_directory =		"Directory";
static char *c_error =			"ErrorLog";
static char *c_exact =			"Exact";
static char *c_export =			"Export";
static char *c_group =			"Group";
static char *c_index_names =		"IndexNames";
static char *c_keep_alive =		"KeepAlive";
static char *c_log =			"Log";
static char *c_log_level =		"LogLevel";
static char *c_name =			"Name";
static char *c_num_connections =	"NumConnections";
static char *c_pid =			"PIDFile";
static char *c_port =			"Port";
static char *c_redirect =		"Redirect";
static char *c_server =			"Server";
static char *c_specials =		"Specials";
static char *c_symlinks_ok =		"SymlinksOK";
static char *c_timeout =		"Timeout";
static char *c_types =			"Types";
static char *c_user =			"User";

static char *e_addr_set =		"address already set";
static char *e_array =			"too many elements in array";
static char *e_bad_addr =		"bad address";
static char *e_bad_alias =		"alias without matching location";
static char *e_bad_group =		"bad group name";
static char *e_bad_user =		"bad user name";
static char *e_help =			"unknown error (help)";
static char *e_inval =			"illegal quantity";
static char *e_keyword =		"unknown keyword";
static char *e_memory =			"out of memory";
static char *e_unknown_host =		"no hostname specified";

static char *t_close =			"unexpected closing brace";
static char *t_eof =			"unexpected end of file";
static char *t_open =			"unexpected opening brace";
static char *t_string =			"unexpected string";
static char *t_too_long =		"token too long";
static char *t_word =			"unexpected word";

static char *gettoken(void)
{
	int c;
	char w;
	int i = 0;
	char state = 1;

	lastline = line;
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

static char *config_string(char **a)
{
	GETSTRING();
	COPY(*a, tokbuf);
	return 0;
}

static char *config_int(int *i)
{
	GETWORD();
	return ((*i = atoi(tokbuf)) < 1) ? e_inval : 0;
}

static char *config_user(uid_t *u)
{
	struct passwd *pwent;

	GETSTRING();
	COPY(user_name, tokbuf);
	*u = ((pwent = getpwnam(tokbuf)) == 0) ? 0 : pwent->pw_uid;
	return *u ? 0 : e_bad_user;
}

static char *config_group(gid_t *g)
{
	struct group *grent;

	GETSTRING();
	*g = ((grent = getgrnam(tokbuf)) == 0) ? 0 : grent->gr_gid;
	return *g ? 0 : e_bad_group;
}

static char *config_address(char **a, struct in_addr *b)
{
	if (*a)
		return e_addr_set;
	GETSTRING();
	COPY(*a, tokbuf);
	if ((b->s_addr = inet_addr(tokbuf)) == (unsigned long) -1)
		return e_bad_addr;
	return 0;
}

static char *config_name(char **a, struct in_addr *b)
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

static char *config_array(char ***a)
{
	static char *b[MAX_ARRAY+1];
	char **c;
	int i;
	int j;

	GETOPEN();
	i = 0;
	while (NOTCLOSE()) {
		REQSTRING();
		if (i >= MAX_ARRAY)
			return e_array;
		COPY(b[i++], tokbuf);
	}
	b[i++] = 0;
	if ((c = (char **) malloc(i * sizeof (char *))) == 0)
		return e_memory;
	for (j = 0; j < i; j++)
		c[j] = b[j];
	*a = c;
	return 0;
}

static char *config_mime(struct mime **ms, int type)
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

static char *config_access(struct access **ls)
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

static char *config_control(struct control **as)
{
	char *t = 0;
	struct control *a;
	struct control *b;

	b = *as;
	while (b && b->directory)
		b = b->next;
	MAKE(a, struct control);
	a->directory = 0;
	a->alias = 0;
	a->symlinksok = 0;
	a->redirectno = 0;
	if (b) {
		a->index_names = b->index_names;
		a->redirects = b->redirects;
		a->accesses = b->accesses;
		a->mimes = b->mimes;
		a->log_level = b->log_level;
	}
	else {
		a->index_names = &null;
		a->redirects = &null;
		a->accesses = 0;
		a->mimes = 0;
		a->log_level = 2;
	}
	a->next = *as;
	*as = a;
	GETOPEN();
	while (NOTCLOSE()) {
		REQWORD();
		if (strceq(tokbuf, c_directory)) {
			GETSTRING();
			chopslash(tokbuf);
			COPY(a->directory, tokbuf);
		}
		else if (strceq(tokbuf, c_alias)) {
			GETSTRING();
			chopslash(tokbuf);
			COPY(a->alias, tokbuf);
		}
		else if (strceq(tokbuf, c_symlinks_ok))
			a->symlinksok = 1;
		else if (strceq(tokbuf, c_index_names))
			t = config_array(&a->index_names);
		else if (strceq(tokbuf, c_redirect))
			t = config_array(&a->redirects);
		else if (strceq(tokbuf, c_access))
			t = config_access(&a->accesses);
		else if (strceq(tokbuf, c_types))
			t = config_mime(&a->mimes, M_TYPE);
		else if (strceq(tokbuf, c_specials))
			t = config_mime(&a->mimes, M_SPECIAL);
		else if (strceq(tokbuf, c_log_level))
			t = config_int(&a->log_level);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	if (a->alias && (a->directory == 0))
		return e_bad_alias;
	return 0;
}

static char *config_server(struct server **ss)
{
	char *t = 0;
	struct server *s;

	MAKE(s, struct server);
	num_servers++;
	s->port = 0;
	s->addr.s_addr = htonl(INADDR_ANY);
	s->name = 0;
	s->fullname = 0;
	s->controls = controls;
	s->next = *ss;
	s->naccepts = 0;
	s->nhandled = 0;
	s->nrequests = 0;
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
		else if (strceq(tokbuf, c_control))
			t = config_control(&s->controls);
		else
			t = e_keyword;
		if (t)
			return t;
	}
	if (s->port == 0)
		s->port = DEFAULT_PORT;
	if (s->name == 0) {
		if (fqdn == 0)
			return e_unknown_host;
		s->name = fqdn;
	}
	if (s->port == DEFAULT_PORT)
		s->fullname = s->name;
	else {
		char buf[80];

		sprintf(buf, "%s:%d", s->name, s->port);
		COPY(s->fullname, buf);
	}
	return 0;
}

static char *config_main(void)
{
	char *t = 0;

	while (NOTEOF()) {
		REQWORD();
		if (strceq(tokbuf, c_keep_alive))
			keepalive = 1;
		else if (strceq(tokbuf, c_log_level))
			t = config_int(&log_level);
		else if (strceq(tokbuf, c_directory))
			t = config_string(&coredir);
		else if (strceq(tokbuf, c_name))
			t = config_string(&fqdn);
		else if (strceq(tokbuf, c_admin))
			t = config_string(&admin);
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
		else if (strceq(tokbuf, c_agent))
			t = config_string(&agent_filename);
		else if (strceq(tokbuf, c_export))
			t = config_array(&exports);
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
	char *s;
	int n;
	struct connection *cn;

	s = config_main();

	if (s)
		die(0,
		    "An error occurred at token `%s' around line %d:\n"
		    "*** %s ***",
		    tokbuf,
		    lastline,
		    s);

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
