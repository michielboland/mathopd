/*
 *   Copyright 1996, 1997, 1998, 2000, 2001 Michiel Boland.
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

static void dump_connections(FILE *f, struct connection *cn)
{
	int ncrd, ncwr, ncwt, i;

	ncrd = 0;
	ncwr = 0;
	ncwt = 0;
	i = -1;
	fprintf(f, "Connections:\n");
	while (cn) {
		if (++i == 50) {
			putc('\n', f);
			i = 0;
		}
		switch (cn->state) {
		case HC_FREE:
			putc('.', f);
			break;
		case HC_ACTIVE:
			switch (cn->action) {
			case HC_READING:
				putc('r', f);
				ncrd++;
				break;
			case HC_WRITING:
				putc('W', f);
				ncwr++;
				break;
			case HC_WAITING:
				putc('-', f);
				ncwt++;
				break;
			default:
				putc('?', f);
				break;
			}
			break;
		case HC_FORKED:
			putc('F', f);
			break;
		default:
			putc('#', f);
			break;
		}
		cn = cn->next;
	}
	fprintf(f, "\nReading: %d, Writing: %d, Waiting: %d\n", ncrd, ncwr, ncwt);
}

static void fdump(FILE *f)
{
	fprintf(f,
		"Uptime: %d seconds\n"
		"Active connections: %d out of %lu\n"
		"Max simultaneous connections since last dump: %d\n"
		"Number of exited children: %d\n"
		"\n",
		(int) (current_time - startuptime),
		nconnections, tuning.num_connections,
		maxconnections,
		numchildren);
	maxconnections = nconnections;
	dump_servers(f, servers);
	dump_connections(f, connections);
	fprintf(f, "*** End of dump\n");
}

static int dump(int fd)
{
	FILE *f;
	int fd2;

	fd2 = dup(fd);
	if (fd2 == -1) {
		log_d("dump: failed to duplicate file descriptor %d", fd);
		lerror("dup");
		return -1;
	}
	f = fdopen(fd2, "a+");
	if (f == 0) {
		log_d("dump: failed to associate stream with descriptor %d", fd2);
		close(fd2);
		return -1;
	}
	fdump(f);
	if (fclose(f) == EOF) {
		lerror("fclose");
		close(fd2);
		return -1;
	}
	return 0;
}

static int do_dump(int fd, const char *name, struct request *r)
{
	if (remove(name) == -1) {
		log_d("do_dump: cannot remove temporary file %s", name);
		lerror("remove");
		return -1;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	if (dump(fd) == -1) {
		log_d("do_dump: failed to dump to file %s", name);
		return -1;
	}
	if (fstat(fd, &r->finfo) == -1) {
		lerror("fstat");
		return -1;
	}
	return 0;
}

int process_dump(struct request *r)
{
	int fd;
	char tmpbuf[32];

	if (r->method != M_GET && r->method != M_HEAD)
		return 405;
	if (r->path_args[0]) {
		r->error_file = r->c->error_404_file;
		return 404;
	}
	strcpy(tmpbuf, "/tmp/mathop-dump.XXXXXXXX");
	fd = mkstemp(tmpbuf);
	if (fd == -1) {
		lerror("mkstemp");
		return 500;
	}
	if (do_dump(fd, tmpbuf, r) == -1) {
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
