#include "mathopd.h"

int nconnections;
int maxconnections;
time_t current_time;

static int log_file = -1;
static int error_file = -1;

#ifdef NEED_STRERROR
const char *strerror(int err)
{
	extern int sys_nerr;
	extern char *sys_errlist[];

	return (err < 0 || err >= sys_nerr)
		? "Unknown error"
		: sys_errlist[err];
}
#endif

static void init_pool(struct pool *p)
{
	p->start = p->end = p->floor;
	p->state = 0;
}

static void reinit_connection(struct connection *cn, int action)
{
	if (cn->rfd != -1) {
		close(cn->rfd);
		cn->rfd = -1;
	}
	init_pool(cn->input);
	init_pool(cn->output);
	cn->assbackwards = 1;
	cn->keepalive = 0;
	cn->action = action;
}

static void init_connection(struct connection *cn, struct server *s, int fd,
			    struct sockaddr_in sa)
{
	s->nhandled++;
	cn->state = HC_ACTIVE;
	cn->s = s;
	cn->fd = fd;
	cn->rfd = -1;
	cn->peer = sa;
	strncpy(cn->ip, inet_ntoa(sa.sin_addr), 15);
	cn->t = cn->it = current_time;
	++nconnections;
	if (nconnections > maxconnections)
		maxconnections = nconnections;
	reinit_connection(cn, HC_READING);
}

static void close_connection(struct connection *cn)
{
	--nconnections;
	close(cn->fd);
	if (cn->rfd != -1)
		close(cn->rfd);
	cn->state = HC_FREE;
}

static void nuke_servers(void)
{
	struct server *s;

	s = servers;
	while (s) {
		close(s->fd);
		s->fd = -1;
		s = s->next;
	}
}

static void nuke_connections(void)
{
	struct connection *cn;

	cn = connections;
	while (cn) {
		if (cn->state != HC_FREE)
		close_connection(cn);
		cn = cn->next;
	}
}

static void accept_connection(struct server *s)
{
	struct sockaddr_in sa;
	int lsa, fd;
	struct connection *cn, *cw;

	lsa = sizeof sa;
	while ((fd = accept(s->fd, (struct sockaddr *) &sa, &lsa)) != -1) {
		s->naccepts++;
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		fcntl(fd, F_SETFL, M_NONBLOCK);
		cn = connections;
		cw = 0;
		while (cn) {
			if (cn->state == HC_FREE)
				break;
			if (cw == 0 && cn->action == HC_WAITING)
				cw = cn;
			cn = cn->next;
		}
		if (cn)
			init_connection(cn, s, fd, sa);
		else if (cw) {
			close_connection(cw);
			init_connection(cw, s, fd, sa);
		}
		else {
			log(L_ERROR, "connection to %s dropped",
			    inet_ntoa(sa.sin_addr));
			close(fd);
		}
	}
	if (errno != M_AGAIN)
		lerror("accept");
}

static int fill_connection(struct connection *cn)
{
	struct pool *p;
	int poolleft, n, m;
	long fileleft;

	if (cn->rfd == -1)
		return 0;
	p = cn->output;
	poolleft = p->ceiling - p->end;
	fileleft = cn->r->content_length;
	n = fileleft > poolleft ? poolleft : (int) fileleft;
	if (n <= 0)
		return 0;
	cn->r->content_length -= n;
	m = read(cn->rfd, p->end, n);
	if (m != n) {
		if (m == -1)
			lerror("read");
		else
			log(L_ERROR, "premature end of file %s",
			    cn->r->path_translated);
		return -1;
	}
	p->end += n;
	return n;
}

static void write_connection(struct connection *cn)
{
	struct pool *p = cn->output;
	int m, n;

	do {
		n = p->end - p->start;
		if (n == 0) {
			init_pool(p);
			n = fill_connection(cn);
			if (n <= 0) {
				if (n == 0 && cn->keepalive)
					reinit_connection(cn, HC_WAITING);
				else
					cn->action = HC_CLOSING;
				return;
			}
		}

		cn->t = current_time;

		m = send(cn->fd, p->start, n, 0);
		log(L_DEBUG, "send(%d) = %d", n, m);
		if (m == -1) {
			switch (errno) {
			default:
				log(L_ERROR, "error sending to %s", cn->ip);
				lerror("send");
			case EPIPE:
				cn->action = HC_CLOSING;
			case M_AGAIN:
				return;
			}
		}
		if (cn->r->vs)
			cn->r->vs->nwritten += m;
		p->start += m;
	} while (n == m);
}

