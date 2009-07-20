SUBDIRS = src man
if ANACRON
SUBDIRS += anacron
endif

if PAM
pamdir = $(sysconfdir)/pam.d
dist_pam_DATA = pam/crond
endif

EXTRA_DIST = cronie.init crond.sysconfig contrib/regularly-jobs \
			contrib/0anacron contrib/0hourly
