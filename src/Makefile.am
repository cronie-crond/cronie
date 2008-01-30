# Makefile.am - two binaries crond and crontab

sbin_PROGRAMS = crond
bin_PROGRAMS = crontab

crond_SOURCES = \
	cron.c database.c user.c job.c do_command.c popen.c \
	$(common_src)
crontab_SOURCES = crontab.c $(common_src)
common_src = entry.c env.c misc.c pw_dup.c security.c cron.h \
	externs.h funcs.h globals.h macros.h pathnames.h structs.h \
	bitstring.h


LDADD = $(LIBSELINUX) $(LIBPAM) $(LIBAUDIT)

## if DEBUG
## noinst_PROGRAMS = debug
## endif
