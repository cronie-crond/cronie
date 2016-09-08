/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * All rights reserved
 */

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"

#include <stdlib.h>
#include <pwd.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "funcs.h"
#include "globals.h"

typedef struct _job {
	struct _job *next;
	entry *e;
	user *u;
} job;

static job *jhead = NULL, *jtail = NULL;

void job_add(entry * e, user * u) {
	job *j;
	struct passwd *newpwd;
	struct passwd *temppwd;
	const char *uname;

	/* if already on queue, keep going */
	for (j = jhead; j != NULL; j = j->next)
		if (j->e == e && j->u == u)
			return;

	uname = e->pwd->pw_name;
	/* check if user exists in time of job is being run f.e. ldap */
	if ((temppwd = getpwnam(uname)) != NULL) {
		char **tenvp;

		Debug(DSCH | DEXT, ("user [%s:%ld:%ld:...] cmd=\"%s\"\n",
				e->pwd->pw_name, (long) temppwd->pw_uid,
				(long) temppwd->pw_gid, e->cmd));
		if ((newpwd = pw_dup(temppwd)) == NULL) {
			log_it(uname, getpid(), "ERROR", "memory allocation failed", errno);
			return;
		}
		free(e->pwd);
		e->pwd = newpwd;

		if ((tenvp = env_update_home(e->envp, e->pwd->pw_dir)) == NULL) {
			log_it(uname, getpid(), "ERROR", "memory allocation failed", errno);
			return;
		}
		e->envp = tenvp;
	} else {
		log_it(uname, getpid(), "ERROR", "getpwnam() failed - user unknown",errno);
		Debug(DSCH | DEXT, ("%s:%d pid=%d time=%ld getpwnam(%s) failed errno=%d error=%s\n",
			__FILE__,__LINE__,getpid(),time(NULL),uname,errno,strerror(errno)));
		return;
	}

	/* build a job queue element */
	if ((j = (job *) malloc(sizeof (job))) == NULL)
		return;
	j->next = NULL;
	j->e = e;
	j->u = u;

	/* add it to the tail */
	if (jhead == NULL)
		jhead = j;
	else
		jtail->next = j;
	jtail = j;
}

int job_runqueue(void) {
	job *j, *jn;
	int run = 0;

	for (j = jhead; j; j = jn) {
		do_command(j->e, j->u);
		jn = j->next;
		free(j);
		run++;
	}
	jhead = jtail = NULL;
	return (run);
}
