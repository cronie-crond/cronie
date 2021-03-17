AM_CFLAGS = -I$(top_srcdir)

BUILT_SOURCES =
CLEANFILES =
EXTRA_DIST =
bin_PROGRAMS =
common_nodist =
sbin_PROGRAMS =

dist_noinst_HEADERS = \
	cronie_common.h

EXTRA_DIST += \
	README.anacron \
	COPYING.anacron \
	COPYING.obstack \
	obstack/obstack.h \
	cronie.init \
	crond.sysconfig \
	contrib/anacrontab \
	contrib/0anacron \
	contrib/0hourly \
	contrib/dailyjobs \
	contrib/cronie.systemd \
	anacron/ChangeLog.anacron

if PAM
pamdir = $(sysconfdir)/pam.d
dist_pam_DATA = pam/crond
else
EXTRA_DIST += pam/crond
endif

include anacron/Makemodule.am
include man/Makemodule.am
include src/Makemodule.am
