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

/* In der Halle des Bergk"onigs */

#ifndef _mathopd_h
#define _mathopd_h

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>

#define CGI_MAGIC_TYPE "CGI"
#define IMAP_MAGIC_TYPE "Imagemap"
#define REDIRECT_MAGIC_TYPE "Redirect"
#define DUMP_MAGIC_TYPE "Dump"

#define DEFAULT_BUF_SIZE 12288
#define DEFAULT_INPUT_BUF_SIZE 2048
#define DEFAULT_NUM_CONNECTIONS 64
#define DEFAULT_TIMEOUT 60
#define DEFAULT_UMASK 022
#define DEFAULT_NUM_HEADERS 100
#define DEFAULT_SCRIPT_TIMEOUT 60
#define DEFAULT_SCRIPT_BUF_SIZE 4096
#define DEFAULT_BACKLOG 128

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

enum connection_state {
	HC_UNATTACHED,
	HC_FREE,
	HC_FORKED,
	HC_READING,
	HC_WRITING,
	HC_WAITING
};

enum {
	ML_CTIME,
	ML_USERNAME,
	ML_REMOTE_ADDRESS,
	ML_REMOTE_PORT,
	ML_LOCAL_ADDRESS,
	ML_LOCAL_PORT,
	ML_SERVERNAME,
	ML_METHOD,
	ML_URI,
	ML_VERSION,
	ML_STATUS,
	ML_CONTENT_LENGTH,
	ML_REFERER,
	ML_USER_AGENT,
	ML_BYTES_READ,
	ML_BYTES_WRITTEN,
	ML_QUERY_STRING,
	ML_TIME_TAKEN,
	ML_MICRO_TIME
};

enum {
	SI_DONOTCHANGE,
	SI_CHANGETOFIXED,
	SI_CHANGETOOWNER
};

struct pool {
	char *floor;
	char *ceiling;
	char *start;
	char *middle;
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
	struct simple_list *exports;
	int script_identity;
	uid_t script_uid;
	gid_t script_gid;
	int allow_dotfiles;
	int user_directory;
	struct simple_list *putenvs;
	struct simple_list *extra_headers;
	int path_info_ok;
	char *auto_index_command;
};

struct virtual {
	char *host;
	struct virtual *next;
	struct vserver *vserver;
	int anyhost;
};

struct vserver {
	struct control *controls;
	struct vserver *next;
};

struct server {
	int fd;
	unsigned long port;
	struct in_addr addr;
	struct virtual *children;
	struct control *controls;
	struct server *next;
	int pollno;
	struct vserver *vservers;
	unsigned long backlog;
};

struct request_header {
	const char *rh_name;
	const char *rh_value;
};

struct request {
	struct connection *cn;
	struct virtual *vs;
	char *user_agent;
	char *referer;
	char *authorization;
	char *host;
	char *in_content_type;
	char *in_content_length;
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
	char newloc[PATHLEN];
	size_t location_length;
	size_t nheaders;
	struct request_header *headers;
	char *range_s;
	char *if_range_s;
	time_t if_range;
	int range;
	unsigned long range_floor;
	unsigned long range_ceiling;
	unsigned long range_suffix;
	unsigned long range_total;
	char *ius_s;
	time_t ius;
	char rhost[STRLEN];
	char *in_transfer_encoding;
	size_t in_mblen;
	char curdir[PATHLEN];
};

struct cgi_header {
	const char *name;
	size_t namelen;
	const char *value;
	size_t len;
};

struct pipe_params {
	int state;
	size_t imax;
	int chunkit;
	int nocontent;
	int haslen;
	size_t pmax;
};

struct connection {
	struct connection *next;
	struct connection *prev;
	enum connection_state connection_state;
	struct request *r;
	struct server *s;
	int fd;
	int rfd;
	struct sockaddr_in peer;
	struct sockaddr_in sock;
	time_t t;
	struct pool header_input;
	struct pool output;
	struct pool client_input;
	struct pool script_input;
	int keepalive;
	int pollno;
	int rpollno;
	unsigned long nread;
	unsigned long nwritten;
	long left;
	int logged;
	struct timeval itv;
	struct pipe_params pipe_params;
};

struct connection_list {
	struct connection *head;
	struct connection *tail;
};

struct tuning {
	unsigned long buf_size;
	unsigned long input_buf_size;
	unsigned long num_connections;
	unsigned long timeout;
	int accept_multi;
	unsigned long num_headers;
	unsigned long script_timeout;
	unsigned long script_buf_size;
};

struct statistics {
	int nconnections;
	int maxconnections;
	unsigned long nrequests;
	unsigned long forked_children;
	unsigned long exited_children;
};

/* main */

extern const char server_version[];
extern volatile sig_atomic_t gotsigterm;
extern volatile sig_atomic_t gotsighup;
extern volatile sig_atomic_t gotsigusr1;
extern volatile sig_atomic_t gotsigusr2;
extern volatile sig_atomic_t gotsigchld;
extern volatile sig_atomic_t gotsigquit;
extern volatile sig_atomic_t gotsigwinch;
extern int debug;
extern unsigned long fcm;
extern int stayroot;
extern int amroot;
extern volatile int my_pid;
extern pid_t spawn(const char *, char *const[], char *const[], int, int, uid_t, gid_t, const char *);

/* config */

extern struct tuning tuning;
extern char *pid_filename;
extern char *log_filename;
extern char *error_filename;
extern char *rootdir;
extern char *coredir;
extern struct server *servers;
extern uid_t server_uid;
extern gid_t server_gid;
extern int log_columns;
extern int *log_column;
extern int log_gmt;
extern const char *config(const char *);
extern int init_buffers(void);

/* core */

extern struct statistics stats;
extern time_t startuptime;
extern time_t current_time;
extern struct pollfd *pollfds;
extern struct connection *connection_array;
extern void set_connection_state(struct connection *, enum connection_state);
extern int reinit_connection(struct connection *);
extern void close_connection(struct connection *);
extern void httpd_main(void);
extern int init_pollfds(size_t);
extern int init_connections(size_t);

/* request */

extern char *rfctime(time_t, char *);
extern struct control *faketoreal(char *, char *, struct request *, int, int);
extern int pool_print(struct pool *, const char *, ...);
extern void init_request(struct request *);
extern int process_request(struct request *);
extern int cgi_error(struct request *);

/* imap */

extern int process_imap(struct request *);

/* cgi */

extern int process_cgi(struct request *);

/* dump */

extern int process_dump(struct request *);
extern void internal_dump(void);

/* base64 */

extern int webuserok(const char *, const char *, char *, int, int);

/* redirect */

extern int process_redirect(struct request *);

/* util */

extern int unescape_url(const char *, char *);
extern int unescape_url_n(const char *, char *, size_t);
extern void sanitize_host(char *);

/* log */

extern int init_log_buffer(size_t);
extern void log_request(struct request *);
extern int open_log(const char *);
extern int init_logs(int);
extern void log_d(const char *, ...);
extern void lerror(const char *);

/* stub */

extern int init_cgi_headers(void);
extern void pipe_run(struct connection *);
extern void init_child(struct connection *, int);
extern int setup_child_pollfds(int, struct connection *);

#endif
