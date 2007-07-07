/*
 *   Copyright 2003, Michiel Boland.
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

/* I will now proceed to entangle the entire area. */

static const char rcsid[] = "$Id$";

#if defined LINUX_SENDFILE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include "mathopd.h"
 
int set_nopush(int sock, int onoff)
{
	if (debug)
		log_d("set_nopush: %d %d", sock, onoff);
	return setsockopt(sock, IPPROTO_TCP, TCP_CORK, &onoff, sizeof onoff);
}

off_t sendfile_connection(struct connection *cn)
{
	ssize_t s;

	if (cn->rfd == -1)
		return 0;
	if (cn->left == 0)
		return 0;
	s = sendfile(cn->fd, cn->rfd, &cn->file_offset, cn->left);
	if (debug)
		log_d("sendfile_connection: %d %d %jd %zd", cn->rfd, cn->fd, cn->left, s);
	if (s == -1) {
		if (errno == EAGAIN)
			return 0;
		if (debug)
			lerror("sendfile");
		return -1;
	}
	if (s) {
		cn->left -= s;
		cn->nwritten += s;
		cn->t = current_time;
	} else {
		log_d("premature end of file %s", cn->r->path_translated);
		return -1;
	}
	return s;
}

#elif defined FREEBSD_SENDFILE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include "mathopd.h"

int set_nopush(int sock, int onoff)
{
	if (debug)
		log_d("set_nopush: %d %d", sock, onoff);
	return setsockopt(sock, IPPROTO_TCP, TCP_NOPUSH, &onoff, sizeof onoff);
}

off_t sendfile_connection(struct connection *cn)
{
	int rv;
	off_t n;

	if (cn->rfd == -1)
		return 0;
	if (cn->left == 0)
		return 0;
	n = 0;
	rv = sendfile(cn->rfd, cn->fd, cn->file_offset, cn->left, 0, &n, 0);
	if (debug)
		log_d("sendfile_connection: %d %d %jd %jd", cn->rfd, cn->fd, cn->left, (intmax_t) n);
	if (rv == -1 && errno != EAGAIN) {
		if (debug)
			lerror("sendfile");
		return -1;
	}
	if (n) {
		cn->left -= n;
		cn->nwritten += n;
		cn->t = current_time;
		cn->file_offset += n;
	} else {
		log_d("premature end of file %s", cn->r->path_translated);
		return -1;
	}
	return n;
}

#endif
