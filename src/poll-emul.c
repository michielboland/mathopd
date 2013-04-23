/*
 *   Copyright 2001, 2002 Michiel Boland.
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

/* Dave. My mind is going. I can feel it. */

#include <sys/types.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>

#include "poll-emul.h"

int poll(struct pollfd *fds, unsigned n, int timeout)
{
	fd_set rfds;
	fd_set wfds;
	unsigned i;
	int nfds;
	struct timeval tv;
	int rv;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	nfds = 0;
	for (i = 0; i < n; i++) {
		if (fds[i].fd != -1) {
			if (fds[i].events & POLLIN)
				FD_SET(fds[i].fd, &rfds);
			if (fds[i].events & POLLOUT)
				FD_SET(fds[i].fd, &wfds);
			if (nfds < fds[i].fd + 1)
				nfds = fds[i].fd + 1;
		}
	}
	if (timeout == INFTIM)
		rv = select(nfds, &rfds, &wfds, 0, 0);
	else {
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		rv = select(nfds, &rfds, &wfds, 0, &tv);
	}
	if (rv == -1)
		return -1;
	for (i = 0; i < n; i++) {
		fds[i].revents = 0;
		if (fds[i].fd != -1) {
			if (FD_ISSET(fds[i].fd, &rfds))
				fds[i].revents |= POLLIN;
			if (FD_ISSET(fds[i].fd, &wfds))
				fds[i].revents |= POLLOUT;
		}
	}
	return rv;
}
