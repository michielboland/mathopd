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
#include <stdlib.h>
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
	int ifd;
	int ofd;
	int fd;
	int timeout;
};

struct cgi_header {
	const char *name;
	size_t namelen;
	const char *value;
	size_t len;
};

static int convert_cgi_headers(const char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t *r, int *sp)
{
	int addheader, c, s;
	struct cgi_header headers[100];
	size_t i, nheaders, status, location;
	const char *p, *tmpname, *tmpvalue;
	int havestatus, havelocation;
	size_t len, tmpnamelen, tmpvaluelen;
	char sbuf[40], dbuf[50], gbuf[40];

	headers[0].len = sprintf(sbuf, "Server: %.30s", server_version);
	headers[0].name = sbuf;
	headers[0].namelen = 6;
	headers[0].value = sbuf + 8;
	headers[1].len = sprintf(dbuf, "Date: %s", rfctime(current_time, gbuf));
	headers[1].name = dbuf;
	headers[1].namelen = 4;
	headers[2].value = dbuf + 6;
	tmpname = 0;
	tmpnamelen = 0;
	tmpvalue = 0;
	havestatus = 0;
	havelocation = 0;
	status = 0;
	location = 0;
	s = 0;
	nheaders = 2;
	len = 0;
	addheader = 0;
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
				tmpnamelen = 0;
				tmpvalue = 0;
				len = 0;
				s = 1;
				break;
			}
			break;
		case 1:
			++len;
			switch (c) {
			case '\r':
			case '\n':
				addheader = 1;
				s = 0;
				break;
			case ':':
			case ' ':
			case '\t':
				if (tmpnamelen == 0)
					tmpnamelen = len;
				break;
			default:
				if (tmpnamelen && tmpvalue == 0)
					tmpvalue = p;
				break;
			}
			break;
		}
		if (addheader) {
			if (tmpvalue == 0)
				addheader = 0;
			else
				switch (tmpnamelen) {
				case 4:
					if (strncasecmp(tmpname, "Date", 4) == 0)
						addheader = 0;
					break;
				case 6:
					if (strncasecmp(tmpname, "Server", 6) == 0)
						addheader = 0;
					else if (strncasecmp(tmpname, "Status", 6) == 0) {
						if (havestatus)
							addheader = 0;
						else {
							status = nheaders;
							havestatus = 1;
						}
					}
					break;
				case 8:
					if (strncasecmp(tmpname, "Location", 8) == 0) {
						if (havelocation)
							addheader = 0;
						else {
							location = nheaders;
							havelocation = 1;
						}
					} else if (strncmp(tmpname, "HTTP/", 5) == 0) {
						if (havestatus)
							addheader = 0;
						else {
							status = nheaders;
							havestatus = 1;
						}
					}
					break;
				}
			if (addheader == 0) {
				if (debug)
					log_d("convert_cgi_headers: disallowing header \"%.*s\"", len, tmpname);
			} else {
				if (nheaders == 100) {
					log_d("convert_cgi_headers: too many header lines");
					return -1;
				}
				headers[nheaders].name = tmpname;
				headers[nheaders].value = tmpvalue;
				headers[nheaders].namelen = tmpnamelen;
				headers[nheaders++].len = len;
				addheader = 0;
			}
		}
	}
	if (s) {
		log_d("convert_cgi_headers: s=%d!?", s);
		return -1;
	}
	len = 0;
	if (havelocation && havestatus == 0) {
		if (len + 20 > outsize) {
			log_d("convert_cgi_headers: no room to put Moved line");
			return -1;
		}
		s = 302;
		memcpy(outbuf + len, "HTTP/1.0 302 Moved\r\n", 20);
		len += 20;
	} else if (havestatus == 0) {
		if (len + 17 > outsize) {
			log_d("convert_cgi_headers: no room to put OK line");
			return -1;
		}
		s = 200;
		memcpy(outbuf + len, "HTTP/1.0 200 OK\r\n", 17);
		len += 17;
	} else {
		s = atoi(headers[status].value);
		if (s < 100 || s > 999) {
			log_d("convert_cgi_headers: illegal header line \"%.*s\"", headers[status].len, headers[status].name);
			return -1;
		}
		tmpvaluelen = headers[status].len - (headers[status].value - headers[status].name);
		if (len + tmpvaluelen + 11 > outsize) {
			log_d("convert_cgi_headers: no room to put status line");
			return -1;
		}
		memcpy(outbuf + len, "HTTP/1.0 ", 9);
		len += 9;
		memcpy(outbuf + len, headers[status].value, tmpvaluelen);
		len += tmpvaluelen;
		outbuf[len++] = '\r';
		outbuf[len++] = '\n';
	}
	for (i = 0; i < nheaders; i++) {
		if (havestatus == 0 || i != status) {
			if (len + headers[i].len + 2 > outsize) {
				log_d("convert_cgi_headers: no room to put header");
				return -1;
			}
			memcpy(outbuf + len, headers[i].name, headers[i].len);
			len += headers[i].len;
			outbuf[len++] = '\r';
			outbuf[len++] = '\n';
		}
	}
	if (len + 2 > outsize) {
		log_d("convert_cgi_headers: no room to put trailing newline");
		return -1;
	}
	outbuf[len++] = '\r';
	outbuf[len++] = '\n';
	*r = len;
	*sp = s;
	return 0;
}

static int pipe_run(struct pipe_params *p, struct connection *cn)
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
	current_time = time(0);
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
			cn->nread += r;
			p->ibp += r;
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
			cn->nwritten += r;
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
			convert_result = convert_cgi_headers(p->pbuf, p->pstart, p->obuf, p->osize, &p->otop, &cn->r->status);
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

static int pipe_loop(int fd, struct connection *cn, int timeout)
{
	char ibuf[PLBUFSIZE];
	char obuf[PLBUFSIZE];
	char pbuf[PLBUFSIZE];
	struct pipe_params p;
	int rv;

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
	p.ifd = cn->fd;
	p.ofd = cn->fd;
	p.fd = fd;
	timeout *= 1000;
	p.timeout = timeout;
	do
		rv = pipe_run(&p, cn);
	while (rv == 0);
	return rv;
}

int cgi_stub(struct request *r, int (*f)(struct request *))
{
	int p[2], efd, rv;
	pid_t pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, p) == -1) {
		lerror("socketpair");
		rv = -1;
	} else {
		pid = fork();
		switch (pid) {
		case -1:
			lerror("fork");
			rv = -1;
			break;
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
			rv = pipe_loop(p[0], r->cn, 3600);
			break;
		}
	}
	log_request(r);
	return rv;
}
