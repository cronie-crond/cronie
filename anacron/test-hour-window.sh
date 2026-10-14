#!/bin/sh
# Regression test for cronie-crond/cronie#219.
set -eu
export TZ=UTC

anacron_bin="$(pwd)/anacron/anacron"
if [ ! -x "$anacron_bin" ]; then
    echo "Missing anacron executable: $anacron_bin" >&2
    exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/anacron-hours.XXXXXXXX")
trap 'rm -rf "$tmp"' 0
trap 'exit 1' 1 2 3 15
mkdir "$tmp/spool"

# Pick an execution window at least two hours away from the current hour.
# Keeping the window far away makes hour-boundary runs stable.
case "$(date -u +%H)" in
    19|20|21|22|23) blocked_hours='08-10' ;;
    *) blocked_hours='21-23' ;;
esac

cat > "$tmp/anacrontab" <<EOT
START_HOURS_RANGE=$blocked_hours
RANDOM_DELAY=0
NO_MAIL_OUTPUT=1
1 0 regression /usr/bin/touch $tmp/ran
EOT

run_anacron() {
    timeout 30 "$anacron_bin" -d -t "$tmp/anacrontab" -S "$tmp/spool" "$@"
}

# New job: no timestamp yet. It must not run outside the hours range.
run_anacron
[ ! -e "$tmp/ran" ] || { echo 'Missing timestamp bypassed hours range' >&2; exit 1; }

# Incomplete timestamp: the hours limit must still apply.
printf 2026 > "$tmp/spool/regression"
run_anacron
[ ! -e "$tmp/ran" ] || { echo 'Short timestamp bypassed hours range' >&2; exit 1; }

# Complete previous-run timestamp: existing behavior must remain intact.
printf '20000101\n' > "$tmp/spool/regression"
run_anacron
[ ! -e "$tmp/ran" ] || { echo 'Complete timestamp bypassed hours range' >&2; exit 1; }

# Explicit -f / -n bypass semantics must remain unchanged.
rm -f "$tmp/spool/regression"
run_anacron -f
[ -e "$tmp/ran" ] || { echo '-f did not bypass hours range' >&2; exit 1; }
rm -f "$tmp/spool/regression" "$tmp/ran"
run_anacron -n
[ -e "$tmp/ran" ] || { echo '-n did not bypass hours range' >&2; exit 1; }
