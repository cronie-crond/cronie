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

/* vix 26jan87 [RCS has the log]
 */

#include <cron.h>

#define TMAX(a,b) ((a)>(b)?(a):(b))

/* size of the event structure, not counting name */
#define EVENT_SIZE  (sizeof (struct inotify_event))

/* reasonable guess as to size of 1024 events */
#define BUF_LEN 	(1024 * (EVENT_SIZE + 16))

static void overwrite_database(cron_db *, cron_db *);

static void process_crontab(const char *, const char *,
	const char *, cron_db *, cron_db *);

static int not_a_crontab(DIR_T * dp);
/* return 1 if we should skip this file */

static void max_mtime(char *dir_name, struct stat *max_st);
/* record max mtime of any file under dir_name in max_st */

static int
check_open(const char *tabname, const char *fname, const char *uname,
	struct passwd *pw, time_t * mtime) {
	struct stat statbuf;
	int crontab_fd;
	pid_t pid = getpid();

	if ((crontab_fd =
			open(tabname, O_RDONLY | O_NONBLOCK, 0)) == -1) {
		log_it(uname, pid, "CAN'T OPEN", tabname, errno);
		return (-1);
	}
	if (fstat(crontab_fd, &statbuf) < OK) {
		log_it(uname, pid, "STAT FAILED", tabname, errno);
		close(crontab_fd);
		return (-1);
	}
	*mtime = statbuf.st_mtime;
	if (PermitAnyCrontab == 0) {
		if (!S_ISREG(statbuf.st_mode)) {
			log_it(uname, pid, "NOT REGULAR", tabname, 0);
			close(crontab_fd);
			return (-1);
		}
		if ((statbuf.st_mode & 07533) != 0400) {
			log_it(uname, pid, "BAD FILE MODE", tabname, 0);
			close(crontab_fd);
			return (-1);
		}
		if (statbuf.st_uid != ROOT_UID && (pw == NULL ||
				statbuf.st_uid != pw->pw_uid ||
				strcmp(uname, pw->pw_name) != 0)) {
			log_it(uname, pid, "WRONG FILE OWNER", tabname, 0);
			close(crontab_fd);
			return (-1);
		}
		if (pw && statbuf.st_nlink != 1) {
			log_it(uname, pid, "BAD LINK COUNT", tabname, 0);
			close(crontab_fd);
			return (-1);
		}
	}
	return (crontab_fd);
}

static void
process_crontab(const char *uname, const char *fname, const char *tabname,
	cron_db * new_db, cron_db * old_db) {
	struct passwd *pw = NULL;
	int crontab_fd = -1;
	user *u;
	time_t mtime;
	int crond_crontab = (fname == NULL) && (strcmp(tabname, SYSCRONTAB) != 0);

	if (fname == NULL) {
		/* must be set to something for logging purposes.
		 */
		fname = "*system*";
	}
	else if ((pw = getpwnam(uname)) == NULL) {
		/* file doesn't have a user in passwd file.
		 */
		log_it(uname, getpid(), "ORPHAN", "no passwd entry", 0);
		goto next_crontab;
	}

	if ((crontab_fd = check_open(tabname, fname, uname, pw, &mtime)) == -1)
		goto next_crontab;

	Debug(DLOAD, ("\t%s:", fname))

		u = find_user(old_db, fname, crond_crontab ? tabname : NULL);	/* find user in old_db */

	if (u != NULL) {
		/* if crontab has not changed since we last read it
		 * in, then we can just use our existing entry.
		 */
		if (u->mtime == mtime) {
			Debug(DLOAD, (" [no change, using old data]"))
				unlink_user(old_db, u);
			link_user(new_db, u);
			goto next_crontab;
		}

		/* before we fall through to the code that will reload
		 * the user, let's deallocate and unlink the user in
		 * the old database.  This is more a point of memory
		 * efficiency than anything else, since all leftover
		 * users will be deleted from the old database when
		 * we finish with the crontab...
		 */
		Debug(DLOAD, (" [delete old data]"))
			unlink_user(old_db, u);
		free_user(u);
		log_it(fname, getpid(), "RELOAD", tabname, 0);
	}

	u = load_user(crontab_fd, pw, uname, fname, tabname);	/* read the file */
	if (u != NULL) {
		u->mtime = mtime;
		link_user(new_db, u);
	}

  next_crontab:
	if (crontab_fd != -1) {
		Debug(DLOAD, (" [done]\n"))
			close(crontab_fd);
	}
}

