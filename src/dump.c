/*
 *   Copyright 1996, 1997, 1998, 2000, 2001, 2003 Michiel Boland.
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "mathopd.h"

static void dump_servers(FILE *f, struct server *s)
{
	char tmp[32];

	fprintf(f, "server                 accepts  handled\n");
	while (s) {
		sprintf(tmp, "%s:%lu", inet_ntoa(s->addr), s->port);
		fprintf(f, "%-21s %8lu %8lu\n", tmp, s->naccepts, s->nhandled);
		s = s->next;
	}
	fprintf(f, "\n");
}

static void dump_connections(FILE *f, struct connection *currcon)
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
		if (cn == currcon)
			putc('*', f);
		else
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

static void fdump(FILE *f, struct request *r)
{
	struct rusage ru;

	fprintf(f,
		"Uptime: %d seconds\n"
		"Active connections: %d out of %lu\n"
		"Max simultaneous connections since last dump: %d\n"
		"Number of exited children: %d\n"
		"Number of requests executed: %lu\n"
		"\n",
		(int) (current_time - startuptime),
		stats.nconnections, tuning.num_connections,
		stats.maxconnections,
		numchildren,
		stats.nrequests);
	getrusage(RUSAGE_SELF, &ru);
	fprintf(f, "CPU time used by this process: %11.2f user %11.2f system\n", ru.ru_utime.tv_sec + 1e-6 * ru.ru_utime.tv_usec, ru.ru_stime.tv_sec + 1e-6 * ru.ru_stime.tv_usec);
	getrusage(RUSAGE_CHILDREN, &ru);
	fprintf(f, "                     children: %11.2f user %11.2f system\n\n", ru.ru_utime.tv_sec + 1e-6 * ru.ru_utime.tv_usec, ru.ru_stime.tv_sec + 1e-6 * ru.ru_stime.tv_usec);
	stats.maxconnections = stats.nconnections;
	dump_servers(f, servers);
	dump_connections(f, r ? r->cn : 0);
	fprintf(f, "*** End of dump\n");
}

int process_dump(struct request *r)
{
	FILE *f;
	int fd, fd2;
	char name[32];

	if (r->method != M_GET && r->method != M_HEAD)
		return 405;
	if (r->path_args[0]) {
		r->error_file = r->c->error_404_file;
		return 404;
	}
	strcpy(name, "/tmp/mathop-dump.XXXXXXXX");
	fd = mkstemp(name);
	if (fd == -1)
		return 500;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	if (remove(name) == -1) {
		log_d("cannot remove temporary file %s", name);
		lerror("remove");
		close(fd);
		return -1;
	}
	fd2 = dup(fd);
	if (fd2 == -1) {
		lerror("dup");
		close(fd);
		return 500;
	}
	fcntl(fd2, F_SETFD, FD_CLOEXEC);
	f = fdopen(fd2, "a+");
	if (f == 0) {
		log_d("dump: failed to associate stream with descriptor %d", fd2);
		close(fd2);
		close(fd);
		return 500;
	}
	fdump(f, r);
	if (fclose(f) == EOF) {
		lerror("fclose");
		close(fd2);
		close(fd);
		return 500;
	}
	if (fstat(fd, &r->finfo) == -1) {
		lerror("fstat");
		close(fd);
		return 500;
	}
	r->content_length = r->finfo.st_size;
	r->last_modified = r->finfo.st_mtime;
	if (r->method == M_GET) {
		lseek(fd, 0, SEEK_SET);
		r->cn->rfd = fd;
	} else
		close(fd);
	r->content_type = "text/plain";
	r->num_content = 0;
	return 200;
}

void internal_dump(void)
{
	FILE *f;
	char name[32];
	struct timeval tv;

	sprintf(name, "/tmp/mathopd-%d-dump", my_pid);
	f = fopen(name, "a");
	if (f == 0) {
		log_d("cannot open %s for appending", name);
		lerror("fopen");
		return;
	}
	gettimeofday(&tv, 0);
	fprintf(f, "*** Dump performed at %.6f\n", tv.tv_sec + 1e-6 * tv.tv_usec);
	fdump(f, 0);
	fclose(f);
}
