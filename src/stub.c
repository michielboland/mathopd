/*
 *   Copyright 2000-2003 Michiel Boland.
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

/* Careful, man, there's a beverage here! */

static const char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sysexits.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "mathopd.h"

#define PLBUFSIZE 4096

struct pipe_params {
	char *ibuf;
	char *obuf;
	char *pbuf;
	size_t isize;
	size_t osize;
	size_t psize;
	size_t ibp;
	size_t obp;
	size_t ipp;
	size_t opp;
	size_t otop;
	int istate;
	int pstate;
	size_t pstart;
	int state;
	unsigned long cir;
	unsigned long cow;
	unsigned long cpr;
	unsigned long cpw;
	int ifd;
	int ofd;
	int fd;
	int timeout;
};

struct cgi_header {
	const char *name;
	size_t namelen;
	const char *value;
	size_t valuelen;
};

static int convert_cgi_headers(const char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t *r)
{
	int s;
	int c;
	struct cgi_header headers[100];
	size_t n;
	size_t i;
	const char *p, *tmpname;
	int status, location;
	size_t l, tmpnamelen, tmpvaluelen;

	tmpname = 0;
	tmpnamelen = tmpvaluelen = 0;
	if (insize >= 5 && memcmp(inbuf, "HTTP/", 5) == 0) {
		if (outsize < insize) {
			log_d("convert_cgi_headers: output buffer is too small");
			return -1;
		}
		log_d("convert_cgi_headers: NPH script detected");
		memcpy(outbuf, inbuf, insize);
		*r = insize;
		return 0;
	}
	s = 0;
	n = 0;
	l = 0;
	status = -1;
	location = -1;
	for (i = 0, p = inbuf; i < insize; i++, p++) {
		c = *p;
		switch (s) {
		case 0:
			switch (c) {
			case '\r':
			case '\n':
				break;
			default:
				tmpname = p;
				l = 0;
				s = 1;
				break;
			}
			break;
		case 1:
			++l;
			switch (c) {
			case '\r':
			case '\n':
				s = 0;
				break;
			case ':':
				tmpnamelen = l;
				s = 2;
				break;
			}
			break;
		case 2:
			switch (c) {
			case '\r':
			case '\n':
				s = 0;
				break;
			case ' ':
			case '\t':
				break;
			default:
				if (n == 100) {
					log_d("convert_cgi_headers: too many header lines");
					return -1;
				}
				headers[n].name = tmpname;
				headers[n].namelen = tmpnamelen;
				headers[n].value = p;
				l = 0;
				s = 3;
				break;
			}
			break;
		case 3:
			++l;
			switch (c) {
			case '\r':
			case '\n':
				headers[n].valuelen = l;
				s = 0;
				++n;
				break;
			}
			break;
		}
	}
	if (s) {
		log_d("convert_cgi_headers: s=%d!?", s);
		return -1;
	}
	for (i = 0; i < n; i++) {
		if (headers[i].namelen == 6 && strncasecmp(headers[i].name, "Status", 6) == 0)
			status = i;
		else if (headers[i].namelen == 8 && strncasecmp(headers[i].name, "Location", 8) == 0)
			location = i;
	}
	l = 0;
	if (location != -1 && status == -1) {
		if (l + 20 > outsize) {
			log_d("convert_cgi_headers: no room to put Moved line");
			return -1;
		}
		memcpy(outbuf + l, "HTTP/1.0 302 Moved\r\n", 20);
		l += 20;
	} else if (status == -1) {
		if (l + 17 > outsize) {
			log_d("convert_cgi_headers: no room to put OK line");
			return -1;
		}
		memcpy(outbuf + l, "HTTP/1.0 200 OK\r\n", 17);
		l += 17;
	} else {
		if (l + headers[status].valuelen + 11 > outsize) {
			log_d("convert_cgi_headers: no room to put status line");
			return -1;
		}
		memcpy(outbuf + l, "HTTP/1.0 ", 9);
		l += 9;
		memcpy(outbuf + l, headers[status].value, headers[status].valuelen);
		l += headers[status].valuelen;
		outbuf[l++] = '\r';
		outbuf[l++] = '\n';
	}
	for (i = 0; i < n; i++) {
		if (i != status) {
			if (l + headers[i].namelen + headers[i].valuelen + 4 > outsize) {
				log_d("convert_cgi_headers: no room to put header");
				return -1;
			}
			memcpy(outbuf + l, headers[i].name, headers[i].namelen);
			l += headers[i].namelen;
			outbuf[l++] = ':';
			outbuf[l++] = ' ';
			memcpy(outbuf + l, headers[i].value, headers[i].valuelen);
			l += headers[i].valuelen;
			outbuf[l++] = '\r';
			outbuf[l++] = '\n';
		}
	}
	if (l + 2 > outsize) {
		log_d("convert_cgi_headers: no room to put trailing newline");
		return -1;
	}
	outbuf[l++] = '\r';
	outbuf[l++] = '\n';
	*r = l;
	return 0;
}

