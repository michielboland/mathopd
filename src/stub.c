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
#include <poll.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mathopd.h"

struct cgi_header {
	const char *name;
	size_t namelen;
	const char *value;
	size_t len;
};

static struct cgi_header *cgi_headers;

struct pipe_params *pipe_params_array;

static struct pipe_params_list free_pipes;
static struct pipe_params_list busy_pipes;

static void p_unlink(struct pipe_params *c, struct pipe_params_list *l)
{
	struct pipe_params *p, *n;

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

static void p_link(struct pipe_params *c, struct pipe_params_list *l)
{
	struct pipe_params *n;

	n = l->head;
	c->prev = 0;
	c->next = n;
	if (n == 0)
		l->tail = c;
	else
		n->prev = c;
	l->head = c;
}

struct pipe_params *new_pipe_params(void)
{
	struct pipe_params *p;

	p = free_pipes.head;
	if (p) {
		p_unlink(p, &free_pipes);
		p_link(p, &busy_pipes);
	}
	return p;
}

int init_children(size_t n)
{
	size_t i;
	struct pipe_params *p;

	if (tuning.num_headers) {
		cgi_headers = malloc(tuning.num_headers * sizeof *cgi_headers);
		if (cgi_headers == 0)
			return -1;
	}
	pipe_params_array = malloc(n * sizeof *pipe_params_array);
	if (pipe_params_array == 0)
		return -1;
	for (i = 0; i < n; i++){
		p = pipe_params_array + i;
		if (new_pool(&p->client_input, tuning.script_buf_size) == -1)
			return -1;
		if (new_pool(&p->client_output, tuning.script_buf_size + 16) == -1)
			return -1;
		if (new_pool(&p->script_input, tuning.script_buf_size) == -1)
			return -1;
		p->cn = 0;
		p_link(p, &free_pipes);
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
	char buf[50], gbuf[40], *cp, *dest;
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
	dest = pp->client_output.floor;
	if (havelocation && havestatus == 0) {
		if (dest + 20 > pp->client_output.ceiling)
			return no_room();
		s = 302;
		memcpy(dest, "HTTP/1.1 302 Moved\r\n", 20);
		dest += 20;
	} else if (havestatus == 0) {
		if (dest + 17 > pp->client_output.ceiling)
			return no_room();
		s = 200;
		memcpy(dest, "HTTP/1.1 200 OK\r\n", 17);
		dest += 17;
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
		if (dest + tmpvaluelen + 11 > pp->client_output.ceiling)
			return no_room();
		memcpy(dest, "HTTP/1.1 ", 9);
		dest += 9;
		memcpy(dest, cgi_headers[status].value, tmpvaluelen);
		dest += tmpvaluelen;
		*dest++ = '\r';
		*dest++ = '\n';
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
		pp->chunkit = 0;
		pp->haslen = 1;
		pp->pmax = ul;
		pp->cn->r->num_content = 0;
		pp->cn->r->content_length = ul;
	} else if (pp->cn->r->protocol_minor == 0 && pp->nocontent == 0)
		pp->cn->keepalive = 0;
	l = sprintf(buf, "Server: %.30s\r\n", server_version);
	if (dest + l > pp->client_output.ceiling)
		return no_room();
	memcpy(dest, buf, l);
	dest += l;
	l = sprintf(buf, "Date: %s\r\n", rfctime(current_time, gbuf));
	if (dest + l > pp->client_output.ceiling)
		return no_room();
	memcpy(dest, buf, l);
	dest += l;
	if (pp->chunkit) {
		l = sprintf(buf, "Transfer-Encoding: chunked\r\n");
		if (dest + l > pp->client_output.ceiling)
			return no_room();
		memcpy(dest, buf, l);
		dest += l;
	}
	if (pp->cn->r->protocol_minor == 0 && pp->cn->keepalive) {
		l = sprintf(buf, "Connection: keep-alive\r\n");
		if (dest + l > pp->client_output.ceiling)
			return no_room();
		memcpy(dest, buf, l);
		dest += l;
	}
	if (pp->cn->r->protocol_minor && pp->cn->keepalive == 0) {
		l = sprintf(buf, "Connection: close\r\n");
		if (dest + l > pp->client_output.ceiling)
			return no_room();
		memcpy(dest, buf, l);
		dest += l;
	}
	for (i = 0; i < nheaders; i++) {
		if (havestatus == 0 || i != status) {
			if (dest + cgi_headers[i].len + 2 > pp->client_output.ceiling)
				return no_room();
			memcpy(dest, cgi_headers[i].name, cgi_headers[i].len);
			dest += cgi_headers[i].len;
			*dest++ = '\r';
			*dest++ = '\n';
		}
	}
	if (dest + 2 > pp->client_output.ceiling)
		return no_room();
	*dest++ = '\r';
	*dest++ = '\n';
	pp->client_output.end = dest;
	*sp = s;
	return 0;
}

static int readfromclient(struct pipe_params *p)
{
	size_t bytestoread;
	ssize_t r;

	bytestoread = p->client_input.ceiling - p->client_input.end;
	if (bytestoread > p->imax)
		bytestoread = p->imax;
	if (bytestoread == 0) {
		log_d("readfromclient: bytestoread is zero!");
		return 0;
	}
	r = recv(p->cfd, p->client_input.end, bytestoread, 0);
	if (debug)
		log_d("readfromclient: %d %d %d %d", p->cfd, p->client_input.end - p->client_input.floor, bytestoread, r);
	switch (r) {
	case -1:
		switch (errno) {
		default:
			lerror("readfromclient");
		case ECONNRESET:
		case EPIPE:
			p->error_condition = STUB_ERROR_CLIENT;
			return -1;
		case EAGAIN:
			return 0;
		}
		break;
	case 0:
		log_d("readfromclient: client went away while posting data");
		p->error_condition = STUB_ERROR_CLIENT;
		return -1;
	default:
		p->t = current_time;
		p->cn->nread += r;
		p->client_input.end += r;
		p->imax -= r;
		break;
	}
	return 0;
}

static int readfromchild(struct pipe_params *p)
{
	size_t bytestoread;
	ssize_t r;

	bytestoread = p->script_input.ceiling - p->script_input.end;
	if (p->haslen && bytestoread > p->pmax)
		bytestoread = p->pmax;
	if (bytestoread == 0) {
		log_d("readfromchild: bytestoread is zero!");
		return 0;
	}
	r = recv(p->pfd, p->script_input.end, bytestoread, 0);
	if (debug)
		log_d("readfromchild: %d %d %d %d", p->pfd, p->script_input.end - p->script_input.floor, bytestoread, r);
	switch (r) {
	case -1:
		if (errno == EAGAIN)
			return 0;
		lerror("readfromchild");
		p->error_condition = STUB_ERROR_PIPE;
		return -1;
	case 0:
		if (p->state != 2) {
			log_d("readfromchild: premature end of script headers (ipp=%d)", p->script_input.end - p->script_input.floor);
			p->error_condition = STUB_ERROR_RESTART;
			return -1;
		}
		if (p->haslen) {
			log_d("readfromchild: script went away (pmax=%d)", p->pmax);
			p->error_condition = STUB_ERROR_PIPE;
			return -1;
		}
		p->t = current_time;
		p->script_input.state = 2;
		break;
	default:
		p->t = current_time;
		p->script_input.end += r;
		if (p->haslen) {
			p->pmax -= r;
			if (p->pmax == 0)
				p->script_input.state = 2;
		}
		break;
	}
	return 0;
}

static int writetoclient(struct pipe_params *p)
{
	size_t bytestowrite;
	ssize_t r;

	bytestowrite = p->client_output.end - p->client_output.start;
	if (bytestowrite == 0) {
		log_d("writetoclient: bytestowrite is zero!");
		return 0;
	}
	r = send(p->cfd, p->client_output.start, bytestowrite, 0);
	if (debug)
		log_d("writetoclient: %d %d %d %d", p->cfd, p->client_output.start - p->client_output.floor, bytestowrite, r);
	switch (r) {
	case -1:
		switch (errno) {
		case EAGAIN:
			return 0;
		default:
			lerror("writetoclient");
		case EPIPE:
		case ECONNRESET:
			p->error_condition = STUB_ERROR_CLIENT;
			return -1;
		}
		break;
	default:
		p->t = current_time;
		p->cn->nwritten += r;
		p->client_output.start += r;
		if (p->client_output.start == p->client_output.end)
			p->client_output.start = p->client_output.end = p->client_output.floor;
		break;
	}
	return 0;
}

static int writetochild(struct pipe_params *p)
{
	size_t bytestowrite;
	ssize_t r;

	bytestowrite = p->client_input.end - p->client_input.start;
	if (bytestowrite == 0) {
		log_d("writetochild: bytestowrite is zero!");
		return 0;
	}
	r = send(p->pfd, p->client_input.start, bytestowrite, 0);
	if (debug)
		log_d("writetochild: %d %d %d %d", p->pfd, p->client_input.start - p->client_input.floor, bytestowrite, r);
	switch (r) {
	case -1:
		if (errno == EAGAIN)
			return 0;
		lerror("writetochild");
		p->error_condition = STUB_ERROR_PIPE;
		return -1;
		break;
	default:
		p->t = current_time;
		p->client_input.start += r;
		if (p->client_input.start == p->client_input.end)
			p->client_input.start = p->client_input.end = p->client_input.floor;
		break;
	}
	return 0;
}

static int scanlflf(struct pipe_params *p)
{
	char c;

	while (p->script_input.start < p->script_input.end && p->state != 2) {
		c = *p->script_input.start++;
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
		if (convert_cgi_headers(p, &p->cn->r->status) == -1) {
			p->error_condition = STUB_ERROR_RESTART;
			return -1;
		}
		if (p->nocontent)
			p->script_input.state = 3;
		else if (p->haslen) {
			if (p->script_input.start < p->script_input.end) {
				if (p->script_input.end > p->script_input.start + p->pmax) {
					log_d("extra garbage from script ignored");
					p->script_input.end = p->script_input.start + p->pmax;
					p->pmax = 0;
				} else
					p->pmax -= p->script_input.end - p->script_input.start;
			}
			if (p->pmax == 0)
				p->script_input.state = 2;
		}
	} else if (p->script_input.start == p->script_input.ceiling) {
		log_d("scanlflf: buffer full");
		p->error_condition = STUB_ERROR_RESTART;
		return -1;
	}
	return 0;
}

static void copychunk(struct pipe_params *p)
{
	size_t room;
	size_t bytestocopy;
	char chunkbuf[16];
	size_t chunkheaderlen;

	if (p->nocontent) {
		p->script_input.start = p->script_input.end = p->script_input.floor;
		return;
	}
	room = p->client_output.ceiling - p->client_output.end;
	bytestocopy = p->script_input.end - p->script_input.start;
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
			memcpy(p->client_output.end, chunkbuf, chunkheaderlen);
			p->client_output.end += chunkheaderlen;
		}
	}
	if (bytestocopy) {
		memcpy(p->client_output.end, p->script_input.start, bytestocopy);
		p->client_output.end += bytestocopy;
		p->script_input.start += bytestocopy;
		if (p->script_input.start == p->script_input.end)
			p->script_input.start = p->script_input.end = p->script_input.floor;
		if (p->chunkit) {
			memcpy(p->client_output.end, "\r\n", 2);
			p->client_output.end += 2;
		}
	}
}

