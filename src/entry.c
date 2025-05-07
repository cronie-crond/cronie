/*
 * Copyright 1988,1990,1993,1994 by Paul Vixie
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

/* vix 26jan87 [RCS'd; rest of log is in RCS file]
 * vix 01jan87 [added line-level error recovery]
 * vix 31dec86 [added /step to the from-to range, per bob@acornrc]
 * vix 30dec86 [written]
 */

#include "config.h"

#include <ctype.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "bitstring.h"
#include "funcs.h"
#include "globals.h"
#include "macros.h"
#include "pathnames.h"

typedef enum ecode {
	e_none, e_minute, e_hour, e_dom, e_month, e_dow,
	e_cmd, e_timespec, e_username, e_option, e_memory
} ecode_e;

static const char *ecodes[] = {
	"no error",
	"bad minute",
	"bad hour",
	"bad day-of-month",
	"bad month",
	"bad day-of-week",
	"bad command",
	"bad time specifier",
	"bad username",
	"bad option",
	"out of memory"
};

typedef enum {
	R_START,
	R_AST,
	R_STEP,
	R_TERMS,
	R_NUM1,
	R_RANGE,
	R_RANGE_NUM2,
	R_RANDOM,
	R_RANDOM_NUM2,
	R_FINISH,
} range_state_t;

static int get_list(bitstr_t *, int, int, const char *[], int, FILE *),
get_range(bitstr_t *, int, int, const char *[], FILE *),
get_number(int *, int, const char *[], FILE *),
set_element(bitstr_t *, int, int, int);

void free_entry(entry * e) {
	free(e->cmd);
	free(e->pwd);
	env_free(e->envp);
	free(e);
}

/* return NULL if eof or syntax error occurs;
 * otherwise return a pointer to a new entry.
 */
