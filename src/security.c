/* security.c 
 *
 * Implement Red Hat crond security context transitions
 *
 * Jason Vas Dias <jvdias@redhat.com> January 2006
 *
 * Copyright(C) Red Hat Inc., 2006
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

#include "cron.h"

#ifdef WITH_SELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#include <selinux/flask.h>
#include <selinux/av_permissions.h>
#include <selinux/get_context_list.h>
#endif

#ifdef WITH_AUDIT
#include <libaudit.h>
#define _GNU_SOURCE
#include <stdio.h>
#endif

static char ** build_env(char **cronenv);

#ifdef WITH_SELINUX
static int cron_change_selinux_range( user *u,
                                      security_context_t ucontext );
static int cron_get_job_range( user *u, security_context_t *ucontextp, char **jobenv );
#endif

void cron_restore_default_security_context() {
	setexeccon(NULL);
}

int cron_set_job_security_context(entry *e, user *u, char ***jobenv) {
    time_t minutely_time = 0;

    if ((e->flags & MIN_STAR)==MIN_STAR) {
		/* "minute-ly" job: Every minute for given hour/dow/month/dom. 
		 * Ensure that these jobs never run in the same minute:
		 */
		minutely_time = time(0);
		Debug(DSCH, ("Minute-ly job. Recording time %lu\n", minutely_time))
    }

#ifdef WITH_PAM
    if (cron_start_pam(e->pwd) != 0) {
		syslog(LOG_INFO, "CRON (%s): failed to open PAM security session: %s", e->pwd->pw_name, pam_strerror(pamh,cron_start_pam(e->pwd)));
		return -1;
    }
#endif

    *jobenv = build_env( e->envp );

#ifdef WITH_SELINUX
    /* we must get the crontab context BEFORE changing user, else
     * we'll not be permitted to read the cron spool directory :-)
     */
    security_context_t ucontext=0; 

    if (cron_get_job_range(u, &ucontext, *jobenv) < OK) {
		syslog(LOG_ERR, "CRON (%s) ERROR: failed to get selinux context: %s", 
	       e->pwd->pw_name, strerror(errno));
		return -1;
    }

    if (cron_change_selinux_range(u, ucontext) != 0) {
        syslog(LOG_INFO,"CRON (%s) ERROR: failed to change SELinux context", 
	       e->pwd->pw_name);
		if ( ucontext )
			freecon(ucontext);
		return -1;
    }
    if (ucontext)
		freecon(ucontext);
#endif

/*    if (cron_open_pam_session(e->pwd) != 0) {
		syslog(LOG_INFO, "CRON (%s) ERROR: failed to open PAM security session: %s", e->pwd->pw_name, strerror(errno));
		return -1;
    }
*/
    if (cron_change_user(e->pwd, env_get("HOME", *jobenv)) != 0) {
		syslog(LOG_INFO, "CRON (%s) ERROR: failed to open PAM security session: %s", e->pwd->pw_name, strerror(errno));
		return -1;
    }	

    log_close();
    openlog(ProgramName, LOG_PID, LOG_CRON);

    time_t job_run_time = time(0L);

    if ((minutely_time > 0)	&&((job_run_time / 60) != (minutely_time / 60))) {
     /* if a per-minute job is delayed into the next minute 
      * (eg. by network authentication method timeouts), skip it.
      */
		struct tm tmS, tmN;
		localtime_r(&job_run_time, &tmN);
		localtime_r(&minutely_time,&tmS);
		syslog(LOG_ERR, 
	       "(%s) error: Job execution of per-minute job scheduled for "
	       "%.2u:%.2u delayed into subsequent minute %.2u:%.2u. Skipping job run.",
	       e->pwd->pw_name, tmS.tm_hour, tmS.tm_min, tmN.tm_hour, tmN.tm_min);
		return -1;
    }
    return 0;
}

int cron_start_pam(struct passwd *pw) {
    int	retcode = 0;

#if defined(WITH_PAM)
    retcode = pam_start("crond", pw->pw_name, &conv, &pamh);
    PAM_FAIL_CHECK;
    retcode = pam_set_item(pamh, PAM_TTY, "cron");
    PAM_FAIL_CHECK;
    retcode = pam_acct_mgmt(pamh, PAM_SILENT);
    PAM_FAIL_CHECK;
#endif
	
    return retcode;
}