#if defined WITH_INOTIFY
void check_inotify_database(cron_db * old_db) {
	cron_db new_db;
	DIR_T *dp;
	DIR *dir;
	struct timeval time;
	fd_set rfds;
	int retval = 0;
	char buf[BUF_LEN];
	pid_t pid = getpid();

	time.tv_sec = 0;
	time.tv_usec = 0;

	FD_ZERO(&rfds);
	FD_SET(old_db->ifd, &rfds);

	retval = select(old_db->ifd + 1, &rfds, NULL, NULL, &time);
	if (retval == -1) {
		if (errno != EINTR)
			log_it("CRON", pid, "INOTIFY", "select failed", errno);
		return;
	}
	else if (FD_ISSET(old_db->ifd, &rfds)) {
		new_db.head = new_db.tail = NULL;
		new_db.ifd = old_db->ifd;
		while ((retval = read(old_db->ifd, buf, sizeof (buf))) == -1 &&
			errno == EINTR) ;

		if (retval == 0) {
			/* this should not happen as the buffer is large enough */
			errno = ENOMEM;
		}

		if (retval <= 0) {
			log_it("CRON", pid, "INOTIFY", "read failed", errno);
			/* something fatal must have occured we have no other reasonable
			 * way how to handle this failure than exit.
			 */
			(void) exit(ERROR_EXIT);
		}

		/* we must reinstate the watches here - TODO reinstate only watches
		 * which get IN_IGNORED event
		 */
		set_cron_watched(old_db->ifd);

		/* TODO: parse the events and read only affected files */

		process_crontab("root", NULL, SYSCRONTAB, &new_db, old_db);

		if (!(dir = opendir(SYS_CROND_DIR))) {
			log_it("CRON", pid, "OPENDIR FAILED", SYS_CROND_DIR, errno);
		}
		else {
			while (NULL != (dp = readdir(dir))) {
				char tabname[MAXNAMLEN + 1];

				if (not_a_crontab(dp))
					continue;

				if (!glue_strings(tabname, sizeof tabname, SYS_CROND_DIR,
						dp->d_name, '/'))
					continue;
				process_crontab("root", NULL, tabname, &new_db, old_db);
			}
			closedir(dir);
		}

		if (!(dir = opendir(SPOOL_DIR))) {
			log_it("CRON", pid, "OPENDIR FAILED", SPOOL_DIR, errno);
		}
		else {
			while (NULL != (dp = readdir(dir))) {
				char fname[MAXNAMLEN + 1], tabname[MAXNAMLEN + 1];

				if (not_a_crontab(dp))
					continue;

				strncpy(fname, dp->d_name, MAXNAMLEN);

				if (!glue_strings(tabname, sizeof tabname, SPOOL_DIR,
						dp->d_name, '/'))
					continue;
				process_crontab(fname, fname, tabname, &new_db, old_db);
			}
			closedir(dir);
		}

		/* if we don't do this, then when our children eventually call
		 * getpwnam() in do_command.c's child_process to verify MAILTO=,
		 * they will screw us up (and v-v).
		 */
		endpwent();
	}
	else {
		/* just return as no db reload is needed */
		return;
	}

	overwrite_database(old_db, &new_db);
	Debug(DLOAD, ("check_inotify_database is done\n"))
}
#endif

static void overwrite_database(cron_db * old_db, cron_db * new_db) {
	user *u, *nu;
	/* whatever's left in the old database is now junk.
	 */
	Debug(DLOAD, ("unlinking old database:\n"))
		for (u = old_db->head; u != NULL; u = nu) {
		Debug(DLOAD, ("\t%s\n", u->name))
			nu = u->next;
		unlink_user(old_db, u);
		free_user(u);
	}

	/* overwrite the database control block with the new one.
	 */
	*old_db = *new_db;
}

