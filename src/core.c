/*
 *   Copyright 1996 - 2005 Michiel Boland.
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
#include <netinet/tcp.h>
#include <netdb.h>
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
#include <string.h>
#include "mathopd.h"

#ifdef NEED_SOCKLEN_T
typedef int socklen_t;
#endif

struct statistics stats;
time_t startuptime;
time_t current_time;
struct pollfd *pollfds;
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

static struct connection_list *clistp(enum connection_state state)
{
	switch (state) {
	default:
		return 0;
	case HC_FREE:
		return &free_connections;
	case HC_WAITING:
		return &waiting_connections;
	case HC_READING:
		return &reading_connections;
	case HC_WRITING:
		return &writing_connections;
	case HC_FORKED:
		return &forked_connections;
	}
}

void set_connection_state(struct connection *c, enum connection_state state)
{
	enum connection_state oldstate;
	struct connection_list *o, *n;

	oldstate = c->connection_state;
	if (state == oldstate)
		return;
	o = clistp(oldstate);
	n = clistp(state);
	if (o)
		c_unlink(c, o);
	c->connection_state = state;
	if (n)
		c_link(c, n);
}

static void init_connection(struct connection *cn)
{
	cn->header_input.state = 0;
	cn->output.start = cn->output.end = cn->output.floor;
	init_request(cn->r);
	cn->keepalive = 0;
	cn->nread = 0;
	cn->nwritten = 0;
	cn->left = 0;
	cn->havefile = 0;
	gettimeofday(&cn->itv, 0);
}

static int scan_request(struct connection *cn);

int reinit_connection(struct connection *cn)
{
	char *s;

	++stats.nrequests;
	log_request(cn->r);
	cn->logged = 1;
	if (cn->rfd != -1) {
		close(cn->rfd);
		cn->rfd = -1;
	}
	init_connection(cn);
	set_connection_state(cn, HC_WAITING);
	s = cn->header_input.middle;
	if (s == cn->header_input.end) {
		cn->header_input.start = cn->header_input.middle = cn->header_input.end = cn->header_input.floor;
		return 0;
	}
	++stats.pipelined_requests;
	cn->header_input.start = s;
	return scan_request(cn);
}

void close_connection(struct connection *cn)
{
	if (cn->nread || cn->nwritten || cn->logged == 0) {
		++stats.nrequests;
		log_request(cn->r);
	}
	--stats.nconnections;
	if (debug)
		log_d("close_connection: %d", cn->fd);
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
	if (free_connections.head)
		return free_connections.head;
	if (tuning.clobber)
		return waiting_connections.tail;
	return 0;
}

static void pool_adjust(struct pool *p, size_t s)
{
	size_t n;

	n = p->size;
	if (s > 0 && s < n)
		n -= n % s;
	if (debug)
		log_d("pool_adjust: n=%d", n);
	p->ceiling = p->floor + n;
}

static int accept_connection(struct server *s)
{
	struct sockaddr_storage sa_remote, sa_local;
	socklen_t l;
	int fd, rv, mss;
	struct connection *cn;

	do {
		cn = find_connection();
		if (cn == 0)
			return 0;
		l = sizeof sa_remote;
		fd = accept(s->fd, (struct sockaddr *) &sa_remote, &l);
		if (fd == -1)
			switch (errno) {
			case EAGAIN:
				return 0;
#ifdef ECONNABORTED
			case ECONNABORTED:
#endif
#ifdef EPROTO
			case EPROTO:
#endif
				lerror("accept");
				continue;
			default:
				lerror("accept");
				return -1;
			}
		++stats.accepted_connections;
		if (debug)
			log_d("accept_connection: %d", fd);
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		fcntl(fd, F_SETFL, O_NONBLOCK);
		if (cn->connection_state != HC_FREE) {
			if (debug)
				log_d("clobbering connection to %s[%s]", cn->peer.ap_address, cn->peer.ap_port);
			close_connection(cn);
		}
		rv = getnameinfo((struct sockaddr *) &sa_remote, l, cn->peer.ap_address, sizeof cn->peer.ap_address, cn->peer.ap_port, sizeof cn->peer.ap_port, NI_NUMERICHOST | NI_NUMERICSERV);
		if (rv) {
			log_d("accept_connection: getnameinfo failed for peer: %s", gai_strerror(rv));
			close(fd);
			break;
		}
		l = sizeof sa_local;
		if (getsockname(fd, (struct sockaddr *) &sa_local, &l) == -1) {
			lerror("getsockname");
			close(fd);
			break;
		}
		rv = getnameinfo((struct sockaddr *) &sa_local, l, cn->sock.ap_address, sizeof cn->sock.ap_address, cn->sock.ap_port, sizeof cn->sock.ap_port, NI_NUMERICHOST | NI_NUMERICSERV);
		if (rv) {
			log_d("accept_connection: getnameinfo failed for sock: %s", gai_strerror(rv));
			close(fd);
			break;
		}
		l = sizeof mss;
		if (getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &mss, &l) == -1) {
			lerror("getsockopt");
			close(fd);
			break;
		}
		cn->s = s;
		cn->fd = fd;
		cn->rfd = -1;
		cn->t = current_time;
		cn->pollno = -1;
		++stats.nconnections;
		if (stats.nconnections > stats.maxconnections)
			stats.maxconnections = stats.nconnections;
		pool_adjust(&cn->output, mss);
		init_connection(cn);
		cn->logged = 0;
		cn->header_input.start = cn->header_input.middle = cn->header_input.end = cn->header_input.floor;
		set_connection_state(cn, HC_WAITING);
	} while (tuning.accept_multi);
	return 0;
}

#if ! (defined LINUX_SENDFILE || defined FREEBSD_SENDFILE)
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
	cn->file_offset += n;
	return n;
}
#endif

static void end_response(struct connection *cn)
{
#if defined LINUX_SENDFILE || defined FREEBSD_SENDFILE
	if (cn->havefile)
		if (set_nopush(cn->fd, 0) == -1) {
			if (debug)
				lerror("set_nopush");
			close_connection(cn);
			return;
		}
#endif
	if (cn->keepalive)
		reinit_connection(cn);
	else
		close_connection(cn);
}

static void write_connection(struct connection *cn)
{
	struct pool *p;
	int m, n;

	p = &cn->output;
	do {
		n = p->end - p->start;
		if (n == 0) {
#if defined LINUX_SENDFILE || defined FREEBSD_SENDFILE
			if (sendfile_connection(cn) == -1) {
				close_connection(cn);
				return;
			}
			if (cn->left == 0)
		       		end_response(cn);
			return;
#else
			p->start = p->end = p->floor;
			n = fill_connection(cn);
			if (n == -1) {
				close_connection(cn);
				return;
			}
			if (n == 0) {
				end_response(cn);
				return;
			}
#endif
		}
		m = write(cn->fd, p->start, n);
		if (debug)
			log_d("write_connection: %d %d %d %d", cn->fd, p->start - p->floor, n, m);
		if (m == -1) {
			if (errno == EAGAIN)
				return;
			if (debug)
				lerror("write");
			close_connection(cn);
			return;
		}
		cn->t = current_time;
		cn->nwritten += m;
		p->start += m;
	} while (n == m);
}

static int read_connection(struct connection *cn)
{
	size_t bytestoread, bytestomove, offset;
	ssize_t nr;

	bytestoread = cn->header_input.ceiling - cn->header_input.end;
	if (bytestoread == 0) {
		offset = cn->header_input.start - cn->header_input.floor;
		if (offset == 0) {
			log_d("input buffer full");
			close_connection(cn);
			return -1;
		}
		bytestomove = cn->header_input.end - cn->header_input.start;
		memmove(cn->header_input.floor, cn->header_input.start, bytestomove);
		cn->header_input.start -= offset;
		cn->header_input.middle -= offset;
		cn->header_input.end -= offset;
		bytestoread = cn->header_input.ceiling - cn->header_input.end;
	}
	nr = read(cn->fd, cn->header_input.end, bytestoread);
	if (debug)
		log_d("read_connection: %d %d %d %d", cn->fd, cn->header_input.end - cn->header_input.floor, bytestoread, nr);
	if (nr == -1) {
		if (errno == EAGAIN)
			return 0;
		if (debug)
			lerror("read");
		close_connection(cn);
		return -1;
	}
	if (nr == 0) {
		close_connection(cn);
		return -1;
	}
	cn->nread += nr;
	cn->header_input.end += nr;
	cn->t = current_time;
	return 0;
}

static int begin_response(struct connection *cn)
{
	if (cn->rfd == -1) {
		cn->left = 0;
		return 0;
	}
	cn->havefile = 1;
	cn->left = cn->r->content_length;
#if defined LINUX_SENDFILE || defined FREEBSD_SENDFILE
	if (set_nopush(cn->fd, 1) == -1) {
		if (debug)
			lerror("set_nopush");
		return -1;
	}
	return 0;
#else
	if (cn->file_offset)
		if (lseek(cn->rfd, cn->file_offset, SEEK_SET) == -1) {
			lerror("lseek");
			return -1;
		}
	return fill_connection(cn);
#endif
}

static int scan_request(struct connection *cn)
{
	char *s;
	int c, state;

	s = cn->header_input.middle;
	state = cn->header_input.state;
	while (state != 8 && s < cn->header_input.end) {
		c = *s++;
		if (c == 0) {
			log_d("read_connection: NUL in headers");
			close_connection(cn);
			return -1;
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
	cn->header_input.state = state;
	cn->header_input.middle = s;
	if (state == 8) {
		if (process_request(cn->r) == -1) {
			if (cn->connection_state != HC_FORKED) {
				close_connection(cn);
				return -1;
			}
			return 0;
		}
		if (begin_response(cn) == -1) {
			close_connection(cn);
			return -1;
		}
		set_connection_state(cn, HC_WRITING);
	}
	return 0;
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

static void run_rconnection(struct connection *cn)
{
	int n;
	short r;

	n = cn->pollno;
	if (n == -1)
		return;
	r = pollfds[n].revents;
	if (r & POLLERR || r == POLLHUP) {
		close_connection(cn);
		return;
	}
	if (r & POLLIN) {
		if (read_connection(cn) == -1 || scan_request(cn) == -1)
			return;
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
	if (r & POLLERR || r == POLLHUP) {
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
				log_d("%s timeout to %s[%s]", what, c->peer.ap_address, c->peer.ap_port);
			close_connection(c);
		}
		c = n;
	}
}

static void cleanup_connections(void)
{
	timeout_connections(waiting_connections.head, tuning.wait_timeout, debug ? "wait" : 0);
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
			++stats.exited_children;
			exitstatus = WEXITSTATUS(status);
			if (exitstatus || debug)
				log_d("child process %d exited with status %d", pid, exitstatus);
		} else if (WIFSIGNALED(status)) {
			++stats.exited_children;
			termsig = WTERMSIG(status);
			log_d("child process %d killed by signal %d", pid, termsig);
		} else
			log_d("child process %d stopped!?", pid);
	}
	if (pid < 0 && errno != ECHILD)
		lerror("waitpid");
	errno = errno_save;
}

static void dump_pollfds(int n, int r)
{
	char *buf, *b;
	int i;

	buf = malloc(5 * n + 10);
	if (buf == 0)
		return;
	if (r == 0) {
		b = buf + sprintf(buf, "fds:     ");
		for (i = 0; i < n; i++)
			b += sprintf(b, " %4d", pollfds[i].fd);
		log_d(buf);
		b = buf + sprintf(buf, "events:  ");
		for (i = 0; i < n; i++)
			b += sprintf(b, " %4hd", pollfds[i].events);
		log_d(buf);
	} else {
		b = buf + sprintf(buf, "revents: ");
		for (i = 0; i < n; i++)
			b += sprintf(b, " %4hd", pollfds[i].revents);
		log_d(buf);
	}
	free(buf);
}

void httpd_main(void)
{
	int rv, n, t, accepting;
	time_t hours, last_time;

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
		if (accepting && find_connection())
			n = setup_server_pollfds(n);
		n = setup_connection_pollfds(n);
		n = setup_child_pollfds(n, forked_connections.head);
		if (n == 0 && accepting && stats.nconnections == 0) {
			log_d("no more sockets to poll from");
			break;
		}
		t = accepting ? 60000 : 1000;
		if (debug)
			dump_pollfds(n, 0);
		rv = poll(pollfds, n, t);
		current_time = time(0);
		if (rv == -1) {
			if (errno != EINTR) {
				lerror("poll");
				break;
			} else {
				if (debug)
					log_d("poll interrupted");
				continue;
			}
		}
		if (debug)
			dump_pollfds(n, 1);
		if (current_time != last_time) {
			if (accepting == 0)
				accepting = 1;
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
		if (current_time != last_time) {
			cleanup_connections();
			last_time = current_time;
		}
	}
	log_d("*** shutting down");
}

int init_pollfds(size_t n)
{
	if (n == 0)
		return 0;
	pollfds = malloc(n * sizeof *pollfds);
	if (pollfds == 0) {
		log_d("init_pollfds: out of memory");
		return -1;
	}
	return 0;
}

static int new_pool(struct pool *p, size_t s)
{
	char *t;

	t = malloc(s);
	if (t == 0) {
		log_d("new_pool: out of memory");
		return -1;
	}
	p->floor = p->start = p->middle = p->end = t;
	p->ceiling = t + s;
	p->size = s;
	return 0;
}

int init_connections(size_t n)
{
	size_t i;
	struct connection *cn;

	connection_array = malloc(n * sizeof *connection_array);
	if (connection_array == 0) {
		log_d("init_connections: out of memory");
		return -1;
	}
	for (i = 0; i < n; i++) {
		cn = connection_array + i;
		if ((cn->r = malloc(sizeof *cn->r)) == 0) {
			log_d("init_connections: out of memory");
			return -1;
		}
		if (tuning.num_headers == 0)
			cn->r->headers = 0;
		else if ((cn->r->headers = malloc(tuning.num_headers * sizeof *cn->r->headers)) == 0) {
			log_d("init_connections: out of memory");
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