entry *load_entry(FILE * file, void (*error_func) (const char *), struct passwd *pw,
	char **envp) {
	/* this function reads one crontab entry -- the next -- from a file.
	 * it skips any leading blank lines, ignores comments, and returns
	 * NULL if for any reason the entry can't be read and parsed.
	 *
	 * the entry is also parsed here.
	 *
	 * syntax:
	 *   user crontab:
	 *  minutes hours doms months dows cmd\n
	 *   system crontab (/etc/crontab):
	 *  minutes hours doms months dows USERNAME cmd\n
	 */

	ecode_e ecode = e_none;
	entry *e = NULL;
	int ch;
	char cmd[MAX_COMMAND];
	char envstr[MAX_ENVSTR];
	char **tenvp;
	char *p;
	struct passwd temppw;
	int i;

	Debug(DPARS, ("load_entry()...about to eat comments\n"));

	ch = get_char(file);
	if (ch == EOF)
		return (NULL);

	/* ch is now the first useful character of a useful line.
	 * it may be an @special or it may be the first character
	 * of a list of minutes.
	 */

	e = (entry *) calloc(sizeof (entry), sizeof (char));
	if (e == NULL) {
		ecode = e_memory;
		goto eof;
	}

	/* check for '-' as a first character, this option will disable 
	* writing a syslog message about command getting executed
	*/
	if (ch == '-') {
	/* if we are editing system crontab or user uid is 0 (root) 
	* we are allowed to disable logging 
	*/
		if (pw == NULL || pw->pw_uid == 0)
			e->flags |= DONT_LOG;
		else {
			log_it("CRON", getpid(), "ERROR", "Only privileged user can disable logging", 0);
			ecode = e_option;
			goto eof;
		}
		ch = get_char(file);
		if (ch == EOF) {
			free(e);
			return NULL;
		}
	}

	if (ch == '@') {
		/* all of these should be flagged and load-limited; i.e.,
		 * instead of @hourly meaning "0 * * * *" it should mean
		 * "close to the front of every hour but not 'til the
		 * system load is low".  Problems are: how do you know
		 * what "low" means? (save me from /etc/cron.conf!) and:
		 * how to guarantee low variance (how low is low?), which
		 * means how to we run roughly every hour -- seems like
		 * we need to keep a history or let the first hour set
		 * the schedule, which means we aren't load-limited
		 * anymore.  too much for my overloaded brain. (vix, jan90)
		 * HINT
		 */
		ch = get_string(cmd, MAX_COMMAND, file, " \t\n");
		if (!strcmp("reboot", cmd)) {
			e->flags |= WHEN_REBOOT;
		}
		else if (!strcmp("yearly", cmd) || !strcmp("annually", cmd)) {
			bit_set(e->minute, 0);
			bit_set(e->hour, 0);
			bit_set(e->dom, 0);
			bit_set(e->month, 0);
			bit_nset(e->dow, 0, LAST_DOW - FIRST_DOW);
			e->flags |= DOW_STAR;
		}
		else if (!strcmp("monthly", cmd)) {
			bit_set(e->minute, 0);
			bit_set(e->hour, 0);
			bit_set(e->dom, 0);
			bit_nset(e->month, 0, LAST_MONTH - FIRST_MONTH);
			bit_nset(e->dow, 0, LAST_DOW - FIRST_DOW);
			e->flags |= DOW_STAR;
		}
		else if (!strcmp("weekly", cmd)) {
			bit_set(e->minute, 0);
			bit_set(e->hour, 0);
			bit_nset(e->dom, 0, LAST_DOM - FIRST_DOM);
			bit_nset(e->month, 0, LAST_MONTH - FIRST_MONTH);
			bit_set(e->dow, 0);
			e->flags |= DOM_STAR;
		}
		else if (!strcmp("daily", cmd) || !strcmp("midnight", cmd)) {
			bit_set(e->minute, 0);
			bit_set(e->hour, 0);
			bit_nset(e->dom, 0, LAST_DOM - FIRST_DOM);
			bit_nset(e->month, 0, LAST_MONTH - FIRST_MONTH);
			bit_nset(e->dow, 0, LAST_DOW - FIRST_DOW);
		}
		else if (!strcmp("hourly", cmd)) {
			bit_set(e->minute, 0);
			bit_nset(e->hour, 0, LAST_HOUR - FIRST_HOUR);
			bit_nset(e->dom, 0, LAST_DOM - FIRST_DOM);
			bit_nset(e->month, 0, LAST_MONTH - FIRST_MONTH);
			bit_nset(e->dow, 0, LAST_DOW - FIRST_DOW);
			e->flags |= HR_STAR;
		}
		else {
			ecode = e_timespec;
			goto eof;
		}
		/* Advance past whitespace between shortcut and
		 * username/command.
		 */
		Skip_Blanks(ch, file);
		if (ch == EOF || ch == '\n') {
			ecode = e_cmd;
			goto eof;
		}
	}
	else {
		Debug(DPARS, ("load_entry()...about to parse numerics\n"));

		if (ch == '*')
			e->flags |= MIN_STAR;
		ch = get_list(e->minute, FIRST_MINUTE, LAST_MINUTE, PPC_NULL, ch, file);
		if (ch == EOF) {
			ecode = e_minute;
			goto eof;
		}

		/* hours
		 */

		if (ch == '*')
			e->flags |= HR_STAR;
		ch = get_list(e->hour, FIRST_HOUR, LAST_HOUR, PPC_NULL, ch, file);
		if (ch == EOF) {
			ecode = e_hour;
			goto eof;
		}

		/* DOM (days of month)
		 */

		if (ch == '*')
			e->flags |= DOM_STAR;
		ch = get_list(e->dom, FIRST_DOM, LAST_DOM, PPC_NULL, ch, file);
		if (ch == EOF) {
			ecode = e_dom;
			goto eof;
		}

		/* month
		 */

		ch = get_list(e->month, FIRST_MONTH, LAST_MONTH, MonthNames, ch, file);
		if (ch == EOF) {
			ecode = e_month;
			goto eof;
		}

		/* DOW (days of week)
		 */

		if (ch == '*')
			e->flags |= DOW_STAR;
		ch = get_list(e->dow, FIRST_DOW, LAST_DOW, DowNames, ch, file);
		if (ch == EOF) {
			ecode = e_dow;
			goto eof;
		}
	}

	/* make sundays equivalent */
	if (bit_test(e->dow, 0) || bit_test(e->dow, 7)) {
		bit_set(e->dow, 0);
		bit_set(e->dow, 7);
	}

	/* check for permature EOL and catch a common typo */
	if (ch == '\n' || ch == '*') {
		ecode = e_cmd;
		goto eof;
	}

	/* ch is the first character of a command, or a username */
	unget_char(ch, file);

	if (!pw) {
		char *username = cmd;	/* temp buffer */

		Debug(DPARS, ("load_entry()...about to parse username\n"));
		ch = get_string(username, MAX_COMMAND, file, " \t\n");

		Debug(DPARS, ("load_entry()...got %s\n", username));
		if (ch == EOF || ch == '\n' || ch == '*') {
			ecode = e_cmd;
			goto eof;
		}

		pw = getpwnam(username);
		if (pw == NULL) {
			Debug(DPARS, ("load_entry()...unknown user entry\n"));
			memset(&temppw, 0, sizeof (temppw));
			temppw.pw_name = username;
			temppw.pw_passwd = "";
			pw = &temppw;
		} else {
			Debug(DPARS, ("load_entry()...uid %ld, gid %ld\n",
				(long) pw->pw_uid, (long) pw->pw_gid));
		}
		/* Advance past whitespace before command. */
		Skip_Blanks(ch, file);

		/* check for permature EOL or EOF */
		if (ch == EOF || ch == '\n') {
			ecode = e_cmd;
			goto eof;
		}

		/* ch is the first character of a command */
		unget_char(ch, file);
	}

	if ((e->pwd = pw_dup(pw)) == NULL) {
		ecode = e_memory;
		goto eof;
	}
	memset(e->pwd->pw_passwd, 0, strlen(e->pwd->pw_passwd));

	p = env_get("RANDOM_DELAY", envp);
	if (p) {
		char *endptr;
		long val;

		errno = 0;    /* To distinguish success/failure after call */
		val = strtol(p, &endptr, 10);
		if (errno != 0 || val < 0 || val > 24*60) {
			log_it("CRON", getpid(), "ERROR", "bad value of RANDOM_DELAY", 0);
		} else {
			e->delay = (int)((double)val * RandomScale);
		}
	}

	/* copy and fix up environment.  some variables are just defaults and
	 * others are overrides.
	 */
	if ((e->envp = env_copy(envp)) == NULL) {
		ecode = e_memory;
		goto eof;
	}
	if (!env_get("SHELL", e->envp)) {
		if (glue_strings(envstr, sizeof envstr, "SHELL", _PATH_BSHELL, '=')) {
			if ((tenvp = env_set(e->envp, envstr)) == NULL) {
				ecode = e_memory;
				goto eof;
			}
			e->envp = tenvp;
		}
		else
			log_it("CRON", getpid(), "ERROR", "can't set SHELL", 0);
	}
	if ((tenvp = env_update_home(e->envp, pw->pw_dir)) == NULL) {
		ecode = e_memory;
		goto eof;
	}
	e->envp = tenvp;
#ifndef LOGIN_CAP
	/* If login.conf is in used we will get the default PATH later. */
	if (!env_get("PATH", e->envp)) {
		char *defpath;

		if (ChangePath)
			defpath = _PATH_STDPATH;
		else {
			defpath = getenv("PATH");
			if (defpath == NULL)
				defpath = _PATH_STDPATH;
		}

		if (glue_strings(envstr, sizeof envstr, "PATH", defpath, '=')) {
			if ((tenvp = env_set(e->envp, envstr)) == NULL) {
				ecode = e_memory;
				goto eof;
			}
			e->envp = tenvp;
		}
		else
			log_it("CRON", getpid(), "ERROR", "can't set PATH", 0);
	}
#endif /* LOGIN_CAP */
	if (glue_strings(envstr, sizeof envstr, "LOGNAME", pw->pw_name, '=')) {
		if ((tenvp = env_set(e->envp, envstr)) == NULL) {
			ecode = e_memory;
			goto eof;
		}
		e->envp = tenvp;
	}
	else
		log_it("CRON", getpid(), "ERROR", "can't set LOGNAME", 0);
#if defined(BSD) || defined(__linux)
	if (glue_strings(envstr, sizeof envstr, "USER", pw->pw_name, '=')) {
		if ((tenvp = env_set(e->envp, envstr)) == NULL) {
			ecode = e_memory;
			goto eof;
		}
		e->envp = tenvp;
	}
	else
		log_it("CRON", getpid(), "ERROR", "can't set USER", 0);
#endif

	Debug(DPARS, ("load_entry()...about to parse command\n"));

	/* If the first character of the command is '-', it is a cron option. */
	ch = get_char(file);
	while (ch == '-') {
		switch (ch = get_char(file)) {
			case 'n':
				/* only allow user to set the option once */
				if ((e->flags & MAIL_WHEN_ERR) == MAIL_WHEN_ERR) {
					ecode = e_option;
					goto eof;
				}
				e->flags |= MAIL_WHEN_ERR;
				break;

			default:
				ecode = e_option;
				goto eof;
		}

		ch = get_char(file);
		if (ch != '\t' && ch != ' ') {
			ecode = e_option;
			goto eof;
		}
		Skip_Blanks(ch, file);
		if (ch == EOF || ch == '\n') {
			ecode = e_cmd;
			goto eof;
		}
	}
	unget_char(ch, file);

	/* Everything up to the next \n or EOF is part of the command...
	 * too bad we don't know in advance how long it will be, since we
	 * need to malloc a string for it... so, we limit it to MAX_COMMAND.
	 */
	ch = get_string(cmd, MAX_COMMAND, file, "\n");

	/* a file without a \n before the EOF is rude, so we'll complain...
	 */
	if (ch == EOF) {
		ecode = e_cmd;
		goto eof;
	}

	/* got the command in the 'cmd' string; save it in *e.
	 */
	if ((e->cmd = strdup(cmd)) == NULL) {
		ecode = e_memory;
		goto eof;
	}

	Debug(DPARS, ("load_entry()...returning successfully\n"));

	/* success, fini, return pointer to the entry we just created...
	 */
	return (e);

  eof:
	if (e) {
		if (e->envp)
			env_free(e->envp);
		free(e->pwd);
		free(e->cmd);
		free(e);
	}
	for (i = 0; i < MAX_COMMAND && ch != '\n' && !feof(file); i++)
		ch = get_char(file);
	if (ecode != e_none && error_func)
		(*error_func) (ecodes[(int) ecode]);
	return (NULL);
}