static void copylastchunk(struct pipe_params *p)
{
	if (p->chunkit) {
		if (p->client_output.ceiling - p->client_output.end >= 5) {
			memcpy(p->client_output.end, "0\r\n\r\n", 5);
			p->client_output.end += 5;
			p->script_input.state = 3;
		}
	} else
		p->script_input.state = 3;
}

static void pipe_run(struct pipe_params *p)
{
	short cevents, pevents;
	int canwritetoclient, canwritetochild;

	cevents = p->cpollno != -1 ? pollfds[p->cpollno].revents : 0;
	pevents = p->ppollno != -1 ? pollfds[p->ppollno].revents : 0;
	if (cevents & POLLERR) {
		log_socket_error(p->cfd, "pipe_run: error on client socket");
		p->error_condition = STUB_ERROR_CLIENT;
		return;
	}
	if (pevents & POLLERR) {
		log_socket_error(p->pfd, "pipe_run: error on child socket");
		p->error_condition = STUB_ERROR_PIPE;
		return;
	}
	cevents &= POLLIN | POLLOUT;
	pevents &= POLLIN | POLLOUT;
	canwritetoclient = cevents & POLLOUT || p->client_output.end == p->client_output.floor;
	canwritetochild = pevents & POLLOUT || p->client_input.end == p->client_input.floor;
	if (cevents & POLLIN) {
		if (readfromclient(p) == -1)
			return;
	}
	if (pevents & POLLIN) {
		if (readfromchild(p) == -1)
			return;
	}
	if (p->script_input.end && p->state != 2) {
		if (scanlflf(p) == -1)
			return;
	}
	if (p->state == 2 && p->script_input.start < p->script_input.end)
		copychunk(p);
	if (p->script_input.state == 2)
		copylastchunk(p);
	if (p->client_output.end > p->client_output.start && canwritetoclient) {
		if (writetoclient(p) == -1)
			return;
	}
	if (p->client_input.end > p->client_input.start && canwritetochild) {
		if (writetochild(p) == -1)
			return;
	}
}

