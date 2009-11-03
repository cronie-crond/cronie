SUBDIRS = src man
if ANACRON
SUBDIRS += anacron
endif

if PAM
pamdir = $(sysconfdir)/pam.d
dist_pam_DATA = pam/crond
endif

EXTRA_DIST = cronie.init crond.sysconfig contrib/anacrontab \
			contrib/0anacron contrib/0hourly \
			contrib/dailyjobs
