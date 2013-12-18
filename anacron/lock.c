/*
    Anacron - run commands periodically
    Copyright (C) 1998  Itai Tzur <itzur@actcom.co.il>
    Copyright (C) 1999  Sean 'Shaleh' Perry <shaleh@debian.org>
    Copyirght (C) 2004  Pascal Hakim <pasc@redellipse.net>

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


/* Lock and timestamp management
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include "global.h"
#include "gregor.h"

static void
open_tsfile(job_rec *jr)
/* Open the timestamp file for job jr */
{
    jr->timestamp_fd = open(jr->ident, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (jr->timestamp_fd == -1)
	die_e("Can't open timestamp file for job %s", jr->ident);
    fcntl(jr->timestamp_fd, F_SETFD, 1);    /* set close-on-exec flag */
    /* We want to own this file, and set its mode to 0600. This is necessary
     * in order to prevent other users from putting locks on it. */
    if (fchown(jr->timestamp_fd, getuid(), getgid()))
	die_e("Can't chown timestamp file %s", jr->ident);
    if (fchmod(jr->timestamp_fd, S_IRUSR | S_IWUSR))
	die_e("Can't chmod timestamp file %s", jr->ident);
}

static int
lock_file(int fd)
/* Attempt to put an exclusive fcntl() lock on file "fd"
 * Return 1 on success, 0 on failure.
 */
{
    int r;
    struct flock sfl;

    sfl.l_type = F_WRLCK;
    sfl.l_start = 0;
    sfl.l_whence = SEEK_SET;
    sfl.l_len = 0;   /* we lock all the file */
    errno = 0;
    r = fcntl(fd, F_SETLK, &sfl);
    if (r != -1) return 1;
    if (errno != EACCES && errno != EAGAIN)
	die_e("fcntl() error");
    return 0;
}

int
consider_job(job_rec *jr)
/* Check the timestamp of the job. If "its time has come", lock the job
 * and return 1, if it's too early, or we can't get the lock, return 0.
 */
{
    char timestamp[9];
    int ts_year, ts_month, ts_day, dn;
    ssize_t b;

    open_tsfile(jr);

    /* read timestamp */
    b = read(jr->timestamp_fd, timestamp, 8);
    if (b == -1) die_e("Error reading timestamp file %s", jr->ident);
    timestamp[8] = 0;

    /* is it too early? */
    if (!force && b == 8)
    {
	int day_delta;
	time_t jobtime;
	struct tm *t;

	if (sscanf(timestamp, "%4d%2d%2d", &ts_year, &ts_month, &ts_day) == 3)
	    dn = day_num(ts_year, ts_month, ts_day);
	else
	    dn = 0;

	day_delta = day_now - dn;

	/*
	 * if day_delta is negative, we assume there was a clock skew 
	 * and re-run any affected jobs
	 * otherwise we check if the job's time has come
	 */
	if (day_delta >= 0 && day_delta < jr->period)
	{
            /* yes, skip job */
	    xclose(jr->timestamp_fd);
	    return 0;
	}

	/*
	 * Check to see if it's a named period, in which case we need 
	 * to figure it out.
	 */
	if (jr->named_period)
	{
	    int period = 0, bypass = 0;
	    switch (jr->named_period)
	    {
		case 1: /* monthly */
		    period = days_last_month ();
		    bypass = days_this_month ();
		    break;
		case 2: /* yearly, annually */
		    period = days_last_year ();
		    bypass = days_this_year ();
		    break;
		case 3: /* daily */
			period = 1;
			bypass = 1;
			break;
		case 4:	/* weekly */
			period = 7;
			bypass = 7;
			break;
		default:
		    die ("Unknown named period for %s (%d)", jr->ident, jr->named_period);
	    }
	    printf ("Checking against %d with %d\n", day_delta, period);
	    if (day_delta < period && day_delta != bypass)
	    {
		/* Job is still too young */
		xclose (jr->timestamp_fd);
		return 0;
	    }
	}

	jobtime = start_sec + jr->delay * 60;

	t = localtime(&jobtime);
	if (!now && preferred_hour != -1 && t->tm_hour != preferred_hour) {
		Debug(("The job's %s preferred hour %d was missed, skipping the job.", jr->ident, preferred_hour));
		xclose (jr->timestamp_fd);
		return 0;
	}

	if (!now && range_start != -1 && range_stop != -1 && 
		(t->tm_hour < range_start || t->tm_hour >= range_stop))
	{
		Debug(("The job `%s' falls out of the %02d:00-%02d:00 hours range, skipping.",
			jr->ident, range_start, range_stop));
		xclose (jr->timestamp_fd);
		return 0;
	}
    }

    /* no! try to grab the lock */
    if (lock_file(jr->timestamp_fd)) return 1;  /* success */

    /* didn't get lock */
    xclose(jr->timestamp_fd);
    explain("Job `%s' locked by another anacron - skipping", jr->ident);
    return 0;
}

void
unlock(job_rec *jr)
{
    xclose(jr->timestamp_fd);
}

void
update_timestamp(job_rec *jr)
/* We write the date "now".  "Now" can be either the time when anacron
 * started, or the time when the job finished.
 * I'm not quite sure which is more "right", but I've decided on the first
 * option.
 * Note that this is not the way it was with anacron 1.0.3 to 1.0.7.  
 */
{
    char stamp[10];

    snprintf(stamp, 10, "%04d%02d%02d\n", year, month, day_of_month);
    if (lseek(jr->timestamp_fd, 0, SEEK_SET))
	die_e("Can't lseek timestamp file for job %s", jr->ident);
    if (write(jr->timestamp_fd, stamp, 9) != 9)
	die_e("Can't write timestamp file for job %s", jr->ident);
    if (ftruncate(jr->timestamp_fd, 9))
	die_e("ftruncate error");
}

void
fake_job(job_rec *jr)
/* We don't bother with any locking here.  There's no point. */
{
    open_tsfile(jr);
    update_timestamp(jr);
    xclose(jr->timestamp_fd);
}
