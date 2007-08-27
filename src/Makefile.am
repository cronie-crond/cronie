# Makefile.am - two binaries crond and crontab

sbin_PROGRAMS = crond
bin_PROGRAMS = crontab

pam_DATA = ../crond.pam

crond_SOURCES = \
	cron.c database.c user.c entry.c job.c do_command.c \
	misc.c env.c popen.c pw_dup.c security.c

crontab_SOURCES = \
	crontab.c misc.c entry.c env.c pw_dup.c security.c

crontab_LDADD = \
	-lselinux -lpam -lpam_misc -laudit

crond_LDADD = \
	-lselinux -lpam -lpam_misc -laudit

crond_LDFLAGS = \
	-fpie

if DEBUG
DBG = debug
else
DBG =
endif
noinst_PROGRAMS = $(DBG)
