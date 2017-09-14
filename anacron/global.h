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

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

    The GNU General Public License can also be found in the file
    `COPYING' that comes with the Anacron source distribution.
*/

#ifndef _ANACRON_GLOBAL_H
#define _ANACRON_GLOBAL_H

/* Syslog facility and priorities messages will be logged to (see syslog(3)).
 * If you change these, please update the man page. */
#define SYSLOG_FACILITY LOG_CRON
#define EXPLAIN_LEVEL LOG_NOTICE  /* informational messages */
#define COMPLAIN_LEVEL LOG_ERR    /* error messages */
#define DEBUG_LEVEL LOG_DEBUG     /* only used when DEBUG is defined */

/* Mail interface.  (All MTAs should supply this command) */
#define SENDMAIL "/usr/sbin/sendmail"

/* End of user-configurable section */


#define FAILURE_EXIT 1
#define MAX_MSG 150

#include <signal.h>
#include <time.h>
#include "anacron-paths.h"
#include "cronie_common.h"

/* Some declarations */

struct env_rec1 {
   char *assign;

   struct env_rec1 *next;
};
typedef struct env_rec1 env_rec;

struct job_rec1 {
   int period;
   int named_period;
   int delay;
   char *ident;
   char *command;
   char *mailto;

   int tab_line;
   int arg_num;
   int timestamp_fd;
   int input_fd;
   int output_fd;
   off_t mail_header_size;
   pid_t job_pid;
   pid_t mailer_pid;
   int drop_job;

   struct job_rec1 *next;
   env_rec *prev_env_rec;
};
typedef struct job_rec1 job_rec;

/* Global variables */

extern pid_t primary_pid;
extern char *program_name;
extern char *anacrontab;
extern char *spooldir;
extern int old_umask;
extern sigset_t old_sigmask;
extern int serialize,force,update_only,now,no_daemon,quiet,testing_only;
extern int day_now;
extern int year,month,day_of_month;
extern int in_background;

extern job_rec *first_job_rec;
extern env_rec *first_env_rec;

extern char **job_args;
extern int job_nargs;

extern int njobs;
extern job_rec **job_array;

extern int running_jobs,running_mailers;

extern int complaints;

extern time_t start_sec;

/* time ranges for START_HOURS_RANGE */
extern int range_start;
extern int range_stop;

/* preferred hour for jobs */
extern int preferred_hour;

/* Function prototypes */

/* main.c */
int xopen(int fd, const char *file_name, int flags);
void xclose(int fd);
pid_t xfork(void);

#ifdef __GNUC__
#define PRINTF_FORMAT(n, m) \
   __attribute__ ((format (printf, n, m)))
#else
#define PRINTF_FORMAT(n, m)
#endif

/* log.c */
void explain(const char *fmt, ...)PRINTF_FORMAT(1,2);
void explain_e(const char *fmt, ...)PRINTF_FORMAT(1,2);
void complain(const char *fmt, ...)PRINTF_FORMAT(1,2);
void complain_e(const char *fmt, ...)PRINTF_FORMAT(1,2);
void die(const char *fmt, ...)PRINTF_FORMAT(1,2) ATTRIBUTE_NORETURN;
void die_e(const char *fmt, ...)PRINTF_FORMAT(1,2) ATTRIBUTE_NORETURN;
void xdebug(const char *fmt, ...)PRINTF_FORMAT(1,2);
void xdebug_e(const char *fmt, ...)PRINTF_FORMAT(1,2);
void xcloselog(void);

#ifdef DEBUG
#define Debug(args) xdebug args
#define Debug_e(args) xdebug_e args
#else /* not DEBUG */
#define Debug(args) (void)(0)
#define Debug_e(args) (void)(0)
#endif /* not DEBUG */

/* readtab.c */
void read_tab(int cwd);
void arrange_jobs(void);

/* lock.c */
int consider_job(job_rec *jr);
void unlock(job_rec *jr);
void update_timestamp(job_rec *jr);
void fake_job(job_rec *jr);

/* runjob.c */
void tend_children();
void launch_job(job_rec *jr);

#endif