int cron_open_pam_session(struct passwd *pw) {
    int	retcode = 0;

#if defined(WITH_PAM)
    retcode = pam_setcred(pamh, PAM_ESTABLISH_CRED | PAM_SILENT);
    PAM_FAIL_CHECK;
    retcode = pam_open_session(pamh, PAM_SILENT);
    PAM_FAIL_CHECK;
	if (retcode == PAM_SUCCESS)
        pam_session_opened = 1;
#endif

    return retcode;
}

void cron_close_pam(void) {
#if defined(WITH_PAM)
    if (pam_session_opened != 0) {
	    pam_setcred(pamh, PAM_DELETE_CRED | PAM_SILENT);
	    pam_close_session(pamh, PAM_SILENT);
    }
    pam_end(pamh, PAM_SUCCESS);
#endif
}

int cron_change_user(struct passwd *pw, char *homedir) {
    /* set our directory, uid and gid.  Set gid first, since once
     * we set uid, we've lost root privledges.
     */
    if (setgid(pw->pw_gid) != 0) {
		log_it("CRON", getpid(), "setgid failed:", strerror(errno));
		return -1;
    }

    if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
		log_it("CRON", getpid(), "initgroups failed:", strerror(errno));
		return -1;	
    }

    if (setuid( pw->pw_uid ) != 0) {
		log_it("CRON", getpid(), "setuid failed:", strerror(errno));
		return -1;
    }

    if (chdir(homedir) == -1) {
	        log_it("CRON", getpid(), "chdir(HOME) failed:", strerror(errno));
                log_it("CRON", getpid(), homedir, strerror(errno));
                return -1;
    }
    return 0;
}

static int cron_authorize_context
(security_context_t scontext,security_context_t file_context) {
#ifdef WITH_SELINUX
	struct av_decision avd;
	int retval;
	unsigned int bit = FILE__ENTRYPOINT;
	/*
	 * Since crontab files are not directly executed,
	 * crond must ensure that the crontab file has
	 * a context that is appropriate for the context of
	 * the user cron job.  It performs an entrypoint
	 * permission check for this purpose.
	 */
	retval = security_compute_av(scontext, file_context,
				     SECCLASS_FILE, bit, &avd);
	if (retval || ((bit & avd.allowed) != bit))
		return 0;
#endif
	return 1;
}

static int cron_authorize_range
(security_context_t scontext,security_context_t ucontext) {
#ifdef WITH_SELINUX
	struct av_decision avd;
	int retval;
    unsigned int bit = CONTEXT__CONTAINS;
	/*
	 * Since crontab files are not directly executed,
	 * so crond must ensure that any user specified range
	 * falls within the seusers-specified range for that Linux user.
	 */
	retval = security_compute_av(scontext, ucontext,
				     SECCLASS_CONTEXT, bit, &avd);

	if (retval || ((bit & avd.allowed) != bit))
		return 0;
#endif
	return 1;
}

int cron_get_job_context(user *u, void *scontextp, void *file_contextp, char **jobenv) {
#if WITH_SELINUX
	char *sroletype;

	if (is_selinux_enabled() <= 0)
		return 0;
	if ((file_contextp == 0) || (scontextp == 0L))
		return -1;

	*((security_context_t*)scontextp) = u->scontext;
	*((void **)file_contextp) = 0L;

	if ((sroletype = env_get("SELINUX_ROLE_TYPE",jobenv)) != 0L) {
		*((security_context_t*)scontextp) = (security_context_t) sroletype;
		
		char crontab[MAX_FNAME];
		if (strcmp(u->name,"*system*") == 0)
			strncpy(crontab, u->tabname, MAX_FNAME);
		else
			snprintf(crontab, MAX_FNAME, "%s/%s", CRONDIR, u->tabname);

		if (getfilecon( crontab, file_contextp ) == -1) {
			if (security_getenforce() > 0) { 
				log_it(u->name, getpid(), "getfilecon FAILED for SELINUX_ROLE_TYPE", 
				       sroletype);
				return -1;
			} else if (access( crontab, F_OK ) == 0)
				log_it(u->name, getpid(), 
					"getfilecon FAILED but SELinux in permissive mode, continuing "
				    "- SELINUX_ROLE_TYPE=", sroletype);
		}
	}
#endif
	return 0;
}

