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

/* vix 26jan87 [RCS has the rest of the log]
 * vix 30dec86 [written]
 */

#include "config.h"

#include "globals.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(SYSLOG)
# include <syslog.h>
#endif

#ifdef WITH_AUDIT
# include <libaudit.h>
#endif

#ifdef HAVE_FCNTL_H	/* fcntl(2) */
# include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H	/* lockf(3) */
# include <unistd.h>
#endif
#ifdef HAVE_FLOCK	/* flock(2) */
# include <sys/file.h>
#endif

#include "funcs.h"
#include "macros.h"
#include "pathnames.h"

#if defined(SYSLOG) && defined(LOG_FILE)
# undef LOG_FILE
#endif

#if defined(LOG_DAEMON) && !defined(LOG_CRON)
# define LOG_CRON LOG_DAEMON
#endif

#ifndef FACILITY
# define FACILITY LOG_CRON
#endif

static int LogFD = ERR;

#if defined(SYSLOG)
static int syslog_open = FALSE;
#endif

#if defined(HAVE_FLOCK)
# define trylock_file(fd)      flock((fd), LOCK_EX|LOCK_NB)
#elif defined(HAVE_FCNTL) && defined(F_SETLK)
static int trylock_file(int fd) {
	struct flock fl;

	memset(&fl, '\0', sizeof (fl));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;

	return fcntl(fd, F_SETLK, &fl);
}
#elif defined(HAVE_LOCKF)
# define trylock_file(fd)      lockf((fd), F_TLOCK, 0)
#endif

/*
 * glue_strings is the overflow-safe equivalent of
 *		sprintf(buffer, "%s%c%s", a, separator, b);
 *
 * returns 1 on success, 0 on failure.  'buffer' MUST NOT be used if
 * glue_strings fails.
 */
int
glue_strings(char *buffer, size_t buffer_size, const char *a, const char *b,
	char separator) {
	char *buf;
	char *buf_end;

	if (buffer_size <= 0)
		return (0);
	buf_end = buffer + buffer_size;
	buf = buffer;

	for ( /* nothing */ ; buf < buf_end && *a != '\0'; buf++, a++)
		*buf = *a;
	if (buf == buf_end)
		return (0);
	if (separator != '/' || buf == buffer || buf[-1] != '/')
		*buf++ = separator;
	if (buf == buf_end)
		return (0);
	for ( /* nothing */ ; buf < buf_end && *b != '\0'; buf++, b++)
		*buf = *b;
	if (buf == buf_end)
		return (0);
	*buf = '\0';
	return (1);
}

int strcmp_until(const char *left, const char *right, char until) {
	while (*left && *left != until && *left == *right) {
		left++;
		right++;
	}

	if ((*left == '\0' || *left == until) && (*right == '\0' ||
			*right == until)) {
		return (0);
	}
	return (*left - *right);
}

/* strdtb(s) - delete trailing blanks in string 's' and return new length
 */
size_t strdtb(char *s) {
	char *x = s;

	/* scan forward to the null
	 */
	while (*x)
		x++;

	/* scan backward to either the first character before the string,
	 * or the last non-blank in the string, whichever comes first.
	 */
	do {
		x--;
	} while (x >= s && isspace((unsigned char) *x));

	/* one character beyond where we stopped above is where the null
	 * goes.
	 */
	*++x = '\0';

	/* the difference between the position of the null character and
	 * the position of the first character of the string is the length.
	 */
	return ((size_t)(x - s));
}

int set_debug_flags(const char *flags) {
	/* debug flags are of the form    flag[,flag ...]
	 *
	 * if an error occurs, print a message to stdout and return FALSE.
	 * otherwise return TRUE after setting ERROR_FLAGS.
	 */

#if !DEBUGGING

	printf("this program was compiled without debugging enabled\n");
	return (FALSE);

#else /* DEBUGGING */

	const char *pc = flags;

	DebugFlags = 0;

	while (*pc) {
		const char **test;
		int mask;

		/* try to find debug flag name in our list.
		 */
		for (test = DebugFlagNames, mask = 1;
			*test != NULL && strcmp_until(*test, pc, ','); test++, mask <<= 1) ;

		if (!*test) {
			fprintf(stderr, "unrecognized debug flag <%s> <%s>\n", flags, pc);
			return (FALSE);
		}

		DebugFlags |= mask;

		/* skip to the next flag
		 */
		while (*pc && *pc != ',')
			pc++;
		if (*pc == ',')
			pc++;
	}

	if (DebugFlags) {
		int flag;

		fprintf(stderr, "debug flags enabled:");

		for (flag = 0; DebugFlagNames[flag]; flag++)
			if (DebugFlags & (1 << flag))
				fprintf(stderr, " %s", DebugFlagNames[flag]);
		fprintf(stderr, "\n");
	}

	return (TRUE);

#endif /* DEBUGGING */
}

