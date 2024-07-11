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

/*
 * Modified 2010/09/10 by Colin Dean, Durham University IT Service,
 * to add clustering support.
 */

#include "config.h"

#define	MAIN_PROGRAM

#include <errno.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>

#ifdef WITH_PAM
# include <security/pam_appl.h>
#endif

#ifdef WITH_SELINUX
# include <selinux/selinux.h>
# include <selinux/context.h>
#endif

#include "cronie_common.h"
#include "bitstring.h"
#include "externs.h"
#include "funcs.h"
#include "globals.h"
#include "macros.h"
#include "pathnames.h"
#include "structs.h"

#define NHEADER_LINES 0

#define COMMENT_COLOR "\x1B[34;1m"
#define ERROR_COLOR "\x1B[31;1m"
#define RESET_COLOR "\x1B[0m"

enum opt_t {
	opt_unknown, opt_list, opt_delete, opt_edit, opt_replace, opt_hostset,
	opt_hostget, opt_test
};

#if DEBUGGING
static const char *Options[] = {
	"???", "list", "delete", "edit", "replace", "hostset", "hostget", "test"
};

# ifdef WITH_SELINUX
static const char *getoptargs = "u:lerisncx:VT";
# else
static const char *getoptargs = "u:lerincx:VT";
# endif
#else
# ifdef WITH_SELINUX
static const char *getoptargs = "u:lerisncVT";
# else
static const char *getoptargs = "u:lerincVT";
# endif
#endif
#ifdef WITH_SELINUX
static char *selinux_context = 0;
#endif

static PID_T Pid;
static char User[MAX_UNAME], RealUser[MAX_UNAME];
static char Filename[MAX_FNAME], TempFilename[MAX_FNAME];
static char Host[MAXHOSTNAMELEN];
static FILE *NewCrontab;
static int CheckErrorCount;
static int PromptOnDelete;
static int HostSpecified;
static enum opt_t Option;
static struct passwd *pw;
static void list_cmd(void),
delete_cmd(void),
edit_cmd(void),
poke_daemon(void),
check_error(const char *), parse_args(int c, char *v[]),
die(int) ATTRIBUTE_NORETURN;
static int replace_cmd(void), hostset_cmd(void), hostget_cmd(void),
test_cmd(void), check_syntax(FILE *), backup_crontab(const char *);
static char *host_specific_filename(const char *prefix,
const char *suffix);
static const char *tmp_path(void);

static void usage(const char *msg) ATTRIBUTE_NORETURN;
static void usage(const char *msg) {
	fprintf(stderr, "%s: usage error: %s\n", ProgramName, msg);
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, " %s [options] file\n", ProgramName);
	fprintf(stderr, " %s [options]\n", ProgramName);
	fprintf(stderr, " %s -n [hostname]\n", ProgramName);
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, " -u <user>  define user\n");
	fprintf(stderr, " -e         edit user's crontab\n");
	fprintf(stderr, " -l         list user's crontab\n");
	fprintf(stderr, " -r         delete user's crontab\n");
	fprintf(stderr, " -i         prompt before deleting\n");
	fprintf(stderr, " -n <host>  set host in cluster to run users' crontabs\n");
	fprintf(stderr, " -c         get host in cluster to run users' crontabs\n");
	fprintf(stderr, " -T <file>  test a crontab file syntax\n");
#ifdef WITH_SELINUX
	fprintf(stderr, " -s         selinux context\n");
#endif
	fprintf(stderr, " -V         print version and exit\n");
#ifdef DEBUGGING
	fprintf(stderr, " -x <mask>  enable debugging\n");
#endif
	fprintf(stderr, "\nDefault operation is replace, per 1003.2\n");
	exit(ERROR_EXIT);
}