#if WITH_SELINUX
/* always uses u->scontext as the default process context, then changes the
   level, and retuns it in ucontextp (or NULL otherwise) */
static int cron_get_job_range(user *u, security_context_t *ucontextp, char **jobenv) {
	char *range;

	if (is_selinux_enabled() <= 0)
		return 0;
	if (ucontextp == 0L)
		return -1;

	*ucontextp = 0L;

	if ((range = env_get("MLS_LEVEL",jobenv)) != 0L) {
	    context_t ccon;
        if (!(ccon = context_new(u->scontext))) {
			log_it(u->name, getpid(), "context_new FAILED for MLS_LEVEL", range);
	        return -1;
        }                  

        if (context_range_set(ccon, range)) {
            log_it(u->name, getpid(), "context_range_set FAILED for MLS_LEVEL", 
	            range);
            return -1;
        }

        if (!(*ucontextp = context_str(ccon))) {
	        log_it(u->name, getpid(), "context_str FAILED for MLS_LEVEL", 
	            range);
            return -1;
        }

        if (!(*ucontextp = strdup(*ucontextp))) {
	        log_it(u->name, getpid(), "strdup FAILED for MLS_LEVEL", 
	            range);
            return -1;
        }
        context_free(ccon);
	}
    else if (!u->scontext) { 
			/* cron_change_selinux_range() deals with this */
            return 0;
    }
    else if (!(*ucontextp = strdup(u->scontext))) {
    	log_it(u->name, getpid(), "strdup FAILED for MLS_LEVEL", 
	        range);
        return -1;
    }

	return 0;
}
#endif

int cron_change_selinux_context(user *u, void *scontext, void *file_context) {
#ifdef WITH_SELINUX
	if (is_selinux_enabled() <= 0)
		return 0;

	if (scontext == 0L) {
		if (security_getenforce() > 0) { 
			log_it( u->name, getpid(), "NULL security context for user", "");
			return -1;
		} 
		else {
			log_it( u->name, getpid(), 
				"NULL security context for user, "
				"but SELinux in permissive mode, continuing",
				"");
			return 0;
		}
	}
	
	if (file_context) {
		if (!cron_authorize_context( scontext, file_context)) {
			if (security_getenforce() > 0) {
				syslog(LOG_ERR,
				       "CRON (%s) ERROR:"
				       "Unauthorized exec context to SELINUX_ROLE_TYPE %s for user", 
				       u->name, (char*)scontext);
				return -1;
			}
			else {
				syslog(LOG_INFO,
				       "CRON (%s) WARNING:"
				       "Unauthorized exec context to SELINUX_ROLE_TYPE %s for user,"
				       " but SELinux in permissive mode, continuing", 
				       u->name, (char*)scontext);
			}
		}
	}

	if (setexeccon(scontext) < 0) { 
		if (security_getenforce() > 0) {
			syslog(LOG_ERR,
			       "CRON (%s) ERROR:"
			       "Could not set exec context to %s for user", 
			       u->name, (char*)scontext);
			return -1;
		}
	}
#endif
	return 0;
}

