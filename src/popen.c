/*	$NetBSD: popen.c,v 1.9 2005/03/16 02:53:55 xtraeme Exp $	*/

/*
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software written by Ken Arnold and
 * published in UNIX Review, Vol. 6, No. 8.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "config.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "funcs.h"
#include "globals.h"
#include "macros.h"

#ifdef HAVE_SYS_CDEFS_H
# include <sys/cdefs.h>
#endif

#include <signal.h>

/*
 * Special version of popen which avoids call to shell.  This insures noone
 * may create a pipe to a hidden program as a side effect of a list or dir
 * command.
 */
static PID_T *pids;
static int fds;

#define MAX_ARGS 1024

FILE *cron_popen(char *program, const char *type, struct passwd *pw, char **jobenv) {
	char *cp;
	FILE *iop;
	int argc, pdes[2];
	PID_T pid;
	char *argv[MAX_ARGS];
	ssize_t out;
	char buf[PIPE_BUF];
	struct sigaction sa;
	int fd;

#ifdef __GNUC__
	(void) &iop;	/* Avoid fork clobbering */
#endif

	if ((*type != 'r' && *type != 'w') || type[1])
		return (NULL);

	if (!pids) {
		if ((fds = getdtablesize()) <= 0)
			return (NULL);
		if (!(pids = (PID_T *) malloc((u_int) ((size_t)fds * sizeof (PID_T)))))
			return (NULL);
		memset((char *) pids, 0, (size_t)fds * sizeof (PID_T));
	}
	if (pipe(pdes) < 0)
		return (NULL);

	/* break up string into pieces */
	for (argc = 0, cp = program; argc < MAX_ARGS; cp = NULL)
		if (!(argv[argc++] = strtok(cp, " \t\n")))
			break;

	iop = NULL;
	switch (pid = fork()) {
	case -1:	/* error */
		(void) close(pdes[0]);
		(void) close(pdes[1]);
		goto pfree;
		/* NOTREACHED */
	case 0:	/* child */
		if (*type == 'r') {
			if (pdes[1] != STDOUT) {
				dup2(pdes[1], STDOUT);
				dup2(pdes[1], STDERR);	/* stderr, too! */
				(void) close(pdes[1]);
			}
			(void) close(pdes[0]);
		}
		else {
			if (pdes[0] != STDIN) {
				dup2(pdes[0], STDIN);
				(void) close(pdes[0]);
			}
			(void) close(pdes[1]);
		}

		/* reset SIGPIPE to default for the child */
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = SIG_DFL;
		sigaction(SIGPIPE, &sa, NULL);

		/* close all unwanted open file descriptors */
		for (fd = STDERR + 1; fd < fds; fd++) {
			close(fd);
		}

		if (cron_change_user_permanently(pw, env_get("HOME", jobenv)) != 0)
			_exit(2);

		if (execvpe(argv[0], argv, jobenv) < 0) {
			int save_errno = errno;

			log_it("CRON", getpid(), "EXEC FAILED", program, save_errno);
			if (*type != 'r') {
				while (0 != (out = read(STDIN, buf, PIPE_BUF))) {
					if ((out == -1) && (errno != EINTR))
						break;
				}
			}
		}
		_exit(1);
	}
	/* parent; assume fdopen can't fail...  */
	if (*type == 'r') {
		iop = fdopen(pdes[0], type);
		(void) close(pdes[1]);
	}
	else {
		iop = fdopen(pdes[1], type);
		(void) close(pdes[0]);
	}
	pids[fileno(iop)] = pid;

  pfree:
	return (iop);
}

int cron_pclose(FILE * iop) {
	int fdes;
	sigset_t oset, nset;
	WAIT_T stat_loc;
	PID_T pid;

	/*
	 * pclose returns -1 if stream is not associated with a
	 * `popened' command, or, if already `pclosed'.
	 */
	if (pids == 0 || pids[fdes = fileno(iop)] == 0)
		return (-1);
	(void) fclose(iop);

	sigemptyset(&nset);
	sigaddset(&nset, SIGINT);
	sigaddset(&nset, SIGQUIT);
	sigaddset(&nset, SIGHUP);
	(void) sigprocmask(SIG_BLOCK, &nset, &oset);
	while ((pid = wait(&stat_loc)) != pids[fdes] && pid != -1) ;
	(void) sigprocmask(SIG_SETMASK, &oset, NULL);
	pids[fdes] = 0;
	return (pid == -1 ? -1 : WEXITSTATUS(stat_loc));
}
