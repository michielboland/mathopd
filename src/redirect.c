/*
 *   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002 Michiel Boland.
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

/* De verpletterende werkelijkheid */

static const char rcsid[] = "$Id$";

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "mathopd.h"

int process_redirect(struct request *r)
{
	char *c;
	int fd;
	ssize_t l;

	if (r->method != M_GET && r->method != M_HEAD)
		return 405;
	fd = open(r->path_translated, O_RDONLY | O_NONBLOCK);
	if (fd == -1) {
		log_d("process_redirect: cannot open %s", r->path_translated);
		lerror("open");
		return 500;
	}
	l = read(fd, r->newloc, PATHLEN - 1);
	if (l == -1) {
		log_d("process_redirect: cannot read %s", r->path_translated);
		lerror("read");
		close(fd);
		return 500;
	}
	r->newloc[l] = 0;
	close(fd);
	c = strchr(r->newloc, '\n');
	if (c == 0) {
		log_d("process_redirect: no newline in %s", r->path_translated);
		return 500;
	}
	*c = 0;
	r->location = r->newloc;
	return 302;
}
