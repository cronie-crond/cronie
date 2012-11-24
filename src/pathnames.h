/* Copyright 1993,1994 by Paul Vixie
 * All rights reserved
 */

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * $Id: pathnames.h,v 1.9 2004/01/23 18:56:43 vixie Exp $
 */

#ifndef _PATHNAMES_H_
#define _PATHNAMES_H_

#if (defined(BSD)) && (BSD >= 199103) || defined(__linux) || defined(AIX)
# include <paths.h>
#endif /*BSD*/

#include "cron-paths.h"

			/* where should the daemon stick its PID?
			 * PIDDIR must end in '/'.
			 * (Don't ask why the default is "/etc/".)
			 */
#ifdef _PATH_VARRUN
# define PIDDIR	_PATH_VARRUN
#else
# define PIDDIR SYSCONFDIR "/"
#endif
#define PIDFILE		"crond.pid"
#define _PATH_CRON_PID	PIDDIR PIDFILE
#define REBOOT_LOCK     PIDDIR "cron.reboot"

#ifndef _PATH_BSHELL
# define _PATH_BSHELL "/bin/sh"
#endif

#ifndef _PATH_DEFPATH
# define _PATH_DEFPATH "/usr/bin:/bin"
#endif

#ifndef _PATH_TMP
# define _PATH_TMP "/tmp"
#endif

#ifndef _PATH_DEVNULL
# define _PATH_DEVNULL "/dev/null"
#endif

#endif /* _PATHNAMES_H_ */
