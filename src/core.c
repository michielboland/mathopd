/*
 *   Copyright 1996-2003 Michiel Boland.
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

/* Les trois soers aveugles */

static const char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#ifndef POLL_EMULATION
#include <poll.h>
#else
#include "poll-emul.h"
#endif
#include <stdlib.h>
#include "mathopd.h"

#ifdef NEED_SOCKLEN_T
typedef int socklen_t;
#endif

int nconnections;
int maxconnections;
time_t startuptime;
time_t current_time;

struct pollfd *pollfds;

static void init_pool(struct pool *p)
{
	p->start = p->end = p->floor;
	p->state = 0;
}

static void init_connection(struct connection *cn)
{
	init_pool(cn->input);
	init_pool(cn->output);
	init_request(cn->r);
	cn->assbackwards = 1;
	cn->keepalive = 1;
	cn->nread = 0;
	cn->nwritten = 0;
	cn->left = 0;
}

static void reinit_connection(struct connection *cn)
{
	log_request(cn->r);
	cn->logged = 1;
	if (cn->rfd != -1) {
		close(cn->rfd);
		cn->rfd = -1;
	}
	init_connection(cn);
	cn->action = HC_WAITING;
}

static void close_connection(struct connection *cn)
{
	if (cn->nread || cn->nwritten || cn->logged == 0)
		log_request(cn->r);
	--nconnections;
	close(cn->fd);
	if (cn->rfd != -1) {
		close(cn->rfd);
		cn->rfd = -1;
	}
	cn->state = HC_FREE;
}

static void close_servers(void)
{
	struct server *s;

	s = servers;
	while (s) {
		close(s->fd);
		s->fd = -1;
		s = s->next;
	}
}

static void close_connections(void)
{
	struct connection *cn;

	cn = connections;
	while (cn) {
		if (cn->state != HC_FREE)
			close_connection(cn);
		cn = cn->next;
	}
}

static int accept_connection(struct server *s)
{
	struct sockaddr_in sa_remote, sa_local;
	socklen_t lsa;
	int fd;
	struct connection *cn, *cw;

	do {
		lsa = sizeof sa_remote;
		fd = accept(s->fd, (struct sockaddr *) &sa_remote, &lsa);
		if (fd == -1) {
			if (errno != EAGAIN) {
				lerror("accept");
				return -1;
			}
			return 0;
		}
		s->naccepts++;
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		fcntl(fd, F_SETFL, O_NONBLOCK);
		lsa = sizeof sa_local;
		if (getsockname(fd, (struct sockaddr *) &sa_local, &lsa) == -1) {
			lerror("getsockname");
			close(fd);
			break;
		}
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
			log_d("connection to %s[%hu] dropped", inet_ntoa(sa_remote.sin_addr), ntohs(sa_remote.sin_port));
			close(fd);
		} else {
			s->nhandled++;
			cn->state = HC_ACTIVE;
			cn->s = s;
			cn->fd = fd;
			cn->rfd = -1;
			cn->peer = sa_remote;
			cn->sock = sa_local;
			cn->t = current_time;
			++nconnections;
			if (nconnections > maxconnections)
				maxconnections = nconnections;
			init_connection(cn);
			cn->logged = 0;
			cn->action = HC_READING;
		}
	} while (tuning.accept_multi);
	return 0;
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
		if (m == -1) {
			switch (errno) {
			default:
				log_d("error sending to %s[%hu]", inet_ntoa(cn->peer.sin_addr), ntohs(cn->peer.sin_port));
				lerror("send");
			case EPIPE:
				cn->action = HC_CLOSING;
			case EAGAIN:
				return;
			}
		}
		cn->nwritten += m;
		p->start += m;
	} while (n == m);
}

