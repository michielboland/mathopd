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
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sysexits.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "mathopd.h"

#define PLBUFSIZE 4080

struct cgi_header {
	const char *name;
	size_t namelen;
	const char *value;
	size_t len;
};

static struct cgi_header *cgi_headers;

struct pipe_params *children;

int init_children(size_t n)
{
	size_t i;
	struct pipe_params *p;

	if (tuning.num_headers) {
		cgi_headers = malloc(tuning.num_headers * sizeof *cgi_headers);
		if (cgi_headers == 0)
			return -1;
	}
	for (i = 0; i < n; i++){
		if ((p = malloc(sizeof *p)) == 0)
			return -1;
		p->isize = PLBUFSIZE;
		p->osize = PLBUFSIZE + 16;
		p->psize = PLBUFSIZE;
		if ((p->ibuf = malloc(p->isize)) == 0)
			return -1;
		if ((p->obuf = malloc(p->osize)) == 0)
			return -1;
		if ((p->pbuf = malloc(p->psize)) == 0)
			return -1;
		p->cn = 0;
		p->next = children;
		children = p;
	}
	return 0;
}

static int no_room(void)
{
	log_d("no room left for HTTP headers");
	return -1;
}

static int convert_cgi_headers(struct pipe_params *pp, int *sp)
{
	int addheader, c, s, l;
	size_t i, nheaders, status, location, length;
	const char *p, *tmpname, *tmpvalue;
	int havestatus, havelocation, firstline, havelength;
	size_t len, tmpnamelen, tmpvaluelen;
	char buf[50], gbuf[40], *cp;
	unsigned long ul;

	nheaders = 0;
	tmpname = 0;
	tmpnamelen = 0;
	tmpvalue = 0;
	havestatus = 0;
	havelocation = 0;
	havelength = 0;
	status = 0;
	location = 0;
	length = 0;
	s = 0;
	len = 0;
	addheader = 0;
	firstline = 1;
	for (i = 0, p = pp->pbuf; i < pp->pstart; i++, p++) {
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
			else if (firstline && tmpnamelen >= 8 && strncmp(tmpname, "HTTP/", 5) == 0) {
				status = nheaders;
				havestatus = 1;
			} else
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
					}
					break;
				case 10:
					if (strncasecmp(tmpname, "Connection", 10) == 0)
						addheader = 0;
					break;
				case 14:
					if (strncasecmp(tmpname, "Content-Length", 14) == 0) {
						if (havelength)
							addheader = 0;
						else {
							length = nheaders;
							havelength = 1;
						}
					}
					break;
				case 17:
					if (strncasecmp(tmpname, "Transfer-Encoding", 17) == 0) {
						log_d("convert_cgi_headers: script sent Transfer-Encoding!?!?");
						return -1;
					}
					break;
				}
			if (firstline)
				firstline = 0;
			if (addheader == 0)
				log_d("convert_cgi_headers: disallowing header \"%.*s\"", len, tmpname);
			else {
				if (nheaders >= tuning.num_headers) {
					log_d("convert_cgi_headers: too many header lines");
					return -1;
				}
				cgi_headers[nheaders].name = tmpname;
				cgi_headers[nheaders].value = tmpvalue;
				cgi_headers[nheaders].namelen = tmpnamelen;
				cgi_headers[nheaders++].len = len;
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
		if (len + 20 > pp->osize)
			return no_room();
		s = 302;
		memcpy(pp->obuf + len, "HTTP/1.1 302 Moved\r\n", 20);
		len += 20;
	} else if (havestatus == 0) {
		if (len + 17 > pp->osize)
			return no_room();
		s = 200;
		memcpy(pp->obuf + len, "HTTP/1.1 200 OK\r\n", 17);
		len += 17;
	} else {
		s = atoi(cgi_headers[status].value);
		if (s < 200 || s > 599) {
			log_d("convert_cgi_headers: illegal header line \"%.*s\"", cgi_headers[status].len, cgi_headers[status].name);
			return -1;
		}
		if (s == 204 || s == 304) {
			if (havelength) {
				log_d("convert_cgi_headers: script should not send content-length with status %d", s);
				return -1;
			}
			pp->nocontent = 1;
			pp->chunkit = 0;
		}
		tmpvaluelen = cgi_headers[status].len - (cgi_headers[status].value - cgi_headers[status].name);
		if (len + tmpvaluelen + 11 > pp->osize)
			return no_room();
		memcpy(pp->obuf + len, "HTTP/1.1 ", 9);
		len += 9;
		memcpy(pp->obuf + len, cgi_headers[status].value, tmpvaluelen);
		len += tmpvaluelen;
		pp->obuf[len++] = '\r';
		pp->obuf[len++] = '\n';
	}
	if (havelength) {
		tmpvalue = cgi_headers[length].value;
		if (*tmpvalue == '-') {
			log_d("convert_cgi_headers: illegal content-length header \"%.*s\"", cgi_headers[length].len, cgi_headers[length].name);
			return -1;
		}
		ul = strtoul(tmpvalue, &cp, 10);
		if (cp != tmpvalue) {
			while (*cp != '\n' && isspace(*cp))
				++cp;
		}
		if (*cp != '\n' || ul >= UINT_MAX) {
			log_d("convert_cgi_headers: illegal content-length header \"%.*s\"", cgi_headers[length].len, cgi_headers[length].name);
			return -1;
		}
		pp->chunkit = 0;
		pp->haslen = 1;
		pp->pmax = ul;
	} else if (pp->cn->r->protocol_minor == 0 && pp->nocontent == 0)
		pp->cn->keepalive = 0;
	l = sprintf(buf, "Server: %.30s\r\n", server_version);
	if (len + l > pp->osize)
		return no_room();
	memcpy(pp->obuf + len, buf, l);
	len += l;
	l = sprintf(buf, "Date: %s\r\n", rfctime(current_time, gbuf));
	if (len + l > pp->osize)
		return no_room();
	memcpy(pp->obuf + len, buf, l);
	len += l;
	if (pp->chunkit) {
		l = sprintf(buf, "Transfer-Encoding: chunked\r\n");
		if (len + l > pp->osize)
			return no_room();
		memcpy(pp->obuf + len, buf, l);
		len += l;
	}
	for (i = 0; i < nheaders; i++) {
		if (havestatus == 0 || i != status) {
			if (len + cgi_headers[i].len + 2 > pp->osize)
				return no_room();
			memcpy(pp->obuf + len, cgi_headers[i].name, cgi_headers[i].len);
			len += cgi_headers[i].len;
			pp->obuf[len++] = '\r';
			pp->obuf[len++] = '\n';
		}
	}
	if (len + 2 > pp->osize)
		return no_room();
	pp->obuf[len++] = '\r';
	pp->obuf[len++] = '\n';
	pp->otop = len;
	*sp = s;
	return 0;
}

