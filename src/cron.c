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

#define	MAIN_PROGRAM

#include <cron.h>

#if defined WITH_INOTIFY
int inotify_enabled;
#else
# define inotify_enabled 0
#endif

enum timejump { negative, small, medium, large };

static void usage(void),
run_reboot_jobs(cron_db *),
find_jobs(int, cron_db *, int, int),
set_time(int),
cron_sleep(int, cron_db *),
sigchld_handler(int),
sighup_handler(int),
sigchld_reaper(void), quit(int), parse_args(int c, char *v[]);

static volatile sig_atomic_t got_sighup, got_sigchld;
static int timeRunning, virtualTime, clockTime;
static long GMToff;
static int DisableInotify;

#if defined WITH_INOTIFY

# define NUM_WATCHES 3

int wd[NUM_WATCHES];
const char *watchpaths[NUM_WATCHES] = {SPOOL_DIR, SYS_CROND_DIR, SYSCRONTAB};

void set_cron_unwatched(int fd) {
	int i;

	for (i = 0; i < sizeof (wd) / sizeof (wd[0]); ++i) {
		if (wd[i] < 0) {
			inotify_rm_watch(fd, wd[i]);
			wd[i] = -1;
		}
	}
}

void set_cron_watched(int fd) {
	pid_t pid = getpid();
	int i;

	if (fd < 0) {
		inotify_enabled = 0;
		return;
	}

	for (i = 0; i < sizeof (wd) / sizeof (wd[0]); ++i) {
		int w;

		w = inotify_add_watch(fd, watchpaths[i],
			IN_CREATE | IN_CLOSE_WRITE | IN_ATTRIB | IN_MODIFY | IN_MOVED_TO |
			IN_MOVED_FROM | IN_MOVE_SELF | IN_DELETE | IN_DELETE_SELF);
		if (w < 0) {
			if (wd[i] != -1) {
				log_it("CRON", pid, "This directory or file can't be watched",
					watchpaths[i], errno);
				log_it("CRON", pid, "INFO", "running without inotify support",
					0);
			}
			inotify_enabled = 0;
			set_cron_unwatched(fd);
			return;
		}
		wd[i] = w;
	}

	if (!inotify_enabled) {
		log_it("CRON", pid, "INFO", "running with inotify support", 0);
	}

	inotify_enabled = 1;
}
#endif

static void handle_signals(cron_db * database) {
	if (got_sighup) {
		got_sighup = 0;
#if defined WITH_INOTIFY
		/* watches must be reinstated on reload */
		if (inotify_enabled) {
			set_cron_unwatched(database->ifd);
			inotify_enabled = 0;
		}
#endif
		database->mtime = (time_t) 0;
		log_close();
	}

	if (got_sigchld) {
		got_sigchld = 0;
		sigchld_reaper();
	}
}

static void usage(void) {
	const char **dflags;

	fprintf(stderr, "usage:  %s [-n] [-p] [-s] [-i] [-m <mail command>] [-x [",
		ProgramName);
	for (dflags = DebugFlagNames; *dflags; dflags++)
		fprintf(stderr, "%s%s", *dflags, dflags[1] ? "," : "]");
	fprintf(stderr, "]\n");
	exit(ERROR_EXIT);
}

