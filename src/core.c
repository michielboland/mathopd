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

/* Les trois soers aveugles */

static const char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
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
unsigned long nrequests;
struct connection *connection_array;

static struct connection_list free_connections;
static struct connection_list waiting_connections;
static struct connection_list reading_connections;
static struct connection_list writing_connections;
static struct connection_list forked_connections;

static void c_unlink(struct connection *c, struct connection_list *l)
{
	struct connection *p, *n;

	p = c->prev;
	n = c->next;
	if (p)
		p->next = n;
	if (n)
		n->prev = p;
	c->prev = 0;
	c->next = 0;
	if (c == l->head)
		l->head = n;
	if (c == l->tail)
		l->tail = p;
}

static void c_link(struct connection *c, struct connection_list *l)
{
	struct connection *n;

	n = l->head;
	c->prev = 0;
	c->next = n;
	if (n == 0)
		l->tail = c;
	else
		n->prev = c;
	l->head = c;
}

void set_connection_state(struct connection *c, enum connection_state state)
{
	enum connection_state oldstate;
	struct connection_list *o, *n;

	if (debug)
		log_d("set_connection_state: %d %d", c - connection_array, state);
	oldstate = c->connection_state;
	if (state == oldstate) {
		log_d("set_connection_state: state == oldstate (%d)", state);
		return;
	}
	switch (oldstate) {
	case HC_UNATTACHED:
		o = 0;
		break;
	case HC_FREE:
		o = &free_connections;
		break;
	case HC_WAITING:
		o = &waiting_connections;
		break;
	case HC_READING:
		o = &reading_connections;
		break;
	case HC_WRITING:
		o = &writing_connections;
		break;
	case HC_FORKED:
		o = &forked_connections;
		break;
	default:
		log_d("set_connection_state: unknown state: %d", state);
		abort();
	}
	switch (state) {
	case HC_UNATTACHED:
		n = 0;
		break;
	case HC_FREE:
		n = &free_connections;
		break;
	case HC_WAITING:
		n = &waiting_connections;
		break;
	case HC_READING:
		n = &reading_connections;
		break;
	case HC_WRITING:
		n = &writing_connections;
		break;
	case HC_FORKED:
		n = &forked_connections;
		break;
	default:
		log_d("set_connection_state: unknown state: %d", state);
		abort();
	}
	if (o)
		c_unlink(c, o);
	c->connection_state = state;
	if (n)
		c_link(c, n);
}

static void init_connection(struct connection *cn)
{
	cn->header_input.start = cn->header_input.end = cn->header_input.floor;
	cn->header_input.state = 0;
	cn->output.start = cn->output.end = cn->output.floor;
	init_request(cn->r);
	cn->keepalive = 0;
	cn->nread = 0;
	cn->nwritten = 0;
	cn->left = 0;
	gettimeofday(&cn->itv, 0);
	cn->pid = 0;
}

void reinit_connection(struct connection *cn)
{
	++nrequests;
	log_request(cn->r);
	cn->logged = 1;
	if (cn->rfd != -1) {
		close(cn->rfd);
		cn->rfd = -1;
	}
	init_connection(cn);
	set_connection_state(cn, HC_WAITING);
}

void close_connection(struct connection *cn)
{
	if (cn->nread || cn->nwritten || cn->logged == 0) {
		++nrequests;
		log_request(cn->r);
	}
	--nconnections;
	close(cn->fd);
	if (cn->rfd != -1) {
		close(cn->rfd);
		cn->rfd = -1;
	}
	set_connection_state(cn, HC_FREE);
}

static void close_servers(void)
{
	struct server *s;

	s = servers;
	while (s) {
		if (s->fd != -1) {
			close(s->fd);
			s->fd = -1;
		}
		s = s->next;
	}
}

static void close_connections(void)
{
	struct connection *c, *n;

	c = waiting_connections.head;
	while (c) {
		n = c->next;
		close_connection(c);
		c = n;
	}
	c = reading_connections.head;
	while (c) {
		n = c->next;
		close_connection(c);
		c = n;
	}
	c = writing_connections.head;
	while (c) {
		n = c->next;
		close_connection(c);
		c = n;
	}
	c = forked_connections.head;
	while (c) {
		n = c->next;
		close_connection(c);
		c = n;
	}
}

