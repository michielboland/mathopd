/*
 *   Copyright 1999 - 2004 Michiel Boland.
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
#include <sys/time.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include "mathopd.h"

static int log_fd = -1;
static int error_fd = -1;
static int tee_fd = -1;
static char *log_buffer;
static size_t log_buffer_size;

int init_log_buffer(size_t size)
{
	log_buffer_size = size;
	if (size == 0)
		return 0;
	log_buffer = malloc(size);
	if (log_buffer == 0) {
		log_d("init_log_buffer: out of memory");
		return -1;
	}
	return 0;
}

static void tvdiff(const struct timeval *t1, const struct timeval *t2, struct timeval *r)
{
	long u;

	r->tv_sec = t1->tv_sec - t2->tv_sec;
	u = t1->tv_usec - t2->tv_usec;
	if (u < 0) {
		u += 1000000;
		--r->tv_sec;
	}
	r->tv_usec = u;
}

void log_request(struct request *r)
{
	const char *s;
	char tmp[20];
	long cl;
	int i, l, n;
	int left;
	char *b;
	static int l1, l2;
	struct tm *tp;
	struct timeval tv, dtv;
	time_t rounded_time;

	if (log_fd == -1)
		return;
	if (log_columns <= 0) {
		if (l1 == 0) {
			l1 = 1;
			log_d("log_request: nothing to log!");
		}
		return;
	}
	left = log_buffer_size - log_columns;
	if (left < 0) {
		if (l2 == 0) {
			l2 = 1;
			log_d("log_request: buffer too small!?!?");
		}
		return;
	}
	b = log_buffer;
	gettimeofday(&tv, 0);
	rounded_time = tv.tv_sec;
	for (i = 0; i < log_columns; i++) {
		l = log_buffer_size;
		s = 0;
		switch (log_column[i]) {
		case ML_CTIME:
			tp = log_gmt ? gmtime(&rounded_time) : localtime(&rounded_time);
			s = asctime(tp);
			l = 24;
			break;
		case ML_USERNAME:
			s = r->user;
			break;
		case ML_REMOTE_ADDRESS:
			s = r->cn->peer.ap_address;
			break;
		case ML_REMOTE_PORT:
			s = r->cn->peer.ap_port;
			break;
		case ML_LOCAL_ADDRESS:
			s = r->cn->sock.ap_address;
			break;
		case ML_LOCAL_PORT:
			s = r->cn->sock.ap_port;
			break;
		case ML_SERVERNAME:
			s = r->host;
			break;
		case ML_METHOD:
			s = r->method_s;
			break;
		case ML_URI:
			s = r->url;
			break;
		case ML_VERSION:
			s = r->version;
			break;
		case ML_STATUS:
			sprintf(tmp, "%d", r->status);
			s = tmp;
			break;
		case ML_CONTENT_LENGTH:
			cl = r->num_content;
			if (cl >= 0)
				cl = r->content_length;
			if (cl < 0)
				cl = 0;
			sprintf(tmp, "%ld", cl);
			s = tmp;
			break;
		case ML_REFERER:
			s = r->referer;
			break;
		case ML_USER_AGENT:
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
		case ML_QUERY_STRING:
			s = r->args;
			break;
		case ML_TIME_TAKEN:
			tvdiff(&tv, &r->cn->itv, &dtv);
			sprintf(tmp, "%ld.%06ld", dtv.tv_sec, dtv.tv_usec);
			s = tmp;
			break;
		case ML_MICRO_TIME:
			sprintf(tmp, "%ld.%06ld", tv.tv_sec, tv.tv_usec);
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
	if (write(log_fd, log_buffer, b - log_buffer) == -1) {
		gotsigterm = 1;
		log_d("log_request: cannot write to log file");
		lerror("write");
	}
	return;
}

int open_log(const char *name)
{
	char converted_name[PATHLEN];
	const char *n;
	struct tm *tp;
	int rv;

	n = name;
	if (strchr(name, '%')) {
		current_time = time(0);
		if (log_gmt)
			tp = gmtime(&current_time);
		else
			tp = localtime(&current_time);
		if (tp) {
			if (strftime(converted_name, PATHLEN - 1, name, tp))
				n = converted_name;
		}
	}
	rv = open(n, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (rv == -1) {
		log_d("cannot open %s", n);
		lerror("open");
	}
	return rv;
}

static int init_log_d(char *name, int *fdp)
{
	int fd, nfd;

	if (name) {
		fd = *fdp;
		if (strcmp(name, "/dev/stdout") == 0) {
			if (fd != -1)
				return 0;
			nfd = dup(1);
		} else if (strcmp(name, "/dev/stderr") == 0) {
			if (fd != -1)
				return 0;
			nfd = dup(2);
		} else
			nfd = open_log(name);
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

int init_logs(int tee)
{
	if (tee && tee_fd == -1) {
		tee_fd = dup(2);
		if (tee_fd == -1)
			return -1;
	}
	return init_log_d(error_filename, &error_fd) == -1 || init_log_d(log_filename, &log_fd) == -1 ? -1 : 0;
}

void log_d(const char *fmt, ...)
{
	va_list ap;
	char log_line[1000];
	int m, n, saved_errno;
	char *ti;
	size_t l;
	struct tm *tp;

	if (error_fd == -1 && tee_fd == -1)
		return;
	va_start(ap, fmt);
	saved_errno = errno;
	if (log_gmt)
		tp = gmtime(&current_time);
	else
		tp = localtime(&current_time);
	ti = asctime(tp);
	l = sprintf(log_line, "%.24s [%d] ", ti, my_pid);
	m = sizeof log_line - l - 1;
	n = vsnprintf(log_line + l, m, fmt, ap);
	l += n < m ? n : m - 1;
	log_line[l++] = '\n';
	if (error_fd != -1 && write(error_fd, log_line, l) == -1)
		gotsigterm = 1;
	if (tee_fd != -1 && write(tee_fd, log_line, l) == -1) {
		close(tee_fd);
		tee_fd = -1;
	}
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
