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

static int log_file;
static int error_file;

static void init_pool(struct pool *p)
{
	p->start = p->end = p->floor;
	p->state = 0;
}

static void reinit_connection(struct connection *cn, int action)
{
	int rv;

	if (cn->rfd != -1) {
		rv = close(cn->rfd);
		if (debug)
			log(L_DEBUG, "reinit_connection: close(%d) = %d", cn->rfd, rv);
		cn->rfd = -1;
	}
	init_pool(cn->input);
	init_pool(cn->output);
	cn->assbackwards = 1;
	cn->keepalive = 0;
	cn->action = action;
}

static void close_connection(struct connection *cn)
{
	int rv;

	--nconnections;
	rv = close(cn->fd);
	if (debug)
		log(L_DEBUG, "close_connection: close(%d) = %d", cn->fd, rv);
	if (cn->rfd != -1) {
		rv = close(cn->rfd);
		if (debug)
			log(L_DEBUG, "close_connection: close(%d) = %d (rfd)", cn->rfd, rv);
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
			log(L_DEBUG, "nuke_servers: close(%d) = %d", s->fd, rv);
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
				log(L_DEBUG, "accept_connection: accept(%d) = %d", s->fd, fd);
			else
				log(L_DEBUG, "accept_connection: accept(%d) = %d; addr=[%s], port=%d",
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
			log(L_DEBUG, "accept_connection: fcntl(%d, F_SETFD, FD_CLOEXEC) = %d", fd, rv);
		rv = fcntl(fd, F_SETFL, O_NONBLOCK);
		if (debug)
			log(L_DEBUG, "accept_connection: fcntl(%d, F_SETFL, O_NONBLOCK) = %d", fd, rv);
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
			log(L_ERROR, "connection to %s dropped", inet_ntoa(sa.sin_addr));
			rv = close(fd);
			if (debug)
				log(L_DEBUG, "accept_connection: close(%d) = %d", fd, rv);
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
			reinit_connection(cn, HC_READING);
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
	fileleft = cn->r->content_length;
	n = fileleft > poolleft ? poolleft : (int) fileleft;
	if (n <= 0)
		return 0;
	cn->r->content_length -= n;
	m = read(cn->rfd, p->end, n);
	if (debug)
		log(L_DEBUG, "fill_connection: read(%d, %p, %d) = %d", cn->rfd, p->end, n, m);
	if (m != n) {
		if (m == -1)
			lerror("read");
		else
			log(L_ERROR, "premature end of file %s", cn->r->path_translated);
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
					reinit_connection(cn, HC_WAITING);
				else
					cn->action = HC_CLOSING;
				return;
			}
		}
		cn->t = current_time;
		m = send(cn->fd, p->start, n, 0);
		if (debug)
			log(L_DEBUG, "write_connection: send(%d, %p, %d, 0) = %d", cn->fd, p->start, n, m);
		if (m == -1) {
			switch (errno) {
			default:
				log(L_ERROR, "error sending to %s", cn->ip);
				lerror("send");
			case EPIPE:
				cn->action = HC_CLOSING;
			case EAGAIN:
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
	struct pool *p;
	register char state;

	p = cn->input;
	state = p->state;
	cn->action = HC_READING;
	fd = cn->fd;
	i = p->ceiling - p->end;
	if (i == 0) {
		log(L_ERROR, "input buffer full");
		cn->action = HC_CLOSING;
		return;
	}
	nr = recv(fd, p->end, i, MSG_PEEK);
	if (debug)
		log(L_DEBUG, "read_connection: recv(%d, %p, %d, MSG_PEEK) = %d", fd, p->end, i, nr);
	if (nr == -1) {
		switch (errno) {
		default:
			log(L_ERROR, "error peeking from %s", cn->ip);
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
	if (debug)
		log(L_DEBUG, "read_connection: recv(%d, %p, %d, 0) = %d", fd, p->end, i, nr);
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
		if (process_request(cn->r) == -1 || fill_connection(cn) == -1)
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
	struct connection *cn;

	cn = connections;
	while (cn) {
		if (cn->state == HC_ACTIVE) {
			if (current_time - cn->t >= tuning.timeout) {
				log(L_ERROR, "timeout to %s", cn->ip);
				cn->action = HC_CLOSING;
			}
			if (cn->action == HC_CLOSING)
				close_connection(cn);
		}
		cn = cn->next;
	}
}

static void init_log(char *name, int *fdp)
{
	int fd;
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
		fd = *fdp;
		if (fd != -1)
			close(fd);
		fd = open(n, O_WRONLY | O_CREAT | O_APPEND, DEFAULT_FILEMODE);
		if (fd != -1)
			fcntl(fd, F_SETFD, FD_CLOEXEC);
		*fdp = fd;
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
	int l, fd, saved_errno;
	char *ti;

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
	ti = ctime(&current_time);
	l = sprintf(log_line, "%.24s\t", ti ? ti : "???");
	if (type != L_TRANS)
		l += sprintf(log_line + l, "[%d]\t", my_pid);
	va_start(args, fmt);
	l += vsprintf(log_line + l, fmt, args);
	va_end(args);
	log_line[l] = '\n';
	write(fd, log_line, l + 1);
	errno = saved_errno;
}

void lerror(const char *s)
{
	int saved_errno;

	saved_errno = errno;
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

void httpd_main(void)
{
	struct server *s;
	struct connection *cn;
	int first;
	int error;
	int rv;
#ifdef POLL
	int n;
	short r;
#else
	fd_set rfds, wfds;
	int m;
#endif

	first = 1;
	error = 0;
	log_file = -1;
	error_file = -1;
	while (gotsigterm == 0) {
		if (gotsighup) {
			gotsighup = 0;
			init_logs();
			if (first) {
				first = 0;
				log(L_LOG, "*** %s starting", server_version);
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
		if (n == 0) {
			log(L_ERROR, "no more sockets to poll from");
			break;
		}
#else
		if (m == -1) {
			log(L_ERROR, "no more sockets to select from");
			break;
		}
#endif
#ifdef POLL
		if (debug)
			log(L_DEBUG, "httpd_main: poll(%d) ...", n);
		rv = poll(pollfds, n, INFTIM);
#else
		if (debug)
			log(L_DEBUG, "httpd_main: select(%d) ...", m + 1);
		rv = select(m + 1, &rfds, &wfds, 0, 0);
#endif
		current_time = time(0);
#ifdef POLL
		if (debug)
			log(L_DEBUG, "httpd_main: poll() = %d", rv);
#else
		if (debug)
			log(L_DEBUG, "httpd_main: select() = %d", rv);
#endif
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
		} else {
			error = 0;
			if (rv) {
				s = servers;
				while (s) {
					if (s->fd != -1) {
#ifdef POLL
						if (pollfds[s->pollno].revents & POLLIN)
#else
						if (FD_ISSET(s->fd, &rfds))
#endif
							accept_connection(s);
					}
					s = s->next;
				}
				cn = connections;
				while (cn) {
					if (cn->state == HC_ACTIVE) {
#ifdef POLL
						if (cn->pollno != -1) {
							r = pollfds[cn->pollno].revents;
							if (r & POLLIN)
								read_connection(cn);
							else if (r & POLLOUT)
								write_connection(cn);
							else if (r) {
								log(L_ERROR, "dropping %s: unexpected event %hd", cn->ip, r);
								cn->action = HC_CLOSING;
						}
#else
						if (FD_ISSET(cn->fd, &rfds))
							read_connection(cn);
						else if (FD_ISSET(cn->fd, &wfds))
							write_connection(cn);
#endif
					}
					cn = cn->next;
				}
			}
			cleanup_connections();
		}
	}
	dump();
	log(L_LOG, "*** shutting down", my_pid);
}
