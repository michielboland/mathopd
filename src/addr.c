/*
 *   Copyright 2013, Michiel Boland.
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

/* Did I mention the tank is a tank? */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include "mathopd.h"

void sockaddr_to_addrport(struct sockaddr *sa, struct addrport *ap)
{
	int l;

	switch (sa->sa_family) {
	case AF_INET:
		l = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		l = sizeof (struct sockaddr_in6);
		break;
	default:
		l = 0;
		break;
	}
	if (getnameinfo(sa, l, ap->ap_address, sizeof ap->ap_address, ap->ap_port, sizeof ap->ap_port, NI_NUMERICHOST | NI_NUMERICSERV)) {
		strcpy(ap->ap_address, "?");
		strcpy(ap->ap_port, "?");
	}
}

int match_address(struct sockaddr *a, struct sockaddr *b, unsigned prefixlen)
{
	unsigned char *abits, *bbits;

	if (a->sa_family != b->sa_family) {
		return 0;
	}
	switch (a->sa_family) {
	case AF_INET:
		abits = (unsigned char *) &((struct sockaddr_in *) a)->sin_addr;
		bbits = (unsigned char *) &((struct sockaddr_in *) b)->sin_addr;
		break;
	case AF_INET6:
		abits = (unsigned char *) &((struct sockaddr_in6 *) a)->sin6_addr;
		bbits = (unsigned char *) &((struct sockaddr_in6 *) b)->sin6_addr;
		break;
	default:
		return 0;
	}
	if (prefixlen >= 8) {
		if (memcmp(abits, bbits, prefixlen >> 3)) {
			return 0;
		}
	}
	if ((prefixlen & 7) == 0) {
		return 1;
	}
	return ((abits[prefixlen >> 3] ^ bbits[prefixlen >> 3]) & (0xff00 >> (prefixlen & 7))) == 0;
}
