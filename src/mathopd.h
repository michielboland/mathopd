/*
 * mathopd.h - header file for Mathopd
 *
 * Copyright 1996, 1997, Michiel Boland
 */

/* In der Halle des Bergk"onigs */

#ifndef _mathopd_h
#define _mathopd_h

/*
 * If you don't want/need any of these thingies, simply undefine
 * them.
 */

#define CGI_MAGIC_TYPE "CGI"
#define IMAP_MAGIC_TYPE "Imagemap"

#if defined SOLARIS

#define POLL

#elif defined SUNOS

#define NEED_PROTOTYPES
#define NEED_STRERROR
#define NEED_MEMORY_H
#define BROKEN_SPRINTF

#elif defined ULTRIX

#define NEED_PROTOTYPES
#define NEED_STRDUP
#define NEED_MEMORY_H
#define NO_GETRLIMIT

#endif

#if defined SUNOS || defined ULTRIX

#define M_NONBLOCK FNDELAY
#define M_AGAIN EWOULDBLOCK

#else /* a sane system */

#define M_NONBLOCK O_NONBLOCK
#define M_AGAIN EAGAIN

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>

#ifdef NEED_MEMORY_H
#include <memory.h>
#endif

#ifdef POLL
#include <stropts.h>
#include <poll.h>
#endif

#ifndef NO_GETRLIMIT
#include <sys/resource.h>
#endif

#ifndef SA_RESTART
#define SA_RESTART 0
#endif

#ifndef SA_INTERRUPT
#define SA_INTERRUPT 0
#endif

#define DEFAULT_BUF_SIZE 4096
#define INPUT_BUF_SIZE 2048
#define DEFAULT_NUM_CONNECTIONS 24
#define DEFAULT_TIMEOUT 60
#define DEFAULT_PORT 80
#define DEFAULT_FILEMODE 0666

#define STRLEN 400
#define PATHLEN (2 * STRLEN)

enum {
	ALLOW,
	DENY,
	APPLY
};

enum {
	M_TYPE,
	M_SPECIAL
};

enum {
	M_UNKNOWN,
	M_HEAD,
	M_GET,
	M_POST
};

enum {
	HC_FREE,
	HC_ACTIVE
};

enum {
	HC_READING,
	HC_WRITING,
	HC_WAITING,
	HC_CLOSING
};

enum {
	L_LOG,
	L_PANIC,
	L_ERROR,
	L_WARNING,
	L_TRANS,
	L_AGENT,
	L_DEBUG
};

#define streq(x,y) (strcmp(x,y) == 0)
#define strceq(x,y) (strcasecmp(x,y) == 0)
#define strneq(x,y,n) (strncmp(x,y,n) == 0)

#define STRING(x) const char x[]

struct pool {
	char *floor;
	char *ceiling;
	char *start;
	char *end;
	char state;
};

struct access {
	int type;
	unsigned long mask;
	unsigned long addr;
	struct access *next;
};

struct mime {
	int type;
	char *ext;
	char *name;
	struct mime *next;
};

struct simple_list {
	char *name;
	struct simple_list *next;
};

struct control {
	char *alias;
	int symlinksok;
	int loglevel;
	int path_args_ok;
	struct simple_list *index_names;
	struct access *accesses;
	struct mime *mimes;
	struct control *next;
	struct simple_list *locations;
	struct access *clients;
	char *admin;
	int refresh;
};

struct virtual {
	char *host;
	char *fullname;
	struct server *parent;
	struct control *controls;
	unsigned long nrequests;
	unsigned long nread;
	unsigned long nwritten;
	struct virtual *next;
};

struct server {
	int fd;
	int port;
	struct in_addr addr;
	char *name;
	struct virtual *children;
	struct control *controls;
	struct server *next;
#ifdef POLL
	int pollno;
#endif
	unsigned long naccepts;
	unsigned long nhandled;
};

