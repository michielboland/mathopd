/*
 *   Copyright 2003 Michiel Boland.
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
#ifndef POLL_EMULATION
#include <poll.h>
#else
#include "poll-emul.h"
#endif
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mathopd.h"

static struct cgi_header *cgi_headers;

int init_cgi_headers(void)
{
	if (tuning.num_headers == 0)
		return 0;
	cgi_headers = malloc(tuning.num_headers * sizeof *cgi_headers);
	if (cgi_headers == 0) {
		log_d("init_cgi_headers: out of memory");
		return -1;
	}
	return 0;
}

static int no_room(void)
{
	log_d("no room left for HTTP headers");
	return -1;
}

static int convert_cgi_headers(struct connection *pp, int *sp)
{
	int addheader, c, s;
	size_t i, nheaders, status, location, length;
	const char *p, *tmpname, *tmpvalue;
	int havestatus, havelocation, firstline, havelength;
	size_t len, tmpnamelen, tmpvaluelen;
	char gbuf[40], *cp;
	struct pool *po;
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
	for (p = pp->script_input.floor; p < pp->script_input.start; p++) {
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
	po = &pp->output;
	if (havelocation && havestatus == 0) {
		s = 302;
		if (pool_print(po, "HTTP/1.1 302 Moved\r\n") == -1)
			return no_room();
	} else if (havestatus == 0) {
		s = 200;
		if (pool_print(po, "HTTP/1.1 200 OK\r\n") == -1)
			return no_room();
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
			pp->pipe_params.nocontent = 1;
			pp->pipe_params.chunkit = 0;
		}
		tmpvaluelen = cgi_headers[status].len - (cgi_headers[status].value - cgi_headers[status].name);
		if (pool_print(po, "HTTP/1.1 %.*s\r\n", tmpvaluelen, cgi_headers[status].value) == -1)
			return no_room();
	}
	if (havelength) {
		tmpvalue = cgi_headers[length].value;
		if (*tmpvalue == '-') {
			log_d("convert_cgi_headers: illegal content-length header \"%.*s\"", cgi_headers[length].len, cgi_headers[length].name);
			return -1;
		}
		ul = strtoul(tmpvalue, &cp, 10);
		if (cp != tmpvalue) {
			while (*cp != '\n' && (*cp == ' ' || *cp == '\t' || *cp == '\r'))
				++cp;
		}
		if (*cp != '\n' || ul >= UINT_MAX) {
			log_d("convert_cgi_headers: illegal content-length header \"%.*s\"", cgi_headers[length].len, cgi_headers[length].name);
			return -1;
		}
		pp->pipe_params.chunkit = 0;
		pp->pipe_params.haslen = 1;
		pp->pipe_params.pmax = ul;
		pp->r->num_content = 0;
		pp->r->content_length = ul;
	} else if (pp->r->protocol_minor == 0 && pp->pipe_params.nocontent == 0)
		pp->keepalive = 0;
	if (pool_print(po, "Server: %.30s\r\n", server_version) == -1)
		return no_room();
	if (pool_print(po, "Date: %s\r\n", rfctime(current_time, gbuf)) == -1)
		return no_room();
	if (pp->pipe_params.chunkit)
		if (pool_print(po, "Transfer-Encoding: chunked\r\n") == -1)
			return no_room();
	if (pp->r->protocol_minor == 0 && pp->keepalive)
		if (pool_print(po, "Connection: keep-alive\r\n") == -1)
			return no_room();
	if (pp->r->protocol_minor && pp->keepalive == 0)
		if (pool_print(po, "Connection: close\r\n") == -1)
			return no_room();
	for (i = 0; i < nheaders; i++)
		if (havestatus == 0 || i != status)
			if (pool_print(po, "%.*s\r\n", cgi_headers[i].len, cgi_headers[i].name) == -1)
				return no_room();
	if (pool_print(po, "\r\n") == -1)
		return no_room();
	*sp = s;
	return 0;
}

static int readfromclient(struct connection *p)
{
	size_t bytestoread;
	ssize_t r;

	bytestoread = p->client_input.ceiling - p->client_input.end;
	if (bytestoread > p->pipe_params.imax)
		bytestoread = p->pipe_params.imax;
	if (bytestoread == 0) {
		log_d("readfromclient: bytestoread is zero!");
		return 0;
	}
	r = read(p->fd, p->client_input.end, bytestoread);
	if (debug)
		log_d("readfromclient: %d %d %d %d", p->fd, p->client_input.end - p->client_input.floor, bytestoread, r);
	switch (r) {
	case -1:
		if (errno == EAGAIN)
			return 0;
		if (debug)
			lerror("read");
		close_connection(p);
		return -1;
	case 0:
		log_d("readfromclient: client went away while posting data");
		close_connection(p);
		return -1;
	default:
		p->t = current_time;
		p->nread += r;
		p->client_input.end += r;
		p->pipe_params.imax -= r;
		break;
	}
	return 0;
}

static int readfromchild(struct connection *p)
{
	size_t bytestoread;
	ssize_t r;

	bytestoread = p->script_input.ceiling - p->script_input.end;
	if (p->pipe_params.haslen && bytestoread > p->pipe_params.pmax)
		bytestoread = p->pipe_params.pmax;
	if (bytestoread == 0) {
		log_d("readfromchild: bytestoread is zero!");
		return 0;
	}
	r = read(p->rfd, p->script_input.end, bytestoread);
	if (debug)
		log_d("readfromchild: %d %d %d %d", p->rfd, p->script_input.end - p->script_input.floor, bytestoread, r);
	switch (r) {
	case -1:
		if (errno == EAGAIN)
			return 0;
		if (debug)
			lerror("read");
		close_connection(p);
		return -1;
	case 0:
		if (p->pipe_params.state != 2) {
			log_d("readfromchild: premature end of script headers (ipp=%d)", p->script_input.end - p->script_input.floor);
			cgi_error(p->r);
			return -1;
		}
		if (p->pipe_params.haslen) {
			log_d("readfromchild: script went away (pmax=%d)", p->pipe_params.pmax);
			close_connection(p);
			return -1;
		}
		p->t = current_time;
		p->script_input.state = 2;
		break;
	default:
		p->t = current_time;
		p->script_input.end += r;
		if (p->pipe_params.haslen) {
			p->pipe_params.pmax -= r;
			if (p->pipe_params.pmax == 0)
				p->script_input.state = 2;
		}
		break;
	}
	return 0;
}

static int writetoclient(struct connection *p)
{
	size_t bytestowrite;
	ssize_t r;

	bytestowrite = p->output.end - p->output.start;
	if (bytestowrite == 0) {
		log_d("writetoclient: bytestowrite is zero!");
		return 0;
	}
	r = write(p->fd, p->output.start, bytestowrite);
	if (debug)
		log_d("writetoclient: %d %d %d %d", p->fd, p->output.start - p->output.floor, bytestowrite, r);
	switch (r) {
	case -1:
		if (errno == EAGAIN)
			return 0;
		if (debug)
			lerror("write");
		close_connection(p);
		return -1;
	default:
		p->t = current_time;
		p->nwritten += r;
		p->output.start += r;
		if (p->output.start == p->output.end)
			p->output.start = p->output.end = p->output.floor;
		break;
	}
	return 0;
}

static int writetochild(struct connection *p)
{
	size_t bytestowrite;
	ssize_t r;

	bytestowrite = p->client_input.end - p->client_input.start;
	if (bytestowrite == 0) {
		log_d("writetochild: bytestowrite is zero!");
		return 0;
	}
	r = write(p->rfd, p->client_input.start, bytestowrite);
	if (debug)
		log_d("writetochild: %d %d %d %d", p->rfd, p->client_input.start - p->client_input.floor, bytestowrite, r);
	switch (r) {
	case -1:
		if (errno == EAGAIN)
			return 0;
		if (debug)
			lerror("write");
		close_connection(p);
		return -1;
	default:
		p->t = current_time;
		p->client_input.start += r;
		if (p->client_input.start == p->client_input.end)
			p->client_input.start = p->client_input.end = p->client_input.floor;
		break;
	}
	return 0;
}

static int scanlflf(struct connection *p)
{
	char c;
	int state;

	state = p->pipe_params.state;
	while (p->script_input.start < p->script_input.end && state != 2) {
		c = *p->script_input.start++;
		switch (state) {
		case 0:
			if (c == '\n')
				state = 1;
			break;
		case 1:
			switch (c) {
			case '\r':
				break;
			case '\n':
				state = 2;
				break;
			default:
				state = 0;
				break;
			}
			break;
		}
	}
	p->pipe_params.state = state;
	if (state == 2) {
		if (convert_cgi_headers(p, &p->r->status) == -1) {
			cgi_error(p->r);
			return -1;
		}
		if (p->pipe_params.nocontent)
			p->script_input.state = 3;
		else if (p->pipe_params.haslen) {
			if (p->script_input.start < p->script_input.end) {
				if (p->script_input.end > p->script_input.start + p->pipe_params.pmax) {
					log_d("extra garbage from script ignored");
					p->script_input.end = p->script_input.start + p->pipe_params.pmax;
					p->pipe_params.pmax = 0;
				} else
					p->pipe_params.pmax -= p->script_input.end - p->script_input.start;
			}
			if (p->pipe_params.pmax == 0)
				p->script_input.state = 2;
		}
	} else if (p->script_input.start == p->script_input.ceiling) {
		log_d("scanlflf: buffer full");
		cgi_error(p->r);
		return -1;
	}
	return 0;
}

static void copychunk(struct connection *p)
{
	size_t room;
	size_t bytestocopy;
	char chunkbuf[16];
	size_t chunkheaderlen;

	if (p->pipe_params.nocontent) {
		p->script_input.start = p->script_input.end = p->script_input.floor;
		return;
	}
	room = p->output.ceiling - p->output.end;
	bytestocopy = p->script_input.end - p->script_input.start;
	if (bytestocopy > room)
		bytestocopy = room;
	if (bytestocopy && p->pipe_params.chunkit) {
		chunkheaderlen = sprintf(chunkbuf, "%lx\r\n", (unsigned long) bytestocopy);
		if (chunkheaderlen + 2 >= room)
			bytestocopy = 0;
		else {
			if (bytestocopy + chunkheaderlen + 2 > room) {
				bytestocopy -= chunkheaderlen + 2;
				chunkheaderlen = sprintf(chunkbuf, "%lx\r\n", (unsigned long) bytestocopy);
			}
			memcpy(p->output.end, chunkbuf, chunkheaderlen);
			p->output.end += chunkheaderlen;
		}
	}
	if (bytestocopy) {
		memcpy(p->output.end, p->script_input.start, bytestocopy);
		p->output.end += bytestocopy;
		p->script_input.start += bytestocopy;
		if (p->script_input.start == p->script_input.end)
			p->script_input.start = p->script_input.end = p->script_input.floor;
		if (p->pipe_params.chunkit) {
			memcpy(p->output.end, "\r\n", 2);
			p->output.end += 2;
		}
	}
}

static void copylastchunk(struct connection *p)
{
	if (p->pipe_params.chunkit) {
		if (p->output.ceiling - p->output.end >= 5) {
			memcpy(p->output.end, "0\r\n\r\n", 5);
			p->output.end += 5;
			p->script_input.state = 3;
		}
	} else
		p->script_input.state = 3;
}

static int childisfinished(struct connection *p)
{
	if (p->script_input.state != 3)
		return 0;
	if (p->output.end > p->output.start)
		return 0;
	if (p->script_input.end > p->script_input.start)
		return 0;
	if (p->client_input.state == 1 && p->pipe_params.imax)
		return 0;
	return 1;
}

void pipe_run(struct connection *p)
{
	short cevents, pevents;
	int canwritetoclient, canwritetochild;

	cevents = p->pollno != -1 ? pollfds[p->pollno].revents : 0;
	pevents = p->rpollno != -1 ? pollfds[p->rpollno].revents : 0;
	if (cevents & (POLLERR | POLLHUP)) {
		close_connection(p);
		return;
	}
	if (pevents & (POLLERR | POLLHUP)) {
		close_connection(p);
		return;
	}
	canwritetoclient = cevents & POLLOUT || p->output.end == p->output.floor;
	canwritetochild = pevents & POLLOUT || p->client_input.end == p->client_input.floor;
	if (cevents & POLLIN)
		if (readfromclient(p) == -1)
			return;
	if (pevents & POLLIN)
		if (readfromchild(p) == -1)
			return;
	if (p->script_input.end && p->pipe_params.state != 2)
		if (scanlflf(p) == -1)
			return;
	if (p->pipe_params.state == 2 && p->script_input.start < p->script_input.end)
		copychunk(p);
	if (p->script_input.state == 2)
		copylastchunk(p);
	if (p->output.end > p->output.start && canwritetoclient)
		if (writetoclient(p) == -1)
			return;
	if (p->client_input.end > p->client_input.start && canwritetochild)
		if (writetochild(p) == -1)
			return;
	if (childisfinished(p)) {
		if (p->keepalive) {
			if (p->client_input.end > p->client_input.start) {
				log_d("client_input is not empty");
				close_connection(p);
			} else
				reinit_connection(p);
		} else
			close_connection(p);
	}
}

void init_child(struct connection *p, int fd)
{
	struct request *r;
	size_t bytestomove, bytestoshift;

	r = p->r;
	p->client_input.start = p->client_input.end = p->client_input.floor;
	p->output.start = p->output.end = p->output.floor;
	p->script_input.start = p->script_input.end = p->script_input.floor;
	p->script_input.state = 1;
	p->pipe_params.state = 1;
	if (p->rfd != -1) /* this should never happen */
		close(p->rfd);
	p->rfd = fd;
	p->pipe_params.chunkit = r->protocol_minor > 0;
	p->pipe_params.nocontent = r->method == M_HEAD;
	p->pipe_params.haslen = 0;
	p->pipe_params.pmax = 0;
	if (r->in_mblen) {
		p->client_input.state = 1;
		p->pipe_params.imax = r->in_mblen;
		bytestomove = p->header_input.end - p->header_input.middle;
		if (bytestomove > p->pipe_params.imax) {
			bytestoshift = p->pipe_params.imax;
			bytestomove = p->pipe_params.imax;
		} else
			bytestoshift = 0;
		if (bytestomove) {
			if (p->client_input.start + bytestomove > p->client_input.ceiling) {
				log_d("init_child: script buffer too small!");
				close(fd);
				close_connection(p);
				return;
			}
			memmove(p->client_input.start, p->header_input.middle, bytestomove);
			p->client_input.end += bytestomove;
			p->pipe_params.imax -= bytestomove;
			if (bytestoshift)
				p->header_input.middle += bytestoshift;
			else
				p->header_input.end = p->header_input.middle;
		}
	} else {
		p->client_input.state = 0;
		p->pipe_params.imax = 0;
	}
	set_connection_state(p, HC_FORKED);
	p->pollno = -1;
	p->rpollno = -1;
}

int setup_child_pollfds(int n, struct connection *p)
{
	struct connection *q;
	short e;

	while (p) {
		q = p->next;
		e = 0;
		if (p->client_input.state == 1 && p->client_input.end < p->client_input.ceiling && p->pipe_params.imax)
			e |= POLLIN;
		if (p->output.end > p->output.start || (p->pipe_params.state == 2 && p->script_input.start < p->script_input.end && p->output.end == p->output.floor))
			e |= POLLOUT;
		if (e) {
			pollfds[n].fd = p->fd;
			pollfds[n].events = e;
			p->pollno = n++;
		} else
			p->pollno = -1;
		e = 0;
		if (p->script_input.state == 1 && p->script_input.end < p->script_input.ceiling && (p->pipe_params.chunkit || p->pipe_params.haslen == 0 || p->pipe_params.pmax))
			e |= POLLIN;
		if (p->client_input.end > p->client_input.start)
			e |= POLLOUT;
		if (e) {
			pollfds[n].fd = p->rfd;
			pollfds[n].events = e;
			p->rpollno = n++;
		} else
			p->rpollno = -1;
		p = q;
	}
	return n;
}