int main(int argc, char *argv[]) {
	struct sigaction sact;
	cron_db database;
	int fd;
	char *cs;
	pid_t pid = getpid();
#if defined WITH_INOTIFY
	int i;
#endif

	ProgramName = argv[0];
	MailCmd[0] = '\0';
	cron_default_mail_charset[0] = '\0';

	setlocale(LC_ALL, "");

#if defined(BSD)
	setlinebuf(stdout);
	setlinebuf(stderr);
#endif

	SyslogOutput = 0;
	NoFork = 0;
	parse_args(argc, argv);

	bzero((char *) &sact, sizeof sact);
	sigemptyset(&sact.sa_mask);
	sact.sa_flags = 0;
#ifdef SA_RESTART
	sact.sa_flags |= SA_RESTART;
#endif
	sact.sa_handler = sigchld_handler;
	(void) sigaction(SIGCHLD, &sact, NULL);
	sact.sa_handler = sighup_handler;
	(void) sigaction(SIGHUP, &sact, NULL);
	sact.sa_handler = quit;
	(void) sigaction(SIGINT, &sact, NULL);
	(void) sigaction(SIGTERM, &sact, NULL);

	acquire_daemonlock(0);
	set_cron_uid();
	set_cron_cwd();

	if (putenv("PATH=" _PATH_DEFPATH) < 0) {
		log_it("CRON", pid, "DEATH", "can't putenv PATH", errno);
		exit(1);
	}

	/* Get the default locale character set for the mail 
	 * "Content-Type: ...; charset=" header
	 */
	setlocale(LC_ALL, "");	/* set locale to system defaults or to
							 * that specified by any  LC_* env vars */
	if ((cs = nl_langinfo(CODESET)) != 0L)
		strncpy(cron_default_mail_charset, cs, MAX_ENVSTR);
	else
		strcpy(cron_default_mail_charset, "US-ASCII");

	/* if there are no debug flags turned on, fork as a daemon should.
	 */
	if (DebugFlags) {
#if DEBUGGING
		(void) fprintf(stderr, "[%ld] cron started\n", (long) getpid());
#endif
	}
	else if (NoFork == 0) {
		switch (fork()) {
		case -1:
			log_it("CRON", pid, "DEATH", "can't fork", errno);
			exit(0);
			break;
		case 0:
			/* child process */
			(void) setsid();
			if ((fd = open(_PATH_DEVNULL, O_RDWR, 0)) >= 0) {
				(void) dup2(fd, STDIN);
				(void) dup2(fd, STDOUT);
				(void) dup2(fd, STDERR);
				if (fd != STDERR)
					(void) close(fd);
			}
			log_it("CRON", getpid(), "STARTUP", PACKAGE_VERSION, 0);
			break;
		default:
			/* parent process should just die */
			_exit(0);
		}
	}

	pid = getpid();
	acquire_daemonlock(0);
	database.head = NULL;
	database.tail = NULL;
	database.mtime = (time_t) 0;

	load_database(&database);

	fd = -1;
#if defined WITH_INOTIFY
	if (DisableInotify) {
		log_it("CRON", getpid(), "No inotify - daemon runs with -i option", 
			"", 0);
	}
	else {
		for (i = 0; i < sizeof (wd) / sizeof (wd[0]); ++i) {
			/* initialize to negative number other than -1
			 * so an eventual error is reported for the first time
			 */
			wd[i] = -2;
		}

		database.ifd = fd = inotify_init();
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		if (fd < 0)
			log_it("CRON", pid, "INFO", "Inotify init failed", errno);
		set_cron_watched(fd);
	}
#endif

	set_time(TRUE);
	run_reboot_jobs(&database);
	timeRunning = virtualTime = clockTime;

	/*
	 * Too many clocks, not enough time (Al. Einstein)
	 * These clocks are in minutes since the epoch, adjusted for timezone.
	 * virtualTime: is the time it *would* be if we woke up
	 * promptly and nobody ever changed the clock. It is
	 * monotonically increasing... unless a timejump happens.
	 * At the top of the loop, all jobs for 'virtualTime' have run.
	 * timeRunning: is the time we last awakened.
	 * clockTime: is the time when set_time was last called.
	 */
	while (TRUE) {
		int timeDiff;
		enum timejump wakeupKind;

		/* ... wait for the time (in minutes) to change ... */
		do {
			cron_sleep(timeRunning + 1, &database);
			set_time(FALSE);
		} while (clockTime == timeRunning);
		timeRunning = clockTime;

		/*
		 * Calculate how the current time differs from our virtual
		 * clock.  Classify the change into one of 4 cases.
		 */
		timeDiff = timeRunning - virtualTime;
#if defined WITH_INOTIFY
		if (inotify_enabled) {
			check_inotify_database(&database);
		}
		else {
			if (load_database(&database))
				/* try reinstating the watches */
				set_cron_watched(fd);
		}
#else
		load_database(&database);
#endif

		/* shortcut for the most common case */
		if (timeDiff == 1) {
			virtualTime = timeRunning;
			find_jobs(virtualTime, &database, TRUE, TRUE);
		}
		else {
			if (timeDiff > (3 * MINUTE_COUNT) || timeDiff < -(3 * MINUTE_COUNT))
				wakeupKind = large;
			else if (timeDiff > 5)
				wakeupKind = medium;
			else if (timeDiff > 0)
				wakeupKind = small;
			else
				wakeupKind = negative;

			switch (wakeupKind) {
			case small:
				/*
				 * case 1: timeDiff is a small positive number
				 * (wokeup late) run jobs for each virtual
				 * minute until caught up.
				 */
				Debug(DSCH, ("[%ld], normal case %d minutes to go\n",
						(long) pid, timeDiff))
					do {
					if (job_runqueue())
						sleep(10);
					virtualTime++;
					find_jobs(virtualTime, &database, TRUE, TRUE);
				} while (virtualTime < timeRunning);
				break;

			case medium:
				/*
				 * case 2: timeDiff is a medium-sized positive
				 * number, for example because we went to DST
				 * run wildcard jobs once, then run any
				 * fixed-time jobs that would otherwise be
				 * skipped if we use up our minute (possible,
				 * if there are a lot of jobs to run) go
				 * around the loop again so that wildcard jobs
				 * have a chance to run, and we do our
				 * housekeeping.
				 */
				Debug(DSCH, ("[%ld], DST begins %d minutes to go\n",
						(long) pid, timeDiff))
					/* run wildcard jobs for current minute */
					find_jobs(timeRunning, &database, TRUE, FALSE);

				/* run fixed-time jobs for each minute missed */
				do {
					if (job_runqueue())
						sleep(10);
					virtualTime++;
					find_jobs(virtualTime, &database, FALSE, TRUE);
					set_time(FALSE);
				} while (virtualTime < timeRunning && clockTime == timeRunning);
				break;

			case negative:
				/*
				 * case 3: timeDiff is a small or medium-sized
				 * negative num, eg. because of DST ending.
				 * Just run the wildcard jobs. The fixed-time
				 * jobs probably have already run, and should
				 * not be repeated.  Virtual time does not
				 * change until we are caught up.
				 */
				Debug(DSCH, ("[%ld], DST ends %d minutes to go\n",
						(long) pid, timeDiff))
					find_jobs(timeRunning, &database, TRUE, FALSE);
				break;
			default:
				/*
				 * other: time has changed a *lot*,
				 * jump virtual time, and run everything
				 */
				Debug(DSCH, ("[%ld], clock jumped\n", (long) pid))
					virtualTime = timeRunning;
				find_jobs(timeRunning, &database, TRUE, TRUE);
			}
		}

		/* Jobs to be run (if any) are loaded; clear the queue. */
		job_runqueue();

		handle_signals(&database);
	}

#if defined WITH_INOTIFY
	if (inotify_enabled)
		set_cron_unwatched(fd);

	if (fd >= 0 && close(fd) < 0)
		log_it("CRON", pid, "INFO", "Inotify close failed", errno);
#endif
}

