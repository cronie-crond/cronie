/*
 * $Id: funcs.h,v 1.9 2004/01/23 18:56:42 vixie Exp $
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

/* Notes:
 *	We should reorg this into sections by module.
 */

#ifndef CRONIE_FUNCS_H
#define CRONIE_FUNCS_H

#include <stdio.h>
#include <sys/types.h>

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
#endif

#include "externs.h"
#include "structs.h"

void		set_cron_uid(void),
		check_spool_dir(void),
		open_logfile(void),
		sigpipe_func(void),
		job_add(entry *, user *),
		do_command(entry *, user *),
		link_user(cron_db *, user *),
		unlink_user(cron_db *, user *),
		free_user(user *),
		env_free(char **),
		unget_char(int, FILE *),
		free_entry(entry *),
		acquire_daemonlock(int),
		skip_comments(FILE *),
		log_it(const char *, PID_T, const char *, const char *, int),
		log_close(void),
		check_orphans(cron_db *);
#if defined WITH_INOTIFY
void 		set_cron_watched(int ),
		set_cron_unwatched(int ),
		check_inotify_database(cron_db *);
#endif

int		load_database(cron_db *),
		job_runqueue(void),
		set_debug_flags(const char *),
		get_char(FILE *),
		get_string(char *, int, FILE *, const char *),
		swap_uids(void),
		swap_uids_back(void),
		load_env(char *, FILE *),
		env_set_from_environ(char ***envpp),
		cron_pclose(FILE *),
		glue_strings(char *, size_t, const char *, const char *, char),
		strcmp_until(const char *, const char *, char),
		allowed(const char * ,const char * ,const char *);

size_t		strlens(const char *, ...),
		strdtb(char *);

char		*env_get(const char *, char **),
		*arpadate(time_t *),
		*mkprints(unsigned char *, size_t),
		*first_word(const char *, const char *),
		**env_init(void),
		**env_copy(char **),
		**env_set(char **, const char *),
		**env_update_home(char **, const char *);

user		*load_user(int, struct passwd *, const char *, const char *, const char *),
		*find_user(cron_db *, const char *, const char *);

entry		*load_entry(FILE *, void (*)(), struct passwd *, char **);

FILE		*cron_popen(char *, const char *, struct passwd *, char **);

struct passwd	*pw_dup(const struct passwd *);

#ifndef HAVE_STRUCT_TM_TM_GMTOFF
long		get_gmtoff(time_t *, struct tm *);
#endif

/* Red Hat security stuff (security.c): 
 */
void cron_restore_default_security_context( void );

int cron_set_job_security_context( entry *e, user *u, char ***jobenvp );

int cron_open_security_session( struct passwd *pw );

void cron_close_security_session( void );

int cron_change_groups( struct passwd *pw );

int cron_change_user_permanently( struct passwd *pw, char *homedir );

int get_security_context(const char *name, 
			 int crontab_fd, 
			 security_context_t *rcontext, 
			 const char *tabname
                        );

void free_security_context( security_context_t *scontext );

int crontab_security_access(void);

/* PAM */
#ifdef WITH_PAM
int cron_start_pam(struct passwd *pw);
void cron_close_pam(void);
#endif

#endif /* CRONIE_FUNCS_H */