void set_cron_uid(void) {
#if defined(BSD) || defined(POSIX)
	if (seteuid(ROOT_UID) < OK) {
		perror("seteuid");
		exit(ERROR_EXIT);
	}
#else
	if (setuid(ROOT_UID) < OK) {
		perror("setuid");
		exit(ERROR_EXIT);
	}
#endif
}

void check_spool_dir(void) {
	struct stat sb;
#ifdef CRON_GROUP
	struct group *grp = NULL;

	grp = getgrnam(CRON_GROUP);
#endif
	/* check SPOOL_DIR existence
	 */
	if (stat(SPOOL_DIR, &sb) < OK && errno == ENOENT) {
		perror(SPOOL_DIR);
		if (OK == mkdir(SPOOL_DIR, 0700)) {
			fprintf(stderr, "%s: created\n", SPOOL_DIR);
			if (stat(SPOOL_DIR, &sb) < OK) {
				perror("stat retry");
				exit(ERROR_EXIT);
			}
		}
		else {
			fprintf(stderr, "%s: ", SPOOL_DIR);
			perror("mkdir");
			exit(ERROR_EXIT);
		}
	}
	if (!S_ISDIR(sb.st_mode)) {
		fprintf(stderr, "'%s' is not a directory, bailing out.\n", SPOOL_DIR);
		exit(ERROR_EXIT);
	}
#ifdef CRON_GROUP
	if (grp != NULL) {
		if (sb.st_gid != grp->gr_gid)
			if (chown(SPOOL_DIR, -1, grp->gr_gid) == -1) {
				fprintf(stderr, "chown %s failed: %s\n", SPOOL_DIR,
					strerror(errno));
				exit(ERROR_EXIT);
			}
		if (sb.st_mode != 01730)
			if (chmod(SPOOL_DIR, 01730) == -1) {
				fprintf(stderr, "chmod 01730 %s failed: %s\n", SPOOL_DIR,
					strerror(errno));
				exit(ERROR_EXIT);
			}
	}
#endif
}

/* acquire_daemonlock() - write our PID into /etc/cron.pid, unless
 *	another daemon is already running, which we detect here.
 *
 * note: main() calls us twice; once before forking, once after.
 *	we maintain static storage of the file pointer so that we
 *	can rewrite our PID into _PATH_CRON_PID after the fork.
 */
void acquire_daemonlock(int closeflag) {
	static int fd = -1;
	char buf[3 * MAX_FNAME];
	const char *pidfile;
	char *ep;
	long otherpid = -1;
	ssize_t num, len;
	pid_t pid = getpid();

	if (closeflag) {
		/* close stashed fd for child so we don't leak it. */
		if (fd != -1) {
			close(fd);
			fd = -1;
		}
		/* and restore default sig handlers so we don't remove pid file if killed */
		signal(SIGINT,SIG_DFL);
		signal(SIGTERM,SIG_DFL);
		return;
	}

	if (NoFork == 1)
		return; //move along, nothing to do here...

	if (fd == -1) {
		pidfile = _PATH_CRON_PID;
		/* Initial mode is 0600 to prevent flock() race/DoS. */
		if ((fd = open(pidfile, O_RDWR | O_CREAT, 0600)) == -1) {
			int save_errno = errno;
			sprintf(buf, "can't open or create %s", pidfile);
			fprintf(stderr, "%s: %s: %s\n", ProgramName, buf,
				strerror(save_errno));
			log_it("CRON", pid, "DEATH", buf, save_errno);
			exit(ERROR_EXIT);
		}

		if (trylock_file(fd) < OK) {
			int save_errno = errno;

			memset(buf, 0, sizeof (buf));
			if ((num = read(fd, buf, sizeof (buf) - 1)) > 0 &&
				(otherpid = strtol(buf, &ep, 10)) > 0 &&
				ep != buf && *ep == '\n' && otherpid != LONG_MAX) {
				snprintf(buf, sizeof (buf),
					"can't lock %s, otherpid may be %ld", pidfile, otherpid);
			}
			else {
				snprintf(buf, sizeof (buf),
					"can't lock %s, otherpid unknown", pidfile);
			}
			fprintf(stderr, "%s: %s: %s\n", ProgramName, buf,
				strerror(save_errno));
			log_it("CRON", pid, "DEATH", buf, save_errno);
			exit(ERROR_EXIT);
		}
		(void) fchmod(fd, 0644);
		(void) fcntl(fd, F_SETFD, 1);
	}
#if !defined(HAVE_FLOCK)
	else {
		/* Racy but better than nothing, just hope the parent exits */
		sleep(0);
		trylock_file(fd);	
	}
#endif

	sprintf(buf, "%ld\n", (long) pid);
	(void) lseek(fd, (off_t) 0, SEEK_SET);
	len = (ssize_t)strlen(buf);
	if ((num = write(fd, buf, (size_t)len)) != len)
		log_it("CRON", pid, "ERROR", "write() failed", errno);
	else {
		if (ftruncate(fd, num) == -1)
			log_it("CRON", pid, "ERROR", "ftruncate() failed", errno);
	}

	/* abandon fd even though the file is open. we need to keep
	 * it open and locked, but we don't need the handles elsewhere.
	 */
}

