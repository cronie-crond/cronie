/*
    Anacron - run commands periodically
    Copyright (C) 1998  Itai Tzur <itzur@actcom.co.il>
    Copyright (C) 1999  Sean 'Shaleh' Perry <shaleh@debian.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 
    The GNU General Public License can also be found in the file
    `COPYING' that comes with the Anacron source distribution.
*/


#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "global.h"

#include <langinfo.h>

static int
temp_file(job_rec *jr)
/* Open a temporary file and return its file descriptor */
{
    char *dir;
    char template[PATH_MAX+1];
    int fdin = -1;
    int fdout;
    int len;

    dir = getenv("TMPDIR");
    if (dir == NULL || *dir == '\0')
	dir = P_tmpdir;

    len = snprintf(template, sizeof(template), "%s/$anacronXXXXXX", dir);
    if (len >= sizeof(template))
	die_e("TMPDIR too long");

    fdout = mkstemp(template);
    if (fdout == -1) die_e("Can't open temporary file for writing");

    fdin = open(template, O_RDONLY, S_IRUSR | S_IWUSR);
    if (fdin == -1) die_e("Can't open temporary file for reading");

    if (unlink(template)) die_e("Can't unlink temporary file");

    fcntl(fdout, F_SETFD, FD_CLOEXEC);    /* set close-on-exec flag */
    fcntl(fdin, F_SETFD, FD_CLOEXEC);    /* set close-on-exec flag */

    jr->input_fd = fdin;
    jr->output_fd = fdout;

    return fdout;
}

static off_t
file_size(int fd)
/* Return the size of temporary file fd */
{
    struct stat st;

    if (fstat(fd, &st)) die_e("Can't fstat temporary file");
    return st.st_size;
}

static char *
username(void)
{
    struct passwd *ps;
    static char *user;

    if (user)
	return user;

    ps = getpwuid(geteuid());
    if (ps == NULL || ps->pw_name == NULL) die_e("getpwuid() error");

    user = strdup(ps->pw_name);
    if (user == NULL) die_e("memory allocation error");

    return user;
}

static void
xputenv(const char *s)
{
    char *name = NULL, *val = NULL;
    char *eq_ptr;
    const char *errmsg;
    size_t eq_index;

    if (s == NULL) {
        die_e("Invalid environment string");
    }

    eq_ptr = strchr(s, '=');
    if (eq_ptr == NULL) {
        die_e("Invalid environment string");
    }

    eq_index = (size_t) (eq_ptr - s);

    name = malloc((eq_index + 1) * sizeof(char));
    if (name == NULL) {
        die_e("Not enough memory to set the environment");
    }

    val = malloc((strlen(s) - eq_index) * sizeof(char));
    if (val == NULL) {
        die_e("Not enough memory to set the environment");
    }

    strncpy(name, s, eq_index);
    name[eq_index] = '\0';
    strcpy(val, s + eq_index + 1);

    if (setenv(name, val, 1)) {
        die_e("Can't set the environment");
    }

    free(name);
    free(val);
    return;

}

static void
setup_env(const job_rec *jr)
/* Setup the environment for the job according to /etc/anacrontab */
{
    env_rec *er;

    er = first_env_rec;
    if (er == NULL || jr->prev_env_rec == NULL) return;
    xputenv(er->assign);
    while (er != jr->prev_env_rec)
    {
	er = er->next;
	xputenv(er->assign);
    }
}

static void
run_job(const job_rec *jr)
/* This is called to start the job, after the fork */
{
    /* setup stdout and stderr */
    xclose(1);
    xclose(2);
    if (dup2(jr->output_fd, 1) != 1 || dup2(jr->output_fd, 2) != 2)
	die_e("dup2() error");     /* dup2 also clears close-on-exec flag */
    in_background = 0;  /* now, errors will be mailed to the user */
    if (chdir("/")) die_e("Can't chdir to '/'");

    if (sigprocmask(SIG_SETMASK, &old_sigmask, NULL))
	die_e("sigprocmask error");
    xcloselog();
    execl("/bin/sh", "/bin/sh", "-c", jr->command, (char *)NULL);
    die_e("execl() error");
}

static void
xwrite(int fd, const char *string)
/* Write (using write()) the string "string" to temporary file "fd".
 * Don't return on failure */
{
    if (write(fd, string, strlen(string)) == -1)
	die_e("Can't write to temporary file");
}

static int
xwait(pid_t pid , int *status)
/* Check if child process "pid" has finished.  If it has, return 1 and its
 * exit status in "*status".  If not, return 0.
 */
{
    pid_t r;

    r = waitpid(pid, status, WNOHANG);
    if (r == -1) die_e("waitpid() error");
    if (r == 0) return 0;
    return 1;
}

