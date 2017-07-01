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

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "externs.h"
#include "funcs.h"
#include "globals.h"
#include "structs.h"

#ifndef isascii
# define isascii(c)	((unsigned)(c)<=0177)
#endif

static int child_process(entry *, char **);
static int safe_p(const char *, const char *);

void do_command(entry * e, user * u) {
	pid_t pid = getpid();
	int ev;
	char **jobenv = 0L;

	Debug(DPROC, ("[%ld] do_command(%s, (%s,%ld,%ld))\n",
			(long) pid, e->cmd, u->name,
			(long) e->pwd->pw_uid, (long) e->pwd->pw_gid));

		/* fork to become asynchronous -- parent process is done immediately,
		 * and continues to run the normal cron code, which means return to
		 * tick().  the child and grandchild don't leave this function, alive.
		 *
		 * vfork() is unsuitable, since we have much to do, and the parent
		 * needs to be able to run off and fork other processes.
		 */
		switch (fork()) {
	case -1:
		log_it("CRON", pid, "CAN'T FORK", "do_command", errno);
		break;
	case 0:
		/* child process */
		acquire_daemonlock(1);
		/* Set up the Red Hat security context for both mail/minder and job processes:
		 */
		if (cron_set_job_security_context(e, u, &jobenv) != 0) {
			_exit(ERROR_EXIT);
		}
		ev = child_process(e, jobenv);
#ifdef WITH_PAM
		cron_close_pam();
#endif
		env_free(jobenv);
		Debug(DPROC, ("[%ld] child process done, exiting\n", (long) getpid()));
		_exit(ev);
		break;
	default:
		/* parent process */
		break;
	}
	Debug(DPROC, ("[%ld] main process returning to work\n", (long) pid));
}

static int child_process(entry * e, char **jobenv) {
	int stdin_pipe[2], stdout_pipe[2];
	char *input_data, *usernm, *mailto, *mailfrom;
	int children = 0;
	pid_t pid = getpid();
	struct sigaction sa;

	/* Ignore SIGPIPE as we will be writing to pipes and do not want to terminate
	   prematurely */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);

	/* our parent is watching for our death by catching SIGCHLD.  we
	 * do not care to watch for our children's deaths this way -- we
	 * use wait() explicitly.  so we have to reset the signal (which
	 * was inherited from the parent).
	 */
	sa.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sa, NULL);


	Debug(DPROC, ("[%ld] child_process('%s')\n", (long) getpid(), e->cmd));
#ifdef CAPITALIZE_FOR_PS
	/* mark ourselves as different to PS command watchers by upshifting
	 * our program name.  This has no effect on some kernels.
	 */
	/*local */  {
		char *pch;

		for (pch = ProgramName; *pch; pch++)
			*pch = MkUpper(*pch);
	}