/* get_char(file) : like getc() but increment LineNumber on newlines
 */
int get_char(FILE * file) {
	int ch;

	ch = getc(file);
	if (ch == '\n')
		Set_LineNum(LineNumber + 1)
	return (ch);
}

/* unget_char(ch, file) : like ungetc but do LineNumber processing
 */
void unget_char(int ch, FILE * file) {
	ungetc(ch, file);
	if (ch == '\n')
		Set_LineNum(LineNumber - 1)
}

/* get_string(str, max, file, termstr) : like fgets() but
 *      (1) has terminator string which should include \n
 *      (2) will always leave room for the null
 *      (3) uses get_char() so LineNumber will be accurate
 *      (4) returns EOF or terminating character, whichever
 */
int get_string(char *string, int size, FILE * file, const char *terms) {
	int ch;

	while (EOF != (ch = get_char(file)) && !strchr(terms, ch)) {
		if (size > 1) {
			*string++ = (char) ch;
			size--;
		}
	}

	if (size > 0)
		*string = '\0';

	return (ch);
}

/* skip_comments(file) : read past comment (if any)
 */
void skip_comments(FILE * file) {
	int ch;

	while (EOF != (ch = get_char(file))) {
		/* ch is now the first character of a line.
		 */
		while (ch == ' ' || ch == '\t')
			ch = get_char(file);

		if (ch == EOF)
			break;

		/* ch is now the first non-blank character of a line.
		 */

		if (ch != '\n' && ch != '#')
			break;

		/* ch must be a newline or comment as first non-blank
		 * character on a line.
		 */

		while (ch != '\n' && ch != EOF)
			ch = get_char(file);

		/* ch is now the newline of a line which we're going to
		 * ignore.
		 */
	}
	if (ch != EOF)
		unget_char(ch, file);
}

/* int in_file(const char *string, FILE *file, int error)
 *	return TRUE if one of the lines in file matches string exactly,
 *	FALSE if no lines match, and error on error.
 */
static int in_file(const char *string, FILE * file, int error) {
	char line[MAX_TEMPSTR];
	char *endp;

	if (fseek(file, 0L, SEEK_SET))
		return (error);
	while (fgets(line, MAX_TEMPSTR, file)) {
		if (line[0] != '\0') {
			endp = &line[strlen(line) - 1];
			if (*endp != '\n')
				return (error);
			*endp = '\0';
			if (0 == strcmp(line, string))
				return (TRUE);
		}
	}
	if (ferror(file))
		return (error);
	return (FALSE);
}

/* int allowed(const char *username, const char *allow_file, const char *deny_file)
 *	returns TRUE if (allow_file exists and user is listed)
 *	or (deny_file exists and user is NOT listed).
 *	root is always allowed.
 */
int allowed(const char *username, const char *allow_file,
	const char *deny_file) {
	FILE *fp;
	int isallowed;
	char buf[128];

	if (getuid() == 0)
		return TRUE;
	isallowed = FALSE;
	if ((fp = fopen(allow_file, "r")) != NULL) {
		isallowed = in_file(username, fp, FALSE);
		fclose(fp);
		if ((getuid() == 0) && (!isallowed)) {
			snprintf(buf, sizeof (buf),
				"root used -u for user %s not in cron.allow", username);
			log_it("crontab", getpid(), "warning", buf, 0);
			isallowed = TRUE;
		}
	}
	else if ((fp = fopen(deny_file, "r")) != NULL) {
		isallowed = !in_file(username, fp, FALSE);
		fclose(fp);
		if ((getuid() == 0) && (!isallowed)) {
			snprintf(buf, sizeof (buf),
				"root used -u for user %s in cron.deny", username);
			log_it("crontab", getpid(), "warning", buf, 0);
			isallowed = TRUE;
		}
	}
#ifdef WITH_AUDIT
	if (isallowed == FALSE) {
		int audit_fd = audit_open();
		audit_log_user_message(audit_fd, AUDIT_USER_START, "cron deny",
			NULL, NULL, NULL, 0);
		close(audit_fd);
	}
#endif
	return (isallowed);
}

