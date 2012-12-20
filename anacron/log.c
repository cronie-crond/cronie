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


/* Error logging
 *
 * We have two levels of logging (plus debugging if DEBUG is defined):
 * "explain" level for informational messages, and "complain" level for errors.
 *
 * We log everything to syslog, see the top of global.h for relevant
 * definitions.
 *
 * Stderr gets "complain" messages when we're in the foreground,
 * and "explain" messages when we're in the foreground, and not "quiet".
 */

#include <unistd.h>
#include <syslog.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include "global.h"

static char truncated[] = " (truncated)";
static char msg[MAX_MSG + 1];
static int log_open = 0;

/* Number of complaints that we've seen */
int complaints = 0;

static void
xopenlog(void)
{
    if (!log_open)
    {
	openlog(program_name, LOG_PID, SYSLOG_FACILITY);
	log_open = 1;
    }
}

void
xcloselog(void)
{
    if (log_open) closelog();
    log_open = 0;
}

static void
make_msg(const char *fmt, va_list args)
/* Construct the message string from its parts */
{
    int len;

    /* There's some confusion in the documentation about what vsnprintf
     * returns when the buffer overflows.  Hmmm... */
    len = vsnprintf(msg, sizeof(msg), fmt, args);
    if (len >= sizeof(msg) - 1)
	strcpy(msg + sizeof(msg) - sizeof(truncated), truncated);
}

static void
slog(int priority, const char *fmt, va_list args)
/* Log a message, described by "fmt" and "args", with the specified
 * "priority". */
{
    make_msg(fmt, args);
    xopenlog();
    syslog(priority, "%s", msg);
    if (!in_background)
    {
	if (priority == EXPLAIN_LEVEL && !quiet)
	    fprintf(stderr, "%s\n", msg);
	else if (priority == COMPLAIN_LEVEL)
	    fprintf(stderr, "%s: %s\n", program_name, msg);
    }
}

static void
log_e(int priority, const char *fmt, va_list args)
/* Same as slog(), but also appends an error description corresponding
 * to "errno". */
{
    int saved_errno;

    saved_errno = errno;
    make_msg(fmt, args);
    xopenlog();
    syslog(priority, "%s: %s", msg, strerror(saved_errno));
    if (!in_background)
    {
	if (priority == EXPLAIN_LEVEL && !quiet)
	    fprintf(stderr, "%s: %s\n", msg, strerror(saved_errno));
	else if (priority == COMPLAIN_LEVEL)
	    fprintf(stderr, "%s: %s: %s\n",
		    program_name, msg, strerror(saved_errno));
    }
}

void
explain(const char *fmt, ...)
/* Log an "explain" level message */
{
    va_list args;

    va_start(args, fmt);
    slog(EXPLAIN_LEVEL, fmt, args);
    va_end(args);
}

void
explain_e(const char *fmt, ...)
/* Log an "explain" level message, with an error description */
{
    va_list args;

    va_start(args, fmt);
    log_e(EXPLAIN_LEVEL, fmt, args);
    va_end(args);
}

void
complain(const char *fmt, ...)
/* Log a "complain" level message */
{
    va_list args;

    va_start(args, fmt);
    slog(COMPLAIN_LEVEL, fmt, args);
    va_end(args);

    complaints += 1;
}

void
complain_e(const char *fmt, ...)
/* Log a "complain" level message, with an error description */
{
    va_list args;

    va_start(args, fmt);
    log_e(COMPLAIN_LEVEL, fmt, args);
    va_end(args);

    complaints += 1;
}

void
die(const char *fmt, ...)
/* Log a "complain" level message, and exit */
{
    va_list args;

    va_start(args, fmt);
    slog(COMPLAIN_LEVEL, fmt, args);
    va_end(args);
    if (getpid() == primary_pid) complain("Aborted");

    exit(FAILURE_EXIT);
}

void
die_e(const char *fmt, ...)
/* Log a "complain" level message, with an error description, and exit */
{
    va_list args;

    va_start(args, fmt);
    log_e(COMPLAIN_LEVEL, fmt, args);
    va_end(args);
    if (getpid() == primary_pid) complain("Aborted");

    exit(FAILURE_EXIT);
}

#ifdef DEBUG

/* These are called through the Debug() and Debug_e() macros, defined
 * in global.h */

void
xdebug(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    slog(DEBUG_LEVEL, fmt, args);
    va_end(args);
}

void
xdebug_e(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    log_e(DEBUG_LEVEL, fmt, args);
    va_end(args);
}

#endif  /* DEBUG */
