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

/* In der Halle des Bergk"onigs */

#ifndef _mathopd_h
#define _mathopd_h

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>

#define CGI_MAGIC_TYPE "CGI"
#define IMAP_MAGIC_TYPE "Imagemap"
#define REDIRECT_MAGIC_TYPE "Redirect"
#define DUMMY_MAGIC_TYPE "Dummy"
#define DUMP_MAGIC_TYPE "Dump"

#define DEFAULT_BUF_SIZE 12288
#define INPUT_BUF_SIZE 2048
#define DEFAULT_NUM_CONNECTIONS 64
#define DEFAULT_TIMEOUT 60
#define DEFAULT_FILEMODE 0666
#define DEFAULT_UMASK 022

#define STRLEN 400
#define PATHLEN (2 * STRLEN)

enum {
	ALLOW,
	DENY,
	APPLY,
	NOAPPLY
};

enum {
	CLASS_FILE = 1,
	CLASS_SPECIAL,
	CLASS_EXTERNAL
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
	ML_CTIME,
	ML_USERNAME,
	ML_ADDRESS,
	ML_PORT,
	ML_SERVERNAME,
	ML_METHOD,
	ML_URI,
	ML_VERSION,
	ML_STATUS,
	ML_CONTENT_LENGTH,
	ML_REFERER,
	ML_USER_AGENT,
	ML_BYTES_READ,
	ML_BYTES_WRITTEN
};

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
	int class;
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
	int path_args_ok;
	int exact_match;
	struct simple_list *index_names;
	struct access *accesses;
	struct mime *mimes;
	struct control *next;
	struct simple_list *locations;
	struct access *clients;
	char *admin;
	char *realm;
	char *userfile;
	char *error_401_file;
	char *error_403_file;
	char *error_404_file;
	int do_crypt;
	char *child_filename;
	int dns;
	struct simple_list *exports;
	char *script_user;
	int run_scripts_as_owner;
	int allow_dotfiles;
};

struct virtual {
	char *host;
	char *fullname;
	struct server *parent;
	struct control *controls;
	struct virtual *next;
	struct vserver *vserver;
	int anyhost;
};

struct vserver {
	struct server *server;
	struct control *controls;
	struct vserver *next;
};

struct server {
	int fd;
	unsigned long port;
	struct in_addr addr;
	char *s_name;
	struct virtual *children;
	struct control *controls;
	struct server *next;
	int pollno;
	unsigned long naccepts;
	unsigned long nhandled;
	struct vserver *vservers;
};

struct request {
	struct connection *cn;
	int processed;
	struct virtual *vs;
	char *user_agent;
	char *referer;
	char *from;
	char *authorization;
	char *cookie;
	char *host;
	char *in_content_type;
	char *in_content_length;
	char *connection;
	char *ims_s;
	char path[PATHLEN];
	char path_translated[PATHLEN];
	char path_args[PATHLEN];
	const char *content_type;
	int num_content;
	int class;
	long content_length;
	time_t last_modified;
	time_t ims;
	char *location;
	const char *status_line;
	char *method_s;
	char *url;
	char *args;
	char *version;
	int protocol_major;
	int protocol_minor;
	int method;
	int status;
	struct control *c;
	struct stat finfo;
	int isindex;
	const char *error_file;
	char user[16];
	char *servername;
	char newloc[PATHLEN];
	const char *allowedmethods;
	size_t location_length;
};

struct connection {
	struct request *r;
	int state;
	struct server *s;
	int fd;
	int rfd;
	struct sockaddr_in peer;
	struct sockaddr_in sock;
	time_t t;
	struct pool *input;
	struct pool *output;
	int assbackwards;
	int keepalive;
	int action;
	struct connection *next;
	int pollno;
	unsigned long nread;
	unsigned long nwritten;
	long left;
	int logged;
};

struct tuning {
	unsigned long buf_size;
	unsigned long input_buf_size;
	unsigned long num_connections;
	unsigned long timeout;
	int accept_multi;
};

/* main */

extern const char server_version[];
extern volatile int gotsigterm;
extern volatile int gotsighup;
extern volatile int gotsigusr1;
extern volatile int gotsigusr2;
extern volatile int gotsigchld;
extern volatile int gotsigquit;
extern int numchildren;
extern time_t startuptime;
extern int debug;
extern unsigned long fcm;
extern int stayroot;
extern int my_pid;
extern int fork_request(struct request *, int (*)(struct request *));
extern int am_daemon;
extern int forked;

/* config */

extern struct tuning tuning;
extern char *pid_filename;
extern char *log_filename;
extern char *error_filename;
extern char *agent_filename;

extern char *admin;
extern char *rootdir;
extern char *coredir;
extern struct connection *connections;
extern struct server *servers;
extern char *user_name;
extern const char *config(const char *);

extern int log_columns;
extern int *log_column;

extern struct pollfd *pollfds;

/* core */

extern int nconnections;
extern int maxconnections;
extern int log_file;
extern int error_file;
extern time_t current_time;

extern void log_d(const char *, ...);
extern void lerror(const char *);
extern void httpd_main(void);

/* request */

extern int process_request(struct request *);
extern struct control *faketoreal(char *, char *, struct request *, int);
extern int prepare_reply(struct request *);

/* imap */

extern int process_imap(struct request *);

/* cgi */

extern int process_cgi(struct request *);

/* dump */

extern int process_dump(struct request *);

/* base64 */

extern int webuserok(const char *, const char *, char *, int, int);

/* redirect */

extern int process_redirect(struct request *);

/* util */

extern int unescape_url(const char *, char *);
extern int unescape_url_n(const char *, char *, size_t);

/* log */

extern void log_request(struct request *);
extern int init_logs(void);

#endif