static void pipe_run(struct pipe_params *p)
{
	short revents;
	char c;
	ssize_t r;
	size_t room;
	size_t bytestocopy;
	int convert_result;
	size_t bytestoread;
	char chunkbuf[16];
	size_t chunkheaderlen;

	if (p->cpollno != -1) {
		revents = pollfds[p->cpollno].revents;
		if (revents & ~(POLLIN | POLLOUT)) {
			log_d("pipe_run: revents=%hd", revents);
			p->error_condition = STUB_ERROR_CLIENT;
			return;
		}
		if (revents & POLLIN) {
			bytestoread = p->isize - p->ibp;
			if (bytestoread > p->imax)
				bytestoread = p->imax;
			if (bytestoread == 0) {
				log_d("pipe_run: nothing to read from client!");
				p->error_condition = STUB_ERROR_CLIENT;
				return;
			}
			r = read(p->cfd, p->ibuf + p->ibp, bytestoread);
			switch (r) {
			case -1:
				if (errno == EAGAIN)
					break;
				if (errno != ECONNRESET && errno != EPIPE)
					lerror("pipe_run: error reading from client");
				p->error_condition = STUB_ERROR_CLIENT;
				return;
			case 0:
				log_d("pipe_run: client went away while posting data");
				p->error_condition = STUB_ERROR_CLIENT;
				return;
			default:
				p->t = current_time;
				p->cn->nread += r;
				p->ibp += r;
				p->imax -= r;
				break;
			}
		}
		if (revents & POLLOUT) {
			r = write(p->cfd, p->obuf + p->obp, p->otop - p->obp);
			switch (r) {
			case -1:
				if (errno == EAGAIN)
					break;
				if (errno != ECONNRESET && errno != EPIPE)
					lerror("pipe_run: error writing to client");
				p->error_condition = STUB_ERROR_CLIENT;
				return;
			default:
				p->t = current_time;
				p->cn->nwritten += r;
				p->obp += r;
				break;
			}
		}
	}
	if (p->ppollno != -1) {
		revents = pollfds[p->ppollno].revents;
		if (revents & ~(POLLIN | POLLOUT)) {
			log_d("pipe_run: revents=%hd", revents);
			p->error_condition = STUB_ERROR_PIPE;
			return;
		}
		if (revents & POLLIN) {
			bytestoread = p->psize - p->ipp;
			if (p->haslen && bytestoread > p->pmax)
				bytestoread = p->pmax;
			if (bytestoread == 0) {
				log_d("pipe_run: nothing to read from pipe");
				p->error_condition = STUB_ERROR_PIPE;
				return;
			}
			r = read(p->pfd, p->pbuf + p->ipp, bytestoread);
			switch (r) {
			case -1:
				if (errno == EAGAIN)
					break;
				lerror("pipe_run: error reading from script");
				p->error_condition = STUB_ERROR_PIPE;
				return;
			case 0:
				if (p->state != 2) {
					log_d("pipe_run: premature end of script headers");
					p->error_condition = STUB_ERROR_RESTART;
					return;
				}
				if (p->haslen) {
					log_d("pipe_run: script went away");
					p->error_condition = STUB_ERROR_PIPE;
					return;
				}
				p->t = current_time;
				p->pstate = 2;
				break;
			default:
				p->t = current_time;
				p->ipp += r;
				if (p->haslen)
					p->pmax -= r;
				break;
			}
		}
		if (revents & POLLOUT) {
			r = write(p->pfd, p->ibuf + p->opp, p->ibp - p->opp);
			switch (r) {
			case -1:
				if (errno == EAGAIN)
					break;
				lerror("pipe_run: error writing to script");
				p->error_condition = STUB_ERROR_PIPE;
				return;
			default:
				p->t = current_time;
				p->opp += r;
				break;
			}
		}
	}
	if (p->opp && p->opp == p->ibp)
		p->opp = p->ibp = 0;
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
			convert_result = convert_cgi_headers(p, &p->cn->r->status);
			if (convert_result == -1) {
				log_d("pipe_run: problem in convert_cgi_headers");
				p->error_condition = STUB_ERROR_RESTART;
				return;
			}
			if (p->haslen && p->pstart < p->ipp) {
				if (p->ipp > p->pstart + p->pmax) {
					log_d("extra garbage from script ignored");
					p->ipp = p->pstart + p->pmax;
					p->pmax = 0;
				} else
					p->pmax -= p->ipp - p->pstart;
			}
		} else if (p->pstart == p->psize) {
			log_d("pipe_run: too many headers");
			p->error_condition = STUB_ERROR_RESTART;
			return;
		}
	}
	if (p->state == 2 && p->pstart < p->ipp) {
		if (p->nocontent)
			p->pstart = p->ipp = 0;
		else {
			room = p->osize - p->otop;
			bytestocopy = p->ipp - p->pstart;
			if (bytestocopy > room)
				bytestocopy = room;
			if (bytestocopy && p->chunkit) {
				chunkheaderlen = sprintf(chunkbuf, "%lx\r\n", (unsigned long) bytestocopy);
				if (chunkheaderlen + 2 >= room)
					bytestocopy = 0;
				else {
					if (bytestocopy + chunkheaderlen + 2 > room) {
						bytestocopy -= chunkheaderlen + 2;
						chunkheaderlen = sprintf(chunkbuf, "%lx\r\n", (unsigned long) bytestocopy);
					}
					memcpy(p->obuf + p->otop, chunkbuf, chunkheaderlen);
					p->otop += chunkheaderlen;
				}
			}
			if (bytestocopy) {
				memcpy(p->obuf + p->otop, p->pbuf + p->pstart, bytestocopy);
				p->otop += bytestocopy;
				p->pstart += bytestocopy;
				if (p->pstart == p->ipp)
					p->pstart = p->ipp = 0;
				if (p->chunkit) {
					memcpy(p->obuf + p->otop, "\r\n", 2);
					p->otop += 2;
				}
			}
		}
	}
	if (p->pstate == 2 && p->chunkit && p->nocontent == 0) {
		if (p->osize - p->otop >= 5) {
			memcpy(p->obuf + p->otop, "0\r\n\r\n", 5);
			p->otop += 5;
			p->pstate = 3;
		}
	}
}

