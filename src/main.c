/*
 * main.c - Mathopd
 *
 * Copyright 1996, 1997, 1998, Michiel Boland
 */

/* Once Around */

#include "mathopd.h"

STRING(server_version) = "Mathopd/1.1";

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

static STRING(su_fork) = "could not fork";

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
	int onoff;
	struct sockaddr_in sa;

	if ((s->fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		die("socket", 0);

	onoff = 1;

	if (setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, 
		       (char *) &onoff, sizeof onoff) == -1
	    || setsockopt(s->fd, SOL_SOCKET, SO_SNDBUF, 
			  (char *) &buf_size, sizeof buf_size) == -1
	    || setsockopt(s->fd, SOL_SOCKET, SO_RCVBUF,
			  (char *) &input_buf_size,
			  sizeof input_buf_size) == -1)
		die("setsockopt", 0);

	fcntl(s->fd, F_SETFD, FD_CLOEXEC);
	fcntl(s->fd, F_SETFL, M_NONBLOCK);

	memset((char *) &sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr = s->addr;
	sa.sin_port = htons(s->port);

	if (bind(s->fd, (struct sockaddr *) &sa, sizeof sa) == -1)
		die("bind", "cannot start up server %s at port %d",
		    s->name, s->port);

	if (listen(s->fd, 128) == -1)
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
	int c;
	int daemon = 1;
	int version = 0;
	int i, pid_fd, n, null_fd;
	struct server *s;
	char buf[10];
#ifndef NO_GETRLIMIT
	struct rlimit rl;
#endif

	progname = argv[0];

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

#ifdef NO_GETRLIMIT
	n = sysconf(_SC_OPEN_MAX);
	if (n == -1)
		die("sysconf", 0);
#else
	if (getrlimit(RLIMIT_NOFILE, &rl) == -1)
		die("getrlimit", 0);
	n = rl.rlim_cur = rl.rlim_max;
	setrlimit(RLIMIT_NOFILE, &rl);
#endif

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
		if (user_id == 0)
			die(0, "No user specified.");
		if (group_id == 0)
			die(0, "No group specified.");
		if (initgroups(user_name, group_id) == -1)
			die("initgroups", 0);
		if (setgid(group_id) == -1)
			die("setgid", 0);
		if (setuid(user_id) == -1)
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

	httpd_main();

	return 0;
}

void die(const char *t, const char *fmt, ...)
{
	if (fmt) {
		va_list ap;

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
