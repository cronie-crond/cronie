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
#include <selinux/flask.h>
#include <selinux/av_permissions.h>
#include <selinux/get_context_list.h>
#endif

static char ** build_env(char **cronenv);

int cron_set_job_security_context( entry *e, user *u, char ***jobenv )
{
    time_t minutely_time = 0;
    if((e->flags & MIN_STAR)==MIN_STAR)
    {
	/* "minute-ly" job: Every minute for given hour/dow/month/dom. 
	 * Ensure that these jobs never run in the same minute:
	 */
	minutely_time = time(0);
	Debug(DSCH, ("Minute-ly job. Recording time %lu\n", minutely_time))
    }

    if ( cron_open_security_session( e->pwd ) != 0 )
    {
	syslog(LOG_INFO, "CRON (%s) ERROR: failed to open PAM security session: %s", 
	       e->pwd->pw_name, strerror(errno)
	      );
	return -1;
    }
    
    if ( cron_change_user( e->pwd ) != 0 )
    {
	syslog(LOG_INFO, "CRON (%s) ERROR: failed to open PAM security session: %s", 
	       e->pwd->pw_name, strerror(errno)
	      );
	return -1;
    }
	
    if ( cron_change_selinux_context( u ) != 0 )
    {
        syslog(LOG_INFO,"CRON (%s) ERROR: failed to change SELinux context", 
	       e->pwd->pw_name);
	return -1;
    }

    *jobenv = build_env( e->envp );

    log_close();
    openlog(ProgramName, LOG_PID, LOG_CRON);

    if ( chdir(env_get("HOME", *jobenv)) == -1 )
    {
	log_it("CRON", getpid(), "chdir(HOME) failed:", strerror(errno));
	return -1;
    }

    time_t job_run_time = time(0L);

    if( (minutely_time > 0)
	&&((job_run_time / 60) != (minutely_time / 60))
      )
    {/* if a per-minute job is delayed into the next minute 
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

#ifdef WITH_PAM
static pam_handle_t *pamh = NULL;
static const struct pam_conv conv = {
    NULL
};
#define PAM_FAIL_CHECK if (retcode != PAM_SUCCESS) {		\
	fprintf(stderr,"\n%s\n",pam_strerror(pamh, retcode));	\
	syslog(LOG_ERR,"%s",pam_strerror(pamh, retcode));	\
	pam_close_session(pamh, PAM_SILENT);			\
	pam_end(pamh, retcode);					\
	return(retcode);					\
   }
#endif

int cron_open_security_session( struct passwd *pw )
{
    int	retcode = 0;

#if defined(WITH_PAM)
    retcode = pam_start("crond", pw->pw_name, &conv, &pamh);
    PAM_FAIL_CHECK;
    retcode = pam_set_item(pamh, PAM_TTY, "cron");
    PAM_FAIL_CHECK;
    retcode = pam_acct_mgmt(pamh, PAM_SILENT);
    PAM_FAIL_CHECK;
    retcode = pam_open_session(pamh, PAM_SILENT);
    PAM_FAIL_CHECK;
    retcode = pam_setcred(pamh, PAM_ESTABLISH_CRED | PAM_SILENT);
    PAM_FAIL_CHECK;
    log_close(); /* PAM has now re-opened our log to auth.info ! */
    openlog(ProgramName, LOG_PID, LOG_CRON);
#endif

    return retcode;
}

void cron_close_security_session( void )
{
#if defined(WITH_PAM)
    pam_setcred(pamh, PAM_DELETE_CRED | PAM_SILENT);
    pam_close_session(pamh, PAM_SILENT);
    pam_end(pamh, PAM_ABORT);
#endif
}

int cron_change_user( struct passwd *pw )
{    	
    /* set our directory, uid and gid.  Set gid first, since once
     * we set uid, we've lost root privledges.
     */
    if ( setgid( pw->pw_gid ) != 0 )
    {
	log_it("CRON", getpid(), "setgid failed:", strerror(errno));
	return -1;
    }

    if ( initgroups( pw->pw_name, pw->pw_gid ) != 0 )
    {
	log_it("CRON", getpid(), "initgroups failed:", strerror(errno));
	return -1;	
    }

    if ( setuid( pw->pw_uid ) != 0 )
    {
	log_it("CRON", getpid(), "setuid failed:", strerror(errno));
	return -1;
    }
    
    return 0;
}

int cron_change_selinux_context( user *u )
{
#ifdef WITH_SELINUX
    if ((is_selinux_enabled() >0) && (u->scontext != 0L)) {
	if (setexeccon(u->scontext) < 0) {
	    if (security_getenforce() > 0) {
		syslog(LOG_INFO,
		       "CRON (%s) ERROR:"
		       "Could not set exec context to %s for user\n", 
		       u->name, u->scontext
		      );
		return -1;
	    }
	}
    }
#endif
    return 0;
}

int get_security_context( const char *name, 
			  int crontab_fd, 
			  security_context_t *rcontext, 
			  const char *tabname) {
	security_context_t scontext=NULL;
	security_context_t  file_context=NULL;
	struct av_decision avd;
	int retval=0;
	char *seuser=NULL;
	char *level=NULL;

	*rcontext = NULL;

#ifdef WITH_SELINUX

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
#endif
	return 0;
}

void free_security_context( security_context_t *scontext )
{
#ifdef WITH_SELINUX
    if( *scontext != NULL )
    {
	freecon(*scontext);
	*scontext=0L;
    }	    
#endif	
}

int crontab_security_access(void)
{
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
}