struct request {
	struct connection *cn;
	struct virtual *vs;
	char *user_agent;
	char *referer;
	char *from;
	char *authorization;
	char *cookie;
	char *host;
	char *in_content_type;
	char *in_content_length;
	char path[PATHLEN];
	char path_translated[PATHLEN];
	char path_args[PATHLEN];
	const char *content_type;
	int num_content;
	int special;
	long content_length;
	time_t last_modified;
	time_t ims;
	char *location;
	const char *status_line;
	const char *error;
	const char *method_s;
	char *url;
	char *args;
	char *params;
	int protocol_major;
	int protocol_minor;
	int method;
	int status;
	struct control *c;
	struct stat finfo;
	int isindex;
};

struct connection {
	struct request *r;
	int state;
	struct server *s;
	int fd;
	int rfd;
	struct in_addr peer;
	char ip[16];
	time_t t;
	time_t it;
	struct pool *input;
	struct pool *output;
	int assbackwards;
	int keepalive;
	int action;
	struct connection *next;
#ifdef POLL
	int pollno;
#endif
};

/* main */

extern STRING(server_version);
extern volatile int gotsigterm;
extern volatile int gotsighup;
extern volatile int gotsigusr1;
extern volatile int gotsigusr2;
extern volatile int gotsigwinch;
extern volatile int numchildren;
extern time_t startuptime;
extern int debug;
extern int fcm;
extern int my_pid;
extern void die(const char *, const char *, ...);
extern int fork_request(struct request *, int (*)(struct request *));

/* config */

extern int buf_size;
extern int input_buf_size;
extern int num_connections;
extern int timeout;
extern struct simple_list *exports;
extern char *pid_filename;
extern char *log_filename;
extern char *error_filename;
extern char *agent_filename;
extern char *child_filename;

extern char *admin;
extern char *rootdir;
extern char *coredir;
extern struct connection *connections;
extern struct server *servers;
extern char *user_name;
extern uid_t user_id;
extern gid_t group_id;
#ifdef POLL
extern struct pollfd *pollfds;
#endif
extern void config(void);

/* core */

extern int nconnections;
extern int maxconnections;
extern time_t current_time;

extern void log(int, const char *, ...);
extern void lerror(const char *);
extern void httpd_main(void);

/* request */

extern int prepare_reply(struct request *);
extern int process_request(struct request *);
extern struct control *faketoreal(char *, char *, struct request *, int);
extern void construct_url(char *, char *, struct virtual *);
extern void escape_url(char *);
extern int unescape_url(char *, char *);

/* imap */

extern int process_imap(struct request *);

/* cgi */

extern int process_cgi(struct request *);

/* dump */

extern void dump(void);

#ifdef NEED_STRERROR
extern const char *strerror(int);
#endif

#ifdef NEED_STRDUP
extern char *strdup(const char *);
#endif

#ifdef NEED_PROTOTYPES
int accept(int, struct sockaddr *, int *);
int bind(int, struct sockaddr *, int);
void bzero(char *, int);
int ftruncate(int, off_t);
int getopt(int, char * const *, const char *);
int initgroups(const char *, gid_t);
int listen(int, int);
int lstat(const char *, struct stat *);
int recv(int, char *, int, int);
int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int send(int, const char *, int, int);
int setsockopt(int, int, int, const char *, int);
int socket(int, int , int);
int strcasecmp(const char *, const char *);

#ifndef NO_GETRLIMIT
int getrlimit(int, struct rlimit *);
int setrlimit(int, const struct rlimit *);
#endif

#ifdef SUNOS
int _filbuf(FILE *);
int fclose(FILE *);
int fprintf(FILE *, const char *, ...);
int fwrite(const char *, size_t, size_t, FILE *);
void perror(const char *);
int strftime(char *, size_t, const char *, const struct tm *);
time_t time(time_t *);
int toupper(int);
int ungetc(int, FILE *);
char *vfprintf(FILE *, const char *, va_list);
char *vsprintf(char *, const char *, va_list);
#endif

#endif

#endif
