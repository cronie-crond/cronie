SUBDIRS = src man
if ANACRON
SUBDIRS += anacron
endif

if PAM
pamdir = $(sysconfdir)/pam.d
dist_pam_DATA = pam/crond
endif

dist_noinst_HEADERS = \
	cronie_common.h

EXTRA_DIST = \
	cronie.init \
	crond.sysconfig \
	contrib/anacrontab \
	contrib/0anacron \
	contrib/0hourly \
	contrib/dailyjobs \
	contrib/cronie.systemd
