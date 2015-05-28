# Makefile.am - two binaries crond and crontab
if ANACRON
sbin_PROGRAMS = anacron
endif

anacron_SOURCES = \
  gregor.c lock.c log.c main.c matchrx.c readtab.c runjob.c \
  $(common_src)
common_src = global.h gregor.h matchrx.h
common_nodist = anacron-paths.h
nodist_anacron_SOURCES = $(common_nodist)
BUILT_SOURCES = $(common_nodist)

AM_CFLAGS = -I$(top_srcdir)

LDADD = $(LIBSELINUX) $(LIBPAM) $(LIBAUDIT)

# This header contains all the paths.
# If they are configurable, they are declared in configure script.
# Depends on this Makefile, because it uses make variables.
anacron-paths.h: Makefile
	@echo 'creating $@'
	@sed >$@ 's/ *\\$$//' <<\END #\
	/* This file has been automatically generated.  Do not edit. */ \
	\
	#ifndef _ANACRON_PATHS_H_ \
	#define _ANACRON_PATHS_H_ \
	#define ANACRON_SPOOL_DIR "$(ANACRON_SPOOL_DIR)" \
	#define ANACRONTAB "$(ANACRONTAB)" \
	#endif /* _ANACRON_PATHS_H_ */ \
	END
