/*
    Anacron - run commands periodically
    Copyright (C) 1998  Itai Tzur <itzur@actcom.co.il>
    Copyright (C) 1999  Sean 'Shaleh' Perry <shaleh@debian.org>
    Copyright (C) 2004  Pascal Hakim <pasc@redellipse.net>
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 
    The GNU General Public License can also be found in the file
    `COPYING' that comes with the Anacron source distribution.
*/


#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include "global.h"
#include "gregor.h"

pid_t primary_pid;
int day_now;
int year, month, day_of_month;                 /* date anacron started */

char *program_name;
char *anacrontab;
char *spooldir;
int serialize, force, update_only, now,
    no_daemon, quiet, testing_only;            /* command-line options */
char **args;                       /* vector of "job" command-line arguments */
int nargs;                                     /* number of these */
char *defarg = "*";
int in_background;                             /* are we in the background? */
int old_umask;                                 /* umask when started */
sigset_t old_sigmask;                          /* signal mask when started */

job_rec *first_job_rec;
env_rec *first_env_rec;

static time_t start_sec;                       /* time anacron started */
static volatile int got_sigalrm, got_sigchld, got_sigusr1;
int running_jobs, running_mailers;              /* , number of */
int range_start = -1;
int range_stop = -1;

static void
print_version()
{
    printf("Anacron \n"
	   "Copyright (C) 1998  Itai Tzur <itzur@actcom.co.il>\n"
	   "Copyright (C) 1999  Sean 'Shaleh' Perry <shaleh@debian.org>\n"
	   "Copyright (C) 2004  Pascal Hakim <pasc@redellipse.net>\n"
	   "\n"
	   "Mail comments, suggestions and bug reports to <pasc@redellipse.net>."
	   "\n\n");
}

static void
print_usage()
{
    printf("Usage:  anacron [-s] [-f] [-n] [-d] [-q] [-t anacrontab] [-S spooldir] [job] ...\n"
	   "        anacron [-S spooldir] -u [job] ...\n"
	   "        anacron [-V|-h]\n"
	   "        anacron -T [-t anacrontab]\n"
	   "\n"
	   " -s  Serialize execution of jobs\n"
	   " -f  Force execution of jobs, even before their time\n"
	   " -n  Run jobs with no delay, implies -s\n"
	   " -d  Don't fork to the background\n"
	   " -q  Suppress stderr messages, only applicable with -d\n"
	   " -u  Update the timestamps without actually running anything\n"
	   " -t  Use this anacrontab\n"
	   " -V  Print version information\n"
	   " -h  Print this message\n"
	   " -T  Test an anacrontab\n"
	   " -S  Select a different spool directory\n"
	   "\n"
	   "See the manpage for more details.\n"
	   "\n");
}

static void
parse_opts(int argc, char *argv[])
/* Parse command-line options */
{
    int opt;

    quiet = no_daemon = serialize = force = update_only = now = 0;
    opterr = 0;
    while ((opt = getopt(argc, argv, "sfundqt:TS:Vh")) != EOF)
    {
	switch (opt)
	{
	case 's':
	    serialize = 1;
	    break;
	case 'f':
	    force = 1;
	    break;
	case 'u':
	    update_only = 1;
	    break;
	case 'n':
	    now = serialize = 1;
	    break;
	case 'd':
	    no_daemon = 1;
	    break;
	case 'q':
	    quiet = 1;
	    break;
	case 't':
	    anacrontab = strdup(optarg);
	    break;
	case 'T':
	    testing_only = 1;
	    break;
	case 'S':
	    spooldir = strdup(optarg);
	    break;
	case 'V':
	    print_version();
	    exit(0);
	case 'h':
	    print_usage();
	    exit(0);
	case '?':
	    fprintf(stderr, "%s: invalid option: %c\n",
		    program_name, optopt);
	    fprintf(stderr, "type: `%s -h' for more information\n",
		    program_name);
	    exit(FAILURE_EXIT);
	}
    }
    if (optind == argc)
    {
	/* no arguments. Equivalent to: `*' */
	nargs = 1;
	args = &defarg;
    }
    else
    {
	nargs = argc - optind;
	args = argv + optind;
    }
}

pid_t
xfork()
/* Like fork(), only never returns on failure */
{
    pid_t pid;

    pid = fork();
    if (pid == -1) die_e("Can't fork");
    return pid;
}

