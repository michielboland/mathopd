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

/* Les trois soers aveugles */

#include "mathopd.h"

int nconnections;
int maxconnections;
time_t current_time;
int log_file;

static int error_file;

static void init_pool(struct pool *p)
{
	p->start = p->end = p->floor;
	p->state = 0;
}

static void init_connection(struct connection *cn)
{
	init_pool(cn->input);
	init_pool(cn->output);
	cn->assbackwards = 1;
	cn->keepalive = 0;
	cn->nread = 0;
	cn->nwritten = 0;
	cn->left = 0;
	cn->r->processed = 0;
}

static void reinit_connection(struct connection *cn)
{
	int rv;

	log_request(cn->r);
	if (cn->rfd != -1) {
		rv = close(cn->rfd);
		if (debug)
			log_d("reinit_connection: close(%d) = %d", cn->rfd, rv);
		cn->rfd = -1;
	}
	init_connection(cn);
	cn->action = HC_WAITING;
}

static void close_connection(struct connection *cn)
{
	int rv;

	if (cn->nread || cn->nwritten)
		log_request(cn->r);
	--nconnections;
	rv = close(cn->fd);
	if (debug)
		log_d("close_connection: close(%d) = %d", cn->fd, rv);
	if (cn->rfd != -1) {
		rv = close(cn->rfd);
		if (debug)
			log_d("close_connection: close(%d) = %d (rfd)", cn->rfd, rv);
		cn->rfd = -1;
	}
	cn->state = HC_FREE;
}

