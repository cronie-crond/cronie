AC_INIT([cronie],[1.2],[mmaslano@redhat.com])
AC_CONFIG_HEADER([config.h])
AC_PREREQ(2.60)

AM_INIT_AUTOMAKE

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
        time.h \
        unistd.h \
        util.h \
        utime.h \
)

AC_CHECK_FUNCS( \
       fcntl \
       lockf \
       flock \
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

AC_MSG_CHECKING(sysconfdir)
AC_ARG_WITH(sysconfdir,
[AS_HELP_STRING([--with-sysconfdir=PATH], [Directory containing permission files (default /etc).])],
[ case "$withval" in
  no)
    AC_MSG_ERROR(Need SYSCONFDIR.)
    ;;
  yes)
    SYSCONFDIR=/etc
    AC_MSG_RESULT(/etc)
    ;;
  *)
    SYSCONFDIR="$withval"
    AC_MSG_RESULT($withval)
    ;;
  esac ],
  SYSCONFDIR="/etc"
  AC_MSG_RESULT(/etc)
)
AC_SUBST(SYSCONFDIR)

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

# Check whether user wants sysconfig file
#AC_ARG_WITH(config,
#	[AS_HELP_STRING([--with-sysconf], [Enable sysconfig file])],
#	[ if test "x$withval" != "xno" ; then
#		configdir=${sysconfdir}/sysconfig/
#		AC_SUBST(configdir)
#	fi ]
#)

# Check whether user wants init file
#AC_ARG_WITH(init,
#        [AS_HELP_STRING([--with-initfile], [Enable init file])],
#        [ if test "x$withval" != "xno" ; then
#		initdir=${initrddir}
#    		AC_SUBST(initdir)
#	fi ]
#)

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
                LIBS="$saved_LIBS $LIBSELINUX"
                AC_SUBST(LIBSELINUX)
        fi ]
)

AC_ARG_WITH(pam, [AS_HELP_STRING([--with-pam], [Enable PAM support])], [], [enable_pam=no])
AM_CONDITIONAL([PAM], [test "$enable_pam" != no])

if test "$enable_pam" != no; then
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
	AC_SUBST([LIBPAM], ["-lpam -lpam_misc $LIBPAM"])
fi

AC_DEFINE(DEBUGGING,1,[Code will be built with debug info.])

AC_DEFINE(MAILARG,"/usr/sbin/sendmail",[There will be path to sendmail.])

AC_DEFINE(MAILFMT,"%s -FCronDaemon -i -odi -oem -oi -t",
[-i    = don't terminate on "." by itself
-Fx   = Set full-name of sender
-odi  = Option Deliverymode Interactive
-oem  = Option Errors Mailedtosender
-oi   = Ignore "." alone on a line
-t    = Get recipient from headers
-d = undocumented but common flag.])

AC_DEFINE(SYSLOG,1,[Using syslog for log messages.])

AC_DEFINE(CAPITALIZE_FOR_PS, 1, [if you have a tm_gmtoff member in struct tm])

##AC_DEFINE(LOGIN_CAP, 1, [if your OS supports BSD authentication])

##AC_DEFINE(CRON_GROUP,"crontab",[crontab])

# Check whether user wants Linux audit support
AC_ARG_WITH(audit,
        [AS_HELP_STRING([--with-audit], [Enable audit trails])],
        [ if test "x$withval" != "xno" ; then
                saved_LIBS="$LIBS"
                AC_DEFINE(WITH_AUDIT,1,[Define if you want Audit trails.])
                SELINUX_MSG="yes"
                AC_CHECK_HEADER([libaudit.h], ,AC_MSG_ERROR(Audit trails requires libaudit.h header))
                LIBS="$saved_LIBS $LIBAUDIT"
                AC_SUBST(LIBAUDIT)
        fi ]
)

dnl AC_ARG_ENABLE(debug, [AS_HELP_STRING([--enable-debug], [Turn on debugging])])
dnl AM_CONDITIONAL(DEBUG, test "$enable_debug" = true)

AC_CONFIG_FILES([Makefile src/Makefile man/Makefile])
AC_OUTPUT