static int pipe_run(struct pipe_params *p)
{
	int done;
	int skip_poll;
	struct pollfd pollfds[3];
	char c;
	ssize_t r;
	size_t room;
	size_t bytestocopy;
	int n;
	int convert_result;

	done = 1;
	skip_poll = 1;
	pollfds[0].fd = -1;
	pollfds[1].fd = -1;
	pollfds[2].fd = -1;
	pollfds[0].events = 0;
	pollfds[1].events = 0;
	pollfds[2].events = 0;
	pollfds[0].revents = 0;
	pollfds[1].revents = 0;
	pollfds[2].revents = 0;
	if (p->istate == 1 && p->ibp < p->isize) {
		done = 0;
		skip_poll = 0;
		pollfds[0].fd = p->ifd;
		pollfds[0].events |= POLLIN;
	}
	if (p->otop > p->obp) {
		done = 0;
		if (p->ofd == -1)
			p->obp = p->otop = 0;
		else {
			skip_poll = 0;
			pollfds[1].fd = p->ofd;
			pollfds[1].events |= POLLOUT;
		}
	}
	if (p->pstate == 1 && p->ipp < p->psize) {
		done = 0;
		skip_poll = 0;
		pollfds[2].fd = p->fd;
		pollfds[2].events |= POLLIN;
	}
	if (p->ibp > p->opp) {
		done = 0;
		skip_poll = 0;
		pollfds[2].fd = p->fd;
		pollfds[2].events |= POLLOUT;
	}
	if (done)
		return 1;
	n = skip_poll ? 1 : poll(pollfds, 3, p->timeout);
	if (gotsigchld) {
		gotsigchld = 0;
		reap_children();
	}
	if (n == -1) {
		if (errno == EINTR)
			return 0;
		lerror("poll");
		return 1;
	}
	if (n == 0) {
		log_d("pipe_loop: timeout");
		return 1;
	}
	if (pollfds[0].revents & POLLIN) {
		r = read(p->ifd, p->ibuf + p->ibp, p->isize - p->ibp);
		switch (r) {
		case -1:
			if (errno == EAGAIN)
				break;
			p->ofd = -1;
			lerror("pipe_loop: error reading from stdin");
		case 0:
			p->istate = 2;
			break;
		default:
			p->ibp += r;
			p->cir += r;
			break;
		}
	}
	if (pollfds[1].revents & POLLOUT && p->ofd != -1) {
		r = write(p->ofd, p->obuf + p->obp, p->otop - p->obp);
		switch (r) {
		case -1:
			if (errno == EAGAIN)
				break;
			p->ofd = -1;
			lerror("pipe_loop: error writing to stdout");
			break;
		default:
			p->cow += r;
			p->obp += r;
			break;
		}
	}
	if (pollfds[2].revents & POLLIN) {
		r = read(p->fd, p->pbuf + p->ipp, p->psize - p->ipp);
		switch (r) {
		case -1:
			if (errno == EAGAIN)
				break;
			lerror("pipe_loop: error reading from pipe");
		case 0:
			if (p->state == 0) {
				log_d("pipe_loop: premature end of script headers");
				return 1;
			}
			p->pstate = 2;
			break;
		default:
			p->cpr += r;
			p->ipp += r;
			break;
		}
	}
	if (pollfds[2].revents & POLLOUT) {
		r = write(p->fd, p->ibuf + p->opp, p->ibp - p->opp);
		switch (r) {
		case -1:
			if (errno == EAGAIN)
				break;
			lerror("pipe_loop: error writing to pipe");
			return 1;
		default:
			p->cpw += r;
			p->opp += r;
			break;
		}
	}
	if (p->opp == p->ibp) {
		if (p->istate == 2) {
			if (shutdown(p->fd, 1) == -1) {
				lerror("pipe_loop: error closing pipe");
				return 1;
			}
			p->istate = 0;
		}
		if (p->opp)
			p->opp = p->ibp = 0;
	}
	if (p->obp && p->obp == p->otop)
		p->obp = p->otop = 0;
	if (p->ipp && p->state != 2) {
		while (p->pstart < p->ipp && p->state != 2) {
			c = p->pbuf[p->pstart++];
			switch (p->state) {
			case 0:
				if (c == '\n')
					p->state = 1;
				break;
			case 1:
				switch (c) {
				case '\r':
					break;
				case '\n':
					p->state = 2;
					break;
				default:
					p->state = 0;
					break;
				}
				break;
			}
		}
		if (p->state == 2) {
			convert_result = convert_cgi_headers(p->pbuf, p->pstart, p->obuf, p->osize, &p->otop);
			if (convert_result == -1) {
				log_d("pipe_loop: problem in convert_cgi_headers");
				return 1;
			}
		} else if (p->pstart == p->psize) {
			log_d("pipe_loop: too many headers");
			return 1;
		}
	}
	if (p->state == 2 && p->pstart < p->ipp) {
		room = p->osize - p->otop;
		bytestocopy = p->ipp - p->pstart;
		if (bytestocopy > room)
			bytestocopy = room;
		if (bytestocopy) {
			memcpy(p->obuf + p->otop, p->pbuf + p->pstart, bytestocopy);
			p->otop += bytestocopy;
			p->pstart += bytestocopy;
			if (p->pstart == p->ipp)
				p->pstart = p->ipp = 0;
		}
	}
	if (p->otop == 0 && p->pstate == 2) {
		if (p->ofd != -1 && shutdown(p->ofd, 1) == -1) {
			lerror("pipe_loop: error closing stdout");
			return 1;
		}
		p->pstate = 0;
	}
	return 0;
}