#endif /* CAPITALIZE_FOR_PS */

	/* discover some useful and important environment settings
	 */
	usernm = e->pwd->pw_name;
	mailto = env_get("MAILTO", jobenv);
	mailfrom = env_get("MAILFROM", e->envp);

	/* create some pipes to talk to our future child
	 */
	if (pipe(stdin_pipe) == -1) {	/* child's stdin */
		log_it("CRON", pid, "PIPE() FAILED", "stdin_pipe", errno);
		return ERROR_EXIT;
	}

	if (pipe(stdout_pipe) == -1) {	/* child's stdout */
		log_it("CRON", pid, "PIPE() FAILED", "stdout_pipe", errno);
		return ERROR_EXIT;
	}

	/* since we are a forked process, we can diddle the command string
	 * we were passed -- nobody else is going to use it again, right?
	 *
	 * if a % is present in the command, previous characters are the
	 * command, and subsequent characters are the additional input to
	 * the command.  An escaped % will have the escape character stripped
	 * from it.  Subsequent %'s will be transformed into newlines,
	 * but that happens later.
	 */
	/*local */  {
		int escaped = FALSE;
		int ch;
		char *p;

		for (input_data = p = e->cmd;
			(ch = *input_data) != '\0'; input_data++, p++) {
			if (p != input_data)
				*p = ch;
			if (escaped) {
				if (ch == '%')
					*--p = ch;
				escaped = FALSE;
				continue;
			}
			if (ch == '\\') {
				escaped = TRUE;
				continue;
			}
			if (ch == '%') {
				*input_data++ = '\0';
				break;
			}
		}
		*p = '\0';
	}


	/* fork again, this time so we can exec the user's command.
	 */
	switch (fork()) {
	case -1:
		log_it("CRON", pid, "CAN'T FORK", "child_process", errno);
		return ERROR_EXIT;
		/*NOTREACHED*/
	case 0:
		Debug(DPROC, ("[%ld] grandchild process fork()'ed\n", (long) getpid()));

		/* write a log message.  we've waited this long to do it
		 * because it was not until now that we knew the PID that
		 * the actual user command shell was going to get and the
		 * PID is part of the log message.
		 */
		if ((e->flags & DONT_LOG) == 0) {
			char *x = mkprints((u_char *) e->cmd, strlen(e->cmd));

			log_it(usernm, getpid(), "CMD", x, 0);
			free(x);
		}

		if (cron_change_user_permanently(e->pwd, env_get("HOME", jobenv)) < 0)
			_exit(ERROR_EXIT);

		/* get new pgrp, void tty, etc.
		 */
		(void) setsid();

		/* reset the SIGPIPE back to default so the child will terminate
		 * if it tries to write to a closed pipe
		 */
		sa.sa_handler = SIG_DFL;
		sigaction(SIGPIPE, &sa, NULL);

		/* close the pipe ends that we won't use.  this doesn't affect
		 * the parent, who has to read and write them; it keeps the
		 * kernel from recording us as a potential client TWICE --
		 * which would keep it from sending SIGPIPE in otherwise
		 * appropriate circumstances.
		 */
		close(stdin_pipe[WRITE_PIPE]);
		close(stdout_pipe[READ_PIPE]);

		/* grandchild process.  make std{in,out} be the ends of
		 * pipes opened by our daddy; make stderr go to stdout.
		 */
		if (stdin_pipe[READ_PIPE] != STDIN) {
			dup2(stdin_pipe[READ_PIPE], STDIN);
			close(stdin_pipe[READ_PIPE]);
		}
		if (stdout_pipe[WRITE_PIPE] != STDOUT) {
			dup2(stdout_pipe[WRITE_PIPE], STDOUT);
			close(stdout_pipe[WRITE_PIPE]);
		}
		dup2(STDOUT, STDERR);

		/*
		 * Exec the command.
		 */
		{
			char *shell = env_get("SHELL", jobenv);
			int fd, fdmax = getdtablesize();

			/* close all unwanted open file descriptors */
			for(fd = STDERR + 1; fd < fdmax; fd++) {
				close(fd);
			}

#if DEBUGGING
			if (DebugFlags & DTEST) {
				fprintf(stderr, "debug DTEST is on, not exec'ing command.\n");
				fprintf(stderr, "\tcmd='%s' shell='%s'\n", e->cmd, shell);
				_exit(OK_EXIT);
			}
#endif		 /*DEBUGGING*/
				execle(shell, shell, "-c", e->cmd, (char *) 0, jobenv);
			fprintf(stderr, "execl: couldn't exec `%s'\n", shell);
			perror("execl");
			_exit(ERROR_EXIT);
		}
		break;
	default:
		cron_restore_default_security_context();
		/* parent process */
		break;
	}

	children++;

	/* middle process, child of original cron, parent of process running
	 * the user's command.
	 */

	Debug(DPROC, ("[%ld] child continues, closing pipes\n", (long) getpid()));

	/* close the ends of the pipe that will only be referenced in the
	 * grandchild process...
	 */
	close(stdin_pipe[READ_PIPE]);
	close(stdout_pipe[WRITE_PIPE]);

	/*
	 * write, to the pipe connected to child's stdin, any input specified
	 * after a % in the crontab entry.  while we copy, convert any
	 * additional %'s to newlines.  when done, if some characters were
	 * written and the last one wasn't a newline, write a newline.
	 *
	 * Note that if the input data won't fit into one pipe buffer (2K
	 * or 4K on most BSD systems), and the child doesn't read its stdin,
	 * we would block here.  thus we must fork again.
	 */

	if (*input_data && fork() == 0) {
		FILE *out = fdopen(stdin_pipe[WRITE_PIPE], "w");
		int need_newline = FALSE;
		int escaped = FALSE;
		int ch;

		Debug(DPROC, ("[%ld] child2 sending data to grandchild\n",
				(long) getpid()));

		/* reset the SIGPIPE back to default so the child will terminate
		 * if it tries to write to a closed pipe
		 */
		sa.sa_handler = SIG_DFL;
		sigaction(SIGPIPE, &sa, NULL);

		/* close the pipe we don't use, since we inherited it and
		 * are part of its reference count now.
		 */
		close(stdout_pipe[READ_PIPE]);

		if (cron_change_user_permanently(e->pwd, env_get("HOME", jobenv)) < 0)
			_exit(ERROR_EXIT);
		/* translation:
		 *  \% -> %
		 *  %  -> \n
		 *  \x -> \x    for all x != %
		 */
		while ((ch = *input_data++) != '\0') {
			if (escaped) {
				if (ch != '%')
					putc('\\', out);
			}
			else {
				if (ch == '%')
					ch = '\n';
			}

			if (!(escaped = (ch == '\\'))) {
				putc(ch, out);
				need_newline = (ch != '\n');
			}
		}
		if (escaped)
			putc('\\', out);
		if (need_newline)
			putc('\n', out);

		/* close the pipe, causing an EOF condition.  fclose causes
		 * stdin_pipe[WRITE_PIPE] to be closed, too.
		 */
		fclose(out);

		Debug(DPROC, ("[%ld] child2 done sending to grandchild\n",
				(long) getpid()));
		_exit(0);
	}

	/* close the pipe to the grandkiddie's stdin, since its wicked uncle
	 * ernie back there has it open and will close it when he's done.
	 */
	close(stdin_pipe[WRITE_PIPE]);

	children++;

	/*
	 * read output from the grandchild.  it's stderr has been redirected to
	 * it's stdout, which has been redirected to our pipe.  if there is any
	 * output, we'll be mailing it to the user whose crontab this is...
	 * when the grandchild exits, we'll get EOF.
	 */

	Debug(DPROC, ("[%ld] child reading output from grandchild\n",
			(long) getpid()));

	/*local */  {
		FILE *in = fdopen(stdout_pipe[READ_PIPE], "r");
		int ch = getc(in);

		if (ch != EOF) {
			FILE *mail = NULL;
			int bytes = 1;
			int status = 0;
#if defined(SYSLOG)
			char logbuf[1024];
			int bufidx = 0;
			if (SyslogOutput) {
				if (ch != '\n')
					logbuf[bufidx++] = ch;
			}
#endif

			Debug(DPROC | DEXT,
				("[%ld] got data (%x:%c) from grandchild\n",
					(long) getpid(), ch, ch));

				/* get name of recipient.  this is MAILTO if set to a
				 * valid local username; USER otherwise.
				 */
				if (mailto) {
				/* MAILTO was present in the environment
				 */
				if (!*mailto) {
					/* ... but it's empty. set to NULL
					 */
					mailto = NULL;
				}
			}
			else {
				/* MAILTO not present, set to USER.
				 */
				mailto = usernm;
			}

			/* get sender address.  this is MAILFROM if set (and safe),
			 * the user account name otherwise.
			 */
			if (!mailfrom || !*mailfrom || !safe_p(usernm, mailfrom)) {
				mailfrom = e->pwd->pw_name;
			}

			/* if we are supposed to be mailing, MAILTO will
			 * be non-NULL.  only in this case should we set
			 * up the mail command and subjects and stuff...
			 */

			/* Also skip it if MailCmd is set to "off" */
			if (mailto && safe_p(usernm, mailto)
				&& strncmp(MailCmd,"off",3) && !SyslogOutput) {
				char **env;
				char mailcmd[MAX_COMMAND];
				char hostname[MAXHOSTNAMELEN];
				char *content_type = env_get("CONTENT_TYPE", jobenv),
					*content_transfer_encoding =
					env_get("CONTENT_TRANSFER_ENCODING", jobenv);

				gethostname(hostname, MAXHOSTNAMELEN);

				if (MailCmd[0] == '\0') {
					if (snprintf(mailcmd, sizeof mailcmd, MAILFMT, MAILARG, mailfrom)
						>= sizeof mailcmd) {
						fprintf(stderr, "mailcmd too long\n");
						(void) _exit(ERROR_EXIT);
					}
				}
				else {
					strncpy(mailcmd, MailCmd, MAX_COMMAND);
				}
				if (!(mail = cron_popen(mailcmd, "w", e->pwd, jobenv))) {
					perror(mailcmd);
					(void) _exit(ERROR_EXIT);
				}

				fprintf(mail, "From: \"(Cron Daemon)\" <%s>\n", mailfrom);
				fprintf(mail, "To: %s\n", mailto);
				fprintf(mail, "Subject: Cron <%s@%s> %s\n",
					usernm, first_word(hostname, "."), e->cmd);

#ifdef MAIL_DATE
				fprintf(mail, "Date: %s\n", arpadate(&StartTime));
#endif /*MAIL_DATE */
				fprintf(mail, "MIME-Version: 1.0\n");
				if (content_type == 0L) {
					fprintf(mail, "Content-Type: text/plain; charset=%s\n",
						cron_default_mail_charset);
				}
				else {	/* user specified Content-Type header. 
						 * disallow new-lines for security reasons 
						 * (else users could specify arbitrary mail headers!)
						 */
					char *nl = content_type;
					size_t ctlen = strlen(content_type);
					while ((*nl != '\0')
						&& ((nl = strchr(nl, '\n')) != 0L)
						&& (nl < (content_type + ctlen))
						)
						*nl = ' ';
					fprintf(mail, "Content-Type: %s\n", content_type);
				}
				if (content_transfer_encoding != 0L) {
					char *nl = content_transfer_encoding;
					size_t ctlen = strlen(content_transfer_encoding);
					while ((*nl != '\0')
						&& ((nl = strchr(nl, '\n')) != 0L)
						&& (nl < (content_transfer_encoding + ctlen))
						)
						*nl = ' ';
					fprintf(mail, "Content-Transfer-Encoding: %s\n",
						content_transfer_encoding);
				}

				/* The Auto-Submitted header is
				 * defined (and suggested by) RFC3834.
				 */
				fprintf(mail, "Auto-Submitted: auto-generated\n");
				fprintf(mail, "Precedence: bulk\n");

				for (env = jobenv; *env; env++)
					fprintf(mail, "X-Cron-Env: <%s>\n", *env);
				fprintf(mail, "\n");

				/* this was the first char from the pipe
				 */
				putc(ch, mail);
			}

			/* we have to read the input pipe no matter whether
			 * we mail or not, but obviously we only write to
			 * mail pipe if we ARE mailing.
			 */

			while (EOF != (ch = getc(in))) {
				bytes++;
				if (mail)
					putc(ch, mail);
#if defined(SYSLOG)
				if (SyslogOutput) {
					logbuf[bufidx++] = ch;
					if ((ch == '\n') || (bufidx == sizeof(logbuf)-1)) {
						if (ch == '\n')
							logbuf[bufidx-1] = '\0';
						else
							logbuf[bufidx] = '\0';
						log_it(usernm, getpid(), "CMDOUT", logbuf, 0);
						bufidx = 0;
					}
				}
#endif
			}
			/* only close pipe if we opened it -- i.e., we're
			 * mailing...
			 */

			if (mail) {
				Debug(DPROC, ("[%ld] closing pipe to mail\n", (long) getpid()));
					/* Note: the pclose will probably see
					 * the termination of the grandchild
					 * in addition to the mail process, since
					 * it (the grandchild) is likely to exit
					 * after closing its stdout.
					 */
					status = cron_pclose(mail);
			}
#if defined(SYSLOG)
			if (SyslogOutput) {
				if (bufidx) {
					logbuf[bufidx] = '\0';
					log_it(usernm, getpid(), "CMDOUT", logbuf, 0);
				}
			}
#endif

			/* if there was output and we could not mail it,
			 * log the facts so the poor user can figure out
			 * what's going on.
			 */
			if (mail && status && !SyslogOutput) {
				char buf[MAX_TEMPSTR];

				sprintf(buf,
					"mailed %d byte%s of output but got status 0x%04x\n",
					bytes, (bytes == 1) ? "" : "s", status);
				log_it(usernm, getpid(), "MAIL", buf, 0);
			}

		}	/*if data from grandchild */

		Debug(DPROC, ("[%ld] got EOF from grandchild\n", (long) getpid()));

		fclose(in);	/* also closes stdout_pipe[READ_PIPE] */
	}

	/* wait for children to die.
	 */
	for (; children > 0; children--) {
		WAIT_T waiter;
		PID_T child;

		Debug(DPROC, ("[%ld] waiting for grandchild #%d to finish\n",
				(long) getpid(), children));
		while ((child = wait(&waiter)) < OK && errno == EINTR) ;
		if (child < OK) {
			Debug(DPROC,
				("[%ld] no more grandchildren--mail written?\n",
					(long) getpid()));
			break;
		}
		Debug(DPROC, ("[%ld] grandchild #%ld finished, status=%04x",
				(long) getpid(), (long) child, WEXITSTATUS(waiter)));
			if (WIFSIGNALED(waiter) && WCOREDUMP(waiter))
				Debug(DPROC, (", dumped core"));
			Debug(DPROC, ("\n"));
	}
	return OK_EXIT;
}

static int safe_p(const char *usernm, const char *s) {
	static const char safe_delim[] = "@!:%-.,_+";	/* conservative! */
	const char *t;
	int ch, first;

	for (t = s, first = 1; (ch = *t++) != '\0'; first = 0) {
		if (isascii(ch) && isprint(ch) &&
			(isalnum(ch) || (!first && strchr(safe_delim, ch))))
			continue;
		log_it(usernm, getpid(), "UNSAFE", s, 0);
		return (FALSE);
	}
	return (TRUE);
}
