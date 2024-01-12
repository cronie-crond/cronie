# Cronie
Cronie contains the standard UNIX daemon crond that runs specified programs at
scheduled times and related tools. The source is based on the original vixie-cron
and has security and configuration enhancements like the ability to use pam and
SELinux.

And why cronie? [http://www.urbandictionary.com/define.php?term=cronie]

# Download
Latest released version is 1.7.1.

User visible changes:

Release 1.7.1

- crond: Wait on finishing the job with -n option to check
  the exit status
- crond: Do not set the return path to <> if non-default MAILFROM is set
- /etc/sysconfig/crond and /etc/default/anacron files are optional

Release 1.7.0

- anacron: Add support for NO_MAIL_OUTPUT environment variable
- anacron: Support enabling anacron jobs on battery power
- crond: Support -n crontab entry option to disable mailing the output
- crontab: Make a backup of the crontab file on edition and deletion

Release 1.6.1

- crond: Fix regression of handling ranges (x-y) in crontab

Release 1.6.0

- crond: Add switch -f as an alias for -n
- crond: Add random within range '~' operator
- crond: Use the configure runstatedir directory for pid file
- crond: Increase the maximum number of crontab entries to 10000

Release 1.5.7

- anacron: Fix problem of anacron not being started on some desktops
- crontab: switch off colors if NO_COLOR is set

Release 1.5.6

- crontab: crontab without arguments now works if stdin is not a TTY
- crond: Fix various issues on loading the crontab databases on startup
- anacron: Expand MAILTO and MAILFROM environment variables
- crontab: New option to test crontab file syntax without installing it

Release 1.5.5
- Explicitly validate upper end of range and step to disallow entries
  such as: 1-234/5678 * * * * ....
- crond: Report missing newline before EOF in syslog so the line is not
  completely silently ignored.
- crontab -l colors comment lines in a different color.
- crond: Revert "Avoid creating pid files when crond doesn't fork".
- anacron is built by default.
- Use non-recursive build.
- cronnext: Allow to optionally select jobs by substring.

Release 1.5.4
- crond: Fix regression from previous release. Only first job from a crontab
  was being run.

Release 1.5.3
- Fix CVE-2019-9704 and CVE-2019-9705 to avoid local DoS of the crond.
- crontab: Make crontab without arguments fail.
- crond: In PAM configuration include system-auth instead of password-auth.
- crond: In the systemd service file restart crond if it fails.
- crond: Use the role from the crond context for system job contexts.
- Multiple small cleanups and fixes.

The source can be downloaded from [https://github.com/cronie-crond/cronie/releases]

Cronie is packaged by these distributions:
- Fedora [https://apps.fedoraproject.org/packages/cronie]
- Mandriva [http://sophie.zarb.org/srpm/Mandriva,cooker,/cronie/history]
- Gentoo [http://packages.gentoo.org/package/sys-process/cronie]
- Source Mage [http://dbg.download.sourcemage.org/grimoire/codex/stable/utils/cronie/]
- openSUSE [https://software.opensuse.org/package/cronie]
- Arch Linux [https://www.archlinux.org/packages/core/x86_64/cronie/]
- Void Linux [https://github.com/void-linux/void-packages/tree/master/srcpkgs/cronie]

# Contact

Mailing list: `cronie-devel AT lists.fedorahosted DOT org`

To report bugs please use the github issue tracker of this project.