static void run_reboot_jobs(cron_db * db) {
	user *u;
	entry *e;
	int reboot;
	pid_t pid = getpid();

	/* lock exist - skip reboot jobs */
	if (access(REBOOT_LOCK, F_OK) == 0) {
		log_it("CRON", pid, "INFO",
			"@reboot jobs will be run at computer's startup.", 0);
		return;
	}
	/* lock doesn't exist - create lock, run reboot jobs */
	if ((reboot = creat(REBOOT_LOCK, S_IRUSR & S_IWUSR)) < 0)
		log_it("CRON", pid, "INFO", "Can't create lock for reboot jobs.",
			errno);
	else
		close(reboot);

	for (u = db->head; u != NULL; u = u->next) {
		for (e = u->crontab; e != NULL; e = e->next) {
			if (e->flags & WHEN_REBOOT)
				job_add(e, u);
		}
	}
	(void) job_runqueue();
}

static void find_jobs(int vtime, cron_db * db, int doWild, int doNonWild) {
	char *orig_tz, *job_tz;
	time_t virtualSecond = vtime * SECONDS_PER_MINUTE;
	struct tm *tm = gmtime(&virtualSecond);
	int minute, hour, dom, month, dow;
	user *u;
	entry *e;
	const char *uname;
	struct passwd *pw = NULL;

	/* make 0-based values out of these so we can use them as indicies
	 */
#define maketime(tz1, tz2) do { \
	char *t = tz1; \
	if (t != NULL && *t != '\0') \
		setenv("TZ", t, 1); \
	else if ((tz2) != NULL) \
		setenv("TZ", (tz2), 1); \
	else \
		unsetenv("TZ"); \
	tm = localtime(&StartTime); \
	minute = tm->tm_min -FIRST_MINUTE; \
	hour = tm->tm_hour -FIRST_HOUR; \
	dom = tm->tm_mday -FIRST_DOM; \
	month = tm->tm_mon +1 /* 0..11 -> 1..12 */ -FIRST_MONTH; \
	dow = tm->tm_wday -FIRST_DOW; \
	} while (0)

	orig_tz = getenv("TZ");
	maketime(NULL, orig_tz);

	Debug(DSCH, ("[%ld] tick(%d,%d,%d,%d,%d) %s %s\n",
			(long) getpid(), minute, hour, dom, month, dow,
			doWild ? " " : "No wildcard", doNonWild ? " " : "Wildcard only"))
		/* the dom/dow situation is odd.  '* * 1,15 * Sun' will run on the
		 * first and fifteenth AND every Sunday;  '* * * * Sun' will run *only*
		 * on Sundays;  '* * 1,15 * *' will run *only* the 1st and 15th.  this
		 * is why we keep 'e->dow_star' and 'e->dom_star'.  yes, it's bizarre.
		 * like many bizarre things, it's the standard.
		 */
		for (u = db->head; u != NULL; u = u->next) {
		for (e = u->crontab; e != NULL; e = e->next) {
			Debug(DSCH | DEXT, ("user [%s:%ld:%ld:...] cmd=\"%s\"\n",
					e->pwd->pw_name, (long) e->pwd->pw_uid,
					(long) e->pwd->pw_gid, e->cmd))
				uname = e->pwd->pw_name;
			/* check if user exists in time of job is being run f.e. ldap */
			if ((pw = getpwnam(uname)) != NULL) {
				job_tz = env_get("CRON_TZ", e->envp);
				maketime(job_tz, orig_tz);
				/* here we test whether time is NOW */
				if (bit_test(e->minute, minute) &&
					bit_test(e->hour, hour) &&
					bit_test(e->month, month) &&
					(((e->flags & DOM_STAR) || (e->flags & DOW_STAR))
						? (bit_test(e->dow, dow) && bit_test(e->dom, dom))
						: (bit_test(e->dow, dow) || bit_test(e->dom, dom))
					)
					) {
					if ((doNonWild &&
							!(e->flags & (MIN_STAR | HR_STAR))) ||
						(doWild && (e->flags & (MIN_STAR | HR_STAR))))
						job_add(e, u);	/*will add job, if it isn't in queue already for NOW. */
				}
			}
		}
	}
	if (orig_tz != NULL)
		setenv("TZ", orig_tz, 1);
	else
		unsetenv("TZ");
}

