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

/* Once Around */

static const char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sysexits.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "mathopd.h"

const char server_version[] = "Mathopd/1.5b9";

volatile sig_atomic_t gotsigterm;
volatile sig_atomic_t gotsighup;
volatile sig_atomic_t gotsigusr1;
volatile sig_atomic_t gotsigusr2;
volatile sig_atomic_t gotsigchld;
volatile sig_atomic_t gotsigquit;
int numchildren;
int debug;
unsigned long fcm; /* should be mode_t */
int stayroot;
int my_pid;

static int am_daemon;
static char *progname;

static const char devnull[] = "/dev/null";

static int mysignal(int sig, void(*f)(int))
{
	struct sigaction act;

	act.sa_handler = f;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	return sigaction(sig, &act, 0);
}

static void die(const char *t, const char *fmt, ...)
{
	va_list ap;

	if (fmt) {
		fprintf(stderr, "%s: ", progname);
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
		va_end(ap);
	}
	if (t)
		perror(t);
	exit(1);
}

static void startup_server(struct server *s)
{
	int onoff;
	struct sockaddr_in sa;

	s->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (s->fd == -1)
		die("socket", 0);
	onoff = 1;
	if (setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, (char *) &onoff, sizeof onoff) == -1)
		die("setsockopt", "cannot set re-use flag");
	fcntl(s->fd, F_SETFD, FD_CLOEXEC);
	fcntl(s->fd, F_SETFL, O_NONBLOCK);
	memset((char *) &sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr = s->addr;
	sa.sin_port = htons(s->port);
	if (bind(s->fd, (struct sockaddr *) &sa, sizeof sa) == -1)
		die("bind", "cannot start up server at %s port %lu", inet_ntoa(s->addr), s->port);
	if (listen(s->fd, s->backlog) == -1)
		die("listen", 0);
}

static void sighandler(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		gotsigterm = 1;
		break;
	case SIGHUP:
		gotsighup = 1;
		break;
	case SIGUSR1:
		gotsigusr1 = 1;
		break;
	case SIGUSR2:
		gotsigusr2 = 1;
		break;
	case SIGCHLD:
		gotsigchld = 1;
		break;
	case SIGQUIT:
		gotsigquit = 1;
		break;
	}
}