static int
get_list(bitstr_t * bits, int low, int high, const char *names[],
	int ch, FILE * file) {
	int done;

	/* we know that we point to a non-blank character here;
	 * must do a Skip_Blanks before we exit, so that the
	 * next call (or the code that picks up the cmd) can
	 * assume the same thing.
	 */

	Debug(DPARS | DEXT, ("get_list()...entered\n"));

	/* list = range {"," range}
	 */
	/* clear the bit string, since the default is 'off'.
	 */
	bit_nclear(bits, 0, (high - low));

	/* process all ranges
	 */
	done = FALSE;
	/* unget ch to allow get_range() to process it properly 
	 */
	unget_char(ch, file);
	while (!done) {
		if (EOF == (ch = get_range(bits, low, high, names, file)))
			return (EOF);
		if (ch == ',')
			continue;
		else
			done = TRUE;
	}

	/* exiting.  skip to some blanks, then skip over the blanks.
	 */
	Skip_Nonblanks(ch, file)
	Skip_Blanks(ch, file)

	Debug(DPARS | DEXT, ("get_list()...exiting w/ %02x\n", ch));

	return (ch);
}

inline static int is_separator(int ch) {
	switch (ch) {
		case '\t':
		case '\n':
		case ' ':
		case ',':
			return 1;
		default:
			return 0;
	}
}



