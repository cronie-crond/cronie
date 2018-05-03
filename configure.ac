AC_INIT([cronie],[1.5.2],[mmaslano@redhat.com,tmraz@fedoraproject.org])
AC_CONFIG_HEADER([config.h])
AC_PREREQ(2.60)

AM_INIT_AUTOMAKE

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])],
			    [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

AC_CANONICAL_HOST

dnl Checks for programs.

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S

dnl Check for _GNU_SOURCE
AC_USE_SYSTEM_EXTENSIONS

AC_CHECK_HEADERS( \
        dirent.h \
        fcntl.h \
        getopt.h \
        glob.h \
        limits.h \
        paths.h \
        pty.h \
        selinux/selinux.h \
        stddef.h \
        stdint.h \
        sys/audit.h \
        sys/inotify.h \
        sys/stat.h \
        sys/stream.h \
        sys/stropts.h \
        sys/time.h \
        sys/timers.h \
        sys/types.h \
        sys/cdefs.h \
        sys/fcntl.h \
        time.h \
        unistd.h \
        util.h \
        utime.h \
)

AC_CHECK_FUNCS( \
       fcntl \
       lockf \
       flock \
       fchown \
       fchgrp \
)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIGNAL
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_STRUCT_TM
AC_CHECK_MEMBERS([struct tm.tm_gmtoff],,,[#include <time.h>])

dnl Checking for programs

AC_ARG_WITH([editor],
  [AC_HELP_STRING([--with-editor=EDITOR], [path to default editor])],
  [editor_defined="$with_editor"],
  [editor_defined="no"])
AS_IF([test "x$editor_defined" = "xno"], [
  AC_PATH_PROG([editor_defined], [vi], [/usr/bin/vi])
])
AC_DEFINE_UNQUOTED([EDITOR], ["$editor_defined"], [default editor])

AC_MSG_CHECKING(username to run under)
AC_ARG_WITH(daemon_username,
[AS_HELP_STRING([--with-daemon_username=DAEMON_USERNAME], [Username to run under (default daemon) ])],
[ case "$withval" in
  no)
    AC_MSG_ERROR(Need DAEMON_USERNAME.)
    ;;
  yes)
    DAEMON_USERNAME=daemon
    AC_MSG_RESULT(daemon)
    ;;
  *)
    DAEMON_USERNAME="$withval";
    AC_MSG_RESULT($withval)
    ;;
    esac ],
    DAEMON_USERNAME=daemon
    AC_MSG_RESULT(daemon)
)
AC_SUBST(DAEMON_USERNAME)

AC_MSG_CHECKING(groupname to run under)
AC_ARG_WITH(daemon_groupname,
[AS_HELP_STRING([--with-daemon_groupname=DAEMON_GROUPNAME], [Groupname to run under (default daemon) ])],
[ case "$withval" in
  no)
    AC_MSG_ERROR(Need DAEMON_GROUPNAME.)
    ;;
  yes)
    DAEMON_GROUPNAME=daemon
    AC_MSG_RESULT(daemon)
    ;;
  *)
    DAEMON_GROUPNAME="$withval";
    AC_MSG_RESULT($withval)
    ;;
    esac ],
    DAEMON_GROUPNAME=daemon
    AC_MSG_RESULT(daemon)
)
AC_SUBST(DAEMON_GROUPNAME)

# Check whether inotify is accepted
AC_ARG_WITH(inotify,
   [AS_HELP_STRING([--with-inotify], [ Enable inotify support])],
   [ if test "x$withval" != "xno" ; then
       AC_DEFINE(WITH_INOTIFY,1,[Define if you want inotify support.])
       AC_CHECK_HEADER([sys/inotify.h], , AC_MSG_ERROR(Inotify support requires sys/inotify.h header))
       AC_CHECK_FUNCS(inotify_init inotify_add_watch)
     fi
   ]
)

AC_ARG_ENABLE(pie,CRONIE_HELP_STRING(--enable-pie,Build cronie as a Position Independent Executable))
if test "x$enable_pie" = xyes; then
    CFLAGS="$CFLAGS -fPIE -DPIE"
    LDFLAGS="$LDFLAGS -pie"
fi

AC_ARG_ENABLE(relro,CRONIE_HELP_STRING(--enable-relro,Build cronie with relro flag))
if test "x$enable_relro" = xyes; then
    LDFLAGS="$LDFLAGS -Wl,-z,relro -Wl,-z,now"
fi

AC_ARG_ENABLE(bsd, BSD_STRING(--enable-bsd,Build cronie with BSD specific parts))

# Check whether user wants SELinux support
SELINUX_MSG="no"
LIBSELINUX=""
AC_ARG_WITH(selinux,
        [AS_HELP_STRING([--with-selinux], [Enable SELinux support])],
        [ if test "x$withval" != "xno" ; then
                saved_LIBS="$LIBS"
                AC_DEFINE(WITH_SELINUX,1,[Define if you want SELinux support.])
                SELINUX_MSG="yes"
                AC_CHECK_HEADER([selinux/selinux.h], ,AC_MSG_ERROR(SELinux support requires selinux.h header))
                AC_CHECK_LIB(selinux, setexeccon, [ LIBSELINUX="-lselinux" ],
                    AC_MSG_ERROR(SELinux support requires libselinux library))
                AC_CHECK_FUNCS(getseuserbyname get_default_context_with_level)
                LIBS="$saved_LIBS"
                AC_SUBST(LIBSELINUX)
        fi ]
)

