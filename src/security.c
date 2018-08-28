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

#include "config.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "cronie_common.h"
#include "funcs.h"
#include "globals.h"
#include "macros.h"

#ifdef WITH_PAM
# include <security/pam_appl.h>
#endif

#ifdef WITH_SELINUX
# include <selinux/selinux.h>
# include <selinux/context.h>
# include <selinux/get_context_list.h>
#endif

#ifdef WITH_AUDIT
# include <libaudit.h>
#endif

#ifdef WITH_PAM
static pam_handle_t *pamh = NULL;
static int pam_session_opened = 0;	//global for open session

static int
cron_conv(int num_msg, const struct pam_message **msgm,
	struct pam_response **response ATTRIBUTE_UNUSED,
	void *appdata_ptr ATTRIBUTE_UNUSED)
{
	int i;

	for (i = 0; i < num_msg; i++) {
		switch (msgm[i]->msg_style) {
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			if (msgm[i]->msg != NULL) {
				log_it("CRON", getpid(), "pam_message", msgm[i]->msg, 0);
			}
			break;
		default:
			break;
		}
	}
	return (0);
}

static const struct pam_conv conv = {
	cron_conv, NULL
};

static int cron_open_pam_session(struct passwd *pw);

# define PAM_FAIL_CHECK if (retcode != PAM_SUCCESS) { \
	log_it(pw->pw_name, getpid(), "PAM ERROR", pam_strerror(pamh, retcode), 0); \
	if (pamh != NULL) { \
		if (pam_session_opened != 0) \
			pam_close_session(pamh, PAM_SILENT); \
		pam_end(pamh, retcode); \
		pamh = NULL; \
	} \
return(retcode); }
#endif

static char **build_env(char **cronenv);

#ifdef WITH_SELINUX
static int cron_change_selinux_range(user * u, security_context_t ucontext);
static int cron_get_job_range(user * u, security_context_t * ucontextp,
	char **jobenv);
#endif

void cron_restore_default_security_context(void) {
#ifdef WITH_SELINUX
	if (is_selinux_enabled() <= 0)
		return;
	if (setexeccon(NULL) < 0)
		log_it("CRON", getpid(), "ERROR",
			"failed to restore SELinux context", 0);
#endif
}