int main(int argc, char *argv[])
{
	int c, i, n, version, pid_fd, null_fd;
	struct server *s;
	char buf[10];
	struct rlimit rl;
	struct passwd *pwd;
	const char *message;
	const char *config_filename;

	my_pid = getpid();
	progname = argv[0];
	am_daemon = 1;
	version = 0;
	config_filename = 0;
	while ((c = getopt(argc, argv, "ndvf:")) != EOF) {
		switch(c) {
		case 'n':
			am_daemon = 0;
			break;
		case 'd':
			debug = 1;
			break;
		case 'v':
			version = 1;
			break;
		case 'f':
			if (config_filename == 0)
				config_filename = optarg;
			else
				die(0, "You may not specify more than one configuration file.");
			break;
		default:
			die(0, "usage: %s [ -ndv ] [ -f configuration_file ]", progname);
			break;
		}
	}
	if (version) {
		fprintf(stderr, "%s\n", server_version);
		return 0;
	}
	if (getrlimit(RLIMIT_NOFILE, &rl) == -1)
		die("getrlimit", 0);
	n = rl.rlim_cur = rl.rlim_max;
	setrlimit(RLIMIT_NOFILE, &rl);
	if (am_daemon)
		for (i = 3; i < n; i++)
			close(i);
	null_fd = open(devnull, O_RDWR);
	if (null_fd == -1)
		die("open", "Cannot open %s", devnull);
	while (null_fd < 3) {
		null_fd = dup(null_fd);
		if (null_fd == -1)
			die("dup", 0);
	}
	message = config(config_filename);
	if (message)
		die(0, "%s", message);
	s = servers;
	while (s) {
		startup_server(s);
		s = s->next;
	}
	if (rootdir) {
		if (chroot(rootdir) == -1)
			die("chroot", 0);
		if (chdir("/") == -1)
			die("chdir", 0);
	}
	setuid(geteuid());
	if (geteuid() == 0) {
		if (user_name == 0)
			die(0, "No user specified.");
		pwd = getpwnam(user_name);
		if (pwd == 0)
			die(0, "%s: Unknown user.", user_name);
		if (pwd->pw_uid == 0)
			die(0, "%s: Invalid user.", user_name);
		if (initgroups(user_name, pwd->pw_gid) == -1)
			die("initgroups", 0);
		if (setgid(pwd->pw_gid) == -1)
			die("setgid", 0);
		if (stayroot) {
			if (seteuid(pwd->pw_uid) == -1)
				die("seteuid", 0);
		} else {
			if (setuid(pwd->pw_uid) == -1)
				die("setuid", 0);
		}
	}
	if (getrlimit(RLIMIT_CORE, &rl) == -1)
		die("getrlimit", 0);
	if (coredir) {
		rl.rlim_cur = rl.rlim_max;
		if (chdir(coredir) == -1)
			die("chdir", 0);
	} else {
		rl.rlim_cur = 0;
		chdir("/");
	}
	setrlimit(RLIMIT_CORE, &rl);
	umask(fcm);
	if (pid_filename) {
		pid_fd = open(pid_filename, O_WRONLY | O_CREAT, 0666);
		if (pid_fd == -1)
			die("open", "Cannot open PID file");
	} else
		pid_fd = -1;
	if (init_logs() == -1)
		die("open", "Cannot open log files");
	if (am_daemon) {
		dup2(null_fd, 0);
		dup2(null_fd, 1);
		dup2(null_fd, 2);
	}
	close(null_fd);
	if (am_daemon) {
		if (fork())
			_exit(0);
		setsid();
		if (fork())
			_exit(0);
	}
	mysignal(SIGCHLD, sighandler);
	mysignal(SIGHUP, sighandler);
	mysignal(SIGTERM, sighandler);
	mysignal(SIGINT, sighandler);
	mysignal(SIGQUIT, sighandler);
	mysignal(SIGUSR1, sighandler);
	mysignal(SIGUSR2, sighandler);
	mysignal(SIGPIPE, sighandler);
	my_pid = getpid();
	if (pid_fd != -1) {
		ftruncate(pid_fd, 0);
		sprintf(buf, "%d\n", my_pid);
		write(pid_fd, buf, strlen(buf));
		close(pid_fd);
	}
	httpd_main();
	return 0;
}

int fork_request(struct request *r, int (*f)(struct request *))
{
	struct pipe_params *pp;
	int p[2], efd;
	pid_t pid;

	if (r->c->child_filename == 0) {
		log_d("ChildLog must be set");
		return 500;
	}
	if (r->cn->assbackwards) {
		log_d("fork_request: no HTTP/0.9 allowed here");
		return 500;
	}
	pp = children;
	while (pp) {
		if (pp->cn == 0)
			break;
		pp = pp->next;
	}
	if (pp == 0) {
		log_d("fork_request: out of children");
		return 503;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, p) == -1) {
		lerror("socketpair");
		return 503;
	}
	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	pid = fork();
	switch (pid) {
	case 0:
		my_pid = getpid();
		efd = open_log(r->c->child_filename);
		if (efd == -1)
			_exit(EX_UNAVAILABLE);
		close(p[0]);
		dup2(p[1], 0);
		dup2(p[1], 1);
		dup2(efd, 2);
		close(p[1]);
		close(efd);
		_exit(f(r));
		break;
	case -1:
		lerror("fork");
		close(p[0]);
		close(p[1]);
		return 503;
	default:
		if (debug)
			log_d("fork_request: child process %d created", pid);
		close(p[1]);
		fcntl(p[0], F_SETFL, O_NONBLOCK);
		init_child(pp, r, p[0]);
		break;
	}
	return -1;
}
