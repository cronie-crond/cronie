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

/* crontab - install and manage per-user crontab files
 * vix 02may87 [RCS has the rest of the log]
 * vix 26jan87 [original]
 */

#define	MAIN_PROGRAM

#include <cron.h>
#ifdef WITH_SELINUX
# include <selinux/selinux.h>
# include <selinux/context.h>
# include <selinux/av_permissions.h>
#endif

#define NHEADER_LINES 0

enum opt_t {opt_unknown, opt_list, opt_delete, opt_edit, opt_replace};

#if DEBUGGING
static char *Options[] = {"???", "list", "delete", "edit", "replace"};

# ifdef WITH_SELINUX
static char *getoptargs = "u:lerisx:";
# else
static char *getoptargs = "u:lerix:";
# endif
#else
# ifdef WITH_SELINUX
static char *getoptargs = "u:leris";
# else
static char *getoptargs = "u:leri";
# endif
#endif
static char *selinux_context = 0;

static PID_T Pid;
static char User[MAX_UNAME], RealUser[MAX_UNAME];
static char Filename[MAX_FNAME], TempFilename[MAX_FNAME];
static FILE *NewCrontab;
static int CheckErrorCount;
static int PromptOnDelete;
static enum opt_t Option;
static struct passwd *pw;
static void list_cmd(void),
delete_cmd(void),
edit_cmd(void),
poke_daemon(void),
check_error(const char *), parse_args(int c, char *v[]), die(int);
static int replace_cmd(void);
static char *tmp_path(void);

static void usage(const char *msg) {
	fprintf(stderr, "%s: usage error: %s\n", ProgramName, msg);
	fprintf(stderr, "usage:\t%s [-u user] file\n", ProgramName);
	fprintf(stderr, "\t%s [-u user] [ -e | -l | -r ]\n", ProgramName);
	fprintf(stderr, "\t\t(default operation is replace, per 1003.2)\n");
	fprintf(stderr, "\t-e\t(edit user's crontab)\n");
	fprintf(stderr, "\t-l\t(list user's crontab)\n");
	fprintf(stderr, "\t-r\t(delete user's crontab)\n");
	fprintf(stderr, "\t-i\t(prompt before deleting user's crontab)\n");
#ifdef WITH_SELINUX
	fprintf(stderr, "\t-s\t(selinux context)\n");
#endif
	exit(ERROR_EXIT);
}

int main(int argc, char *argv[]) {
	int exitstatus;

	Pid = getpid();
	ProgramName = argv[0];
	MailCmd[0] = '\0';
	cron_default_mail_charset[0] = '\0';

	setlocale(LC_ALL, "");

#if defined(BSD)
	setlinebuf(stderr);
#endif
	char *n = "-";	/*set the n string to - so we have a valid string to use */
	/*should we desire to make changes to behavior later. */
	if (argv[1] == NULL) {	/* change behavior to allow crontab to take stdin with no '-' */
		argv[1] = n;
	}
	parse_args(argc, argv);	/* sets many globals, opens a file */
	set_cron_cwd();
	if (!allowed(RealUser, CRON_ALLOW, CRON_DENY)) {
		fprintf(stderr,
			"You (%s) are not allowed to use this program (%s)\n",
			User, ProgramName);
		fprintf(stderr, "See crontab(1) for more information\n");
		log_it(RealUser, Pid, "AUTH", "crontab command not allowed", 0);
		exit(ERROR_EXIT);
	}

#if defined(WITH_PAM)
	if (cron_start_pam(pw) != PAM_SUCCESS) {
		fprintf(stderr,
			"You (%s) are not allowed to access to (%s) because of pam configuration.\n",
			User, ProgramName);
		exit(ERROR_EXIT);
	};
#endif

	exitstatus = OK_EXIT;
	switch (Option) {
	case opt_unknown:
		exitstatus = ERROR_EXIT;
		break;
	case opt_list:
		list_cmd();
		break;
	case opt_delete:
		delete_cmd();
		break;
	case opt_edit:
		edit_cmd();
		break;
	case opt_replace:
		if (replace_cmd() < 0)
			exitstatus = ERROR_EXIT;
		break;
	default:
		abort();
	}
	cron_close_pam();
	exit(exitstatus);
 /*NOTREACHED*/}

