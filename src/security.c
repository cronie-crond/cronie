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

#include <cron.h>

#ifdef WITH_SELINUX
# include <selinux/selinux.h>
# include <selinux/context.h>
# include <selinux/flask.h>
# include <selinux/av_permissions.h>
# include <selinux/get_context_list.h>
#endif

#ifdef WITH_AUDIT
# include <libaudit.h>
#endif

#ifdef WITH_PAM
static pam_handle_t *pamh = NULL;
static int pam_session_opened = 0;	//global for open session
static const struct pam_conv conv = {
	NULL
};

static int cron_open_pam_session(struct passwd *pw);

# define PAM_FAIL_CHECK if (retcode != PAM_SUCCESS) { \
	fprintf(stderr,"\n%s\n",pam_strerror(pamh, retcode)); \
	if (pamh != NULL) { \
		if (pam_session_opened != 0) \
			pam_close_session(pamh, PAM_SILENT); \
		pam_end(pamh, retcode); \
	} \
return(retcode); }
#endif

static char **build_env(char **cronenv);

#ifdef WITH_SELINUX
static int cron_change_selinux_range(user * u, security_context_t ucontext);
static int cron_get_job_range(user * u, security_context_t * ucontextp,
	char **jobenv);
#endif

void cron_restore_default_security_context() {
#ifdef WITH_SELINUX
	setexeccon(NULL);
#endif
}

int cron_set_job_security_context(entry * e, user * u, char ***jobenv) {
	time_t minutely_time = 0;
#ifdef WITH_PAM
	int ret;
#endif

	if ((e->flags & MIN_STAR) == MIN_STAR) {
		/* "minute-ly" job: Every minute for given hour/dow/month/dom. 
		 * Ensure that these jobs never run in the same minute:
		 */
		minutely_time = time(0);
		Debug(DSCH, ("Minute-ly job. Recording time %lu\n", minutely_time))
	}

#ifdef WITH_PAM
	if ((ret = cron_start_pam(e->pwd)) != 0) {
		log_it(e->pwd->pw_name, getpid(), "FAILED to authorize user with PAM",
			pam_strerror(pamh, ret), 0);
		return -1;
	}
#endif

	*jobenv = build_env(e->envp);

#ifdef WITH_SELINUX
	/* we must get the crontab context BEFORE changing user, else
	 * we'll not be permitted to read the cron spool directory :-)
	 */
	security_context_t ucontext = 0;

	if (cron_get_job_range(u, &ucontext, *jobenv) < OK) {
		log_it(e->pwd->pw_name, getpid(), "ERROR",
			"failed to get SELinux context", 0);
		return -1;
	}

	if (cron_change_selinux_range(u, ucontext) != 0) {
		log_it(e->pwd->pw_name, getpid(), "ERROR",
			"failed to change SELinux context", 0);
		if (ucontext)
			freecon(ucontext);
		return -1;
	}
	if (ucontext)
		freecon(ucontext);
#endif
#ifdef WITH_PAM
	if ((ret = cron_open_pam_session(e->pwd)) != 0) {
		log_it(e->pwd->pw_name, getpid(),
			"FAILED to open PAM security session", pam_strerror(pamh, ret), 0);
		return -1;
	}
#endif

	if (cron_change_user(e->pwd) != 0) {
		log_it(e->pwd->pw_name, getpid(), "ERROR", "failed to change user", 0);
		return -1;
	}

	log_close();

	time_t job_run_time = time(0L);

	if ((minutely_time > 0) && ((job_run_time / 60) != (minutely_time / 60))) {
		/* if a per-minute job is delayed into the next minute 
		 * (eg. by network authentication method timeouts), skip it.
		 */
		struct tm tmS, tmN;
		char buf[256];

		localtime_r(&job_run_time, &tmN);
		localtime_r(&minutely_time, &tmS);

		snprintf(buf, sizeof (buf),
			"Job execution of per-minute job scheduled for "
			"%.2u:%.2u delayed into subsequent minute %.2u:%.2u. Skipping job run.",
			tmS.tm_hour, tmS.tm_min, tmN.tm_hour, tmN.tm_min);
		log_it(e->pwd->pw_name, getpid(), "INFO", buf, 0);
		return -1;
	}
	return 0;
}

int cron_start_pam(struct passwd *pw) {
	int retcode = 0;

#if defined(WITH_PAM)
	retcode = pam_start("crond", pw->pw_name, &conv, &pamh);
	PAM_FAIL_CHECK;
	retcode = pam_set_item(pamh, PAM_TTY, "cron");
	PAM_FAIL_CHECK;
	retcode = pam_acct_mgmt(pamh, PAM_SILENT);
	PAM_FAIL_CHECK;
	retcode = pam_setcred(pamh, PAM_ESTABLISH_CRED | PAM_SILENT);
	PAM_FAIL_CHECK;
#endif

	return retcode;
}

