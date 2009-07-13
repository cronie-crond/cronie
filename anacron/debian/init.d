#! /bin/sh
# /etc/init.d/anacron: start anacron
#

PATH=/bin:/usr/bin:/sbin:/usr/sbin

test -x /usr/sbin/anacron || exit 0

. /lib/lsb/init-functions

case "$1" in
  start)
    log_daemon_msg "Starting anac(h)ronistic cron" "anacron"
    if test -x /usr/bin/on_ac_power 
    then
        /usr/bin/on_ac_power >/dev/null
        if test $? -eq 1
        then
          log_progress_msg "deferred while on battery power."
	  log_end_msg 0
	  exit 0
        fi
    fi

    # on_ac_power doesn't exist, on_ac_power returns 0 (ac power being used)
    # or on_ac_power returns 255 (undefined, desktop machine without APM)
    start-stop-daemon --start --exec /usr/sbin/anacron -- -s
    log_end_msg 0
    ;;
  restart|force-reload)
	# nothing to do
    :
    ;;
  stop)
    log_daemon_msg "Stopping anac(h)ronistic cron" "anacron"
    start-stop-daemon --stop --exec /usr/sbin/anacron --oknodo --quiet
    log_end_msg 0
    ;;
  status)
    exit 4
    ;;
  *)
    echo "Usage: /etc/init.d/anacron {start|stop}"
    exit 2
    ;;
esac

exit 0