static void parse_args(int argc, char *argv[]) {
	int argch;

	if (!(pw = getpwuid(getuid()))) {
		fprintf(stderr, "%s: your UID isn't in the passwd file.\n",
			ProgramName);
		fprintf(stderr, "bailing out.\n");
		exit(ERROR_EXIT);
	}
	if (strlen(pw->pw_name) >= sizeof User) {
		fprintf(stderr, "username too long\n");
		exit(ERROR_EXIT);
	}
	strcpy(User, pw->pw_name);
	strcpy(RealUser, User);
	Filename[0] = '\0';
	Option = opt_unknown;
	PromptOnDelete = 0;
	while (-1 != (argch = getopt(argc, argv, getoptargs))) {
		switch (argch) {
#if DEBUGGING
		case 'x':
			if (!set_debug_flags(optarg))
				usage("bad debug option");
			break;
#endif
		case 'u':
			if (MY_UID(pw) != ROOT_UID) {
				fprintf(stderr, "must be privileged to use -u\n");
				exit(ERROR_EXIT);
			}

			if (crontab_security_access() != 0) {
				fprintf(stderr,
					"Access denied by SELinux, must be privileged to use -u\n");
				exit(ERROR_EXIT);
			}

			if (!(pw = getpwnam(optarg))) {
				fprintf(stderr, "%s:  user `%s' unknown\n",
					ProgramName, optarg);
				exit(ERROR_EXIT);
			}
			if (strlen(optarg) >= sizeof User)
				usage("username too long");
			(void) strcpy(User, optarg);
			break;
		case 'l':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			Option = opt_list;
			break;
		case 'r':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			Option = opt_delete;
			break;
		case 'e':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			Option = opt_edit;
			break;
		case 'i':
			PromptOnDelete = 1;
			break;
#ifdef WITH_SELINUX
		case 's':
			if (getprevcon((security_context_t *) & (selinux_context))) {
				fprintf(stderr, "Cannot obtain SELinux process context\n");
				exit(ERROR_EXIT);
			}
			break;
#endif
		default:
			usage("unrecognized option");
		}
	}

	endpwent();

	if (Option != opt_unknown) {
		if (argv[optind] != NULL)
			usage("no arguments permitted after this option");
	}
	else {
		if (argv[optind] != NULL) {
			Option = opt_replace;
			if (strlen(argv[optind]) >= sizeof Filename)
				usage("filename too long");
			(void) strcpy(Filename, argv[optind]);
		}
		else
			usage("file name must be specified for replace");
	}

	if (Option == opt_replace) {
		/* we have to open the file here because we're going to
		 * chdir(2) into /var/cron before we get around to
		 * reading the file.
		 */
		if (!strcmp(Filename, "-"))
			NewCrontab = stdin;
		else {
			/* relinquish the setuid status of the binary during
			 * the open, lest nonroot users read files they should
			 * not be able to read.  we can't use access() here
			 * since there's a race condition.  thanks go out to
			 * Arnt Gulbrandsen <agulbra@pvv.unit.no> for spotting
			 * the race.
			 */

			if (swap_uids() < OK) {
				perror("swapping uids");
				exit(ERROR_EXIT);
			}
			if (!(NewCrontab = fopen(Filename, "r"))) {
				perror(Filename);
				exit(ERROR_EXIT);
			}
			if (swap_uids_back() < OK) {
				perror("swapping uids back");
				exit(ERROR_EXIT);
			}
		}
	}

	Debug(DMISC, ("user=%s, file=%s, option=%s\n",
			User, Filename, Options[(int) Option]))
}

