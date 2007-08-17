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

#if !defined(lint) && !defined(LINT)
static char rcsid[] = "$Id: user.c,v 1.5 2004/01/23 18:56:43 vixie Exp $";
#endif

/* vix 26jan87 [log is in RCS file]
 */

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
#include <selinux/flask.h>
#include <selinux/av_permissions.h>
#include <selinux/get_context_list.h>
#endif

#include "cron.h"

#ifdef WITH_SELINUX
static	int get_security_context(const char *name, 
				 int crontab_fd, 
				 security_context_t *rcontext, 
				 const char *tabname) {
	security_context_t scontext;
	security_context_t  file_context=NULL;
	struct av_decision avd;
	int retval=0;
	*rcontext = NULL;
	if (get_default_context(name, NULL, &scontext)) {
		if (security_getenforce() > 0) {
			log_it(name, getpid(), "No SELinux security context",tabname);
			return -1;
		} else {
			log_it(name, getpid(), "No security context but SELinux in permissive mode, continuing",tabname);
		}
	}
	
	if (fgetfilecon(crontab_fd, &file_context) < OK) {
		if (security_getenforce() > 0) {
			log_it(name, getpid(), "getfilecon FAILED", tabname);
			freecon(scontext);
			return -1;
		} else {
			log_it(name, getpid(), "getfilecon FAILED but SELinux in permissive mode, continuing", tabname);
			*rcontext=scontext;
			return 0;
		}
	}
    
	/*
	 * Since crontab files are not directly executed,
	 * crond must ensure that the crontab file has
	 * a context that is appropriate for the context of
	 * the user cron job.  It performs an entrypoint
	 * permission check for this purpose.
	 */
	retval = security_compute_av(scontext,
				     file_context,
				     SECCLASS_FILE,
				     FILE__ENTRYPOINT,
				     &avd);
	freecon(file_context);
	if (retval || ((FILE__ENTRYPOINT & avd.allowed) != FILE__ENTRYPOINT)) {
		if (security_getenforce() > 0) {
			log_it(name, getpid(), "ENTRYPOINT FAILED", tabname);
			freecon(scontext);
			return -1;
		} else {
			log_it(name, getpid(), "ENTRYPOINT FAILED but SELinux in permissive mode, continuing", tabname);
		}
	}
	*rcontext=scontext;
	return 0;
}
#endif

void
free_user(user *u) {
	entry *e, *ne;

	free(u->name);
	for (e = u->crontab;  e != NULL;  e = ne) {
		ne = e->next;
		free_entry(e);
	}
#ifdef WITH_SELINUX
	if( u->scontext != NULL )
	    freecon(u->scontext);
#endif	
	free(u);
}

user *
load_user(int crontab_fd, struct passwd	*pw, const char *uname, const char *fname, const char *tabname) {
	char envstr[MAX_ENVSTR];
	FILE *file;
	user *u;
	entry *e;
	int status, save_errno;
	char **envp, **tenvp;

	if (!(file = fdopen(crontab_fd, "r"))) {
		perror("fdopen on crontab_fd in load_user");
		return (NULL);
	}

	Debug(DPARS, ("load_user()\n"))

	/* file is open.  build user entry, then read the crontab file.
	 */
	if ((u = (user *) malloc(sizeof(user))) == NULL)
		return (NULL);
	if ((u->name = strdup(fname)) == NULL) {
		save_errno = errno;
		free(u);
		errno = save_errno;
		return (NULL);
	}
	u->crontab = NULL;

	/* init environment.  this will be copied/augmented for each entry.
	 */
	if ((envp = env_init()) == NULL) {
		save_errno = errno;
		free(u->name);
		free(u);
		errno = save_errno;
		return (NULL);
	}

#ifdef WITH_SELINUX
	if (is_selinux_enabled() > 0) {
		const char *sname=uname;
		if (pw==NULL) {
			sname="system_u";
		}

		if (get_security_context(sname, crontab_fd, 
					 &u->scontext, tabname) != 0) {
			free_user(u);
			u = NULL;
			goto done;
		}
	}else
	    u->scontext = NULL;
#endif

	/* load the crontab
	 */
	while ((status = load_env(envstr, file)) >= OK) {
		switch (status) {
		case ERR:
			free_user(u);
			u = NULL;
			goto done;
		case FALSE:
			e = load_entry(file, NULL, pw, envp);
			if (e) {
				e->next = u->crontab;
				u->crontab = e;
			}
			break;
		case TRUE:
			if ((tenvp = env_set(envp, envstr)) == NULL) {
				save_errno = errno;
				free_user(u);
				u = NULL;
				errno = save_errno;
				goto done;
			}
			envp = tenvp;
			break;
		}
	}

 done:
	env_free(envp);
	fclose(file);
	Debug(DPARS, ("...load_user() done\n"))
	return (u);
}