static int
get_range(bitstr_t * bits, int low, int high, const char *names[],
		FILE * file) {
	/* range = number | number "-" number [ "/" number ]
	 *         | [number] "~" [number]
	 */
	
	int ch, i, low_, high_, step;

	/* default value for step
	 */
	step = 1;
	range_state_t state = R_START;

	while (state != R_FINISH && ((ch = get_char(file)) != EOF)) {
		switch (state) {
			case R_START:
				if (ch == '*') {
					low_ = low;
					high_ = high;
					state = R_AST;
					break;
				}
				if (ch == '~') {
					low_ = low;
					state = R_RANDOM;
					break;
				}
				unget_char(ch, file);
				if (get_number(&low_, low, names, file) != EOF) {
					state = R_NUM1;
					break;
				}
				return (EOF);

			case R_AST:
				if (ch == '/') {
					state = R_STEP;
					break;
				}
				if (is_separator(ch)) {
					state = R_FINISH;
					break;
				}
				return (EOF);

			case R_STEP:
				unget_char(ch, file);
				if (get_number(&step, 0, PPC_NULL, file) != EOF
				    && step != 0) {
					state = R_TERMS;
					break;
				}
				return (EOF);

			case R_TERMS:
				if (is_separator(ch)) {
					state = R_FINISH;
					break;
				}
				return (EOF);

			case R_NUM1:
				if (ch == '-') {
					state = R_RANGE;
					break;
				}
				if (ch == '~') {
					state = R_RANDOM;
					break;
				}
				if (is_separator(ch)) {
					high_ = low_;
					state = R_FINISH;
					break;
				}
				return (EOF);

			case R_RANGE:
				unget_char(ch, file);
				if (get_number(&high_, low, names, file) != EOF) {
					state = R_RANGE_NUM2;
					break;
				}
				return (EOF);

			case R_RANGE_NUM2:
				if (ch == '/') {
					state = R_STEP;
					break;
				}
				if (low_ > high_ && high_ == 0) {
					high_ = 7;
				}
				if (is_separator(ch)) {
					state = R_FINISH;
					break;
				}
				return (EOF);

			case R_RANDOM:
				if (is_separator(ch)) {
					high_ = high;
					state = R_FINISH;
				}
				else if (unget_char(ch, file),
						get_number(&high_, low, names, file) != EOF) {
					state = R_TERMS;
				}
				/* fail if couldn't find match on previous term
				 */
				else
					return (EOF);

				/* if invalid random range was selected */
				if (low_ > high_)
					return (EOF);

				/* select random number in range <low_, high_>
				 */
				low_ = high_ = random() % (high_ - low_ + 1) + low_;
				break;


			default:
				/* We should never get here
				 */
				return (EOF);
		}
	}
	if (state != R_FINISH || ch == EOF)
		return (EOF);

	/* Make sure the step size makes any sense */
	if (step > 1 && step > (high_ - low_)) {
		int max =  high_ - low_ > 0 ? high_ - low_ : 1;
		fprintf(stderr, "Warning: Step size %i higher than possible maximum of %i\n", step, max);
	}

	for (i = low_; i <= high_; i += step)
		if (EOF == set_element(bits, low, high, i)) {
			unget_char(ch, file);
			return (EOF);
		}
	return ch;
}

