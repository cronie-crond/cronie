#! /bin/sh

# This script makes anacron jobs start to run when the machine is
# plugged into AC power, or woken up.  For a laptop, these are the 
# closest parallels to turning on a desktop.

# The /etc/init.d/anacron script now normally tries to avoid running
# anacron unless on AC power, so as to avoid running down the battery.
# (Things like the slocate updatedb cause a lot of IO.)  Rather than
# trying to second-guess which events reflect having or not having
# power, we just try to run anacron every time and let it abort if
# there's no AC.  You'll see a message on the cron syslog facility 
# (typically /var/log/cron) if it does run.

case "$1,$2" in
change,power|resume,*)
    /usr/sbin/invoke-rc.d anacron start >/dev/null   
    ;;
esac
