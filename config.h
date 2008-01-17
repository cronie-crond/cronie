/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* if you have a tm_gmtoff member in struct tm */
#define CAPITALIZE_FOR_PS 0

/* Code will be built with debug info. */
#define DEBUGGING 1

/* Define to 1 if you have the <dirent.h> header file. */
#define HAVE_DIRENT_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the <getopt.h> header file. */
#define HAVE_GETOPT_H 1

/* Define to 1 if you have the `getseuserbyname' function. */
/* #undef HAVE_GETSEUSERBYNAME */

/* Define to 1 if you have the `get_default_context_with_level' function. */
/* #undef HAVE_GET_DEFAULT_CONTEXT_WITH_LEVEL */

/* Define to 1 if you have the <glob.h> header file. */
#define HAVE_GLOB_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `dl' library (-ldl). */
/* #undef HAVE_LIBDL */

/* Define to 1 if you have the `pam' library (-lpam). */
/* #undef HAVE_LIBPAM */

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `pam_getenvlist' function. */
/* #undef HAVE_PAM_GETENVLIST */

/* Define to 1 if you have the <pam/pam_appl.h> header file. */
/* #undef HAVE_PAM_PAM_APPL_H */

/* Define to 1 if you have the `pam_putenv' function. */
/* #undef HAVE_PAM_PUTENV */

/* Define to 1 if you have the <paths.h> header file. */
#define HAVE_PATHS_H 1

/* Define to 1 if you have the <pty.h> header file. */
#define HAVE_PTY_H 1

/* Define to 1 if you have the <security/pam_appl.h> header file. */
#define HAVE_SECURITY_PAM_APPL_H 1

/* Define to 1 if you have the <selinux/selinux.h> header file. */
#define HAVE_SELINUX_SELINUX_H 1

/* Define to 1 if you have the <stddef.h> header file. */
#define HAVE_STDDEF_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/audit.h> header file. */
/* #undef HAVE_SYS_AUDIT_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/stream.h> header file. */
/* #undef HAVE_SYS_STREAM_H */

/* Define to 1 if you have the <sys/stropts.h> header file. */
#define HAVE_SYS_STROPTS_H 1

/* Define to 1 if you have the <sys/timers.h> header file. */
/* #undef HAVE_SYS_TIMERS_H */

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <time.h> header file. */
#define HAVE_TIME_H 1

/* if your OS supports a BSD-style login.conf fil */
#define HAVE_TM_GMTOFF 0

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <util.h> header file. */
/* #undef HAVE_UTIL_H */

/* Define to 1 if you have the <utime.h> header file. */
#define HAVE_UTIME_H 1

/* There will be path to sendmail. */
#define MAILARG "/usr/sbin/sendmail"

/* -i = don't terminate on "." by itself -Fx = Set full-name of sender -odi =
   Option Deliverymode Interactive -oem = Option Errors Mailedtosender -oi =
   Ignore "." alone on a line -t = Get recipient from headers -d =
   undocumented but common flag. */
#define MAILFMT "%s -FCronDaemon -i -odi -oem -oi -t"

/* Name of package */
#define PACKAGE "cronie"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "mmaslano@redhat.com"

/* Define to the full name of this package. */
#define PACKAGE_NAME "cronie"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "cronie 1.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "cronie"

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.0"

/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Using syslog for log messages. */
#define SYSLOG 1

/* Define to 1 if your <sys/time.h> declares `struct tm'. */
/* #undef TM_IN_SYS_TIME */

/* Version number of package */
#define VERSION "1.0"

/* Define if you want Audit trails. */
/* #undef WITH_AUDIT */

/* Define if you want to enable PAM support */
/* #undef WITH_PAM */

/* Define if you want SELinux support. */
/* #undef WITH_SELINUX */

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `int' if <sys/types.h> doesn't define. */
/* #undef gid_t */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef mode_t */

/* Define to `long int' if <sys/types.h> does not define. */
/* #undef off_t */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef pid_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Define to `int' if <sys/types.h> doesn't define. */
/* #undef uid_t */
