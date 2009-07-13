/*
    Anacron - run commands periodically
    Copyright (C) 1998  Itai Tzur <itzur@actcom.co.il>
    Copyright (C) 1999  Sean 'Shaleh' Perry <shaleh@debian.org>
 
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


/*  /etc/anacrontab parsing, and job sorting
 */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <obstack.h>
#include <limits.h>
#include <fnmatch.h>
#include <unistd.h>
#include <signal.h>
#include "global.h"
#include "matchrx.h"

static struct obstack input_o;   /* holds input line */
static struct obstack tab_o;    /* holds processed data read from anacrontab */
static FILE *tab;
job_rec **job_array;
int njobs;                       /* number of jobs to run */
static int jobs_read;            /* number of jobs read */
static int line_num;             /* current line in anacrontab */
static job_rec *last_job_rec;    /* last job stored in memory, at the moment */
static env_rec *last_env_rec;    /* last environment assignment stored */

/* some definitions for the obstack macros */
#define obstack_chunk_alloc xmalloc
#define obstack_chunk_free free

static void *
xmalloc (size_t size)
/* Just like standard malloc(), only never returns NULL. */
{
    void * ptr;

    ptr = malloc(size);
    if (ptr == NULL)
	die("Memory exhausted");
    return ptr;
}

static int
conv2int(const char *s)
/* Return the int or -1 on over/under-flow
 */
{
    long l;

    errno = 0;
    l = strtol(s, NULL, 10);
    /* we use negative as error, so I am really returning unsigned int */
    if (errno == ERANGE || l < 0 || l > INT_MAX) return - 1;
    return l;
}

static char *
read_tab_line ()
/* Read one line and return a pointer to it.
Return NULL if no more lines.
 */
{
    int c;

    if (feof(tab)) return NULL;
    while ((c = getc(tab)) != EOF && c != '\n')
	obstack_1grow(&input_o, c);
    if (ferror(tab)) die_e("Error reading %s", anacrontab);
    obstack_1grow(&input_o, '\0');
    return obstack_finish(&input_o);
}

static int
job_arg_num(const char *ident)
/* Return the command-line-argument number refering to this job-identifier.
 * If it isn't specified, return -1.
 */
{
    int i, r;

    for (i = 0; i < nargs; i++)
    {
	r = fnmatch(args[i], ident, 0);
	if (r == 0) return i;
	if (r != FNM_NOMATCH) die("fnmatch() error");
    }
    return - 1;
}

static void
register_env(const char *env_var, const char *value)
/* Store the environment assignment "env_var"="value" */
{
    env_rec *er;
    int var_len, val_len;

    var_len = strlen(env_var);
    val_len = strlen(value);
    er = obstack_alloc(&tab_o, sizeof(env_rec));
    er->assign = obstack_alloc(&tab_o, var_len + 1 + val_len + 1);
    strcpy(er->assign, env_var);
    er->assign[var_len] = '=';
    strcpy(er->assign + var_len + 1, value);
    er->assign[var_len + 1 + val_len] = 0;
    if (last_env_rec != NULL) last_env_rec->next = er;
    else first_env_rec = er;
    last_env_rec = er;
    Debug(("on line %d: %s", line_num, er->assign));
}

static void
register_job(const char *periods, const char *delays,
	     const char *ident, char *command)
/* Store a job definition */
{
    int period, delay;
    job_rec *jr;
    int ident_len, command_len;

    ident_len = strlen(ident);
    command_len = strlen(command);
    jobs_read++;
    period = conv2int(periods);
    delay = conv2int(delays);
    if (period < 0 || delay < 0)
    {
	complain("%s: number out of range on line %d, skipping",
		 anacrontab, line_num);
	return;
    }
    jr = obstack_alloc(&tab_o, sizeof(job_rec));
    jr->period = period;
    jr->delay = delay;
    jr->tab_line = line_num;
    jr->ident = obstack_alloc(&tab_o, ident_len + 1);
    strcpy(jr->ident, ident);
    jr->arg_num = job_arg_num(ident);
    jr->command = obstack_alloc(&tab_o, command_len + 1);
    strcpy(jr->command, command);
    jr->job_pid = jr->mailer_pid = 0;
    if (last_job_rec != NULL) last_job_rec->next = jr;
    else first_job_rec = jr;
    last_job_rec = jr;
    jr->prev_env_rec = last_env_rec;
    jr->next = NULL;
    Debug(("Read job - period=%d, delay=%d, ident=%s, command=%s",
	   jr->period, jr->delay, jr->ident, jr->command));
}

static void
parse_tab_line(char *line)
{
    int r;
    char *env_var;
    char *value;
    char *periods;
    char *delays;
    char *ident;
    char *command;

    /* an empty line? */
    r = match_rx("^[ \t]*($|#)", line, 0);
    if (r == -1) goto reg_err;
    if (r)
    {
	Debug(("line %d empty", line_num));
	return;
    }

    /* an environment assignment? */
    r = match_rx("^[ \t]*([^ \t=]+)[ \t]*=(.*)$", line, 2,
		 &env_var, &value);
    if (r == -1) goto reg_err;
    if (r)
    {
	register_env(env_var, value);
	return;
    }

    /* a job? */
    r = match_rx("^[ \t]*([[:digit:]]+)[ \t]+([[:digit:]]+)[ \t]+"
		 "([^ \t/]+)[ \t]+([^ \t].*)$",
		 line, 4, &periods, &delays, &ident, &command);
    if (r == -1) goto reg_err;
    if (r)
    {
	register_job(periods, delays, ident, command);
	return;
    }
    complain("Invalid syntax in %s on line %d - skipping this line",
	     anacrontab, line_num);
    return;

 reg_err:
    die("Regex error reading %s", anacrontab);
}

void
read_tab()
/* Read the anacrontab file into memory */
{
    char *tab_line;

    first_job_rec = last_job_rec = NULL;
    first_env_rec = last_env_rec = NULL;
    jobs_read = 0;
    line_num = 0;
    /* Open the anacrontab file */
    tab = fopen(anacrontab, "r");
    if (tab == NULL) die_e("Error opening %s", anacrontab);
    /* Initialize the obstacks */
    obstack_init(&input_o);
    obstack_init(&tab_o);
    while ((tab_line = read_tab_line()) != NULL)
    {
	line_num++;
	parse_tab_line(tab_line);
	obstack_free(&input_o, tab_line);
    }
    if (fclose(tab)) die_e("Error closing %s", anacrontab);
}

static int
execution_order(const job_rec **job1, const job_rec **job2)
/* Comparison function for sorting the jobs.
 */
{
    int d;

    d = (*job1)->arg_num - (*job2)->arg_num;
    if (d != 0 && now) return d;
    d = (*job1)->delay - (*job2)->delay;
    if (d != 0) return d;
    d = (*job1)->tab_line - (*job2)->tab_line;
    return d;
}

void
arrange_jobs()
/* Make an array of pointers to jobs that are going to be executed,
 * and arrange them in the order of execution.
 * Also lock these jobs.
 */
{
    job_rec *j;

    j = first_job_rec;
    njobs = 0;
    while (j != NULL)
    {
	if (j->arg_num != -1 && (update_only || consider_job(j)))
	{
	    njobs++;
	    obstack_grow(&tab_o, &j, sizeof(j));
	}
	j = j->next;
    }
    job_array = obstack_finish(&tab_o);

    /* sort the jobs */
    qsort(job_array, njobs, sizeof(*job_array),
	  (int (*)(const void *, const void *))execution_order);
}