int main(int argc, char *argv[]) {
	int exitstatus;

	if ((ProgramName = strrchr(argv[0], '/')) == NULL) {
		ProgramName = argv[0];
	}
	else {
		++ProgramName;
	}

	Pid = getpid();
	MailCmd[0] = '\0';
	cron_default_mail_charset[0] = '\0';

	setlocale(LC_ALL, "");

#if defined(BSD)
	setlinebuf(stderr);
#endif
	parse_args(argc, argv);	/* sets many globals, opens a file */
	check_spool_dir();
	if (!allowed(RealUser, CRON_ALLOW, CRON_DENY)) {
		fprintf(stderr,
			"You (%s) are not allowed to use this program (%s)\n",
			User, ProgramName);
		fprintf(stderr, "See crontab(1) for more information\n");
		log_it(RealUser, Pid, "AUTH", "crontab command not allowed", 0);
		exit(ERROR_EXIT);
	}

#if defined(WITH_PAM)
	if (getuid() != 0 && cron_start_pam(pw) != PAM_SUCCESS) {
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
	case opt_hostset:
		if (hostset_cmd() < 0)
			exitstatus = ERROR_EXIT;
		break;
	case opt_hostget:
		if (hostget_cmd() < 0)
			exitstatus = ERROR_EXIT;
		break;
	case opt_test:
		if (test_cmd() < 0)
			exitstatus = ERROR_EXIT;
		break;
	default:
		abort();
	}
#ifdef WITH_PAM
	cron_close_pam();
#endif
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
	HostSpecified = 0;
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
#ifdef WITH_SELINUX
			if (crontab_security_access() != 0) {
				fprintf(stderr,
					"Access denied by SELinux, must be privileged to use -u\n");
				exit(ERROR_EXIT);
			}
#endif
			if (Option == opt_hostset || Option == opt_hostget ||
				Option == opt_test) {
				fprintf(stderr, "cannot use -u with -n, -c or -T\n");
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
		case 'T':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			Option = opt_test;
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
		case 'n':
			if (MY_UID(pw) != ROOT_UID) {
				fprintf(stderr, "must be privileged to set host with -n\n");
				exit(ERROR_EXIT);
			}
			if (Option != opt_unknown)
				usage("only one operation permitted");
			if (strcmp(User, RealUser) != 0) {
				fprintf(stderr, "cannot use -u with -n or -c\n");
				exit(ERROR_EXIT);
			}
			Option = opt_hostset;
			break;
		case 'c':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			if (strcmp(User, RealUser) != 0) {
				fprintf(stderr, "cannot use -u with -n or -c\n");
				exit(ERROR_EXIT);
			}
			Option = opt_hostget;
			break;
		case 'V':
			puts(PACKAGE_STRING);
			exit(EXIT_SUCCESS);
		default:
			usage("unrecognized option");
		}
	}

	endpwent();

	if (Option == opt_hostset && argv[optind] != NULL) {
		HostSpecified = 1;
		if (strlen(argv[optind]) >= sizeof Host)
			usage("hostname too long");
		(void) strcpy(Host, argv[optind]);
		optind++;
	}

	if (Option == opt_unknown) {
		/* replace is the default option */
		Option = opt_replace;
	}

	if (Option == opt_replace || Option == opt_test) {
		if (argv[optind] != NULL) {
			if (strlen(argv[optind]) >= sizeof Filename)
				usage("filename too long");
			(void) strcpy(Filename, argv[optind]);
			optind++;
		}
		else if (isatty(STDIN_FILENO)) {
			usage("file name or - (for stdin) must be specified");
		}
		else {
			strcpy(Filename, "-");
		}
	}

	if (Option != opt_unknown && argv[optind] != NULL) {
		usage("no arguments permitted after this option");
	}

	if (Filename[0] != '\0') {
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
			struct stat sb;

			if (swap_uids() < OK) {
				perror("swapping uids");
				exit(ERROR_EXIT);
			}
			if (!(NewCrontab = fopen(Filename, "r"))) {
				perror(Filename);
				exit(ERROR_EXIT);
			}
			if (fstat(fileno(NewCrontab), &sb) < 0) {
				perror(Filename);
				exit(ERROR_EXIT);
			}
			if ((sb.st_mode & S_IFMT) == S_IFDIR) {
				fprintf(stderr,
					"invalid crontab file: '%s' is a directory\n", Filename);
				fclose(NewCrontab);
				exit(ERROR_EXIT);
			}
			if (swap_uids_back() < OK) {
				perror("swapping uids back");
				exit(ERROR_EXIT);
			}
		}
	}

	Debug(DMISC, ("user=%s, file=%s, option=%s\n",
			User, Filename, Options[(int) Option]));
}

static void list_cmd(void) {
	char n[MAX_FNAME];
	FILE *f;
	int ch;
	const int colorize = isatty(STDOUT) && getenv("NO_COLOR") == NULL;
	int new_line = 1;
	int in_comment = 0;

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
	Set_LineNum(1);
	while (EOF != (ch = get_char(f))) {
		if (colorize) {
			if (!in_comment && new_line && ch == '#') {
				in_comment = 1;
				fputs(COMMENT_COLOR, stdout);
			}
			if (in_comment && ch == '\n') {
				in_comment = 0;
				fputs(RESET_COLOR, stdout);
			}
		}
		putchar(ch);
		new_line = ch == '\n';
	}
	/* no new line at EOF */
	if (colorize && !new_line) {
		putchar('\n');
		fputs(ERROR_COLOR "No end-of-line character at the end of file"
			RESET_COLOR, stdout);
		putchar('\n');
	}
	fclose(f);
}