static void
launch_mailer(job_rec *jr)
{
    pid_t pid;
    struct stat buf;

    if (jr->mailto == NULL)
    {
	explain("Empty MAILTO set, not mailing output");
	return;
    }

    /* Check that we have a way of sending mail. */
    if(stat(SENDMAIL, &buf))
    {
	complain("Can't find sendmail at %s, not mailing output", SENDMAIL);
	return;
    }

    pid = xfork();
    if (pid == 0)
    {
	/* child */
	in_background = 1;
	/* set stdin to the job's output */
	xclose(STDIN_FILENO);
	if (dup2(jr->input_fd, STDIN_FILENO) != 0) die_e("Can't dup2()");
	if (lseek(STDIN_FILENO, 0, SEEK_SET) != 0) die_e("Can't lseek()");
	if (sigprocmask(SIG_SETMASK, &old_sigmask, NULL))
	    die_e("sigprocmask error");
	xcloselog();

	/* Ensure stdout/stderr are sane before exec-ing sendmail */
	xclose(STDOUT_FILENO); xopen(STDOUT_FILENO, "/dev/null", O_WRONLY);
	xclose(STDERR_FILENO); xopen(STDERR_FILENO, "/dev/null", O_WRONLY);
	xclose(jr->output_fd);

	/* Ensure stdin is not appendable ... ? */
	/* fdflags = fcntl(0, F_GETFL); fdflags &= ~O_APPEND; */
	/* fcntl(0, F_SETFL, fdflags ); */

	/* Here, I basically mirrored the way /usr/sbin/sendmail is called
	 * by cron on a Debian system, except for the "-oem" and "-or0s"
	 * options, which don't seem to be appropriate here.
	 * Hopefully, this will keep all the MTAs happy. */
	execl(SENDMAIL, SENDMAIL, "-FAnacron", "-odi",
	      jr->mailto, (char *)NULL);
	die_e("Can't exec " SENDMAIL);
    }
    /* parent */
    /* record mailer pid */
    jr->mailer_pid = pid;
    running_mailers++;
}

static void
tend_mailer(job_rec *jr, int status)
{
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
	complain("Tried to mail output of job `%s', "
		 "but mailer process (" SENDMAIL ") exited with status %d",
		 jr->ident, WEXITSTATUS(status));
    else if (!WIFEXITED(status) && WIFSIGNALED(status))
	complain("Tried to mail output of job `%s', "
		 "but mailer process (" SENDMAIL ") got signal %d",
		 jr->ident, WTERMSIG(status));
    else if (!WIFEXITED(status) && !WIFSIGNALED(status))
	complain("Tried to mail output of job `%s', "
		 "but mailer process (" SENDMAIL ") terminated abnormally"
		 , jr->ident);

    jr->mailer_pid = 0;
    running_mailers--;
}

void
launch_job(job_rec *jr)
{
    pid_t pid;
    int fd;
    char hostname[512];
    char *mailto;
    char *mailfrom;

    /* get hostname */
    if (gethostname(hostname, 512)) {
      strcpy (hostname,"unknown machine");
    }

    setup_env(jr);

    /* Get the destination email address if set, or current user otherwise */
    mailto = getenv("MAILTO");

    if (mailto == NULL)
	mailto = username();

    /* Get the source email address if set, or current user otherwise */
    mailfrom = getenv("MAILFROM");
    if (mailfrom == NULL)
	mailfrom = username();

    /* create temporary file for stdout and stderr of the job */
    temp_file(jr); fd = jr->output_fd;
    /* write mail header */
    xwrite(fd, "From: ");
    xwrite(fd, "Anacron <");
    xwrite(fd, mailfrom);
    xwrite(fd, ">\n");
    xwrite(fd, "To: ");
    xwrite(fd, mailto);
    xwrite(fd, "\n");
    xwrite(fd, "MIME-Version: 1.0\n");
    xwrite(fd, "Content-Type: text/plain; charset=\"");
    xwrite(fd, nl_langinfo(CODESET));
    xwrite(fd, "\"\n");
    xwrite(fd, "Subject: Anacron job '");
    xwrite(fd, jr->ident);
    xwrite(fd, "' on ");
    xwrite(fd, hostname);
    xwrite(fd, "\n\n");

    if (*mailto == '\0')
	jr->mailto = NULL;
    else
	/* ugly but works without strdup() */
	jr->mailto = mailto;

    jr->mail_header_size = file_size(fd);

    pid = xfork();
    if (pid == 0)
    {
	/* child */
	in_background = 1;
	run_job(jr);
	/* execution never gets here */
    }
    /* parent */
    explain("Job `%s' started", jr->ident);
    jr->job_pid = pid;
    running_jobs++;
}

static void
tend_job(job_rec *jr, int status)
/* Take care of a finished job */
{
    int mail_output;
    const char *m;

    update_timestamp(jr);
    unlock(jr);
    if (file_size(jr->output_fd) > jr->mail_header_size) mail_output = 1;
    else mail_output = 0;

    m = mail_output ? " (produced output)" : "";
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
	explain("Job `%s' terminated%s", jr->ident, m);
    else if (WIFEXITED(status))
	explain("Job `%s' terminated (exit status: %d)%s",
		jr->ident, WEXITSTATUS(status), m);
    else if (WIFSIGNALED(status))
	complain("Job `%s' terminated due to signal %d%s",
		 jr->ident, WTERMSIG(status), m);
    else /* is this possible? */
	complain("Job `%s' terminated abnormally%s", jr->ident, m);

    jr->job_pid = 0;
    running_jobs--;
    if (mail_output) launch_mailer(jr);
    xclose(jr->output_fd);
    xclose(jr->input_fd);
}

void
tend_children(void)
/* This is called whenever we get a SIGCHLD.
 * Takes care of zombie children.
 */
{
    int j;
    int status;

    j = 0;
    while (j < njobs)
    {
	if (job_array[j]->mailer_pid != 0 &&
	    xwait(job_array[j]->mailer_pid, &status))
	    tend_mailer(job_array[j], status);
	if (job_array[j]->job_pid != 0 &&
	    xwait(job_array[j]->job_pid, &status))
	    tend_job(job_array[j], status);
	j++;
    }
}
