AC_INIT([vixie-cron],[4.3],[mmaslano@redhat.com])
AC_CONFIG_HEADER([config.h])
AC_PREREQ(2.7)

VERSION="4.3"

AM_INIT_AUTOMAKE(foreign)

AC_CANONICAL_HOST

dnl Checks for programs.

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S

AC_MSG_CHECKING(HP-UX -Aa)

case "$host" in
  *-*-hpux*)
    if test -z "$GCC"; then
      CFLAGS="$CFLAGS -Aa -D_HPUX_SOURCE"
    fi
    AC_MSG_RESULT(yes)
    ;;
  *)
    AC_MSG_RESULT(no)
    ;;
esac
AC_MSG_CHECKING(Trying to compile a trivial ANSI C program)
AC_TRY_RUN([ main(int ac, char **av) { return 0; } ],
    AC_MSG_RESULT(yes),
    AC_MSG_RESULT(no)
    AC_MSG_ERROR(Could not compile and run even a trivial ANSI C program - check CC.),
    AC_MSG_ERROR(Could not compile and run even a trivial ANSI C program - check CC.))

AC_CHECK_HEADERS( \
        dirent.h \
        fcntl.h \
        getopt.h \
        glob.h \
        limits.h \
        pam/pam_appl.h \
        paths.h \
        pty.h \
        security/pam_appl.h \
        selinux/selinux.h \
        stddef.h \
        stdint.h \
		sys/audit.h \
        sys/stat.h \
        sys/stream.h \
        sys/stropts.h \
		sys/time.h \
        sys/timers.h \
        sys/types.h \
        time.h \
        unistd.h \
        util.h \
        utime.h \
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


dnl Checking for programs

AC_MSG_CHECKING(etcdir)
AC_ARG_WITH(etcdir,
[  --with-etcdir=PATH      Directory containing permission files (default /etc).],
[ case "$withval" in
  no)
    AC_MSG_ERROR(Need ETCDIR.)
    ;;
  yes)
    ETCDIR=/etc
    AC_MSG_RESULT(/etc)
    ;;
  *)
    ETCDIR="$withval"
    AC_MSG_RESULT($withval)
    ;;
  esac ],
  ETCDIR="/etc"
  AC_MSG_RESULT(/etc)
)
AC_SUBST(ETCDIR)

AC_MSG_CHECKING(username to run under)
AC_ARG_WITH(daemon_username,
[ --with-daemon_username=DAEMON_USERNAME	Username to run under (default daemon) ],
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
[ --with-daemon_groupname=DAEMON_GROUPNAME	Groupname to run under (default daemon) ],
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

# Check whether user wants SELinux support
SELINUX_MSG="no"
LIBSELINUX=""
AC_ARG_WITH(selinux,
        [  --with-selinux   Enable SELinux support],
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
        
# Check whether user wants PAM support
AC_ARG_WITH(pam,
        [  --with-pam              Enable PAM support ],
        [  if test "x$withval" != "xno" ; then
                        if test "x$ac_cv_header_security_pam_appl_h" != "xyes" && \
                           test "x$ac_cv_header_pam_pam_appl_h" != "xyes" ; then
                                AC_MSG_ERROR([PAM headers not found])
                        fi

                        saved_LIBS="$LIBS"
                        AC_CHECK_LIB(dl, dlopen, , )
                        AC_CHECK_LIB(pam, pam_set_item, , AC_MSG_ERROR([*** libpam missing]))
                        AC_CHECK_FUNCS(pam_getenvlist)
                        AC_CHECK_FUNCS(pam_putenv)
                        LIBS="$saved_LIBS"

                        PAM_MSG="yes"

                        LIBPAM="-lpam -lpam_misc"
			AC_DEFINE(WITH_PAM,1,
				[Define if you want to enable PAM support])
                        if test $ac_cv_lib_dl_dlopen = yes; then
                                case "$LIBS" in
                                *-ldl*)
                                        # libdl already in LIBS
                                        ;;
                                *)
                                        LIBPAM="$LIBPAM -ldl"
                                        ;;
                                esac
                        fi
                        AC_SUBST(LIBPAM)
                fi
        ]
)

AC_DEFINE(DEBUGGING,1,[Code will be built with debug info.])

AC_DEFINE(MAILARG,"/usr/lib/sendmail",[There will be path to sendmail.])

AC_DEFINE(MAILFMT,"%s -FCronDaemon -i -odi -oem -oi -t",
[-i    = don't terminate on "." by itself
-Fx   = Set full-name of sender
-odi  = Option Deliverymode Interactive
-oem  = Option Errors Mailedtosender
-oi   = Ignore "." alone on a line
-t    = Get recipient from headers
-d = undocumented but common flag.])

AC_DEFINE(MAIL_DATE,0,[Date is set by sendmail.])

AC_DEFINE(SYSLOG,1,[Using syslog for log messages.])

AC_DEFINE(CAPITALIZE_FOR_PS,0,[if you have a tm_gmtoff member in struct tm])

AC_DEFINE(HAVE_TM_GMTOFF,0,[if your OS supports a BSD-style login.conf fil])

##AC_DEFINE(LOGIN_CAP,0,[if your OS supports BSD authentication])

##AC_DEFINE(CRON_GROUP,"crontab",[crontab])

# Check whether user wants Linux audit support
AC_ARG_WITH(audit,
        [  --with-audit   Enable audit trails],
        [ if test "x$withval" != "xno" ; then
                saved_LIBS="$LIBS"
                AC_DEFINE(WITH_AUDIT,1,[Define if you want Audit trails.])
                SELINUX_MSG="yes"
                AC_CHECK_HEADER([libaudit.h], ,AC_MSG_ERROR(Audit trails requires libaudit.h header))
                LIBS="$saved_LIBS $LIBAUDIT"
                AC_SUBST(LIBAUDIT)
        fi ]
)

AC_ARG_ENABLE(debug,
    [  --enable-debug    Turn on debugging],
    [case "${enableval}" in
    yes) debug=true ;;
    no)  debug=false ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
    esac],[debug=false])
AM_CONDITIONAL(DEBUG, test x$debug = xtrue)

pamdir=${sysconfdir}/pam.d
AC_SUBST(pamdir)

docdir=${datadir}/doc/cron/
AC_SUBST(docdir)

AC_CONFIG_FILES(Makefile src/Makefile man/Makefile \
	man/bitstring.3 man/cron.8 man/crontab.1 man/crontab.5)
AC_CONFIG_SUBDIRS([src])
AC_OUTPUT