static void list_cmd(void) {
	char n[MAX_FNAME];
	FILE *f;
	int ch;

	log_it(RealUser, Pid, "LIST", User, 0);
	if (!glue_strings(n, sizeof n, SPOOL_DIR, User, '/')) {
		fprintf(stderr, "path too long\n");
		exit(ERROR_EXIT);
	}
	if (!(f = fopen(n, "r"))) {
		if (errno == ENOENT)
			fprintf(stderr, "no crontab for %s\n", User);
		else
			perror(n);
		exit(ERROR_EXIT);
	}

	/* file is open. copy to stdout, close.
	 */
	Set_LineNum(1)
		while (EOF != (ch = get_char(f)))
		putchar(ch);
	fclose(f);
}

static void delete_cmd(void) {
	char n[MAX_FNAME] = "";
	if (PromptOnDelete == 1) {
		printf("crontab: really delete %s's crontab? ", User);
		fflush(stdout);
		if ((fgets(n, MAX_FNAME - 1, stdin) == 0L)
			|| ((n[0] != 'Y') && (n[0] != 'y'))
			)
			exit(0);
	}

	log_it(RealUser, Pid, "DELETE", User, 0);
	if (!glue_strings(n, sizeof n, SPOOL_DIR, User, '/')) {
		fprintf(stderr, "path too long\n");
		exit(ERROR_EXIT);
	}
	if (unlink(n) != 0) {
		if (errno == ENOENT)
			fprintf(stderr, "no crontab for %s\n", User);
		else
			perror(n);
		exit(ERROR_EXIT);
	}
	poke_daemon();
}

static void check_error(const char *msg) {
	CheckErrorCount++;
	fprintf(stderr, "\"%s\":%d: %s\n", Filename, LineNumber - 1, msg);
}

static char *tmp_path() {
	char *tmpdir = NULL;

	if ((getuid() == geteuid()) && (getgid() == getegid())) {
		tmpdir = getenv("TMPDIR");
	}
	return tmpdir ? tmpdir : "/tmp";
}