int
xopen(int fd, const char *file_name, int flags)
/* Like open, only it:
 * a) never returns on failure, and
 * b) if "fd" is non-negative, expect the file to open
 *    on file-descriptor "fd".
 */
{
    int rfd;

    rfd = open(file_name, flags);
    if (fd >= 0 && rfd != fd)
	die_e("Can't open %s on file-descriptor %d", file_name, fd);
    else if (rfd < 0)
	die_e("Can't open %s", file_name);
    return rfd;
}

void
xclose(int fd)
/* Like close(), only doesn't return on failure */
{
    if (close(fd)) die_e("Can't close file descriptor %d", fd);
}

static void
go_background()
/* Become a daemon. The foreground process exits successfully. */
{
    pid_t pid;

    /* stdin is already closed */

    if (fclose(stdout)) die_e("Can't close stdout");
    xopen(1, "/dev/null", O_WRONLY);

    if (fclose(stderr)) die_e("Can't close stderr");
    xopen(2, "/dev/null", O_WRONLY);

    pid = xfork();
    if (pid != 0)
    {
	/* parent */
	exit(0);
    }
    else
    {
	/* child */
	primary_pid = getpid();
	if (setsid() == -1) die_e("setsid() error");
	in_background = 1;
    }
}

void
handle_sigalrm()
{
    got_sigalrm = 1;
}

void
handle_sigchld()
{
    got_sigchld = 1;
}

void
handle_sigusr1()
{
    got_sigusr1 = 1;
}

static void
set_signal_handling()
/* We only use SIGALRM, SIGCHLD and SIGUSR1, and we unblock them only
 * in wait_signal().
 */
{
    sigset_t ss;
    struct sigaction sa;

    got_sigalrm = got_sigchld = got_sigusr1 = 0;

    /* block SIGALRM, SIGCHLD and SIGUSR1 */
    if (sigemptyset(&ss) ||
	sigaddset(&ss, SIGALRM) ||
	sigaddset(&ss, SIGCHLD) ||
	sigaddset(&ss, SIGUSR1)) die_e("sigset error");
    if (sigprocmask(SIG_BLOCK, &ss, NULL)) die_e ("sigprocmask error");

    /* setup SIGALRM handler */
    sa.sa_handler = handle_sigalrm;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL)) die_e("sigaction error");

    /* setup SIGCHLD handler */
    sa.sa_handler = handle_sigchld;
    sa.sa_mask = ss;
    sa.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL)) die_e("sigaction error");

    /* setup SIGUSR1 handler */
    sa.sa_handler = handle_sigusr1;
    sa.sa_mask = ss;
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL)) die_e("sigaction error");
}

static void
wait_signal()
/* Return after a signal is caught */
{
    sigset_t ss;

    if (sigprocmask(0, NULL, &ss)) die_e("sigprocmask error");
    if (sigdelset(&ss, SIGALRM) ||
	sigdelset(&ss, SIGCHLD) ||
	sigdelset(&ss, SIGUSR1)) die_e("sigset error");
    sigsuspend(&ss);
}

static void
wait_children()
/* Wait until we have no more children (of any kind) */
{
    while (running_jobs > 0 || running_mailers > 0)
    {
	wait_signal();
	if (got_sigchld) tend_children();
	got_sigchld = 0;
	if (got_sigusr1) explain("Received SIGUSR1");
	got_sigusr1 = 0;
    }
}

static void
orderly_termination()
/* Execution is diverted here, when we get SIGUSR1 */
{
    explain("Received SIGUSR1");
    got_sigusr1 = 0;
    wait_children();
    explain("Exited");
    exit(0);
}

static void
xsleep(unsigned int n)
/* Sleep for n seconds, servicing SIGCHLDs and SIGUSR1s in the meantime.
 * If n=0, return immediately.
 */
{
    if (n == 0) return;
    alarm(n);
    do
    {
	wait_signal();
	if (got_sigchld) tend_children();
	got_sigchld = 0;
	if (got_sigusr1) orderly_termination();
    }
    while (!got_sigalrm);
    got_sigalrm = 0;
}

static void
wait_jobs()
/* Wait until there are no running jobs,
 * servicing SIGCHLDs and SIGUSR1s in the meantime.
 */
{
    while (running_jobs > 0)
    {
	wait_signal();
	if (got_sigchld) tend_children();
	got_sigchld = 0;
	if (got_sigusr1) orderly_termination();
    }
}

