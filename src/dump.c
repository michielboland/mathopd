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

/* As */

static const char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "mathopd.h"

static void dump_connections(FILE *f)
{
	int i, n_reading, n_writing, n_waiting, n_forked;
	size_t n;
	struct connection *cn;

	n_reading = 0;
	n_writing = 0;
	n_waiting = 0;
	n_forked = 0;
	i = -1;
	fprintf(f, "Connections:\n");
	for (n = 0; n < tuning.num_connections; n++) {
		cn = connection_array + n;
		if (++i == 50) {
			putc('\n', f);
			i = 0;
		}
		switch (cn->connection_state) {
		case HC_FREE:
			putc('.', f);
			break;
		case HC_READING:
			putc('r', f);
			++n_reading;
			break;
		case HC_WRITING:
			putc('W', f);
			++n_writing;
			break;
		case HC_WAITING:
			putc('-', f);
			++n_waiting;
			break;
		case HC_FORKED:
			putc('F', f);
			++n_forked;
			break;
		default:
			putc('?', f);
			break;
		}
		cn = cn->next;
	}
	fprintf(f, "\nReading: %d, Writing: %d, Waiting: %d, Forked: %d\n", n_reading, n_writing, n_waiting, n_forked);
}

static void tvadd(struct timeval *t1, struct timeval *t2, struct timeval *r)
{
	long u;

	r->tv_sec = t1->tv_sec + t2->tv_sec;
	u = t1->tv_usec + t2->tv_usec;
	if (u >= 1000000) {
		u -= 1000000;
		++r->tv_sec;
	}
	r->tv_usec = u;
}

static void fdump(FILE *f)
{
	struct rusage ru;
	struct timeval t;

	fprintf(f, "Uptime: %d seconds\n", (int) (current_time - startuptime));
	fprintf(f, "Active connections: %d out of %lu\n", stats.nconnections, tuning.num_connections);
	fprintf(f, "Max simultaneous connections since last dump: %d\n", stats.maxconnections);
	fprintf(f, "Forked child processes: %lu\n", stats.forked_children);
	fprintf(f, "Exited child processes: %lu\n", stats.exited_children);
	fprintf(f, "Requests executed: %lu\n", stats.nrequests);
	fprintf(f, "Accepted connections: %lu\n", stats.accepted_connections);
	fprintf(f, "Pipelined requests: %lu\n", stats.pipelined_requests);
	fprintf(f, "\n");
	getrusage(RUSAGE_SELF, &ru);
	tvadd(&ru.ru_utime, &ru.ru_stime, &t);
	fprintf(f, "CPU time used by this process: %8ld.%02ld\n", t.tv_sec, t.tv_usec / 10000);
	getrusage(RUSAGE_CHILDREN, &ru);
	tvadd(&ru.ru_utime, &ru.ru_stime, &t);
	fprintf(f, "                     children: %8ld.%02ld\n\n", t.tv_sec, t.tv_usec / 10000);
	stats.maxconnections = stats.nconnections;
	dump_connections(f);
	fprintf(f, "*** End of dump\n");
}

int process_dump(struct request *r)
{
	FILE *f;
	int fd, fd2;
	char name[32];

	if (r->method != M_GET && r->method != M_HEAD) {
		r->status = 405;
		return 0;
	}
	if (r->path_args[0]) {
		r->error_file = r->c->error_404_file;
		r->status = 404;
		return 1;
	}
	strcpy(name, "/tmp/mathop-dump.XXXXXXXX");
	fd = mkstemp(name);
	if (fd == -1) {
		r->status = 500;
		return 0;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	if (remove(name) == -1) {
		log_d("cannot remove temporary file %s", name);
		lerror("remove");
		close(fd);
		r->status = 500;
		return 0;
	}
	fd2 = dup(fd);
	if (fd2 == -1) {
		lerror("dup");
		close(fd);
		r->status = 500;
		return 0;
	}
	fcntl(fd2, F_SETFD, FD_CLOEXEC);
	f = fdopen(fd2, "a+");
	if (f == 0) {
		log_d("dump: failed to associate stream with descriptor %d", fd2);
		close(fd2);
		close(fd);
		r->status = 500;
		return 0;
	}
	fdump(f);
	if (fclose(f) == EOF) {
		lerror("fclose");
		close(fd2);
		close(fd);
		r->status = 500;
		return 0;
	}
	if (fstat(fd, &r->finfo) == -1) {
		lerror("fstat");
		close(fd);
		r->status = 500;
		return 0;
	}
	r->content_length = r->finfo.st_size;
	r->last_modified = r->finfo.st_mtime;
	if (r->method == M_GET) {
		lseek(fd, 0, SEEK_SET);
		r->cn->file_offset = 0;
		r->cn->rfd = fd;
	} else
		close(fd);
	r->content_type = "text/plain";
	r->num_content = 0;
	r->status = 200;
	return 0;
}

void internal_dump(void)
{
	FILE *f;
	int fd;
	char name[40];
	struct timeval tv;

	sprintf(name, "/tmp/mathopd-%d-dump.XXXXXXX", my_pid);
	fd = mkstemp(name);
	if (fd == -1) {
		lerror("mkstemp");
		return;
	}
	f = fdopen(fd, "a");
	if (f == 0) {
		lerror("fdopen");
		close(fd);
		return;
	}
	gettimeofday(&tv, 0);
	fprintf(f, "*** Dump performed at %ld.%06ld\n", tv.tv_sec, tv.tv_usec);
	fdump(f);
	fclose(f);
}