static void edit_cmd(void) {
	char n[MAX_FNAME], q[MAX_TEMPSTR], *editor;
	FILE *f;
	int ch = '\0', t;
	struct stat statbuf;
	struct utimbuf utimebuf;
	WAIT_T waiter;
	PID_T pid, xpid;

	log_it(RealUser, Pid, "BEGIN EDIT", User, 0);
	if (!glue_strings(n, sizeof n, SPOOL_DIR, User, '/')) {
		fprintf(stderr, "path too long\n");
		exit(ERROR_EXIT);
	}
	if (!(f = fopen(n, "r"))) {
		if (errno != ENOENT) {
			perror(n);
			exit(ERROR_EXIT);
		}
		fprintf(stderr, "no crontab for %s - using an empty one\n", User);
		if (!(f = fopen(_PATH_DEVNULL, "r"))) {
			perror(_PATH_DEVNULL);
			exit(ERROR_EXIT);
		}
	}

	/* Turn off signals. */
	(void) signal(SIGHUP, SIG_IGN);
	(void) signal(SIGINT, SIG_IGN);
	(void) signal(SIGQUIT, SIG_IGN);

	if (!glue_strings(Filename, sizeof Filename, tmp_path(),
			"crontab.XXXXXXXXXX", '/')) {
		fprintf(stderr, "path too long\n");
		exit(ERROR_EXIT);
	}
	if (swap_uids() == -1) {
		perror("swapping uids");
		exit(ERROR_EXIT);
	}
	if (-1 == (t = mkstemp(Filename))) {
		perror(Filename);
		goto fatal;
	}

	if (swap_uids_back() == -1) {
		perror("swapping uids back");
		goto fatal;
	}
	if (!(NewCrontab = fdopen(t, "r+"))) {
		perror("fdopen");
		goto fatal;
	}

	Set_LineNum(1)
		/* 
		 * NHEADER_LINES processing removed for clarity
		 * (NHEADER_LINES == 0 in all Red Hat crontabs)
		 */
		/* copy the rest of the crontab (if any) to the temp file.
		 */
		if (EOF != ch)
		while (EOF != (ch = get_char(f)))
			putc(ch, NewCrontab);

#ifdef WITH_SELINUX
	if (selinux_context) {
		context_t ccon = NULL;
		const char *level = NULL;

		if (!(ccon = context_new(selinux_context))) {
			fprintf(stderr, "context_new failed\n");
			goto fatal;
		}

		if (!(level = context_range_get(ccon))) {
			fprintf(stderr, "context_range failed\n");
			goto fatal;
		}

		fprintf(NewCrontab, "MLS_LEVEL=%s\n", level);
		context_free(ccon);
		freecon(selinux_context);
		selinux_context = NULL;
	}
#endif

	fclose(f);
	if (fflush(NewCrontab) < OK) {
		perror(Filename);
		exit(ERROR_EXIT);
	}
        if (swap_uids() == -1) {
                perror("swapping uids");
                exit(ERROR_EXIT);
        }
	/* Set it to 1970 */
	utimebuf.actime = 0;
	utimebuf.modtime = 0;
	utime(Filename, &utimebuf);
	if (swap_uids_back() == -1) {
		perror("swapping uids");
		exit(ERROR_EXIT);
	}
  again:
	rewind(NewCrontab);
	if (ferror(NewCrontab)) {
		fprintf(stderr, "%s: error while writing new crontab to %s\n",
			ProgramName, Filename);
	  fatal:
		unlink(Filename);
		exit(ERROR_EXIT);
	}

	if (((editor = getenv("VISUAL")) == NULL || *editor == '\0') &&
		((editor = getenv("EDITOR")) == NULL || *editor == '\0')) {
		editor = EDITOR;
	}

	/* we still have the file open.  editors will generally rewrite the
	 * original file rather than renaming/unlinking it and starting a
	 * new one; even backup files are supposed to be made by copying
	 * rather than by renaming.  if some editor does not support this,
	 * then don't use it.  the security problems are more severe if we
	 * close and reopen the file around the edit.
	 */

	switch (pid = fork()) {
	case -1:
		perror("fork");
		goto fatal;
	case 0:
		/* child */
		if (setgid(MY_GID(pw)) < 0) {
			perror("setgid(getgid())");
			exit(ERROR_EXIT);
		}
		if (setuid(MY_UID(pw)) < 0) {
			perror("setuid(getuid())");
			exit(ERROR_EXIT);
		}
		if (!glue_strings(q, sizeof q, editor, Filename, ' ')) {
			fprintf(stderr, "%s: editor command line too long\n", ProgramName);
			exit(ERROR_EXIT);
		}
		execlp(_PATH_BSHELL, _PATH_BSHELL, "-c", q, (char *) 0);
		perror(editor);
		exit(ERROR_EXIT);
	 /*NOTREACHED*/ default:
		/* parent */
		break;
	}

	/* parent */
	for (;;) {
		xpid = waitpid(pid, &waiter, 0);
		if (xpid == -1) {
			if (errno != EINTR)
				fprintf(stderr,
					"%s: waitpid() failed waiting for PID %ld from \"%s\": %s\n",
					ProgramName, (long) pid, editor, strerror(errno));
		}
		else if (xpid != pid) {
			fprintf(stderr, "%s: wrong PID (%ld != %ld) from \"%s\"\n",
				ProgramName, (long) xpid, (long) pid, editor);
			goto fatal;
		}
		else if (WIFEXITED(waiter) && WEXITSTATUS(waiter)) {
			fprintf(stderr, "%s: \"%s\" exited with status %d\n",
				ProgramName, editor, WEXITSTATUS(waiter));
			goto fatal;
		}
		else if (WIFSIGNALED(waiter)) {
			fprintf(stderr,
				"%s: \"%s\" killed; signal %d (%score dumped)\n",
				ProgramName, editor, WTERMSIG(waiter),
				WCOREDUMP(waiter) ? "" : "no ");
			goto fatal;
		}
		else
			break;
	}
	(void) signal(SIGHUP, SIG_DFL);
	(void) signal(SIGINT, SIG_DFL);
	(void) signal(SIGQUIT, SIG_DFL);

	/* lstat doesn't make any harm, because 
	 * the file is stat'ed only when crontab is touched
	 */
	if (lstat(Filename, &statbuf) < 0) {
		perror("lstat");
		goto fatal;
	}

	if (!S_ISREG(statbuf.st_mode)) {
		fprintf(stderr, "%s: illegal crontab\n", ProgramName);
		goto remove;
	}

	if (statbuf.st_mtime == 0) {
		fprintf(stderr, "%s: no changes made to crontab\n", ProgramName);
		goto remove;
	}

	fprintf(stderr, "%s: installing new crontab\n", ProgramName);
	fclose(NewCrontab);
	if (swap_uids() < OK) {
		perror("swapping uids");
		goto remove;
	}
	if (!(NewCrontab = fopen(Filename, "r+"))) {
		perror("cannot read new crontab");
		goto remove;
	}
	if (swap_uids_back() < OK) {
		perror("swapping uids back");
		exit(ERROR_EXIT);
	}
	if (NewCrontab == 0L) {
		perror("fopen");
		goto fatal;
	}
	switch (replace_cmd()) {
	case 0:
		break;
	case -1:
		for (;;) {
			printf("Do you want to retry the same edit? ");
			fflush(stdout);
			q[0] = '\0';
			if (fgets(q, sizeof q, stdin) == 0L)
				continue;
			switch (q[0]) {
			case 'y':
			case 'Y':
				goto again;
			case 'n':
			case 'N':
				goto abandon;
			default:
				fprintf(stderr, "Enter Y or N\n");
			}
		}
	 /*NOTREACHED*/ case -2:
	  abandon:
		fprintf(stderr, "%s: edits left in %s\n", ProgramName, Filename);
		goto done;
	default:
		fprintf(stderr, "%s: panic: bad switch() in replace_cmd()\n",
			ProgramName);
		goto fatal;
	}
  remove:
	unlink(Filename);
  done:
	log_it(RealUser, Pid, "END EDIT", User, 0);
}

