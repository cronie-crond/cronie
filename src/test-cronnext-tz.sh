#!/bin/sh
# Regression test for cronie-crond/cronie#222.
set -eu
export TZ=UTC

cronnext_bin="$(pwd)/src/cronnext"
if [ ! -x "$cronnext_bin" ]; then
    echo "Missing cronnext executable: $cronnext_bin" >&2
    exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cronnext-tz.XXXXXXXX")
trap 'rm -rf "$tmp"' 0
trap 'exit 1' 1 2 3 15

# 2026-01-01 00:00:00 UTC; noon in Tokyo that day is 03:00 UTC.
printf 'CRON_TZ=Asia/Tokyo\n0 12 * * * echo x\n' > "$tmp/tokyo.cron"
next=$("$cronnext_bin" -t 1767225600 "$tmp/tokyo.cron")
[ "$next" = 'next: 1767236400' ] || { echo "CRON_TZ ignored: $next" >&2; exit 1; }