static int cron_open_pam_session(struct passwd *pw) {
	int retcode = 0;

#if defined(WITH_PAM)
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

int cron_change_user(struct passwd *pw) {
	pid_t pid = getpid();
	/* set our directory, uid and gid.  Set gid first, since once
	 * we set uid, we've lost root privledges.
	 */
	if (setgid(pw->pw_gid) != 0) {
		log_it("CRON", pid, "ERROR", "setgid failed", errno);
		return -1;
	}

	if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
		log_it("CRON", pid, "ERROR", "initgroups failed", errno);
		return -1;
	}

	if (setreuid(pw->pw_uid, -1) != 0) {
		log_it("CRON", pid, "ERROR", "setreuid failed", errno);
		return -1;
	}

	return 0;
}

int cron_change_user_permanently(struct passwd *pw, char *homedir) {
	if (setreuid(pw->pw_uid, pw->pw_uid) != 0) {
		log_it("CRON", getpid(), "ERROR", "setreuid failed", errno);
		return -1;
	}
	if (chdir(homedir) == -1) {
		log_it("CRON", getpid(), "ERROR chdir failed", homedir, errno);
		return -1;
	}

	return 0;
}


static int cron_authorize_context(security_context_t scontext,
	security_context_t file_context) {
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

static int cron_authorize_range(security_context_t scontext,
	security_context_t ucontext) {
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

#if WITH_SELINUX
/* always uses u->scontext as the default process context, then changes the
	 level, and retuns it in ucontextp (or NULL otherwise) */
static int
cron_get_job_range(user * u, security_context_t * ucontextp, char **jobenv) {
	char *range;

	if (is_selinux_enabled() <= 0)
		return 0;
	if (ucontextp == 0L)
		return -1;

	*ucontextp = 0L;

	if ((range = env_get("MLS_LEVEL", jobenv)) != 0L) {
		context_t ccon;
		if (!(ccon = context_new(u->scontext))) {
			log_it(u->name, getpid(), "context_new FAILED for MLS_LEVEL",
				range, 0);
			return -1;
		}

		if (context_range_set(ccon, range)) {
			log_it(u->name, getpid(),
				"context_range_set FAILED for MLS_LEVEL", range, 0);
			return -1;
		}

		if (!(*ucontextp = context_str(ccon))) {
			log_it(u->name, getpid(), "context_str FAILED for MLS_LEVEL",
				range, 0);
			return -1;
		}

		if (!(*ucontextp = strdup(*ucontextp))) {
			log_it(u->name, getpid(), "strdup FAILED for MLS_LEVEL", range, 0);
			return -1;
		}
		context_free(ccon);
	}
	else if (!u->scontext) {
		/* cron_change_selinux_range() deals with this */
		return 0;
	}
	else if (!(*ucontextp = strdup(u->scontext))) {
		log_it(u->name, getpid(), "strdup FAILED for MLS_LEVEL", range, 0);
		return -1;
	}

	return 0;
}
#endif

#ifdef WITH_SELINUX
static int cron_change_selinux_range(user * u, security_context_t ucontext) {
	char *msg = NULL;

	if (is_selinux_enabled() <= 0)
		return 0;

	if (u->scontext == 0L) {
		if (security_getenforce() > 0) {
			log_it(u->name, getpid(), "NULL security context for user", "", 0);
			return -1;
		}
		else {
			log_it(u->name, getpid(),
				"NULL security context for user, "
				"but SELinux in permissive mode, continuing", "", 0);
			return 0;
		}
	}

	if (strcmp(u->scontext, ucontext)) {
		if (!cron_authorize_range(u->scontext, ucontext)) {
			if (security_getenforce() > 0) {
# ifdef WITH_AUDIT
				if (asprintf(&msg,
						"cron: Unauthorized MLS range acct=%s new_scontext=%s old_scontext=%s",
						u->name, (char *) ucontext, u->scontext) >= 0) {
					int audit_fd = audit_open();
					audit_log_user_message(audit_fd, AUDIT_USER_ROLE_CHANGE,
						msg, NULL, NULL, NULL, 0);
					close(audit_fd);
					free(msg);
				}
# endif
				if (asprintf
					(&msg, "Unauthorized range in %s for user range in %s",
						(char *) ucontext, u->scontext) >= 0) {
					log_it(u->name, getpid(), "ERROR", msg, 0);
					free(msg);
				}
				return -1;
			}
			else {
				if (asprintf
					(&msg,
						"Unauthorized range in %s for user range in %s,"
						" but SELinux in permissive mod, continuing",
						(char *) ucontext, u->scontext) >= 0) {
					log_it(u->name, getpid(), "WARNING", msg, 0);
					free(msg);
				}
			}
		}
	}

	if (setexeccon(ucontext) < 0 || setkeycreatecon(ucontext) < 0) {
		if (security_getenforce() > 0) {
			if (asprintf
				(&msg, "Could not set exec or keycreate context to %s for user",
					(char *) ucontext) >= 0) {
				log_it(u->name, getpid(), "ERROR", msg, 0);
				free(msg);
			}
			return -1;
		}
		else {
			if (asprintf
				(&msg,
					"Could not set exec or keycreate context to %s for user,"
					" but SELinux in permissive mode, continuing",
					(char *) ucontext) >= 0) {
				log_it(u->name, getpid(), "WARNING", msg, 0);
				free(msg);
			}
			return 0;
		}
	}
	return 0;
}
#endif

int
get_security_context(const char *name, int crontab_fd,
	security_context_t * rcontext, const char *tabname) {
#ifdef WITH_SELINUX
	security_context_t scontext = NULL;
	security_context_t file_context = NULL;
	int retval = 0;
	char *seuser = NULL;
	char *level = NULL;

	*rcontext = NULL;

	if (is_selinux_enabled() <= 0)
		return 0;

	if (name != NULL) {
		if (getseuserbyname(name, &seuser, &level) < 0) {
			log_it(name, getpid(), "getseuserbyname FAILED", name, 0);
			return (security_getenforce() > 0);
		}
	}

	retval = get_default_context_with_level(name == NULL ? "system_u" : seuser,
		level, NULL, &scontext);
	free(seuser);
	free(level);
	if (retval) {
		if (security_getenforce() > 0) {
			log_it(name, getpid(), "No SELinux security context", tabname, 0);
			return -1;
		}
		else {
			log_it(name, getpid(),
				"No security context but SELinux in permissive mode, continuing",
				tabname, 0);
			return 0;
		}
	}

	if (fgetfilecon(crontab_fd, &file_context) < OK) {
		if (security_getenforce() > 0) {
			log_it(name, getpid(), "getfilecon FAILED", tabname, 0);
			freecon(scontext);
			return -1;
		}
		else {
			log_it(name, getpid(),
				"getfilecon FAILED but SELinux in permissive mode, continuing",
				tabname, 0);
			*rcontext = scontext;
			return 0;
		}
	}

	if (!cron_authorize_context(scontext, file_context)) {
		freecon(scontext);
		freecon(file_context);
		if (security_getenforce() > 0) {
			log_it(name, getpid(), "Unauthorized SELinux context", tabname, 0);
			return -1;
		}
		else {
			log_it(name, getpid(),
				"Unauthorized SELinux context, but SELinux in permissive mode, continuing",
				tabname, 0);
			return 0;
		}
	}
	freecon(file_context);

	*rcontext = scontext;
#endif
	return 0;
}

void free_security_context(security_context_t * scontext) {
#ifdef WITH_SELINUX
	if (*scontext != NULL) {
		freecon(*scontext);
		*scontext = 0L;
	}
#endif
}

int crontab_security_access(void) {
#ifdef WITH_SELINUX
	int selinux_check_passwd_access = -1;
	if (is_selinux_enabled() > 0) {
		security_context_t user_context;
		if (getprevcon_raw(&user_context) == 0) {
			security_class_t passwd_class;
			struct av_decision avd;
			int retval;

			passwd_class = string_to_security_class("passwd");
			if (passwd_class == 0) {
				selinux_check_passwd_access = -1;
				fprintf(stderr, "Security class \"passwd\" is not defined in the SELinux policy.\n");
			}

			retval = security_compute_av_raw(user_context,
							user_context,
							passwd_class,
							PASSWD__CRONTAB,
							&avd);

			if ((retval == 0) && ((PASSWD__CRONTAB & avd.allowed) == PASSWD__CRONTAB)) {
				selinux_check_passwd_access = 0;
			}
			freecon(user_context);
		}

		if (selinux_check_passwd_access != 0 && security_getenforce() == 0)
			selinux_check_passwd_access = 0;

		return selinux_check_passwd_access;
	}
#endif
	return 0;
}

/* Build up the job environment from the PAM environment plus the
* crontab environment 
*/
static char **build_env(char **cronenv) {
#ifdef WITH_PAM
	char **jobenv = cronenv;
	char **pamenv = pam_getenvlist(pamh);
	char *cronvar;
	int count = 0;
	jobenv = env_copy(pamenv);

	/* Now add the cron environment variables. Since env_set()
	 * overwrites existing variables, this will let cron's
	 * environment settings override pam's */

	while ((cronvar = cronenv[count++])) {
		if (!(jobenv = env_set(jobenv, cronvar))) {
			log_it("CRON", getpid(),
				"Setting Cron environment variable failed", cronvar, 0);
			return NULL;
		}
	}
	return jobenv;
#else
	return env_copy(cronenv);
#endif
}