void log_it(const char *username, PID_T xpid, const char *event,
	const char *detail, int err) {
#if defined(LOG_FILE) || DEBUGGING
	PID_T pid = xpid;
#endif
#if defined(LOG_FILE)
	char *msg;
	TIME_T now = time((TIME_T) 0);
	struct tm *t = localtime(&now);
	int msg_size;
#endif

#if defined(LOG_FILE)
	/* we assume that MAX_TEMPSTR will hold the date, time, &punctuation.
	 */
	msg = malloc(msg_size = (strlen(username)
			+ strlen(event)
			+ strlen(detail)
			+ MAX_TEMPSTR)
		);
	if (msg == NULL) {	/* damn, out of mem and we did not test that before... */
		fprintf(stderr, "%s: Run OUT OF MEMORY while %s\n",
			ProgramName, __FUNCTION__);
		return;
	}
	if (LogFD < OK) {
		LogFD = open(LOG_FILE, O_WRONLY | O_APPEND | O_CREAT, 0600);
		if (LogFD < OK) {
			fprintf(stderr, "%s: can't open log file\n", ProgramName);
			perror(LOG_FILE);
		}
		else {
			(void) fcntl(LogFD, F_SETFD, 1);
		}
	}

	/* we have to snprintf() it because fprintf() doesn't always write
	 * everything out in one chunk and this has to be atomically appended
	 * to the log file.
	 */
	snprintf(msg, msg_size,
		"%s (%02d/%02d-%02d:%02d:%02d-%d) %s (%s)%s%s\n", username,
		t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, pid,
		event, detail, err != 0 ? ": " : "", err != 0 ? strerror(err) : "");

	/* we have to run strlen() because sprintf() returns (char*) on old BSD
	 */
	if (LogFD < OK || write(LogFD, msg, strlen(msg)) < OK) {
		if (LogFD >= OK)
			perror(LOG_FILE);
		fprintf(stderr, "%s: can't write to log file\n", ProgramName);
		write(STDERR, msg, strlen(msg));
	}

	free(msg);
#endif /*LOG_FILE */

#if defined(SYSLOG)
	if (!syslog_open) {
# ifdef LOG_DAEMON
		openlog(ProgramName, LOG_PID, FACILITY);
# else
		openlog(ProgramName, LOG_PID);
# endif
		syslog_open = TRUE;	/* assume openlog success */
	}

	syslog(err != 0 ? LOG_ERR : LOG_INFO,
		"(%s) %s (%s)%s%s", username, event, detail,
		err != 0 ? ": " : "", err != 0 ? strerror(err) : "");


#endif	 /*SYSLOG*/
#if DEBUGGING
	if (DebugFlags) {
		fprintf(stderr, "log_it: (%s %ld) %s (%s)%s%s\n",
			username, (long) pid, event, detail,
			err != 0 ? ": " : "", err != 0 ? strerror(err) : "");
	}
#endif
}

void log_close(void) {
	if (LogFD != ERR) {
		close(LogFD);
		LogFD = ERR;
	}
#if defined(SYSLOG)
	closelog();
	syslog_open = FALSE;
#endif	 /*SYSLOG*/
}

/* char *first_word(const char *s, const char *t)
 *	return pointer to first word
 * parameters:
 *	s - string we want the first word of
 *	t - terminators, implicitly including \0
 * warnings:
 *	(1) this routine is fairly slow
 *	(2) it returns a pointer to static storage
 */
char *first_word(const char *s, const char *t) {
	static char retbuf[2][MAX_TEMPSTR + 1];	/* sure wish C had GC */
	static int retsel = 0;
	char *rb, *rp;

	/* select a return buffer */
	retsel = 1 - retsel;
	rb = &retbuf[retsel][0];
	rp = rb;

	/* skip any leading terminators */
	while (*s && (NULL != strchr(t, *s))) {
		s++;
	}

	/* copy until next terminator or full buffer */
	while (*s && (NULL == strchr(t, *s)) && (rp < &rb[MAX_TEMPSTR])) {
		*rp++ = *s++;
	}

	/* finish the return-string and return it */
	*rp = '\0';
	return (rb);
}