AC_ARG_WITH(pam, [AS_HELP_STRING([--with-pam], [Build with PAM support])])
AC_ARG_ENABLE(pam, [AS_HELP_STRING([--enable-pam], [Alias for --with-pam])])

# Check that with_pam and enable_pam are consistent.
# If neither one is set, the default is "no."
if test -z "$with_pam"; then
	with_pam=${enable_pam:-no}
elif test -n "$enable_pam" && test "$with_pam" != "$enable_pam"; then
	AC_MSG_ERROR(
	  [Contradicting --with/without-pam and --enable/disable-pam options.])
fi

AM_CONDITIONAL([PAM], [test "$with_pam" != no])

if test "$with_pam" != no; then
	AC_DEFINE(WITH_PAM, 1, [Define if you want to enable PAM support])
	pam_appl_h_found=no
	AC_CHECK_HEADERS([pam/pam_appl.h security/pam_appl.h],
		[pam_appl_h_found=yes])
	test "$pam_appl_h_found" = yes ||
		AC_MSG_ERROR([PAM headers not found])

	saved_LIBS="$LIBS"
	AC_CHECK_LIB([dl], [dlopen], [libdl_found=yes], [libdl_found=no])
	AC_CHECK_LIB(pam, pam_set_item, , AC_MSG_ERROR([*** libpam missing]))
	AC_CHECK_FUNCS([pam_getenvlist pam_putenv])
	LIBS="$saved_LIBS"

	case $libdl_found:" $LIBS " in #(
	*" -ldl "*) LIBPAM= ;; #(
	yes:*) LIBPAM=-ldl ;; # libdl found, but is not in $LIBS
	esac
	AC_SUBST([LIBPAM], ["-lpam $LIBPAM"])
fi

AC_DEFINE(DEBUGGING,1,[Code will be built with debug info.])

AC_DEFINE(MAILARG,"/usr/sbin/sendmail",[There will be path to sendmail.])

AC_DEFINE(MAILFMT,"%s -FCronDaemon -i -odi -oem -oi -t -f %s",
[-i    = don't terminate on "." by itself
-Fx   = Set full-name of sender
-odi  = Option Deliverymode Interactive
-oem  = Option Errors Mailedtosender
-oi   = Ignore "." alone on a line
-t    = Get recipient from headers
-f %s = Envelope sender address
-d = undocumented but common flag.])

AC_DEFINE(SYSLOG,1,[Using syslog for log messages.])

AC_DEFINE(CAPITALIZE_FOR_PS, 1, [if you have a tm_gmtoff member in struct tm])

# Check whether user wants Linux audit support
AC_ARG_WITH(audit,
        [AS_HELP_STRING([--with-audit], [Enable audit trails])],
        [ if test "x$withval" != "xno" ; then
                saved_LIBS="$LIBS"
                AC_DEFINE(WITH_AUDIT,1,[Define if you want Audit trails.])
                AC_CHECK_HEADER([libaudit.h], ,AC_MSG_ERROR(Audit trails requires libaudit.h header))
                AC_CHECK_LIB(audit, audit_open, [ LIBAUDIT="-laudit" ],
                    AC_MSG_ERROR(Audit support needs audit libraries.))
                LIBS="$saved_LIBS"
                AC_SUBST(LIBAUDIT)
        fi ]
)

AC_ARG_ENABLE(syscrontab,
	[AS_HELP_STRING([--enable-syscrontab], [Build cronie with system crontab enabled.])],
	[ if test "x$enableval" != xno; then
		AC_DEFINE(ENABLE_SYSCRONTAB,1,[Define if you want system crontab.])
	fi ], [AC_DEFINE(ENABLE_SYSCRONTAB,1,[Define if you want system crontab.])]
)

dnl CRONIE_VAR_DEFAULT (VAR, DESCRIPTION, DEFAULT)
dnl --------------------------------------------
AC_DEFUN([CRONIE_CONF_VAR],
[AC_ARG_VAR([$1], [$2 @<:@$3@:>@])
if test "$$1" = ""; then
	$1='$3'
fi
])

AC_DEFUN([ANACRON_CONF_VAR],
[AC_ARG_VAR([$1], [$2 @<:@$3@:>@])
if test "$$1" = ""; then
	    $1='$3'
		fi
])

CRONIE_CONF_VAR([SYSCRONTAB], [the current working directory of the running daemon], [${sysconfdir}/crontab])
CRONIE_CONF_VAR([SYS_CROND_DIR], [the current working directory of the running daemon], [${sysconfdir}/cron.d])
CRONIE_CONF_VAR([SPOOL_DIR], [the directory where all the user cron tabs reside], [${localstatedir}/spool/cron])

AC_ARG_ENABLE([anacron], [AS_HELP_STRING([--enable-anacron], [Build also anacron.])])
AM_CONDITIONAL([ANACRON], [test "$enable_anacron" = yes])
if test "$enable_anacron" != no; then
	ANACRON_CONF_VAR([ANACRON_SPOOL_DIR],[The path for anacron locks.],[${localstatedir}/spool/anacron])
	ANACRON_CONF_VAR([ANACRONTAB],[The anacron table for regular jobs.],[${sysconfdir}/anacrontab])
fi

AC_CONFIG_FILES([Makefile src/Makefile man/Makefile anacron/Makefile])
AC_OUTPUT