static void read_connection(struct connection *cn)
{
	int i, nr, fd;
	register char c;
	struct pool *p = cn->input;
	register char state = p->state;

	log(L_DEBUG, "read_connection: starting");

	cn->action = HC_READING;
	fd = cn->fd;
	i = p->ceiling - p->end;
	if (i == 0) {
		log(L_ERROR, "input buffer full");
		cn->action = HC_CLOSING;
		return;
	}
	nr = recv(fd, p->end, i, MSG_PEEK);
	if (nr == -1) {
		switch (errno) {
		default:
			log(L_ERROR, "error peeking from %s", cn->ip);
			lerror("recv");
		case ECONNRESET:
			cn->action = HC_CLOSING;
		case M_AGAIN:
			return;
		}
	}
	if (nr == 0) {
		log(L_DEBUG, "read_connection: eof received");
		cn->action = HC_CLOSING;
		return;
	}
	i = 0;

	while (i < nr && state < 8) {
		c = p->end[i++];
		switch (state) {

		case 0:
			switch (c) {
			case ' ':
			case '\t':
			case '\r':
				state = 1;
				break;
			case '\n':
				state = 9;
				break;
			}
			break;

		case 1:
			switch (c) {
			default:
				state = 2;
			case ' ':
			case '\t':
			case '\r':
				break;
			case '\n':
				state = 9;
				break;
			}
			break;

		case 2:
			switch (c) {
			case ' ':
			case '\t':
			case '\r':
				state = 3;
				break;
			case '\n':
				state = 9;
				break;
			}
			break;

		case 3:
			switch (c) {
			default:
				cn->assbackwards = 0;
				state = 4;
			case ' ':
			case '\t':
			case '\r':
				break;
			case '\n':
				state = 8;
				break;
			}
			break;

		case 4:
			switch (c) {
			case '\r':
				state = 5;
				break;
			case '\n':
				state = 6;
				break;
			}
			break;

		case 5:
			switch (c) {
			default:
				state = 4;
			case '\r':
				break;
			case '\n':
				state = 6;
				break;
			}
			break;

		case 6:
			switch (c) {
			case '\r':
				state = 7;
				break;
			case '\n':
				state = 8;
				break;
			default:
				state = 4;
				break;
			}
			break;

		case 7:
			switch (c) {
			default:
				state = 4;
			case '\r':
				break;
			case '\n':
				state = 8;
				break;
			}
			break;
		}
	}

	nr = recv(fd, p->end, i, 0);
	if (nr != i) {
		if (nr == -1)
			log(L_ERROR, "error reading from %s", cn->ip);
			lerror("recv");
		cn->action = HC_CLOSING;
		return;
	}
	if (state == 9) {
		log(L_ERROR, "bad message from %s", cn->ip);
		cn->action = HC_CLOSING;
		return;
	}
	p->end += i;
	p->state = state;
	if (state == 8) {
		if (process_request(cn->r) == -1
		    || fill_connection(cn) == -1)
			cn->action = HC_CLOSING;
		else {
			cn->action = HC_WRITING;
			write_connection(cn);
		}
	}
	cn->t = current_time;
}

static void cleanup_connections(void)
{
	struct connection *cn = connections;

	while (cn) {
		if (cn->state == HC_ACTIVE) {
			if (cn->action == HC_CLOSING
			    || current_time - cn->t >= timeout)
				close_connection(cn);
		}
		cn = cn->next;
	}
}

static void init_log(char *name, int *fdp)
{
	char converted_name[PATHLEN], *n;
	struct tm *tp;

	if (name) {
		n = name;
		if (strchr(name, '%')) {
			tp = localtime(&current_time);
			if (tp) {
				strftime(converted_name, PATHLEN - 1, name, tp);
				n = converted_name;
			}
		}
		if (*fdp != -1)
			close(*fdp);
		*fdp = open(n, O_WRONLY | O_CREAT | O_APPEND,
			    DEFAULT_FILEMODE);
		if (*fdp != -1)
			fcntl(*fdp, F_SETFD, FD_CLOEXEC);
	}
}

static void init_logs(void)
{
	init_log(log_filename, &log_file);
	init_log(error_filename, &error_file);
}


void log(int type, const char *fmt, ...)
{
	va_list args;
	char log_line[2*PATHLEN];
	char *s = log_line;
	int l, fd, saved_errno;

	switch (type) {
	case L_TRANS:
		fd = log_file;
		break;
	case L_DEBUG:
		if (debug == 0)
			return;
	default:
		fd = error_file;
	}

	if (fd == -1)
		return;

	saved_errno = errno;

	va_start(args, fmt);
#ifdef BROKEN_SPRINTF
	vsprintf(log_line, fmt, args);
	l = strlen(log_line);
#else
	l = vsprintf(log_line, fmt, args);
#endif
	va_end(args);

	s = log_line + l;
	*s++ = '\n';
	*s = '\0';

	write(fd, log_line, l + 1);
	errno = saved_errno;
}

