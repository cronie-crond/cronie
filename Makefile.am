SUBDIRS = src man anacron

if PAM
pamdir = $(sysconfdir)/pam.d
dist_pam_DATA = pam/crond
endif

EXTRA_DIST = cronie.init crond.sysconfig contrib/regularly-jobs

if ANACRON
	SUBDIRS += anacron
	EXTRA_DIST += contrib/0anacron contrib/0hourly
endif
