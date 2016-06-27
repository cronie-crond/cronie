/*
    cronnext - calculate the time cron will execute the next job
    Copyright (C) 2016 Marco Migliori <sgerwk@aol.com>

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

#include "config.h"

#define MAIN_PROGRAM

#include <stdlib.h>
#include <string.h>
#include <pwd.h>

#include "globals.h"
#include "funcs.h"
#include "cron-paths.h"

/*
 * print entry flags
 */
char *flagname[]= {
	[MIN_STAR] =	"MIN_STAR",
	[HR_STAR] =	"HR_STAR",
	[DOM_STAR] =	"DOM_STAR",
	[DOW_STAR] =	"DOW_STAR",
	[WHEN_REBOOT] =	"WHEN_REBOOT",
	[DONT_LOG] =	"DONT_LOG"
};

void printflags(int flags) {
	int f;
	printf("flags: 0x%d = ", flags);
	for (f = 1; f < sizeof(flagname);  f = f << 1)
		if (flags & f)
			printf("%s ", flagname[f]);
	printf("\n");
}

/*
 * print a crontab entry
 */
void printentry(entry *e, int system, time_t next) {
	if (system)
		printf("entry user: %s\n", e->pwd->pw_name);
	printf("cmd: %s\n", e->cmd);
	printflags(e->flags);
	printf("delay: %d\n", e->delay);
	printf("next: %ld = ", (long)next);
	printf("%s", asctime(localtime(&next)));
}

/*
 * print a crontab data
 */
void printcrontab(user *u) {
	printf("==========================\n");
	printf("user: %s\n", u->name);
	printf("crontab: %s\n", u->tabname);
	printf("system: %d\n", u->system);
}

/*
 * basic algorithm: iterate over time from now to 8 year ahead in default steps
 * of 1 minute, checking whether time matches a crontab entry at each step (8
 * years is the largest interval between two crontab matches)
 *
 * to save iterations, use larger steps if month or day don't match the entry:
 * - if the month doesn't match, skip to 00:00 of the first day of next month
 * - for the day, avoid the complication of the different length of months: if
 *   neither the day nor the next day match, increase time of one day
 */

/*
 * check whether time matches day of month and/or day of week; this requires
 * checking dom if dow=*, dow if dom=*, either one otherwise; see comment "the
 * dom/dow situation is odd..." in cron.c
 */
int matchday(entry *e, time_t time) {
	struct tm current;

	localtime_r(&time, &current);

	if (e->flags & DOW_STAR)
		return bit_test(e->dom, current.tm_mday - 1);
	if (e->flags & DOM_STAR) 
		return bit_test(e->dow, current.tm_wday);
	return bit_test(e->dom, current.tm_mday - 1) ||
		bit_test(e->dow, current.tm_wday);
}

/*
 * next time matching a crontab entry
 */
time_t nextmatch(entry *e, time_t start) {
	time_t time;
	struct tm current;

	/* maximum match interval is 8 years (<102 months of 28 days):
	 * crontab has '* * 29 2 *' and we are on 1 March 2096:
	 * next matching time will be 29 February 2104 */

	for (time = start; time < start + 102 * 28 * 24 * 60 * 60; ) {
		localtime_r(&time, &current);

		/* month doesn't match: move to 1st of next month */
		if (!bit_test(e->month, current.tm_mon)) {
			current.tm_mon++;
			if (current.tm_mon >= 12) {
				current.tm_year++;
				current.tm_mon = 0;
			}
			current.tm_mday = 1;
			current.tm_hour = 0;
			current.tm_min = 0;
			time = mktime(&current);
			continue;
		}

		/* neither time nor time+1day match day: increase 1 day */
		if (!matchday(e, time) && !matchday(e, time + 24 * 60 * 60)) {
			time += 24 * 60 * 60;
			continue;
		}

		/* if time matches, return time;
		 * check for month is redudant, but check for day is
		 * necessary because we only know that either time
		 * or time+1day match */
		if (bit_test(e->month, current.tm_mon) &&
			matchday(e, time) &&
			bit_test(e->hour, current.tm_hour) &&
			bit_test(e->minute, current.tm_min)
		)
			return time;

		/* skip to next minute */
		time += 60;
	}

	return -1;
}

/*
 * match a user against a list
 */
int matchuser(char *user, char *list) {
	char *pos;
	int l = strlen(user);

	for (pos = list; (pos = strstr(pos, user)) != NULL; pos += l) {
		if ((pos != list) && (*(pos - 1) != ','))
			continue;
		if ((pos[l] != '\0') && (pos[l] != ','))
			continue;
		return 1;
	}
	return 0;
}

/*
 * find next sheduled job
 */
time_t cronnext(time_t start,
		char *include, char *exclude, int system,
		int verbose) {
	time_t closest, next;
	static cron_db database = {NULL, NULL, (time_t) 0};
	user *u;
	entry *e;

	/* load crontabs */
	load_database(&database);

	/* find next sheduled time */
	closest = -1;
	for (u = database.head; u; u = u->next) {
		if (include && !matchuser(u->name, include))
			continue;
		if (exclude && matchuser(u->name, exclude))
			continue;
		if (!system && u->system)
			continue;

		if (verbose)
			printcrontab(u);

		for (e = u->crontab; e; e = e->next) {
			next = nextmatch(e, start);
			if (next < 0)
				continue;
			if ((closest < 0) || (next < closest))
				closest = next;
			if (verbose)
				printentry(e, u->system, next);
		}
	}

	return closest;
}

void usage() {
	fprintf(stderr, "Find the time of the next scheduled cron job.\n");
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\tcronnext [-i users] [-e users] [-s] [-t time] [-v] [-h]\n");
	fprintf(stderr, "\t\t-i users\tinclude only the crontab of these users\n");
	fprintf(stderr, "\t\t-e users\texclude the crontab of these users\n");
	fprintf(stderr, "\t\t-s\t\tdo not include the system crontab\n");
	fprintf(stderr, "\t\t-t time\t\tstart from this time (seconds since epoch)\n");
	fprintf(stderr, "\t\t-v\t\tverbose mode\n");
	fprintf(stderr, "\t\t-h\t\tthis help\n");
}

/*
 * main
 */
int main(int argn, char *argv[]) {
	int opt;
	char *include, *exclude;
	int system, verbose;
	time_t start, next;

	include = NULL;
	exclude = NULL;
	system = 1;
	start = time(NULL);
	verbose = 0;

	while (-1 != (opt = getopt(argn, argv, "i:e:st:vh"))) {
		switch (opt) {
		case 'i':
			include = optarg;
			break;
		case 'e':
			exclude = optarg;
			break;
		case 's':
			system = 0;
			break;
		case 't':
			start = atoi(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		default:
			fprintf(stderr, "unrecognized option: %s\n",
				argv[optind - 1]);
			usage();
			exit(EXIT_FAILURE);
		}
	}

	/* debug cron */
	if (verbose) {
		printf("SPOOL_DIR=%s\n", SPOOL_DIR);
		set_debug_flags("load");
	}
	/* "load,pars" for debugging loading and parsing, "" for nothing
	   see globals.h for symbolic names and macros.h for meaning */

	/* print time of next scheduled command */
	next = cronnext(start, include, exclude, system, verbose);
	if (next == -1) {
		if (verbose)
			printf("no job scheduled\n");
		return EXIT_FAILURE;
	}
	else
		if (verbose)
			printf("next of all jobs: %ld = %s",
				(long)next, asctime(localtime(&next)));
		else
			printf("%ld\n", (long)next);

	return EXIT_SUCCESS;
}