static void delete_cmd(void) {
	char n[MAX_FNAME] = "";
	if (PromptOnDelete == 1) {
		printf("crontab: really delete %s's crontab? ", User);
		fflush(stdout);
		if ((fgets(n, MAX_FNAME - 1, stdin) == NULL)
			|| ((n[0] != 'Y') && (n[0] != 'y'))
			)
			exit(0);
	}

	log_it(RealUser, Pid, "DELETE", User, 0);
	if (!glue_strings(n, sizeof n, SPOOL_DIR, User, '/')) {
		fprintf(stderr, "path too long\n");
		exit(ERROR_EXIT);
	}
	
	if (backup_crontab(n) == -1) {
		fprintf(stderr, "no crontab for %s\n", User);
		exit(ERROR_EXIT);
	}

	if (unlink(n) != 0) {
		perror(n);
		exit(ERROR_EXIT);
	}
	poke_daemon();
}

/* returns	0	on success
 *		-1	on non existent crontab file
 *		-2	on failure to write the backup file
 */
static int backup_crontab(const char *crontab_path) {
	const char *env_value;
	char backup_dir[MAX_FNAME], backup_path[MAX_FNAME];
	int ch = '\0';
	FILE *crontab_file;
	FILE *backup_file;
	struct stat sb;
	int retval = 0;
	mode_t old_umask;
	
	/* create backup directory */
	if ((env_value = getenv("XDG_CACHE_HOME")) != NULL) {
		if(!glue_strings(backup_dir, sizeof backup_dir, env_value, "", '\0')){
			fprintf(stderr, "$XDG_CACHE_HOME path too long\n");
			return -2;
		}
	}
	else if ((env_value = getenv("HOME")) != NULL) {
		if (!glue_strings(backup_dir, sizeof backup_dir, env_value,
				".cache", '/')) {
			fprintf(stderr, "$HOME path too long\n");
			return -2;
		}
	}
	else {
		fprintf(stderr, "Could not find environment variable XDG_CACHE_HOME or HOME to save the backup\n");
		return -2;
	}

	if (!glue_strings(backup_dir, sizeof backup_dir, backup_dir,
			"crontab", '/')) {
		fprintf(stderr, "backup path too long\n");
		return -2;
	}

	/* create backup file */
	if (!glue_strings(backup_path, sizeof backup_path, backup_dir,
			"crontab", '/')) {
		fprintf(stderr, "backup path too long\n");
		return -2;
	}

	if (getuid() != pw->pw_uid) { // verify if -u is used
		if (!glue_strings(backup_path, sizeof backup_path, backup_path,
				User, '.')) {
			fprintf(stderr, "backup path too long\n");
			return -2;
		}
	}

	if (!glue_strings(backup_path, sizeof backup_path, backup_path,
			"bak", '.')) {
		fprintf(stderr, "backup path too long\n");
		return -2;
	}

	/* perform the backup */
	if ((crontab_file = fopen(crontab_path, "r")) == NULL) {
		if (errno != ENOENT) {
			perror(crontab_path);
			exit(ERROR_EXIT);
		}
		return -1;
	}
	
	if (swap_uids() == -1) {
		perror("swapping uids");
		(void) fclose(crontab_file);
		exit(ERROR_EXIT);
	}

	if (stat(backup_dir, &sb) < OK && errno == ENOENT) {
		if (OK != mkdir(backup_dir, 0755)) {
			fprintf(stderr, "%s: ", backup_dir);
			perror("mkdir");
			retval = -2;
			goto swapback;
		}
	}
	
	/* ensure backup file has strict permissions. Crontabs are not readable for
	   other users and might contain sensitive information */
	old_umask = umask(0077);
	if ((backup_file = fopen(backup_path, "w+")) == NULL) {
		fprintf(stderr, "Failed to write to the backup file: ");
		perror(backup_path);
		retval = -2;
		goto swapback;
	}

	swapback:
	umask(old_umask);

	if (swap_uids_back() < OK) {
		perror("swapping uids back");
		if (backup_file != NULL) {
			(void) fclose(backup_file);
		}
		(void) fclose(crontab_file);
		exit(ERROR_EXIT);
	}

	if (retval != 0)
		goto cleanup;

	if (EOF != ch)
		while (EOF != (ch = get_char(crontab_file)))
			putc(ch, backup_file);

	printf("Backup of %s's previous crontab saved to %s\n", User, backup_path);

cleanup:
	if (backup_file != NULL) {
		(void) fclose(backup_file);
	}
	(void) fclose(crontab_file);

	return retval;
}