void init_child(struct pipe_params *p, struct request *r, int fd)
{
	p->ibp = 0;
	p->obp = 0;
	p->ipp = 0;
	p->opp = 0;
	p->otop = 0;
	p->istate = 1;
	p->pstate = 1;
	p->pstart = 0;
	p->state = 0;
	p->cfd = r->cn->fd;
	p->pfd = fd;
	p->chunkit = r->protocol_minor > 0;
	p->nocontent = r->method == M_HEAD;
	p->haslen = 0;
	p->pmax = 0;
	if (r->method == M_POST) {
		p->istate = 1;
		p->imax = r->in_mblen;
	} else {
		p->istate = 0;
		p->imax = 0;
	}
	p->cn = r->cn;
	r->cn->state = HC_FORKED;
	p->error_condition = 0;
	p->cpollno = -1;
	p->ppollno = -1;
	p->t = current_time;
}

int setup_child_pollfds(int n)
{
	struct pipe_params *p;
	short e;

	p = children;
	while (p) {
		if (p->cn && p->error_condition == 0) {
			if (p->haslen && p->pmax == 0)
				p->pstate = 2;
			e = 0;
			if (p->istate == 1 && p->ibp < p->isize && p->imax)
				e |= POLLIN;
			if (p->otop > p->obp)
				e |= POLLOUT;
			if (e) {
				pollfds[n].fd = p->cfd;
				pollfds[n].events = e;
				p->cpollno = n++;
			} else
				p->cpollno = -1;
			e = 0;
			if (p->pstate == 1 && p->ipp < p->psize && (p->chunkit || p->haslen == 0 || p->pmax))
				e |= POLLIN;
			if (p->ibp > p->opp)
				e |= POLLOUT;
			if (e) {
				pollfds[n].fd = p->pfd;
				pollfds[n].events = e;
				p->ppollno = n++;
			} else
				p->ppollno = -1;
		}
		p = p->next;
	}
	return n;
}