static void
record_start_time()
{
    struct tm *tm_now;

    start_sec = time(NULL);
    tm_now = localtime(&start_sec);
    year = tm_now->tm_year + 1900;
    month = tm_now->tm_mon + 1;
    day_of_month = tm_now->tm_mday;
    day_now = day_num(year, month, day_of_month);
    if (day_now == -1) die("Invalid date (this is really embarrassing)");
    if (!update_only && !testing_only)
	explain("Anacron started on %04d-%02d-%02d",
		year, month, day_of_month);
}

static int
time_till(job_rec *jr)
/* Return the number of seconds that we have to wait until it's time
 * to start job jr.
 */
{
    unsigned int tj, tn;

    if (now) return 0;
    tn = time(NULL);
    tj = start_sec + jr->delay * 60;
    if (tj < tn) return 0;
    return tj - tn;
}

static void
fake_jobs()
{
    int j;

    j = 0;
    while (j < njobs)
    {
	fake_job(job_array[j]);
	explain("Updated timestamp for job `%s' to %04d-%02d-%02d",
		job_array[j]->ident, year, month, day_of_month);
	j++;
    }
}

static void
explain_intentions()
{
    int j;
    struct tm *t;

    j = 0;
    while (j < njobs)
    {
	if (now)
	{
	    explain("Will run job `%s'", job_array[j]->ident);
	}
	else
	{
	    time_t jobtime = start_sec + job_array[j]->delay * 60;

	    t = localtime(&jobtime);
	    if (range_start != -1 && range_stop != -1 && 
		(t->tm_hour < range_start || t->tm_hour >= range_stop))
	    {
		Debug(("The job `%s' falls out of the %02d:00-%02d:00 hours range, skipping.",
			job_array[j]->ident, range_start, range_stop));
		job_array[j]->drop_job = 1;
	    }
	    else
	    {
		explain("Will run job `%s' in %d min.",
		    job_array[j]->ident, job_array[j]->delay);
	    }
	}
	j++;
    }
    if (serialize && njobs > 0)
	explain("Jobs will be executed sequentially");
}

int
main(int argc, char *argv[])
{
    int j;

    int cwd;

    int dropped_jobs = 0;

    anacrontab = NULL;
    spooldir = NULL;
    struct timeval tv;
    struct timezone tz;

    if (gettimeofday(&tv, &tz) != 0)
        explain("Can't get exact time, failure.");

    srandom(getpid()+tv.tv_usec);

    if((program_name = strrchr(argv[0], '/')) == NULL)
	program_name = argv[0];
    else
	++program_name; /* move pointer to char after '/' */

    parse_opts(argc, argv);

    if (anacrontab == NULL)
	anacrontab = strdup(ANACRONTAB);

    if (spooldir == NULL)
	spooldir = strdup(ANACRON_SPOOL_DIR);

    if ((cwd = open ("./", O_RDONLY)) == -1) {
	die_e ("Can't save current directory");
    }

    in_background = 0;

    if (chdir(spooldir)) die_e("Can't chdir to %s", spooldir );

    old_umask = umask(0);

    if (sigprocmask(0, NULL, &old_sigmask)) die_e("sigset error");

    if (fclose(stdin)) die_e("Can't close stdin");
    xopen(0, "/dev/null", O_RDONLY);

    if (!no_daemon && !testing_only)
	go_background();
    else
	primary_pid = getpid();

    record_start_time();
    read_tab(cwd);
    arrange_jobs();

    if (testing_only)
    {
	if (complaints) exit (1);
	
	exit (0);
    }

    if (update_only)
    {
	fake_jobs();
	exit(0);
    }

    explain_intentions();
    set_signal_handling();
    running_jobs = running_mailers = 0;
    for(j = 0; j < njobs; ++j)
    {
	if (job_array[j]->drop_job == 1)
	{
	    ++dropped_jobs;
	    continue;
	}
	xsleep(time_till(job_array[j]));
	if (serialize) wait_jobs();
	launch_job(job_array[j]);
    }
    wait_children();
    explain("Normal exit (%d job%s run)", njobs-dropped_jobs, (njobs-dropped_jobs == 1 ? "" : "s"));
    exit(0);
}
