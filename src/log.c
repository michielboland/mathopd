/*
 *   Copyright 1999, 2000, 2001 Michiel Boland.
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

/* Piece Of My Luck */

static const char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include "mathopd.h"

static int log_file = -1;
static int error_file = -1;

void log_request(struct request *r)
{
	const char *s;
	char tmp[20];
	long cl;
	int i, l, n;
	int left;
	char buf[800];
	char *b;
	static int l1, l2;
	int rok;

	if (log_file == -1)
		return;
	if (log_columns <= 0) {
		if (l1 == 0) {
			l1 = 1;
			log_d("log_request: nothing to log!");
		}
		return;
	}
	left = sizeof buf - log_columns;
	if (left < 0) {
		if (l2 == 0) {
			l2 = 1;
			log_d("log_request: buffer too small!?!?");
		}
		return;
	}
	b = buf;
	rok = r->processed;
	for (i = 0; i < log_columns; i++) {
		l = sizeof buf;
		s = 0;
		switch (log_column[i]) {
		case ML_CTIME:
			s = ctime(&current_time);
			l = 24;
			break;
		case ML_USERNAME:
			if (rok && r->user[0])
				s = r->user;
			break;
		case ML_ADDRESS:
			sprintf(tmp, "%s", inet_ntoa(r->cn->peer.sin_addr));
			s = tmp;
			break;
		case ML_PORT:
			sprintf(tmp, "%hu", ntohs(r->cn->peer.sin_port));
			s = tmp;
			break;
		case ML_SERVERNAME:
			if (rok && r->vs)
				s = r->vs->fullname;
			break;
		case ML_METHOD:
			if (rok)
				s = r->method_s;
			break;
		case ML_URI:
			if (rok)
				s = r->url;
			break;
		case ML_VERSION:
			if (rok)
				s = r->version;
			break;
		case ML_STATUS:
			if (rok) {
				s = r->status_line;
				l = 3;
			}
			break;
		case ML_CONTENT_LENGTH:
			if (rok) {
				cl = r->num_content;
				if (cl >= 0)
					cl = r->content_length;
				if (cl < 0)
					cl = 0;
				sprintf(tmp, "%ld", cl);
				s = tmp;
			}
			break;
		case ML_REFERER:
			if (rok)
				s = r->referer;
			break;
		case ML_USER_AGENT:
			if (rok)
				s = r->user_agent;
			break;
		case ML_BYTES_READ:
			sprintf(tmp, "%lu", r->cn->nread);
			s = tmp;
			break;
		case ML_BYTES_WRITTEN:
			sprintf(tmp, "%lu", r->cn->nwritten);
			s = tmp;
			break;
		}
		if (s == 0 || *s == 0) {
			if (left) {
				*b++ = '-';
				--left;
			}
		} else {
			n = strlen(s);
			if (n > l)
				n = l;
			if (n > left)
				n = left;
			memcpy(b, s, n);
			left -= n;
			b += n;
		}
		*b++ = '\t';
	}
	b[-1] = '\n';
	if (write(log_file, buf, b - buf) == -1) {
		gotsigterm = 1;
		log_d("log_request: cannot write to log file");
		lerror("write");
	}
	return;
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
		nfd = open(n, O_WRONLY | O_CREAT | O_APPEND, 0666);
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

int init_logs(void)
{
	return init_log_d(log_filename, &log_file) == -1 || init_log_d(error_filename, &error_file) == -1 ? -1 : 0;
}

void log_d(const char *fmt, ...)
{
	va_list ap;
	char log_line[1000];
	int m, n, saved_errno;
	char *ti;
	size_t l;

	if (error_file == -1 && am_daemon)
		return;
	va_start(ap, fmt);
	saved_errno = errno;
	ti = ctime(&current_time);
	l = sprintf(log_line, "%.24s [%d] ", ti, my_pid);
	m = sizeof log_line - l - 1;
	n = vsnprintf(log_line + l, m, fmt, ap);
	l += n < m ? n : m - 1;
	log_line[l++] = '\n';
	write(error_file, log_line, l);
	if (am_daemon == 0 && forked == 0)
		write(2, log_line, l);
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
		log_d("%s: %s", s, errmsg ? errmsg : "???");
	else
		log_d("%s", errmsg ? errmsg : "???");
	errno = saved_errno;
}