static void check_error(const char *msg) {
	CheckErrorCount++;
	fprintf(stderr, "\"%s\":%d: %s\n", Filename, LineNumber - 1, msg);
}

static const char *tmp_path(void) {
	const char *tmpdir = NULL;

	if ((getuid() == geteuid()) && (getgid() == getegid())) {
		tmpdir = getenv("TMPDIR");
	}
	return tmpdir ? tmpdir : "/tmp";
}

static char *host_specific_filename(const char *prefix, const char *suffix) {
	/*
	 * For cluster-wide use, where there is otherwise risk of the same
	 * name being generated on more than one host at once, insert hostname
	 * separated with dots, and return static buffer or NULL on failure.
	 */

	static char safename[MAX_FNAME];
	char hostname[MAX_FNAME];

	if (gethostname(hostname, sizeof hostname) != 0)
		return NULL;

	if (prefix) {
		if (!glue_strings(safename, sizeof safename, prefix, hostname, '.'))
			return NULL;
		strcpy(hostname, safename);
	}
	if (suffix) {
		if (!glue_strings(safename, sizeof safename, hostname, suffix, '.'))
			return NULL;
	}

	return safename;
}

static void edit_cmd(void) {
	char n[MAX_FNAME], q[MAX_TEMPSTR];
	const char *editor;
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
			"crontab.XXXXXX", '/')) {
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

	Set_LineNum(1);
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

	if (!glue_strings(q, sizeof q, editor, Filename, ' ')) {
		fprintf(stderr, "%s: %s command line too long\n", ProgramName, editor);
		goto fatal;
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
	if (NewCrontab == NULL) {
		perror("fopen");
		goto fatal;
	}
	switch (replace_cmd()) {
	case 0:
		break;
	case -1:
		for (;;) {
			printf("Do you want to retry the same edit? (Y/N) ");
			fflush(stdout);
			q[0] = '\0';
			if (fgets(q, sizeof q, stdin) == NULL)
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

/*
* Check if crontab file can be installed or not 
*/
static int test_cmd(void) {
	if (check_syntax(NewCrontab) < 0) {
		fprintf(stderr, "Invalid crontab file. Syntax issues were found.\n");
		return (-2);
	}
	else {
		fprintf(stderr, "No syntax issues were found in the crontab file.\n");
	}
	return (0);
}

/* returns	0	on success
 *		-1	on syntax error
 *		-2	on install error
 */
static int replace_cmd(void) {
	char n[MAX_FNAME];
	FILE *tmp;
	int ch, fd;
	int error = 0;
	uid_t file_owner;
	char *safename;

	safename = host_specific_filename("#tmp", "XXXXXXXXXX");
	if (!safename || !glue_strings(TempFilename, sizeof TempFilename, SPOOL_DIR,
			safename, '/')) {
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
	Set_LineNum(1);
	while (EOF != (ch = get_char(NewCrontab)))
		putc(ch, tmp);
	if (ftruncate(fileno(tmp), ftell(tmp)) == -1) {
		fprintf(stderr, "%s: error while writing new crontab to %s\n",
			ProgramName, TempFilename);
		fclose(tmp);
		error = -2;
		goto done;
	}
	if (ferror(tmp) || fflush(tmp) || fsync(fileno(tmp))) {
		fprintf(stderr, "%s: error while writing new crontab to %s\n",
			ProgramName, TempFilename);
		fclose(tmp);
		error = -2;
		goto done;
	}
	rewind(tmp);

	if ((error = check_syntax(tmp)) < 0) {
		fprintf(stderr, "Invalid crontab file, can't install.\n");
		fclose(tmp);
		goto done;
	}

	file_owner = (getgid() == geteuid() &&
		getgid() == getegid()) ? ROOT_UID : pw->pw_uid;

#ifdef HAVE_FCHOWN
	if (fchown(fileno(tmp), file_owner, (gid_t)-1) < OK) {
		perror("fchown");
		fclose(tmp);
		error = -2;
		goto done;
	}
#else
	if (chown(TempFilename, file_owner, (gid_t)-1) < OK) {
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

	backup_crontab(n);

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

/*
 * Check the syntax of a crontab file
 * Returns:
 *   0 no syntax issues
 *  -1 syntax issue (can be fixed by user)
 *  -2 any other error, which can not be fixed by user
 */
static int check_syntax(FILE * crontab_file) {
	char **envp = env_init();
	int eof = FALSE;
	int envs = 0, entries = 0;
	
	CheckErrorCount = 0;
	Set_LineNum(1 - NHEADER_LINES);

	if (envp == NULL) {
		fprintf(stderr, "%s: Cannot allocate memory.\n", ProgramName);
		return (-2);
	}

	while (!CheckErrorCount && !eof) {
		char envstr[MAX_ENVSTR];
		entry *e;

		if (!skip_comments(crontab_file)) {
			check_error
				("too much non-parseable content (comments, empty lines, spaces)");
			break;
		}

		switch (load_env(envstr, crontab_file)) {
		case ERR:
			/* check for data before the EOF */
			if (envstr[0] != '\0') {
				Set_LineNum(LineNumber + 1);
				check_error("premature EOF");
			}
			eof = TRUE;
			break;
		case FALSE:
			e = load_entry(crontab_file, check_error, pw, envp);
			if (e) {
				++entries;
				free_entry(e);
			}
			break;
		case TRUE:
			++envs;
			break;
		}
	}
	env_free(envp);

	if (envs > MAX_USER_ENVS) {
		fprintf(stderr,
			"There are too many environment variables in the crontab file. Limit: %d\n",
			MAX_USER_ENVS);
		return (-1);
	}

	if (entries > MAX_USER_ENTRIES) {
		fprintf(stderr,
			"There are too many entries in the crontab file. Limit: %d\n",
			MAX_USER_ENTRIES);
		return (-1);
	}

	if (CheckErrorCount != 0) {
		return (-1);
	}

	return 0;
}

static int hostset_cmd(void) {
	char n[MAX_FNAME];
	FILE *tmp;
	int fd;
	int error = 0;
	char *safename;

	if (!HostSpecified)
		gethostname(Host, sizeof Host);

	safename = host_specific_filename("#tmp", "XXXXXXXXXX");
	if (!safename || !glue_strings(TempFilename, sizeof TempFilename, SPOOL_DIR,
			safename, '/')) {
		TempFilename[0] = '\0';
		fprintf(stderr, "path too long\n");
		return (-2);
	}
	if ((fd = mkstemp(TempFilename)) == -1 || !(tmp = fdopen(fd, "w"))) {
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

	(void) fchmod(fd, 0600);	/* not all mkstemp() implementations do this */

	if (fprintf(tmp, "%s\n", Host) < 0 || fclose(tmp) == EOF) {
		fprintf(stderr, "%s: error while writing to %s\n",
			ProgramName, TempFilename);
		error = -2;
		goto done;
	}

	if (!glue_strings(n, sizeof n, SPOOL_DIR, CRON_HOSTNAME, '/')) {
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
	log_it(RealUser, Pid, "SET HOST", Host, 0);

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

static int hostget_cmd(void) {
	char n[MAX_FNAME];
	FILE *f;

	if (!glue_strings(n, sizeof n, SPOOL_DIR, CRON_HOSTNAME, '/')) {
		fprintf(stderr, "path too long\n");
		return (-2);
	}

	if (!(f = fopen(n, "r"))) {
		if (errno == ENOENT)
			fprintf(stderr, "File %s not found\n", n);
		else
			perror(n);
		return (-2);
	}

	if (get_string(Host, sizeof Host, f, "\n") == EOF) {
		fprintf(stderr, "Error reading from %s\n", n);
		fclose(f);
		return (-2);
	}

	fclose(f);

	printf("%s\n", Host);
	fflush(stdout);

	log_it(RealUser, Pid, "GET HOST", Host, 0);
	return (0);
}

static void poke_daemon(void) {
	if (utime(SPOOL_DIR, NULL) < OK) {
		fprintf(stderr, "crontab: can't update mtime on spooldir\n");
		perror(SPOOL_DIR);
		return;
	}
}

static void die(int x ATTRIBUTE_UNUSED) {
	if (TempFilename[0])
		(void) unlink(TempFilename);
	_exit(ERROR_EXIT);
}
