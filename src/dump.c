/*
 *   Copyright 1996, 1997, 1998 Michiel Boland.
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

#include "mathopd.h"

static int dump_children(int fd, struct virtual *v)
{
	int l;
	char buf[256];

	while (v) {
		l = sprintf(buf, "VHB %.200s %lu %lu\n", v->fullname, v->nrequests, v->nwritten);
		if (write(fd, buf, l) == -1) {
			lerror("write");
			return -1;
		}
		v = v->next;
	}
	return 0;
}

static int dump_servers(int fd, struct server *s)
{
	int l;
	char buf[200];

	while (s) {
		l = sprintf(buf, "SAH %s:%d %lu %lu\n", inet_ntoa(s->addr), s->port, s->naccepts, s->nhandled);
		if (write(fd, buf, l) == -1) {
			lerror("write");
			return -1;
		}
		if (dump_children(fd, s->children) == -1)
			return -1;
		s = s->next;
	}
	return 0;
}

static int dump(int fd)
{
	int l;
	char buf[80];

	l = sprintf(buf, "*** Start of dump\n");
	if (write(fd, buf, l) == -1) {
		lerror("write");
		return -1;
	}
	l = sprintf(buf, "SCM %lu %lu %d\n", startuptime, current_time, maxconnections);
	if (write(fd, buf, l) == -1) {
		lerror("write");
		return -1;
	}
	maxconnections = nconnections;
	if (dump_servers(fd, servers) == -1)
		return -1;
	l = sprintf(buf, "*** End of dump\n");
	if (write(fd, buf, l) == -1) {
		lerror("write");
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
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
		log_d("do_dump: failed to set FD_CLOEXEC flag !?!?!?");
		lerror("fcntl");
		return -1;
	}
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
	int fd, rv;
	char tmpbuf[32];

	if (r->method != M_GET && r->method != M_HEAD)
		return 405;
	if (r->path_args[0]) {
		log_d("process_dump: no crap");
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
		rv = close(fd);
		return 500;
	}
	r->content_length = r->finfo.st_size;
	r->last_modified = r->finfo.st_mtime;
	if (r->method == M_GET) {
		rv = lseek(fd, 0, SEEK_SET);
		r->cn->rfd = fd;
	} else {
		rv = close(fd);
	}
	r->content_type = "text/plain";
	r->num_content = 0;
	return 200;
}