void pipe_loop(int fd, int cfd, int timeout)
{
	char ibuf[PLBUFSIZE];
	char obuf[PLBUFSIZE];
	char pbuf[PLBUFSIZE];
	struct pipe_params p;

	p.ibuf = ibuf;
	p.obuf = obuf;
	p.pbuf = pbuf;
	p.isize = sizeof ibuf;
	p.osize = sizeof obuf;
	p.psize = sizeof pbuf;
	p.ibp = 0;
	p.obp = 0;
	p.ipp = 0;
	p.opp = 0;
	p.otop = 0;
	p.istate = 1;
	p.pstate = 1;
	p.pstart = 0;
	p.state = 0;
	p.cir = 0;
	p.cow = 0;
	p.cpr = 0;
	p.cpw = 0;
	p.ifd = cfd;
	p.ofd = cfd;
	p.fd = fd;
	timeout *= 1000;
	p.timeout = timeout;
	while (pipe_run(&p) == 0)
		;
	if (debug)
		log_d("pipe_loop: cir=%lu cpw=%lu cpr=%lu cow=%lu", p.cir, p.cpw, p.cpr, p.cow);
}

int cgi_stub(struct request *r, int (*f)(struct request *))
{
	int p[2], efd;
	pid_t pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, p) == -1) {
		lerror("socketpair");
		return -1;
	}
	pid = fork();
	switch (pid) {
	case -1:
		lerror("fork");
		return -1;
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
	default:
		if (debug)
			log_d("cgi_stub: child process %d created", pid);
		close(p[1]);
		fcntl(p[0], F_SETFL, O_NONBLOCK);
		pipe_loop(p[0], r->cn->fd, 3600);
		break;
	}
	return 0;
}