static void nuke_servers(void)
{
	struct server *s;
	int rv;

	s = servers;
	while (s) {
		rv = close(s->fd);
		if (debug)
			log_d("nuke_servers: close(%d) = %d", s->fd, rv);
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
	int lsa, fd, rv;
	struct connection *cn, *cw;

	do {
		lsa = sizeof sa;
		fd = accept(s->fd, (struct sockaddr *) &sa, &lsa);
		if (debug) {
			if (fd == -1)
				log_d("accept_connection: accept(%d) = %d", s->fd, fd);
			else
				log_d("accept_connection: accept(%d) = %d; addr=[%s], port=%d",
					s->fd, fd, inet_ntoa(sa.sin_addr), htons(sa.sin_port));
		}
		if (fd == -1) {
			if (errno != EAGAIN)
				lerror("accept");
			break;
		}
		s->naccepts++;
		rv = fcntl(fd, F_SETFD, FD_CLOEXEC);
		if (debug)
			log_d("accept_connection: fcntl(%d, F_SETFD, FD_CLOEXEC) = %d", fd, rv);
		rv = fcntl(fd, F_SETFL, O_NONBLOCK);
		if (debug)
			log_d("accept_connection: fcntl(%d, F_SETFL, O_NONBLOCK) = %d", fd, rv);
		cn = connections;
		cw = 0;
		while (cn) {
			if (cn->state == HC_FREE)
				break;
			if (cw == 0 && cn->action == HC_WAITING)
				cw = cn;
			cn = cn->next;
		}
		if (cn == 0 && cw) {
			close_connection(cw);
			cn = cw;
		}
		if (cn == 0) {
			log_d("connection to %s dropped", inet_ntoa(sa.sin_addr));
			rv = close(fd);
			if (debug)
				log_d("accept_connection: close(%d) = %d", fd, rv);
		} else {
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
			init_connection(cn);
			cn->action = HC_READING;
		}
	} while (tuning.accept_multi);
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
	fileleft = cn->left;
	n = fileleft > poolleft ? poolleft : (int) fileleft;
	if (n <= 0)
		return 0;
	cn->left -= n;
	m = read(cn->rfd, p->end, n);
	if (debug)
		log_d("fill_connection: read(%d, %p, %d) = %d", cn->rfd, p->end, n, m);
	if (m != n) {
		if (m == -1)
			lerror("read");
		else
			log_d("premature end of file %s", cn->r->path_translated);
		return -1;
	}
	p->end += n;
	return n;
}

static void write_connection(struct connection *cn)
{
	struct pool *p;
	int m, n;

	p = cn->output;
	do {
		n = p->end - p->start;
		if (n == 0) {
			init_pool(p);
			n = fill_connection(cn);
			if (n <= 0) {
				if (n == 0 && cn->keepalive)
					reinit_connection(cn);
				else
					cn->action = HC_CLOSING;
				return;
			}
		}
		cn->t = current_time;
		m = send(cn->fd, p->start, n, 0);
		if (debug)
			log_d("write_connection: send(%d, %p, %d, 0) = %d", cn->fd, p->start, n, m);
		if (m == -1) {
			switch (errno) {
			default:
				log_d("error sending to %s", cn->ip);
				lerror("send");
			case EPIPE:
				cn->action = HC_CLOSING;
			case EAGAIN:
				return;
			}
		}
		cn->nwritten += m;
		if (cn->r->vs)
			cn->r->vs->nwritten += m;
		p->start += m;
	} while (n == m);
}

static void read_connection(struct connection *cn)
{
	int i, nr, fd;
	register char c;
	struct pool *p;
	register char state;

	p = cn->input;
	state = p->state;
	cn->action = HC_READING;
	fd = cn->fd;
	i = p->ceiling - p->end;
	if (i == 0) {
		log_d("input buffer full");
		cn->action = HC_CLOSING;
		return;
	}
	nr = recv(fd, p->end, i, MSG_PEEK);
	if (debug)
		log_d("read_connection: recv(%d, %p, %d, MSG_PEEK) = %d", fd, p->end, i, nr);
	if (nr == -1) {
		switch (errno) {
		default:
			log_d("error peeking from %s", cn->ip);
			lerror("recv");
		case ECONNRESET:
			cn->action = HC_CLOSING;
		case EAGAIN:
			return;
		}
	}
	if (nr == 0) {
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
				state = 0;
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
				state = 8;
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
	if (debug)
		log_d("read_connection: recv(%d, %p, %d, 0) = %d", fd, p->end, i, nr);
	if (nr != i) {
		if (nr == -1) {
			log_d("error reading from %s", cn->ip);
			lerror("recv");
		} else {
			cn->nread += nr;
			log_d("read_connection: %d != %d!", nr, i);
		}
		cn->action = HC_CLOSING;
		return;
	}
	cn->nread += nr;
	p->end += i;
	p->state = state;
	if (state == 8) {
		if (process_request(cn->r) == -1 || fill_connection(cn) == -1)
			cn->action = HC_CLOSING;
		else {
			cn->left = cn->r->content_length;
			cn->action = HC_WRITING;
			write_connection(cn);
		}
	}
	cn->t = current_time;
}

static void cleanup_connections(void)
{
	struct connection *cn;

	cn = connections;
	while (cn) {
		if (cn->state == HC_ACTIVE) {
			if (current_time - cn->t >= tuning.timeout) {
				if (debug)
					log_d("timeout to %s", cn->ip);
				cn->action = HC_CLOSING;
			}
			if (cn->action == HC_CLOSING)
				close_connection(cn);
		}
		cn = cn->next;
	}
}

static void reap_children(void)
{
	int status, pid;

	gotsigchld = 0;
	while (1) {
		pid = waitpid(-1, &status, WNOHANG);
		if (pid <= 0)
			break;
		++numchildren;
		if (WIFEXITED(status))
			log_d("child process %d exited with status %d", pid, WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			log_d("child process %d killed by signal %d", pid, WTERMSIG(status));
		else if (WIFSTOPPED(status))
			log_d("child process %d stopped by signal %d", pid, WSTOPSIG(status));
	}
	if (pid < 0 && errno != ECHILD)
		lerror("waitpid");
}

static int init_log_d(char *name, int *fdp)
{
	int fd, nfd;
	char converted_name[PATHLEN], *n;
	struct tm *tp;

	if (name) {
		n = name;
		if (strchr(name, '%')) {
			tp = localtime(&current_time);
			if (tp) {
				if (strftime(converted_name, PATHLEN - 1, name, tp))
					n = converted_name;
			}
		}
		fd = *fdp;
		nfd = open(n, O_WRONLY | O_CREAT | O_APPEND, DEFAULT_FILEMODE);
		if (nfd == -1)
			return fd == -1 ? -1 : 0;
		if (fd == -1)
			*fdp = nfd;
		else if (nfd != fd) {
			dup2(nfd, fd);
			close(nfd);
			nfd = fd;
		}
		fcntl(nfd, F_SETFD, FD_CLOEXEC);
	}
	return 0;
}

static int init_logs(void)
{
	return init_log_d(log_filename, &log_file) == -1 || init_log_d(error_filename, &error_file) == -1 ? -1 : 0;
}

void log_d(const char *fmt, ...)
{
	va_list ap;
	char log_line[200];
	int l, m, n, saved_errno;
	char *ti;

	if (error_file == -1)
		return;
	va_start(ap, fmt);
	saved_errno = errno;
	ti = ctime(&current_time);
	l = sprintf(log_line, "%.24s [%d] ", ti, my_pid);
	m = sizeof log_line - l - 1;
	n = vsnprintf(log_line + l, m, fmt, ap);
	l += n < m ? n : m;
	log_line[l++] = '\n';
	write(error_file, log_line, l);
	errno = saved_errno;
	va_end(ap);
}

void lerror(const char *s)
{
	int saved_errno;
	char *errmsg;

	saved_errno = errno;
	errmsg = strerror(errno);
	if (s && *s)
		log_d("%.80s: %.80s", s, errmsg ? errmsg : "???");
	else
		log_d("%.80s", errmsg ? errmsg : "???");
	errno = saved_errno;
}

void httpd_main(void)
{
	struct server *s;
	struct connection *cn;
	int first;
	int error;
	int rv;
	fd_set rfds, wfds;
	int m;

	first = 1;
	error = 0;
	log_file = -1;
	error_file = -1;
	while (gotsigterm == 0) {
		if (gotsighup) {
			gotsighup = 0;
			if (init_logs() == -1)
				break;
			if (first) {
				first = 0;
				log_d("*** %s starting", server_version);
			} else
				log_d("logs reopened");
		}
		if (gotsigusr1) {
			gotsigusr1 = 0;
			nuke_connections();
			log_d("connections closed");
		}
		if (gotsigusr2) {
			gotsigusr2 = 0;
			nuke_servers();
			log_d("servers closed");
		}
		if (gotsigwinch) {
			gotsigwinch = 0;
		}
		if (gotsigchld)
			reap_children();
		if (gotsigquit) {
			gotsigquit = 0;
			debug = debug == 0;
			if (debug)
				log_d("debugging turned on");
			else
				log_d("debugging turned off");
		}
		m = -1;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		s = servers;
		while (s) {
			if (s->fd != -1) {
				FD_SET(s->fd, &rfds);
				if (s->fd > m)
					m = s->fd;
			}
			s = s->next;
		}
		cn = connections;
		while (cn) {
			if (cn->state == HC_ACTIVE) {
				switch (cn->action) {
				case HC_WAITING:
				case HC_READING:
					FD_SET(cn->fd, &rfds);
					break;
				default:
					FD_SET(cn->fd, &wfds);
					break;
				}
				if (cn->fd > m)
					m = cn->fd;
			}
			cn = cn->next;
		}
		if (m == -1) {
			log_d("no more sockets to select from");
			break;
		}
		if (debug)
			log_d("httpd_main: select(%d) ...", m + 1);
		rv = select(m + 1, &rfds, &wfds, 0, 0);
		current_time = time(0);
		if (debug)
			log_d("httpd_main: select() = %d", rv);
		if (rv == -1) {
			if (errno != EINTR) {
				lerror("select");
				if (error++) {
					log_d("whoops");
					break;
				}
			}
		} else {
			error = 0;
			if (rv) {
				s = servers;
				while (s) {
					if (s->fd != -1) {
						if (FD_ISSET(s->fd, &rfds))
							accept_connection(s);
					}
					s = s->next;
				}
				cn = connections;
				while (cn) {
					if (cn->state == HC_ACTIVE) {
						if (FD_ISSET(cn->fd, &rfds))
							read_connection(cn);
						else if (FD_ISSET(cn->fd, &wfds))
							write_connection(cn);
					}
					cn = cn->next;
				}
			}
			cleanup_connections();
		}
	}
	log_d("*** shutting down", my_pid);
}
