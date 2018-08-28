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

/* vix 26jan87 [log is in RCS file]
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "funcs.h"
#include "globals.h"

static const char *FileName;

static void
log_error (const char *msg)
{
	log_it ("CRON", getpid (), msg, FileName, 0);
}

void
free_user (user * u) {
	entry *e, *ne;

	if (!u) {
		return;
	}

	free(u->name);
	free(u->tabname);
	for (e = u->crontab; e != NULL; e = ne)	{
		ne = e->next;
		free_entry(e);
	}
#ifdef WITH_SELINUX
	free_security_context(&(u->scontext));
#endif
	free(u);
}

user *
load_user (int crontab_fd, struct passwd *pw, const char *uname,
		   const char *fname, const char *tabname) {
	char envstr[MAX_ENVSTR];
	FILE *file;
	user *u;
	entry *e;
	int status = TRUE, save_errno = 0;
	char **envp = NULL, **tenvp;

	if (!(file = fdopen(crontab_fd, "r")))	{
		save_errno = errno;
		log_it(uname, getpid (), "FAILED", "fdopen on crontab_fd in load_user",
			save_errno);
		close(crontab_fd);
		return (NULL);
	}

	Debug(DPARS, ("load_user()\n"));
	/* file is open.  build user entry, then read the crontab file.
	 */
	if ((u = (user *) malloc (sizeof (user))) == NULL) {
		save_errno = errno;
		goto done;
	}
	memset(u, 0, sizeof(*u));

	if (((u->name = strdup(fname)) == NULL)
		|| ((u->tabname = strdup(tabname)) == NULL)) {
		save_errno = errno;
		goto done;
	}

	u->system = pw == NULL;

	/* init environment.  this will be copied/augmented for each entry.
	*/
	if ((envp = env_init()) == NULL) {
		save_errno = errno;
		goto done;
	}

	if (env_set_from_environ(&envp) == FALSE) {
		save_errno = errno;
		goto done;
	}

#ifdef WITH_SELINUX
	if (get_security_context(pw == NULL ? NULL : uname,
		crontab_fd, &u->scontext, tabname) != 0) {
		goto done;
	}
#endif
	/* load the crontab
	*/
	while ((status = load_env (envstr, file)) >= OK) {
		switch (status) {
			case FALSE:
				FileName = tabname;
				e = load_entry(file, log_error, pw, envp);
				if (e) {
					e->next = u->crontab;
					u->crontab = e;
				}
				break;
			case TRUE:
				if ((tenvp = env_set (envp, envstr)) == NULL) {
					save_errno = errno;
					goto done;
				}
				envp = tenvp;
				break;
		}
	}

done:
	if (status == TRUE) {
		log_it(uname, getpid(), "FAILED", "loading cron table",
			save_errno);
		free_user(u);
		u = NULL;
	}
	if (envp)
		env_free(envp);
	fclose(file);
	Debug(DPARS, ("...load_user() done\n"));
	errno = save_errno;
	return (u);
}
