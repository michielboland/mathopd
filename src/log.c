/*
 *   Copyright 1999 Michiel Boland.
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

#include "mathopd.h"

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

	if (log_file == -1)
		return;
	if (log_columns <= 0) {
		if (l1 == 0) {
			l1 = 1;
			log_d("log_trans: nothing to log!");
		}
		return;
	}
	left = sizeof buf - log_columns;
	if (left < 0) {
		if (l2 == 0) {
			l2 = 1;
			log_d("log_trans: buffer too small!?!?");
		}
		return;
	}
	b = buf;
	for (i = 0; i < log_columns; i++) {
		l = sizeof buf;
		switch (log_column[i]) {
		case ML_CTIME:
			s = ctime(&current_time);
			l = 24;
			break;
		case ML_USERNAME:
			s = r->user;
			break;
		case ML_ADDRESS:
			s = r->cn->ip;
			break;
		case ML_PORT:
			sprintf(tmp, "%hu", ntohs(r->cn->peer.sin_port));
			s = tmp;
			break;
		case ML_SERVERNAME:
			s = r->vs->fullname;
			break;
		case ML_METHOD:
			s = r->method_s;
			break;
		case ML_URI:
			s = r->url;
			break;
		case ML_STATUS:
			s = r->status_line;
			l = 3;
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
		default:
			s = 0;
			break;
		}
		if (s) {
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
		log_d("cannot write to log file");
		lerror("write");
	}
	return;
}