int load_database(cron_db * old_db) {
	struct stat statbuf, syscron_stat, crond_stat;
	cron_db new_db;
	DIR_T *dp;
	DIR *dir;
	pid_t pid = getpid();

	Debug(DLOAD, ("[%ld] load_database()\n", (long) pid))

		/* before we start loading any data, do a stat on SPOOL_DIR
		 * so that if anything changes as of this moment (i.e., before we've
		 * cached any of the database), we'll see the changes next time.
		 */
		if (stat(SPOOL_DIR, &statbuf) < OK) {
		log_it("CRON", pid, "STAT FAILED", SPOOL_DIR, errno);
		statbuf.st_mtime = 0;
	}
	else {
		/* As pointed out in Red Hat bugzilla 198019, with modern Linux it
		 * is possible to modify a file without modifying the mtime of the
		 * containing directory. Hence, we must check the mtime of each file:
		 */
		max_mtime(SPOOL_DIR, &statbuf);
	}

	if (stat(SYS_CROND_DIR, &crond_stat) < OK) {
		log_it("CRON", pid, "STAT FAILED", SYS_CROND_DIR, errno);
		crond_stat.st_mtime = 0;
	}
	else {
		max_mtime(SYS_CROND_DIR, &crond_stat);
	}

	/* track system crontab file
	 */
	if (stat(SYSCRONTAB, &syscron_stat) < OK)
		syscron_stat.st_mtime = 0;

	/* if spooldir's mtime has not changed, we don't need to fiddle with
	 * the database.
	 *
	 * Note that old_db->mtime is initialized to 0 in main(), and
	 * so is guaranteed to be different than the stat() mtime the first
	 * time this function is called.
	 */
	if (old_db->mtime == TMAX(crond_stat.st_mtime,
			TMAX(statbuf.st_mtime, syscron_stat.st_mtime))
		) {
		Debug(DLOAD, ("[%ld] spool dir mtime unch, no load needed.\n",
				(long) pid))
			return 0;
	}

	/* something's different.  make a new database, moving unchanged
	 * elements from the old database, reloading elements that have
	 * actually changed.  Whatever is left in the old database when
	 * we're done is chaff -- crontabs that disappeared.
	 */
	new_db.mtime = TMAX(crond_stat.st_mtime,
		TMAX(statbuf.st_mtime, syscron_stat.st_mtime));
	new_db.head = new_db.tail = NULL;
#if defined WITH_INOTIFY
	new_db.ifd = old_db->ifd;
#endif

	if (syscron_stat.st_mtime)
		process_crontab("root", NULL, SYSCRONTAB, &new_db, old_db);

	if (!(dir = opendir(SYS_CROND_DIR))) {
		log_it("CRON", pid, "OPENDIR FAILED", SYS_CROND_DIR, errno);
	}
	else {
		while (NULL != (dp = readdir(dir))) {
			char tabname[MAXNAMLEN + 1];

			if (not_a_crontab(dp))
				continue;

			if (!glue_strings(tabname, sizeof tabname, SYS_CROND_DIR,
					dp->d_name, '/'))
				continue;	/* XXX log? */

			process_crontab("root", NULL, tabname, &new_db, old_db);
		}
		closedir(dir);
	}

	/* we used to keep this dir open all the time, for the sake of
	 * efficiency.  however, we need to close it in every fork, and
	 * we fork a lot more often than the mtime of the dir changes.
	 */

	if (!(dir = opendir(SPOOL_DIR))) {
		log_it("CRON", pid, "OPENDIR FAILED", SPOOL_DIR, errno);
	}
	else {
		while (NULL != (dp = readdir(dir))) {
			char fname[MAXNAMLEN + 1], tabname[MAXNAMLEN + 1];

			if (not_a_crontab(dp))
				continue;

			strncpy(fname, dp->d_name, MAXNAMLEN);

			if (!glue_strings(tabname, sizeof tabname, SPOOL_DIR, fname, '/'))
				continue;	/* XXX log? */

			process_crontab(fname, fname, tabname, &new_db, old_db);
		}
		closedir(dir);
	}

	/* if we don't do this, then when our children eventually call
	 * getpwnam() in do_command.c's child_process to verify MAILTO=,
	 * they will screw us up (and v-v).
	 */
	endpwent();

	overwrite_database(old_db, &new_db);
	Debug(DLOAD, ("load_database is done\n"))
		return 1;
}

void link_user(cron_db * db, user * u) {
	if (db->head == NULL)
		db->head = u;
	if (db->tail)
		db->tail->next = u;
	u->prev = db->tail;
	u->next = NULL;
	db->tail = u;
}

void unlink_user(cron_db * db, user * u) {
	if (u->prev == NULL)
		db->head = u->next;
	else
		u->prev->next = u->next;

	if (u->next == NULL)
		db->tail = u->prev;
	else
		u->next->prev = u->prev;
}

user *find_user(cron_db * db, const char *name, const char *tabname) {
	user *u;

	for (u = db->head; u != NULL; u = u->next)
		if ((strcmp(u->name, name) == 0)
			&& ((tabname == NULL)
				|| (strcmp(tabname, u->tabname) == 0)
			)
			)
			break;
	return (u);
}

static int not_a_crontab(DIR_T * dp) {
	int len;

	/* avoid file names beginning with ".".  this is good
	 * because we would otherwise waste two guaranteed calls
	 * to getpwnam() for . and .., and there shouldn't be 
	 * hidden files in here anyway
	 */
	if (dp->d_name[0] == '.')
		return (1);

	/* ignore files starting with # and ending with ~ */
	if (dp->d_name[0] == '#')
		return (1);

	len = strlen(dp->d_name);

	if (len >= MAXNAMLEN)
		return (1);	/* XXX log? */

	if ((len > 0) && (dp->d_name[len - 1] == '~'))
		return (1);

	if ((len > 8) && (strncmp(dp->d_name + len - 8, ".rpmsave", 8) == 0))
		return (1);
	if ((len > 8) && (strncmp(dp->d_name + len - 8, ".rpmorig", 8) == 0))
		return (1);
	if ((len > 7) && (strncmp(dp->d_name + len - 7, ".rpmnew", 7) == 0))
		return (1);

	return (0);
}

static void max_mtime(char *dir_name, struct stat *max_st) {
	DIR *dir;
	DIR_T *dp;
	struct stat st;

	if (!(dir = opendir(dir_name))) {
		max_st->st_mtime = 0;
		return;
	}

	while (NULL != (dp = readdir(dir))) {
		char tabname[MAXNAMLEN + 1];

		if (not_a_crontab(dp))
			continue;

		if (!glue_strings(tabname, sizeof tabname, dir_name, dp->d_name, '/'))
			continue;	/* XXX log? */

		if (stat(tabname, &st) < OK)
			continue;	/* XXX log? */

		if (st.st_mtime > max_st->st_mtime)
			max_st->st_mtime = st.st_mtime;
	}
	closedir(dir);
}