/* warning:
 *	heavily ascii-dependent.
 */
static void mkprint(char *dst, unsigned char *src, size_t len) {
/*
 * XXX
 * We know this routine can't overflow the dst buffer because mkprints()
 * allocated enough space for the worst case.
*/
	while (len-- > 0) {
		unsigned char ch = *src++;

		if (ch < ' ') {	/* control character */
			*dst++ = '^';
			*dst++ = (char)(ch + '@');
		}
		else if (ch < 0177) {	/* printable */
			*dst++ = (char)ch;
		}
		else if (ch == 0177) {	/* delete/rubout */
			*dst++ = '^';
			*dst++ = '?';
		}
		else {	/* parity character */
			sprintf(dst, "\\%03o", ch);
			dst += 4;
		}
	}
	*dst = '\0';
}

/* warning:
 *	returns a pointer to malloc'd storage, you must call free yourself.
 */
char *mkprints(unsigned char *src, size_t len) {
	char *dst = malloc(len * 4 + 1);

	if (dst)
		mkprint(dst, src, len);

	return (dst);
}

#ifdef MAIL_DATE
/* Sat, 27 Feb 1993 11:44:51 -0800 (CST)
 * 1234567890123456789012345678901234567
 */
char *arpadate(time_t *clock) {
	time_t t = clock ? *clock : time((TIME_T) 0);
	struct tm tm = *localtime(&t);
	long gmtoff = get_gmtoff(&t, &tm);
	int hours = gmtoff / SECONDS_PER_HOUR;
	int minutes =
			(gmtoff - (hours * SECONDS_PER_HOUR)) / SECONDS_PER_MINUTE;
	static char ret[64];	/* zone name might be >3 chars */

	(void) sprintf(ret, "%s, %2d %s %2d %02d:%02d:%02d %.2d%.2d (%s)",
		DowNames[tm.tm_wday],
		tm.tm_mday,
		MonthNames[tm.tm_mon],
		tm.tm_year + 1900,
		tm.tm_hour, tm.tm_min, tm.tm_sec, hours, minutes, TZONE(tm));
	return (ret);
}
#endif /*MAIL_DATE */

#ifdef HAVE_SAVED_UIDS
static uid_t save_euid;
static gid_t save_egid;

int swap_uids(void) {
	save_egid = getegid();
	save_euid = geteuid();
	return ((setegid(getgid()) || seteuid(getuid()))? -1 : 0);
}

int swap_uids_back(void) {
	return ((setegid(save_egid) || seteuid(save_euid)) ? -1 : 0);
}

#else /*HAVE_SAVED_UIDS */

int swap_uids(void) {
	return ((setregid(getegid(), getgid())
			|| setreuid(geteuid(), getuid())) ? -1 : 0);
}

int swap_uids_back(void) {
	return (swap_uids());
}
#endif /*HAVE_SAVED_UIDS */

size_t strlens(const char *last, ...) {
	va_list ap;
	size_t ret = 0;
	const char *str;

	va_start(ap, last);
	for (str = last; str != NULL; str = va_arg(ap, const char *))
		     ret += strlen(str);
	va_end(ap);
	return (ret);
}

/* Return the offset from GMT in seconds (algorithm taken from sendmail).
 *
 * warning:
 *	clobbers the static storage space used by localtime() and gmtime().
 *	If the local pointer is non-NULL it *must* point to a local copy.
 */
#ifndef HAVE_STRUCT_TM_TM_GMTOFF
long get_gmtoff(time_t * clock, struct tm *local) {
	struct tm gmt;
	long offset;

	gmt = *gmtime(clock);
	if (local == NULL)
		local = localtime(clock);

	offset = (local->tm_sec - gmt.tm_sec) +
		((local->tm_min - gmt.tm_min) * 60) +
		((local->tm_hour - gmt.tm_hour) * 3600);

	/* Timezone may cause year rollover to happen on a different day. */
	if (local->tm_year < gmt.tm_year)
		offset -= 24 * 3600;
	else if (local->tm_year > gmt.tm_year)
		offset += 24 * 3600;
	else if (local->tm_yday < gmt.tm_yday)
		offset -= 24 * 3600;
	else if (local->tm_yday > gmt.tm_yday)
		offset += 24 * 3600;

	return (offset);
}
#endif /* HAVE_STRUCT_TM_TM_GMTOFF */