/*
 * Set StartTime and clockTime to the current time.
 * These are used for computing what time it really is right now.
 * Note that clockTime is a unix wallclock time converted to minutes.
 */
static void set_time(int initialize) {
	struct tm tm;
	static int isdst;

	StartTime = time(NULL);

	/* We adjust the time to GMT so we can catch DST changes. */
	tm = *localtime(&StartTime);
	if (initialize || tm.tm_isdst != isdst) {
		isdst = tm.tm_isdst;
		GMToff = get_gmtoff(&StartTime, &tm);
		Debug(DSCH, ("[%ld] GMToff=%ld\n", (long) getpid(), (long) GMToff))
	}
	clockTime = (StartTime + GMToff) / (time_t) SECONDS_PER_MINUTE;
}

/*
 * Try to just hit the next minute.
 */
static void cron_sleep(int target, cron_db * db) {
	time_t t1, t2;
	int seconds_to_wait;

	t1 = time(NULL) + GMToff;
	seconds_to_wait = (int) (target * SECONDS_PER_MINUTE - t1) + 1;
	Debug(DSCH, ("[%ld] Target time=%ld, sec-to-wait=%d\n",
			(long) getpid(), (long) target * SECONDS_PER_MINUTE,
			seconds_to_wait))

		while (seconds_to_wait > 0 && seconds_to_wait < 65) {
		sleep((unsigned int) seconds_to_wait);

		/*
		 * Check to see if we were interrupted by a signal.
		 * If so, service the signal(s) then continue sleeping
		 * where we left off.
		 */
		handle_signals(db);

		t2 = time(NULL) + GMToff;
		seconds_to_wait -= (int) (t2 - t1);
		t1 = t2;
	}
}

static void sighup_handler(int x) {
	got_sighup = 1;
}

static void sigchld_handler(int x) {
	got_sigchld = 1;
}

static void quit(int x) {
	(void) unlink(_PATH_CRON_PID);
	_exit(0);
}

static void sigchld_reaper(void) {
	WAIT_T waiter;
	PID_T pid;

	do {
		pid = waitpid(-1, &waiter, WNOHANG);
		switch (pid) {
		case -1:
			if (errno == EINTR)
				continue;
			Debug(DPROC, ("[%ld] sigchld...no children\n", (long) getpid()))
				break;
		case 0:
			Debug(DPROC, ("[%ld] sigchld...no dead kids\n", (long) getpid()))
				break;
		default:
			Debug(DPROC,
				("[%ld] sigchld...pid #%ld died, stat=%d\n",
					(long) getpid(), (long) pid, WEXITSTATUS(waiter)))
				break;
		}
	} while (pid > 0);
}

static void parse_args(int argc, char *argv[]) {
	int argch;

	while (-1 != (argch = getopt(argc, argv, "npsix:m:"))) {
		switch (argch) {
		default:
			usage();
		case 'x':
			if (!set_debug_flags(optarg))
				usage();
			break;
		case 'n':
			NoFork = 1;
			break;
		case 'p':
			PermitAnyCrontab = 1;
			break;
		case 's':
			SyslogOutput = 1;
			break;
		case 'i':
			DisableInotify = 1;
			break;
		case 'm':
			strncpy(MailCmd, optarg, MAX_COMMAND);
			break;
		}
	}
}
