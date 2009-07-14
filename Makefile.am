SUBDIRS = src man anacron

if PAM
pamdir = $(sysconfdir)/pam.d
dist_pam_DATA = pam/crond
endif

#config_DATA = crond.sysconfig

#init_SCRIPTS = cronie.init

EXTRA_DIST = cronie.init crond.sysconfig regularly-jobs

