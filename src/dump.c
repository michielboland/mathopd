/*
 * dump.c - Mathopd statistics dump
 *
 * Copyright 1998, Michiel Boland
 */

/* As */

#include "mathopd.h"

static void dump_children(struct virtual *v)
{
	while (v) {
		log(L_DEBUG, "dump_children(%p)", v);
		log(L_LOG, "VHB %s %lu %lu",
		    v->fullname, v->nrequests, v->nwritten);
		v = v->next;
	}
}

static void dump_servers(struct server *s)
{
	while (s) {
		log(L_DEBUG, "dump_servers(%p)", s);
		log(L_LOG, "SAH %s:%d %lu %lu",
		    inet_ntoa(s->addr), s->port,
		    s->naccepts, s->nhandled);
		dump_children(s->children);
		s = s->next;
	}
}

void dump(void)
{
	char *ti;

	ti = ctime(&current_time);
	log(L_LOG, "*** Dump at %.24s (pid %d)\n"
	    "SCM %lu %lu %d",
	    ti, my_pid,
	    startuptime, current_time, maxconnections);
	maxconnections = nconnections;

	dump_servers(servers);
	log(L_LOG, "*** End of dump (pid %d)", my_pid);
}
