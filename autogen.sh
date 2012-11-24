#!/bin/bash
#
# Generate autotool stuff, when code is checked out from SCM.

SCRIPT_INVOCATION_SHORT_NAME=${0##*/}
set -e # exit on errors
# trap ERR is bashism, do not change shebang!
trap 'echo "${SCRIPT_INVOCATION_SHORT_NAME}: exit on error"; exit 1' ERR
set -o pipefail # make pipe writer failure to cause exit on error

msg() {
	echo "${SCRIPT_INVOCATION_SHORT_NAME}: ${@}"
}

test -f src/cron.c || {
	msg "You must run this script in the top-level cronie directory"
	exit 1
}

rm -rf autom4te.cache

aclocal $AL_OPTS
autoconf $AC_OPTS
autoheader $AH_OPTS
automake --add-missing $AM_OPTS

msg "Now type './configure' and 'make' to compile."

exit 0