void lerror(const char *s)
{
	int saved_errno = errno;

	if (s && *s)
		log(L_ERROR, "%s: %s", s, strerror(saved_errno));
	else
		log(L_ERROR, "%s", strerror(saved_errno));
	switch (saved_errno) {
	case ENFILE:
		log(L_PANIC, "oh no!!!");
		gotsigterm = 1;
	}
	errno = saved_errno;
}

static void do_servers(struct server *s, fd_set *r)
{
	while (s) {
		if (s->fd != -1) {
#ifdef POLL
			if (pollfds[s->pollno].revents & POLLIN)
#else
			if (FD_ISSET(s->fd, r))
#endif
				accept_connection(s);
		}
		s = s->next;
	}
}

static void do_connections(struct connection *cn, fd_set *r, fd_set *w)
{
	while (cn) {
		if (cn->state == HC_ACTIVE) {
#ifdef POLL
			if (cn->pollno != -1) {
				r = pollfds[cn->pollno].revents;
				if (r & POLLIN)
					read_connection(cn);
				else if (r & POLLOUT)
					write_connection(cn);
				else if (r)
					cn->action = HC_CLOSING;
			}
#else
			if (FD_ISSET(cn->fd, r))
				read_connection(cn);
			else if (FD_ISSET(cn->fd, w))
				write_connection(cn);
#endif
		}
		cn = cn->next;
	}
}

void httpd_main(void)
{
	struct server *s;
	struct connection *cn;
	int first = 1;
	int error = 0;
	int rv;
#ifdef POLL
	int n;
	short r;
#else
	fd_set rfds, wfds;
	int m;
#endif

	while (gotsigterm == 0) {

		log(L_DEBUG, "top of loop");

		if (gotsighup) {
			gotsighup = 0;
			init_logs();
			if (first) {
				first = 0;
				log(L_LOG, "*** %s (pid %d) ***",
				    server_version, my_pid);
			}
			else
				log(L_LOG, "logs reopened");
		}

		if (gotsigusr1) {
			gotsigusr1 = 0;
			nuke_connections();
			log(L_LOG, "connections closed");
		}

		if (gotsigusr2) {
			gotsigusr2 = 0;
			nuke_servers();
			log(L_LOG, "servers closed");
		}

		if (gotsigwinch) {
			gotsigwinch = 0;
			dump();
		}

#ifdef POLL
		n = 0;
#else
		m = -1;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
#endif
		s = servers;
		while (s) {
			if (s->fd != -1) {
#ifdef POLL
				pollfds[n].events = POLLIN;
				pollfds[n].fd = s->fd;
				s->pollno = n++;
#else
				FD_SET(s->fd, &rfds);
				if (s->fd > m)
					m = s->fd;
#endif
			}
			s = s->next;
		}

		cn = connections;

		while (cn) {
			if (cn->state == HC_ACTIVE) {
				switch (cn->action) {
				case HC_WAITING:
				case HC_READING:
#ifdef POLL
					pollfds[n].events = POLLIN;
#else
					FD_SET(cn->fd, &rfds);
#endif
					break;
				default:
#ifdef POLL
					pollfds[n].events = POLLOUT;
#else
					FD_SET(cn->fd, &wfds);
#endif
					break;
				}
#ifdef POLL
				pollfds[n].fd = cn->fd;
				cn->pollno = n++;
#else
				if (cn->fd > m)
					m = cn->fd;
#endif
			}
#ifdef POLL
			else
				cn->pollno = -1;
#endif
			cn = cn->next;
		}
#ifdef POLL
		if (n == 0)
#else
		if (m == -1)
#endif
		{
			log(L_ERROR, "no more sockets to select from");
			break;
		}
		log(L_DEBUG, "selecting...");
#ifdef POLL
		rv = poll(pollfds, n, INFTIM);
#else
#ifdef HPUX
		rv = select(m + 1, (int *) &rfds, (int *) &wfds, 0, 0);
#else
		rv = select(m + 1, &rfds, &wfds, 0, 0);
#endif /* HPUX */
#endif /* POLL */
		log(L_DEBUG, "...done");
		time(&current_time);
		if (rv == -1) {
			if (errno != EINTR) {
#ifdef POLL
				lerror("poll");
#else
				lerror("select");
#endif
				if (error++) {
					log(L_PANIC, "whoops");
					break;
				}
			}
		}
		else {
			error = 0;
			if (rv) {
				do_servers(servers, &rfds);
				do_connections(connections, &rfds, &wfds);
			}
			log(L_DEBUG, "cleaning up");
			cleanup_connections();
		}
	}
	dump();
	log(L_LOG, "*** Shutting down (pid %d) ***", my_pid);
}
