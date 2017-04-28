# Makefile.am - two binaries crond and crontab

sbin_PROGRAMS = crond
bin_PROGRAMS = crontab cronnext

crond_SOURCES = \
	cron.c database.c user.c job.c do_command.c popen.c security.c \
	$(common_src)
crontab_SOURCES = crontab.c security.c $(common_src)
cronnext_SOURCES = \
	cronnext.c database.c user.c job.c \
	$(common_src)
common_src = entry.c env.c misc.c pw_dup.c \
	externs.h funcs.h globals.h macros.h pathnames.h structs.h \
	bitstring.h
common_nodist = cron-paths.h
nodist_crond_SOURCES = $(common_nodist)
nodist_crontab_SOURCES = $(common_nodist)
nodist_cronnext_SOURCES = $(common_nodist)
BUILT_SOURCES = $(common_nodist)

AM_CFLAGS = -I$(top_srcdir)

LDADD = $(LIBSELINUX) $(LIBPAM) $(LIBAUDIT)

## if DEBUG
## noinst_PROGRAMS = debug
## endif

# This header contains all the paths.
# If they are configurable, they are declared in configure script.
# Depends on this Makefile, because it uses make variables.
# CCD 2010/09/10 added CRON_HOSTNAME for clustered-cron.
CLEANFILES = cron-paths.h
cron-paths.h: Makefile
	@echo 'creating $@'
	@sed >$@ 's/ *\\$$//' <<\END #\
	/* This file has been automatically generated.  Do not edit. */ \
	\
	#ifndef _CRON_PATHS_H_ \
	#define _CRON_PATHS_H_ \
	\
			/* SPOOLDIR is where the crontabs live. \
			 * This directory will have its modtime updated \
			 * whenever crontab(1) changes a crontab; this is \
			 * the signal for cron(8) to look at each individual \
			 * crontab file and reload those whose modtimes are \
			 * newer than they were last time around (or which \
			 * didn't exist last time around...) \
			 * or it will be checked by inotify \
			 */ \
	#define SPOOL_DIR	"$(SPOOL_DIR)" \
	\
			/* CRON_HOSTNAME is file in SPOOL_DIR which, if it \
			 * exists, and does not just contain a line matching \
			 * the name returned by gethostname(), causes all \
			 * crontabs in SPOOL_DIR to be ignored.  This is \
			 * intended to be used when clustering hosts sharing \
			 * one NFS-mounted SPOOL_DIR, and where only one host \
			 * should use the crontab files here at any one time. \
			 */ \
	#define CRON_HOSTNAME	".cron.hostname" \
	\
			/* cron allow/deny file.  At least cron.deny must \
			 * exist for ordinary users to run crontab. \
			 */ \
	#define	CRON_ALLOW	"$(sysconfdir)/cron.allow" \
	#define	CRON_DENY	"$(sysconfdir)/cron.deny" \
	\
			/* 4.3BSD-style crontab f.e. /etc/crontab */ \
	#define SYSCRONTAB	"$(SYSCRONTAB)" \
	\
			/* system crontab dir f.e. /etc/cron.d/ */  \
	#define SYS_CROND_DIR	"$(SYS_CROND_DIR)" \
	\
	#define SYSCONFDIR	"$(sysconfdir)" \
	\
	#endif /* _CRON_PATHS_H_ */ \
	END