#ifdef WITH_SELINUX
static int cron_change_selinux_range(user *u,security_context_t ucontext) {
	if (is_selinux_enabled() <= 0)
		return 0;

	if (u->scontext == 0L) {
		if (security_getenforce() > 0) 
		{
			log_it( u->name, getpid(), 
				"NULL security context for user", 
				"");
			return -1;
		}
		else {
			log_it( u->name, getpid(), 
				"NULL security context for user, "
				"but SELinux in permissive mode, continuing",
				"");
			return 0;
		}
	}

	if (strcmp(u->scontext, ucontext)) {		
	    if (!cron_authorize_range( u->scontext, ucontext)) {
			if (security_getenforce() > 0) {
#ifdef WITH_AUDIT
				char *msg = NULL;
				if (asprintf(&msg, "cron: Unauthorized MLS range acct=%s new_scontext=%s old_scontext=%s",  
					u->name, (char*)ucontext, u->scontext) >= 0) {
					int audit_fd = audit_open();
					audit_log_user_message(audit_fd, AUDIT_USER_ROLE_CHANGE, msg, NULL, NULL, NULL, 0);
					close(audit_fd);
				}
				free(msg);
#endif
				syslog(LOG_ERR,
				       "CRON (%s) ERROR:"
				       "Unauthorized range %s in MLS_LEVEL for user %s ", 
				       u->name, (char*)ucontext, u->scontext);
				return -1;
			}
			else {
				syslog(LOG_INFO,
				       "CRON (%s) WARNING:"
				       "Unauthorized range %s in MLS_LEVEL for user %s,"
				       " but SELinux in permissive mode, continuing", 
				       u->name, (char*)ucontext, u->scontext);
			}
		}
	}

	if (setexeccon(ucontext) < 0) {
		if (security_getenforce() > 0) {
			syslog(LOG_ERR,
			       "CRON (%s) ERROR:"
			       "Could not set exec context to %s for user", 
			       u->name, (char*)ucontext);
			return -1;
		}
		else {
			syslog(LOG_ERR,
			       "CRON (%s) ERROR:"
			       "Could not set exec context to %s for user, "
                               " but SELinux in permissive mode, continuing", 
			       u->name, (char*)ucontext);

			return 0;
		}
	}
	return 0;
}
#endif

int get_security_context( const char *name, 
			  int crontab_fd, 
			  security_context_t *rcontext, 
			  const char *tabname) {
#ifdef WITH_SELINUX
	security_context_t scontext=NULL;
	security_context_t file_context=NULL;
	int retval=0;
	char *seuser=NULL;
	char *level=NULL;

	*rcontext = NULL;

	if (is_selinux_enabled() <= 0) 
	    return 0;

	if (getseuserbyname(name, &seuser, &level) == 0) {
		retval=get_default_context_with_level(seuser, level, NULL, &scontext);
		free(seuser);
		free(level);
		if (retval) {
			if (security_getenforce() > 0) {
				log_it(name, getpid(), "No SELinux security context",tabname);
				return -1;
			} else {
				log_it(name, getpid(), "No security context but SELinux in permissive mode, continuing",tabname);
				return 0;
			}
		}
	} else {
		log_it(name, getpid(), "getseusername FAILED", name);
		return (security_getenforce() > 0);
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
    
	if (!cron_authorize_context( scontext, file_context)) {
		freecon(scontext);
		freecon(file_context);
		if (security_getenforce() > 0) {
			log_it(name, getpid(), "Unauthorized SELinux context", tabname);
			return -1;
		}
		else {
			log_it(name, getpid(), 
			       "Unauthorized SELinux context, but SELinux in permissive mode, continuing",
			       tabname);
			return  0;
		}
	}
	freecon(file_context);

	*rcontext=scontext;
#endif
	return 0;
}

void free_security_context(security_context_t *scontext) {
#ifdef WITH_SELINUX
    if (*scontext != NULL) {
		freecon(*scontext);
		*scontext=0L;
    }
#endif	
}

int crontab_security_access(void) {
#ifdef WITH_SELINUX
    if (is_selinux_enabled() > 0)
	if (selinux_check_passwd_access(PASSWD__CRONTAB)!=0)
	    return -1;
#endif
    return 0;
}

/* Build up the job environment from the PAM environment plus the
   crontab environment */
static char ** build_env(char **cronenv)
{
#ifdef WITH_PAM
    char **jobenv = cronenv;
    char **pamenv = pam_getenvlist(pamh);
    char *cronvar;
    int count = 0;
    jobenv = env_copy(pamenv);

        /* Now add the cron environment variables. Since env_set()
           overwrites existing variables, this will let cron's
           environment settings override pam's */

    while ((cronvar = cronenv[count++])) {
		if (!(jobenv = env_set(jobenv, cronvar))) {
		    syslog(LOG_ERR, "Setting Cron environment variable %s failed", cronvar);
	    	return NULL;
		}
    }
    return jobenv;    
#else
    return env_copy(cronenv);
#endif
}