static void close_child(struct pipe_params *p, int nextaction)
{
	close(p->pfd);
	p->cn->state = HC_ACTIVE;
	p->cn->action = nextaction;
	p->cn = 0;
}

void close_children(void)
{
	struct pipe_params *p;

	p = children;
	while (p) {
		if (p->cn)
			close_child(p, HC_CLOSING);
		p = p->next;
	}
}

int run_children(void)
{
	struct pipe_params *p;

	p = children;
	while (p) {
		if (p->cn)
			pipe_run(p);
		p = p->next;
	}
	return 0;
}

void cleanup_children(void)
{
	struct pipe_params *p;
	int f;

	p = children;
	while (p) {
		if (p->cn) {
			if (p->error_condition) {
				if (p->error_condition == STUB_ERROR_RESTART)
					close_child(p, cgi_error(p->cn->r) == -1 ? HC_CLOSING : HC_WRITING);
				else
					close_child(p, HC_CLOSING);
			} else if (current_time >= p->t + (time_t) tuning.script_timeout) {
				log_d("script timeout to %s[%hu]", inet_ntoa(p->cn->peer.sin_addr), ntohs(p->cn->peer.sin_port));
				close_child(p, HC_CLOSING);
			} else {
				f = 0;
				if (p->istate == 1 && p->ibp < p->isize && p->imax)
					f = 1;
				if (p->otop > p->obp)
					f = 1;
				if (p->pstate == 1 && p->ipp < p->psize && (p->chunkit || p->haslen == 0 || p->pmax))
					f = 1;
				if (p->ibp > p->opp)
					f = 1;
				if (f == 0)
					close_child(p, p->cn->keepalive ? HC_REINIT : HC_CLOSING);
			}
		}
		p = p->next;
	}
}