int cron_set_job_security_context(entry *e, user *u ATTRIBUTE_UNUSED,
	char ***jobenv) {
	time_t minutely_time = 0;
#ifdef WITH_PAM
	int ret;
#endif

	if ((e->flags & MIN_STAR) == MIN_STAR) {
		/* "minute-ly" job: Every minute for given hour/dow/month/dom. 
		 * Ensure that these jobs never run in the same minute:
		 */
		minutely_time = time(0);
		Debug(DSCH, ("Minute-ly job. Recording time %lu\n", minutely_time));
	}

#ifdef WITH_PAM
	/* PAM is called only for non-root users or non-system crontab */
	if ((!u->system || e->pwd->pw_uid != 0) && (ret = cron_start_pam(e->pwd)) != 0) {
		log_it(e->pwd->pw_name, getpid(), "FAILED to authorize user with PAM",
			pam_strerror(pamh, ret), 0);
		return -1;
	}
#endif

#ifdef WITH_SELINUX
	/* we must get the crontab context BEFORE changing user, else
	 * we'll not be permitted to read the cron spool directory :-)
	 */
	security_context_t ucontext = 0;

	if (cron_get_job_range(u, &ucontext, e->envp) < OK) {
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
	if (pamh != NULL && (ret = cron_open_pam_session(e->pwd)) != 0) {
		log_it(e->pwd->pw_name, getpid(),
			"FAILED to open PAM security session", pam_strerror(pamh, ret), 0);
		return -1;
	}
#endif

	if (cron_change_groups(e->pwd) != 0) {
		return -1;
	}

	*jobenv = build_env(e->envp);

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

#if defined(WITH_PAM)
int cron_start_pam(struct passwd *pw) {
	int retcode = 0;

	retcode = pam_start("crond", pw->pw_name, &conv, &pamh);
	PAM_FAIL_CHECK;
	retcode = pam_set_item(pamh, PAM_TTY, "cron");
	PAM_FAIL_CHECK;
	retcode = pam_acct_mgmt(pamh, PAM_SILENT);
	PAM_FAIL_CHECK;
	retcode = pam_setcred(pamh, PAM_ESTABLISH_CRED | PAM_SILENT);
	PAM_FAIL_CHECK;

	return retcode;
}
#endif

#if defined(WITH_PAM)
static int cron_open_pam_session(struct passwd *pw) {
	int retcode;

	retcode = pam_open_session(pamh, PAM_SILENT);
	PAM_FAIL_CHECK;
	if (retcode == PAM_SUCCESS)
		pam_session_opened = 1;

	return retcode;
}
#endif

void cron_close_pam(void) {
#if defined(WITH_PAM)
	if (pam_session_opened != 0) {
		pam_setcred(pamh, PAM_DELETE_CRED | PAM_SILENT);
		pam_close_session(pamh, PAM_SILENT);
	}
	if (pamh != NULL) {
		pam_end(pamh, PAM_SUCCESS);
		pamh = NULL;
	}
#endif
}

int cron_change_groups(struct passwd *pw) {
	pid_t pid = getpid();

	if (setgid(pw->pw_gid) != 0) {
		log_it("CRON", pid, "ERROR", "setgid failed", errno);
		return -1;
	}

	if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
		log_it("CRON", pid, "ERROR", "initgroups failed", errno);
		return -1;
	}

#if defined(WITH_PAM)
	/* credentials may take form of supplementary groups so reinitialize
	 * them here */
	if (pamh != NULL) {
		pam_setcred(pamh, PAM_REINITIALIZE_CRED | PAM_SILENT);
	}
#endif

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

	log_close();

	return 0;
}


#ifdef WITH_SELINUX
static int cron_authorize_context(security_context_t scontext,
	security_context_t file_context) {
	struct av_decision avd;
	int retval;
	security_class_t tclass;
	access_vector_t bit;

	tclass = string_to_security_class("file");
	if (!tclass) {
		log_it("CRON", getpid(), "ERROR", "Failed to translate security class file", errno);
		return 0;
	}
	bit = string_to_av_perm(tclass, "entrypoint");
	if (!bit) {
		log_it("CRON", getpid(), "ERROR", "Failed to translate av perm entrypoint", errno);
		return 0;
	}
	/*
	 * Since crontab files are not directly executed,
	 * crond must ensure that the crontab file has
	 * a context that is appropriate for the context of
	 * the user cron job.  It performs an entrypoint
	 * permission check for this purpose.
	 */
	retval = security_compute_av(scontext, file_context,
		tclass, bit, &avd);
	if (retval || ((bit & avd.allowed) != bit))
		return 0;
	return 1;
}
#endif

#ifdef WITH_SELINUX
static int cron_authorize_range(security_context_t scontext,
	security_context_t ucontext) {
	struct av_decision avd;
	int retval;
	security_class_t tclass;
	access_vector_t bit;

	tclass = string_to_security_class("context");
	if (!tclass) {
		log_it("CRON", getpid(), "ERROR", "Failed to translate security class context", errno);
		return 0;
	}
	bit = string_to_av_perm(tclass, "contains");
	if (!bit) {
		log_it("CRON", getpid(), "ERROR", "Failed to translate av perm contains", errno);
		return 0;
	}

	/*
	 * Since crontab files are not directly executed,
	 * so crond must ensure that any user specified range
	 * falls within the seusers-specified range for that Linux user.
	 */
	retval = security_compute_av(scontext, ucontext,
		tclass, bit, &avd);

	if (retval || ((bit & avd.allowed) != bit))
		return 0;
	return 1;
}
#endif

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
			context_free(ccon);
			return -1;
		}

		if (context_range_set(ccon, range)) {
			log_it(u->name, getpid(),
				"context_range_set FAILED for MLS_LEVEL", range, 0);
			context_free(ccon);
			return -1;
		}

		if (!(*ucontextp = context_str(ccon))) {
			log_it(u->name, getpid(), "context_str FAILED for MLS_LEVEL",
				range, 0);
			context_free(ccon);
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

	if (!ucontext || strcmp(u->scontext, ucontext)) {
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

	if (setexeccon(ucontext) < 0) {
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
					"Could not set exec context to %s for user,"
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

#ifdef WITH_SELINUX
int
get_security_context(const char *name, int crontab_fd,
	security_context_t * rcontext, const char *tabname) {
	security_context_t scontext = NULL;
	security_context_t file_context = NULL;
	security_context_t rawcontext=NULL;
	context_t current_context = NULL;
	int retval;
	char *current_context_str = NULL;
	char *seuser = NULL;
	char *level = NULL;

	*rcontext = NULL;

	if (is_selinux_enabled() <= 0)
		return 0;

	if (name != NULL) {
		if (getseuserbyname(name, &seuser, &level) < 0) {
			log_it(name, getpid(), "getseuserbyname FAILED", name, 0);
			return security_getenforce() > 0 ? -1 : 0;
		}

		retval = get_default_context_with_level(seuser, level, NULL, &scontext);
	}
	else {
		if (getcon(&current_context_str) < 0) {
			log_it(name, getpid(), "getcon FAILED", "", 0);
			return (security_getenforce() > 0);
		}

		current_context = context_new(current_context_str);
		if (current_context == NULL) {
			log_it(name, getpid(), "context_new FAILED", current_context_str, 0);
			freecon(current_context_str);
			return (security_getenforce() > 0);
		}

		const char *current_user = context_user_get(current_context);
		retval = get_default_context_with_level(current_user, level, NULL, &scontext);

		freecon(current_context_str);
		context_free(current_context);
	}

	if (selinux_trans_to_raw_context(scontext, &rawcontext) == 0) {
		freecon(scontext);
		scontext = rawcontext;
	}
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
		char *msg=NULL;
		if (asprintf(&msg,
		     "Unauthorized SELinux context=%s file_context=%s", (char *) scontext, file_context) >= 0) {
			log_it(name, getpid(), msg, tabname, 0);
			free(msg);
		} else {
			log_it(name, getpid(), "Unauthorized SELinux context", tabname, 0);
		}
		freecon(scontext);
		freecon(file_context);
		if (security_getenforce() > 0) {
			return -1;
		}
		else {
			log_it(name, getpid(),
				"SELinux in permissive mode, continuing",
				tabname, 0);
			return 0;
		}
	}
	freecon(file_context);

	*rcontext = scontext;
	return 0;
}
#endif

#ifdef WITH_SELINUX
void free_security_context(security_context_t * scontext) {
	if (*scontext != NULL) {
		freecon(*scontext);
		*scontext = 0L;
	}
}
#endif

#ifdef WITH_SELINUX
int crontab_security_access(void) {
	int selinux_check_passwd_access = -1;
	if (is_selinux_enabled() > 0) {
		security_context_t user_context;
		if (getprevcon_raw(&user_context) == 0) {
			security_class_t passwd_class;
			access_vector_t crontab_bit;
			struct av_decision avd;
			int retval = 0;

			passwd_class = string_to_security_class("passwd");
			if (passwd_class == 0) {
				fprintf(stderr, "Security class \"passwd\" is not defined in the SELinux policy.\n");
				retval = -1;
			}

			if (retval == 0) {
				crontab_bit = string_to_av_perm(passwd_class, "crontab");
				if (crontab_bit == 0) {
					fprintf(stderr, "Security av permission \"crontab\" is not defined in the SELinux policy.\n");
					retval = -1;
				}
			}

			if (retval == 0)
				retval = security_compute_av_raw(user_context,
					user_context, passwd_class,
					crontab_bit, &avd);

			if ((retval == 0) && ((crontab_bit & avd.allowed) == crontab_bit)) {
				selinux_check_passwd_access = 0;
			}
			freecon(user_context);
		}

		if (selinux_check_passwd_access != 0 && security_getenforce() == 0)
			selinux_check_passwd_access = 0;

		return selinux_check_passwd_access;
	}
	return 0;
}
#endif

/* Build up the job environment from the PAM environment plus the
* crontab environment 
*/
static char **build_env(char **cronenv) {
	char **jobenv;
#ifdef WITH_PAM
	char *cronvar;
	int count = 0;

	if (pamh == NULL || (jobenv=pam_getenvlist(pamh)) == NULL) {
#endif
		jobenv = env_copy(cronenv);
		if (jobenv == NULL)
			log_it("CRON", getpid(),
				"ERROR", "Initialization of cron environment variables failed", 0);
		return jobenv;
#ifdef WITH_PAM
	}

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
#endif
}