/* returns	0	on success
 *		-1	on syntax error
 *		-2	on install error
 */
static int replace_cmd(void) {
	char n[MAX_FNAME], envstr[MAX_ENVSTR];
	FILE *tmp;
	int ch, eof, fd;
	int error = 0;
	entry *e;
	uid_t file_owner;
	char **envp = env_init();

	if (envp == NULL) {
		fprintf(stderr, "%s: Cannot allocate memory.\n", ProgramName);
		return (-2);
	}

	if (!glue_strings(TempFilename, sizeof TempFilename, SPOOL_DIR,
			"tmp.XXXXXXXXXX", '/')) {
		TempFilename[0] = '\0';
		fprintf(stderr, "path too long\n");
		return (-2);
	}
	if ((fd = mkstemp(TempFilename)) == -1 || !(tmp = fdopen(fd, "w+"))) {
		perror(TempFilename);
		if (fd != -1) {
			close(fd);
			unlink(TempFilename);
		}
		TempFilename[0] = '\0';
		return (-2);
	}

	(void) signal(SIGHUP, die);
	(void) signal(SIGINT, die);
	(void) signal(SIGQUIT, die);

	/* write a signature at the top of the file.
	 *
	 * VERY IMPORTANT: make sure NHEADER_LINES agrees with this code.
	 */
	/*fprintf(tmp, "# DO NOT EDIT THIS FILE - edit the master and reinstall.\n");
	 *fprintf(tmp, "# (%s installed on %-24.24s)\n", Filename, ctime(&now));
	 *fprintf(tmp, "# (Cron version %s)\n", CRON_VERSION);
	 */
#ifdef WITH_SELINUX
	if (selinux_context)
		fprintf(tmp, "SELINUX_ROLE_TYPE=%s\n", selinux_context);
#endif

	/* copy the crontab to the tmp
	 */
	rewind(NewCrontab);
	Set_LineNum(1)
		while (EOF != (ch = get_char(NewCrontab)))
		putc(ch, tmp);
	if (ftruncate(fileno(tmp), ftell(tmp)) == -1) {
		fprintf(stderr, "%s: error while writing new crontab to %s\n",
			ProgramName, TempFilename);
		fclose(tmp);
		error = -2;
		goto done;
	}
	fflush(tmp);
	rewind(tmp);
	if (ferror(tmp)) {
		fprintf(stderr, "%s: error while writing new crontab to %s\n",
			ProgramName, TempFilename);
		fclose(tmp);
		error = -2;
		goto done;
	}

	/* check the syntax of the file being installed.
	 */

	/* BUG: was reporting errors after the EOF if there were any errors
	 * in the file proper -- kludged it by stopping after first error.
	 *      vix 31mar87
	 */
	Set_LineNum(1 - NHEADER_LINES)
		CheckErrorCount = 0;
	eof = FALSE;
	while (!CheckErrorCount && !eof) {
		switch (load_env(envstr, tmp)) {
		case ERR:
			/* check for data before the EOF */
			if (envstr[0] != '\0') {
				Set_LineNum(LineNumber + 1);
				check_error("premature EOF");
			}
			eof = TRUE;
			break;
		case FALSE:
			e = load_entry(tmp, check_error, pw, envp);
			if (e)
				free(e);
			break;
		case TRUE:
			break;
		}
	}

	if (CheckErrorCount != 0) {
		fprintf(stderr, "errors in crontab file, can't install.\n");
		fclose(tmp);
		error = -1;
		goto done;
	}

	file_owner = (getgid() == getegid())? ROOT_UID : pw->pw_uid;

#ifdef HAS_FCHOWN
	if (fchown(fileno(tmp), file_owner, -1) < OK) {
		perror("fchown");
		fclose(tmp);
		error = -2;
		goto done;
	}
#else
	if (chown(TempFilename, file_owner, -1) < OK) {
		perror("chown");
		fclose(tmp);
		error = -2;
		goto done;
	}
#endif

	if (fclose(tmp) == EOF) {
		perror("fclose");
		error = -2;
		goto done;
	}

	if (!glue_strings(n, sizeof n, SPOOL_DIR, User, '/')) {
		fprintf(stderr, "path too long\n");
		error = -2;
		goto done;
	}
	if (rename(TempFilename, n)) {
		fprintf(stderr, "%s: error renaming %s to %s\n",
			ProgramName, TempFilename, n);
		perror("rename");
		error = -2;
		goto done;
	}
	TempFilename[0] = '\0';
	log_it(RealUser, Pid, "REPLACE", User, 0);

	poke_daemon();

  done:
	(void) signal(SIGHUP, SIG_DFL);
	(void) signal(SIGINT, SIG_DFL);
	(void) signal(SIGQUIT, SIG_DFL);
	if (TempFilename[0]) {
		(void) unlink(TempFilename);
		TempFilename[0] = '\0';
	}
	return (error);
}

static void poke_daemon(void) {
	if (utime(SPOOL_DIR, NULL) < OK) {
		fprintf(stderr, "crontab: can't update mtime on spooldir\n");
		perror(SPOOL_DIR);
		return;
	}
}

static void die(int x) {
	if (TempFilename[0])
		(void) unlink(TempFilename);
	_exit(ERROR_EXIT);
}