static void read_connection(struct connection *cn)
{
	int i, nr, fd;
	char c;
	struct pool *p;
	char state;

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
	if (nr == -1) {
		switch (errno) {
		default:
			log_d("error peeking from %s[%hu]", inet_ntoa(cn->peer.sin_addr), ntohs(cn->peer.sin_port));
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
		if (c == 0) {
			log_d("read_connection: NUL in headers");
			cn->action = HC_CLOSING;
			return;
		}
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
	if (nr != i) {
		if (nr == -1) {
			log_d("error reading from %s[%hu]", inet_ntoa(cn->peer.sin_addr), ntohs(cn->peer.sin_port));
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
	cn->t = current_time;
	if (state == 8) {
		if (process_request(cn->r) == -1) {
			if (cn->state != HC_FORKED)
				cn->action = HC_CLOSING;
			return;
		}
		cn->left = cn->r->content_length;
		if (fill_connection(cn) == -1) {
			cn->action = HC_CLOSING;
			return;
		}
		cn->action = HC_WRITING;
		write_connection(cn);
	}
}

static int setup_server_pollfds(int n)
{
	struct server *s;

	s = servers;
	while (s) {
		if (s->fd != -1) {
			pollfds[n].events = POLLIN;
			pollfds[n].fd = s->fd;
			s->pollno = n++;
		} else
			s->pollno = -1;
		s = s->next;
	}
	return n;
}

static int setup_connection_pollfds(int n)
{
	struct connection *cn;
	short e;

	cn = connections;
	while (cn) {
		e = 0;
		if (cn->state == HC_ACTIVE) {
			switch (cn->action) {
			case HC_WAITING:
			case HC_READING:
				e = POLLIN;
				break;
			default:
				e = POLLOUT;
				break;
			}
		}
		if (e) {
			pollfds[n].fd = cn->fd;
			pollfds[n].events = e;
			cn->pollno = n++;
		} else
			cn->pollno = -1;
		cn = cn->next;
	}
	return n;
}

static int run_servers(void)
{
	struct server *s;

	s = servers;
	while (s) {
		if (s->pollno != -1)
			if (pollfds[s->pollno].revents & POLLIN)
				if (accept_connection(s) == -1)
					return -1;
		s = s->next;
	}
	return 0;
}

static void run_connections(void)
{
	struct connection *cn;
	short r;
	cn = connections;
	while (cn) {
		if (cn->pollno != -1) {
			r = pollfds[cn->pollno].revents;
			if (r & POLLIN)
				read_connection(cn);
			else if (r & POLLOUT)
				write_connection(cn);
			else if (r) {
				log_d("poll: unexpected event %hd", r);
				cn->action = HC_CLOSING;
			}
		}
		cn = cn->next;
	}
}

static void cleanup_connections(void)
{
	struct connection *cn;

	cn = connections;
	while (cn) {
		if (cn->state == HC_ACTIVE) {
			if (cn->action == HC_REINIT)
				reinit_connection(cn);
			else if (cn->action == HC_CLOSING)
				close_connection(cn);
			else if (current_time >= cn->t + (time_t) tuning.timeout) {
				if (debug)
					log_d("timeout to %s[%hu]", inet_ntoa(cn->peer.sin_addr), ntohs(cn->peer.sin_port));
				close_connection(cn);
			}
		}
		cn = cn->next;
	}
}

static void reap_children(void)
{
	int errno_save, status, pid;

	errno_save = errno;
	while (1) {
		pid = waitpid(-1, &status, WNOHANG);
		if (pid <= 0)
			break;
		if (WIFEXITED(status)) {
			++numchildren;
			if (debug)
				log_d("child process %d exited with status %d", pid, WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			++numchildren;
			log_d("child process %d killed by signal %d", pid, WTERMSIG(status));
		} else
			log_d("child process %d stopped!?", pid);
	}
	if (pid < 0 && errno != ECHILD)
		lerror("waitpid");
	errno = errno_save;
}

#ifndef INFTIM
#define INFTIM -1
#endif

void httpd_main(void)
{
	int rv;
	int n, t;
	time_t hours;
	int accepting;
	time_t last_time;

	accepting = 1;
	last_time = current_time = startuptime = time(0);
	hours = current_time / 3600;
	log_d("*** %s starting", server_version);
	while (gotsigterm == 0) {
		if (gotsighup) {
			gotsighup = 0;
			init_logs();
			if (debug)
				log_d("logs reopened");
		}
		if (gotsigusr1) {
			gotsigusr1 = 0;
			close_children();
			close_connections();
			log_d("connections closed");
		}
		if (gotsigusr2) {
			gotsigusr2 = 0;
			close_servers();
			log_d("servers closed");
		}
		if (gotsigchld) {
			gotsigchld = 0;
			reap_children();
		}
		if (gotsigquit) {
			gotsigquit = 0;
			debug = debug == 0;
			if (debug)
				log_d("debugging turned on");
			else
				log_d("debugging turned off");
		}
		n = 0;
		if (accepting)
			n = setup_server_pollfds(n);
		n = setup_connection_pollfds(n);
		n = setup_child_pollfds(n);
		if (n == 0 && accepting) {
			log_d("no more sockets to poll from");
			break;
		}
		t = accepting ? (nconnections ? 60000 : INFTIM) : 1000;
		rv = poll(pollfds, n, t);
		current_time = time(0);
		if (rv == -1) {
			if (errno != EINTR) {
				lerror("poll");
				break;
			} else
				continue;
		}
		if (current_time != last_time) {
			if (accepting == 0)
				accepting = 1;
			last_time = current_time;
			if (current_time / 3600 != hours) {
				hours = current_time / 3600;
				init_logs();
				if (debug)
					log_d("logs rotated");
			}
		}
		if (rv) {
			if (accepting && run_servers() == -1)
				accepting = 0;
			run_connections();
			run_children();
		}
		cleanup_children();
		cleanup_connections();
	}
	log_d("*** shutting down");
}

int init_pollfds(size_t n)
{
	pollfds = realloc(pollfds, n * sizeof *pollfds);
	return pollfds == 0 ? -1 : 0;
}