void init_child(struct pipe_params *p, struct request *r, int fd)
{
	p->client_input.start = p->client_input.floor;
	p->client_input.end = p->client_input.floor;
	p->client_input.state = 1;
	p->client_output.start = p->client_output.floor;
	p->client_output.end = p->client_output.floor;
	p->script_input.start = p->script_input.floor;
	p->script_input.end = p->script_input.floor;
	p->script_input.state = 1;
	p->state = 0;
	p->cfd = r->cn->fd;
	p->pfd = fd;
	p->chunkit = r->protocol_minor > 0;
	p->nocontent = r->method == M_HEAD;
	p->haslen = 0;
	p->pmax = 0;
	if (r->method == M_POST) {
		p->client_input.state = 1;
		p->imax = r->in_mblen;
	} else {
		p->client_input.state = 0;
		p->imax = 0;
	}
	p->cn = r->cn;
	set_connection_state(r->cn, HC_FORKED);
	p->error_condition = 0;
	p->cpollno = -1;
	p->ppollno = -1;
	p->t = current_time;
}

int setup_child_pollfds(int n)
{
	struct pipe_params *p;
	short e;

	p = busy_pipes.head;
	while (p) {
		if (p->cn && p->error_condition == 0) {
			e = 0;
			if (p->client_input.state == 1 && p->client_input.end < p->client_input.ceiling && p->imax)
				e |= POLLIN;
			if (p->client_output.end > p->client_output.start || (p->state == 2 && p->script_input.start < p->script_input.end && p->client_output.end == p->client_output.floor))
				e |= POLLOUT;
			if (e) {
				pollfds[n].fd = p->cfd;
				pollfds[n].events = e;
				p->cpollno = n++;
			} else
				p->cpollno = -1;
			e = 0;
			if (p->script_input.state == 1 && p->script_input.end < p->script_input.ceiling && (p->chunkit || p->haslen == 0 || p->pmax))
				e |= POLLIN;
			if (p->client_input.end > p->client_input.start)
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
	if (p->cn) {
		close(p->pfd);
		switch (nextaction) {
		case HC_CLOSING:
			close_connection(p->cn);
			break;
		case HC_REINIT:
			reinit_connection(p->cn);
			break;
		default:
			set_connection_state(p->cn, nextaction);
			break;
		}
	}
	p->cn = 0;
	p_unlink(p, &busy_pipes);
	p_link(p, &free_pipes);
}

void close_children(void)
{
	struct pipe_params *p, *n;

	p = busy_pipes.head;
	while (p) {
		n = p->next;
		close_child(p, HC_CLOSING);
		p = n;
	}
}

int run_children(void)
{
	struct pipe_params *p, *n;

	p = busy_pipes.head;
	while (p) {
		n = p->next;
		if (p->cn)
			pipe_run(p);
		p = n;
	}
	return 0;
}

static int childisfinished(struct pipe_params *p)
{
	if (p->script_input.state != 3)
		return 0;
	if (p->client_output.end > p->client_output.start)
		return 0;
	if (p->script_input.end > p->script_input.start)
		return 0;
	if (p->client_input.state == 1 && p->imax)
		return 0;
	return 1;
}

void cleanup_children(void)
{
	struct pipe_params *p, *n;

	p = busy_pipes.head;
	while (p) {
		n = p->next;
		if (p->cn == 0) {
			log_d("cleaning up orphan pipe");
			close_child(p, -1);
		} else {
			if (p->error_condition) {
				if (p->error_condition == STUB_ERROR_RESTART)
					close_child(p, cgi_error(p->cn->r) == -1 ? HC_CLOSING : HC_WRITING);
				else
					close_child(p, HC_CLOSING);
			} else if (current_time >= p->t + (time_t) tuning.script_timeout) {
				log_d("script timeout to %s[%hu]", inet_ntoa(p->cn->peer.sin_addr), ntohs(p->cn->peer.sin_port));
				close_child(p, HC_CLOSING);
			} else if (childisfinished(p))
				close_child(p, p->cn->keepalive ? HC_REINIT : HC_CLOSING);
		}
		p = n;
	}
}
