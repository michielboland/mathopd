/*
 *   Copyright 1996, 1997, 1998 Michiel Boland.
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
 *   3. All advertising materials mentioning features or use of
 *      this software must display the following acknowledgement:
 *
 *   This product includes software developed by Michiel Boland.
 *
 *   4. The name of the author may not be used to endorse or promote
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

#include "mathopd.h"

const char server_version[] = "Mathopd/1.2b21";

volatile int gotsigterm;
volatile int gotsighup;
volatile int gotsigusr1;
volatile int gotsigusr2;
volatile int gotsigwinch;
volatile int numchildren;
time_t startuptime;
int debug;
int fcm;
int my_pid;

static char *progname;
static int forked;

static const char su_fork[] = "could not fork";

static int mysignal(int sig, void(*f)(int), int flags)
{
	struct sigaction act;

	act.sa_handler = f;
	sigemptyset(&act.sa_mask);
	act.sa_flags = flags;
	return sigaction(sig, &act, 0);
}

static void startup_server(struct server *s)
{
	int onoff, rv;
	struct sockaddr_in sa;

	s->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (s->fd == -1)
		die("socket", 0);
	onoff = 1;
	rv = setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, (char *) &onoff, sizeof onoff);
	if (rv == -1)
		die("setsockopt", "cannot set re-use flag");
	rv = setsockopt(s->fd, SOL_SOCKET, SO_SNDBUF, (char *) &tuning.buf_size, sizeof tuning.buf_size);
	if (rv == -1)
		die("setsockopt", "cannot set send buffer size");
	rv = setsockopt(s->fd, SOL_SOCKET, SO_RCVBUF, (char *) &tuning.input_buf_size, sizeof tuning.input_buf_size);
	if (rv == -1)
		die("setsockopt", "cannot set receive buffer size");
	fcntl(s->fd, F_SETFD, FD_CLOEXEC);
	fcntl(s->fd, F_SETFL, O_NONBLOCK);
	memset((char *) &sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr = s->addr;
	sa.sin_port = htons(s->port);
	rv = bind(s->fd, (struct sockaddr *) &sa, sizeof sa);
	if (rv == -1)
		die("bind", "cannot start up server at %s port %d", s->s_name ? s->s_name : "0", s->port);
	rv = listen(s->fd, 128);
	if (rv == -1)
		die("listen", 0);
}

static void sigterm(int sig)
{
	gotsigterm = 1;
}

static void sighup(int sig)
{
	gotsighup = 1;
}

static void sigusr1(int sig)
{
	gotsigusr1 = 1;
}

static void sigusr2(int sig)
{
	gotsigusr2 = 1;
}

static void sigwinch(int sig)
{
	gotsigwinch = 1;
}

static void sigchld(int sig)
{
	int pid;
	int saved_errno = errno;

	while ((pid = waitpid(-1, 0, WNOHANG)) > 0)
		++numchildren;
	errno = saved_errno;
}

int main(int argc, char *argv[])
{
	int c, i, n, daemon, version, pid_fd, null_fd;
	struct server *s;
	char buf[10];
	struct rlimit rl;
	struct passwd *pwd;

	progname = argv[0];
	daemon = 1;
	version = 0;
	while ((c = getopt(argc, argv, "ndv")) != EOF) {
		switch(c) {
		case 'n':
			daemon = 0;
			break;
		case 'd':
			debug = 1;
			break;
		case 'v':
			version = 1;
			break;
		default:
			die(0, "usage: %s [ -ndv ]", progname);
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
	if (debug)
		fprintf(stderr, "Number of fds available: %d\n", n);
	setrlimit(RLIMIT_NOFILE, &rl);
	for (i = 0; i < n; i++) {
		switch(i) {
		default:
			close(i);
		case STDIN_FILENO:
		case STDERR_FILENO:
			break;
		}
	}
	config();
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
	if (geteuid() == 0) {
		if (user_name == 0)
			die(0, "No user specified.");
		pwd = getpwnam(user_name);
		if (pwd == 0 || pwd->pw_uid == 0)
			die(0, "Invalid user name.");
		if (initgroups(user_name, pwd->pw_gid) == -1)
			die("initgroups", 0);
		if (setgid(pwd->pw_gid) == -1)
			die("setgid", 0);
		if (setuid(pwd->pw_uid) == -1)
			die("setuid", 0);
	}
	if (coredir) {
		if (chdir(coredir) == -1)
			die("chdir", 0);
	} else
		chdir("/");
	umask(fcm);
	if (pid_filename) {
		pid_fd = open(pid_filename, O_WRONLY | O_CREAT,
			      DEFAULT_FILEMODE);
		if (pid_fd == -1)
			die("open", "Cannot open PID file");
	}
	else
		pid_fd = -1;
	if (daemon) {
		if (fork())
			_exit(0);
		setsid();
		if (fork())
			_exit(0);
	}
	mysignal(SIGCHLD, sigchld, SA_RESTART | SA_NOCLDSTOP);
	mysignal(SIGHUP,  sighup,  SA_INTERRUPT);
	mysignal(SIGTERM, sigterm, SA_INTERRUPT);
	mysignal(SIGINT,  sigterm, SA_INTERRUPT);
	mysignal(SIGQUIT, sigterm, SA_INTERRUPT);
	mysignal(SIGUSR1, sigusr1, SA_INTERRUPT);
	mysignal(SIGUSR2, sigusr2, SA_INTERRUPT);
	mysignal(SIGWINCH, sigwinch, SA_INTERRUPT);
	mysignal(SIGPIPE, SIG_IGN, 0);
	my_pid = getpid();
	if (pid_fd != -1) {
		ftruncate(pid_fd, 0);
		sprintf(buf, "%d\n", my_pid);
		write(pid_fd, buf, strlen(buf));
		close(pid_fd);
	}
	null_fd = open("/", O_RDONLY);
	if (null_fd == -1)
		die("open", "Cannot open /");
	dup2(null_fd, STDIN_FILENO);
	dup2(null_fd, STDERR_FILENO);
	close(null_fd);
	gotsighup = 1;
	gotsigterm = 0;
	gotsigusr1 = 0;
	gotsigusr2 = 0;
	gotsigwinch = 1;
	time(&startuptime);
	time(&current_time);
	base64initialize();
	httpd_main();
	return 0;
}

void die(const char *t, const char *fmt, ...)
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

int fork_request(struct request *r, int (*f)(struct request *))
{
	int fd, efd;

	if (forked)
		_exit(1);
	switch (fork()) {
	case 0:
		forked = 1;
		mysignal(SIGPIPE, SIG_DFL, 0);
		fd = r->cn->fd;
		efd = open(child_filename,
			   O_WRONLY | O_CREAT | O_APPEND, DEFAULT_FILEMODE);
		if (efd == -1)
			efd = fd;
		dup2(efd, STDERR_FILENO);
		dup2(fd, STDIN_FILENO);
		dup2(STDIN_FILENO, STDOUT_FILENO);
		fcntl(STDIN_FILENO, F_SETFL, 0);
		fcntl(STDOUT_FILENO, F_SETFL, 0);
		close(fd);
		if (efd != fd)
			close(efd);
		_exit((*f)(r));
		break;
	case -1:
		lerror("fork");
		r->error = su_fork;
		return 503;
	default:
		r->status_line = "---";
	}
	return -1;
}