static int
get_number(int *numptr, int low, const char *names[], FILE * file) {
	char temp[MAX_TEMPSTR], *pc;
	int len, i, ch;
	char *endptr;

	pc = temp;
	len = 0;

	/* get all alnum characters available */
	while (isalnum((ch = get_char(file)))) {
		if (++len >= MAX_TEMPSTR)
			goto bad;
		*pc++ = (char)ch;
	}
	*pc = '\0';
	if (len == 0)
		goto bad;

	unget_char(ch, file);

	/* try to get number */
	*numptr = (int) strtol(temp, &endptr, 10);
	if (*endptr == '\0' && temp != endptr) {
		/* We have a number */
		return 0;
	}

	/* no numbers, look for a string if we have any */
	if (names) {
		for (i = 0; names[i] != NULL; i++) {
			Debug(DPARS | DEXT, ("get_num, compare(%s,%s)\n", names[i], temp));
			if (strcasecmp(names[i], temp) == 0) {
				*numptr = i + low;
				return 0;
			}
		}
	} else {
		goto bad;
	}

  bad:
	unget_char(ch, file);
	return (EOF);
}

static int set_element(bitstr_t * bits, int low, int high, int number) {
	Debug(DPARS | DEXT, ("set_element(?,%d,%d,%d)\n", low, high, number));

	if (number < low || number > high)
		return (EOF);

	bit_set(bits, (number - low));
	return (OK);
}