static struct connection *find_connection(void)
{
	struct connection *c;

	if (free_connections.head)
		return free_connections.head;
	if (waiting_connections.tail == 0)
		return 0;
	c = waiting_connections.tail;
	if (debug)
		log_d("clobbering connection to %s[%hu]", inet_ntoa(c->peer.sin_addr), ntohs(c->peer.sin_port));
	close_connection(c);
	return c;
}

static int accept_connection(struct server *s)
{
	struct sockaddr_in sa_remote, sa_local;
	socklen_t l;
	int fd;
	struct connection *cn;

	do {
		if (free_connections.head == 0 && waiting_connections.head == 0)
			return 0;
		l = sizeof sa_remote;
		fd = accept(s->fd, (struct sockaddr *) &sa_remote, &l);
		if (fd == -1) {
			if (errno != EAGAIN) {
				lerror("accept");
				return -1;
			}
			return 0;
		}
		if (debug)
			log_d("accept_connection: %d", fd);
		s->naccepts++;
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		fcntl(fd, F_SETFL, O_NONBLOCK);
		l = sizeof sa_local;
		if (getsockname(fd, (struct sockaddr *) &sa_local, &l) == -1) {
			lerror("getsockname");
			close(fd);
			break;
		}
		cn = find_connection();
		if (cn == 0) {
			log_d("connection to %s[%hu] dropped", inet_ntoa(sa_remote.sin_addr), ntohs(sa_remote.sin_port));
			close(fd);
		} else {
			s->nhandled++;
			cn->s = s;
			cn->fd = fd;
			cn->rfd = -1;
			cn->peer = sa_remote;
			cn->sock = sa_local;
			cn->t = current_time;
			cn->pollno = -1;
			++nconnections;
			if (nconnections > maxconnections)
				maxconnections = nconnections;
			init_connection(cn);
			cn->logged = 0;
			set_connection_state(cn, HC_WAITING);
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
	p = &cn->output;
	poolleft = p->ceiling - p->end;
	fileleft = cn->left;
	n = fileleft > poolleft ? poolleft : (int) fileleft;
	if (n <= 0)
		return 0;
	cn->left -= n;
	m = read(cn->rfd, p->end, n);
	if (debug)
		log_d("fill_connection: %d %d %d %d", cn->rfd, p->end - p->floor, n, m);
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

	p = &cn->output;
	do {
		n = p->end - p->start;
		if (n == 0) {
			p->start = p->end = p->floor;
			n = fill_connection(cn);
			if (n <= 0) {
				if (n == 0 && cn->keepalive)
					reinit_connection(cn);
				else
					close_connection(cn);
				return;
			}
		}
		cn->t = current_time;
		m = send(cn->fd, p->start, n, 0);
		if (debug)
			log_d("write_connection: %d %d %d %d", cn->fd, p->start - p->floor, n, m);
		if (m == -1) {
			switch (errno) {
			default:
				log_d("error sending to %s[%hu]", inet_ntoa(cn->peer.sin_addr), ntohs(cn->peer.sin_port));
				lerror("send");
			case ECONNRESET:
			case EPIPE:
				close_connection(cn);
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

	p = &cn->header_input;
	state = p->state;
	fd = cn->fd;
	i = p->ceiling - p->end;
	if (i == 0) {
		log_d("input buffer full");
		close_connection(cn);
		return;
	}
	nr = recv(fd, p->end, i, MSG_PEEK);
	if (debug)
		log_d("read_connection (peek): %d %d %d %d", fd, p->end - p->floor, i, nr);
	if (nr == -1) {
		switch (errno) {
		default:
			log_d("error peeking from %s[%hu]", inet_ntoa(cn->peer.sin_addr), ntohs(cn->peer.sin_port));
			lerror("recv");
		case ECONNRESET:
		case EPIPE:
			close_connection(cn);
		case EAGAIN:
			return;
		}
	}
	if (nr == 0) {
		close_connection(cn);
		return;
	}
	i = 0;
	while (i < nr && state < 8) {
		c = p->end[i++];
		if (c == 0) {
			log_d("read_connection: NUL in headers");
			close_connection(cn);
			return;
		}
		switch (state) {
		case 0:
			switch (c) {
			default:
				state = 1;
				break;
			case '\r':
			case '\n':
				break;
			case ' ':
			case '\t':
				state = 2;
				break;
			}
			if (state) {
				gettimeofday(&cn->itv, 0);
				set_connection_state(cn, HC_READING);
			}
			break;
		case 1:
			switch (c) {
			default:
				break;
			case ' ':
			case '\t':
				state = 2;
				break;
			case '\r':
				state = 3;
				break;
			case '\n':
				state = 8;
				break;
			}
			break;
		case 2:
			switch (c) {
			case 'H':
				state = 4;
				break;
			default:
				state = 1;
				break;
			case ' ':
			case '\t':
				break;
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
			case '\n':
				state = 8;
				break;
			default:
				state = 1;
				break;
			case ' ':
			case '\t':
				state = 2;
				break;
			case '\r':
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
		log_d("read_connection: %d %d %d %d", fd, p->end - p->floor, i, nr);
	if (nr != i) {
		if (nr == -1) {
			log_d("error reading from %s[%hu]", inet_ntoa(cn->peer.sin_addr), ntohs(cn->peer.sin_port));
			lerror("recv");
		} else {
			cn->nread += nr;
			log_d("read_connection: %d != %d!", nr, i);
		}
		close_connection(cn);
		return;
	}
	cn->nread += nr;
	p->end += i;
	p->state = state;
	cn->t = current_time;
	if (state == 8) {
		if (process_request(cn->r) == -1) {
			if (cn->connection_state != HC_FORKED)
				close_connection(cn);
			return;
		}
		cn->left = cn->r->content_length;
		if (fill_connection(cn) == -1) {
			close_connection(cn);
			return;
		}
		set_connection_state(cn, HC_WRITING);
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

	cn = waiting_connections.head;
	while (cn) {
		pollfds[n].fd = cn->fd;
		pollfds[n].events = POLLIN;
		cn->pollno = n++;
		cn = cn->next;
	}
	cn = reading_connections.head;
	while (cn) {
		pollfds[n].fd = cn->fd;
		pollfds[n].events = POLLIN;
		cn->pollno = n++;
		cn = cn->next;
	}
	cn = writing_connections.head;
	while (cn) {
		pollfds[n].fd = cn->fd;
		pollfds[n].events = POLLOUT;
		cn->pollno = n++;
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

void log_socket_error(int fd, const char *s)
{
	int e, errno_save;
	socklen_t l;

	errno_save = errno;
	l = sizeof e;
	getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &l);
	errno = e;
	lerror(s);
	errno = errno_save;
}

static void log_connection_error(struct connection *cn)
{
	char buf[80];

	sprintf(buf, "error on connection to %s[%hu]", inet_ntoa(cn->peer.sin_addr), ntohs(cn->peer.sin_port));
	log_socket_error(cn->fd, buf);
}

static void run_rconnection(struct connection *cn)
{
	int n;
	short r;

	n = cn->pollno;
	if (n == -1)
		return;
	r = pollfds[n].revents;
	if (r & POLLERR) {
		log_connection_error(cn);
		close_connection(cn);
		return;
	}
	if (r & POLLIN) {
		read_connection(cn);
		r &= ~POLLIN;
		if (cn->connection_state == HC_WRITING)
			r |= POLLOUT;
		pollfds[n].revents = r;
	}
}

static void run_wconnection(struct connection *cn)
{
	int n;
	short r;

	n = cn->pollno;
	if (n == -1)
		return;
	r = pollfds[n].revents;
	if (r & POLLERR) {
		log_connection_error(cn);
		close_connection(cn);
		return;
	}
	if (r & POLLOUT)
		write_connection(cn);
}

static void run_connections(void)
{
	struct connection *c, *n;

	c = waiting_connections.head;
	while (c) {
		n = c->next;
		run_rconnection(c);
		c = n;
	}
	c = reading_connections.head;
	while (c) {
		n = c->next;
		run_rconnection(c);
		c = n;
	}
	c = writing_connections.head;
	while (c) {
		n = c->next;
		run_wconnection(c);
		c = n;
	}
	c = forked_connections.head;
	while (c) {
		n = c->next;
		pipe_run(c);
		c = n;
	}
}

static void timeout_connections(struct connection *c, time_t t, const char *what)
{
	struct connection *n;

	while (c) {
		n = c->next;
		if (current_time >= c->t + t) {
			if (what)
				log_d("%s timeout to %s[%hu]", what, inet_ntoa(c->peer.sin_addr), ntohs(c->peer.sin_port));
			close_connection(c);
		}
		c = n;
	}
}

static void cleanup_connections(void)
{
	timeout_connections(waiting_connections.head, tuning.timeout, 0);
	timeout_connections(reading_connections.head, tuning.timeout, "read");
	timeout_connections(writing_connections.head, tuning.timeout, "write");
	timeout_connections(forked_connections.head, tuning.script_timeout, "script");
}

static void reap_children(void)
{
	pid_t pid;
	int errno_save, status, exitstatus, termsig;

	errno_save = errno;
	while (1) {
		pid = waitpid(-1, &status, WNOHANG);
		if (pid <= 0)
			break;
		if (WIFEXITED(status)) {
			++numchildren;
			exitstatus = WEXITSTATUS(status);
			if (exitstatus || debug)
				log_d("child process %d exited with status %d", pid, exitstatus);
		} else if (WIFSIGNALED(status)) {
			++numchildren;
			termsig = WTERMSIG(status);
			log_d("child process %d killed by signal %d", pid, termsig);
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
			init_logs(0);
			if (debug)
				log_d("logs reopened");
		}
		if (gotsigusr1) {
			gotsigusr1 = 0;
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
		if (gotsigwinch) {
			gotsigwinch = 0;
			log_d("performing internal dump");
			internal_dump();
		}
		n = 0;
		if (accepting && (free_connections.head || waiting_connections.head))
			n = setup_server_pollfds(n);
		n = setup_connection_pollfds(n);
		n = setup_child_pollfds(n, forked_connections.head);
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
				init_logs(0);
				if (debug)
					log_d("logs rotated");
			}
		}
		if (rv) {
			if (accepting && run_servers() == -1)
				accepting = 0;
			run_connections();
		}
		cleanup_connections();
	}
	log_d("*** shutting down");
}

int init_pollfds(size_t n)
{
	pollfds = realloc(pollfds, n * sizeof *pollfds);
	return pollfds == 0 ? -1 : 0;
}

int new_pool(struct pool *p, size_t s)
{
	char *t;

	t = malloc(s);
	if (t == 0)
		return -1;
	p->floor = t;
	p->ceiling = t + s;
	return 0;
}

int init_connections(size_t n)
{
	size_t i;
	struct connection *cn;

	connection_array = malloc(n * sizeof *connection_array);
	if (connection_array == 0)
		return -1;
	for (i = 0; i < n; i++) {
		cn = connection_array + i;
		if ((cn->r = malloc(sizeof *cn->r)) == 0)
			return -1;
		if (tuning.num_headers == 0) {
			cn->r->headers = 0;
			cn->pipe_params.cgi_headers = 0;
		}
		else {
			if ((cn->r->headers = malloc(tuning.num_headers * sizeof *cn->r->headers)) == 0)
				return -1;
			if ((cn->pipe_params.cgi_headers = malloc(tuning.num_headers * sizeof *cn->pipe_params.cgi_headers)) == 0)
				return -1;
		}
		if (new_pool(&cn->header_input, tuning.input_buf_size) == -1)
			return -1;
		if (new_pool(&cn->output, tuning.buf_size) == -1)
			return -1;
		if (new_pool(&cn->client_input, tuning.script_buf_size) == -1)
			return -1;
		if (new_pool(&cn->script_input, tuning.script_buf_size) == -1)
			return -1;
		cn->r->cn = cn;
		cn->connection_state = HC_UNATTACHED;
		set_connection_state(cn, HC_FREE);
	}
	return 0;
}
