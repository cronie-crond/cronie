# Cronie
Cronie contains the standard UNIX daemon crond that runs specified programs at
scheduled times and related tools. It is based on the original cron and
has security and configuration enhancements like the ability to use pam and
SELinux.

And why cronie? [http://www.urbandictionary.com/define.php?term=cronie]

# Download
Latest released version is 1.5.1.

User visible changes:
- crontab: Use temporary file name that is ignored by crond.
- crond: Inherit PATH from the crond environment if -P option is used.
- crond: Remove hardcoded "system_u" SELinux user, use the SELinux user of the running crond.
- anacron: Small cleanups and fixes.
- crond: Fix longstanding race condition on repeated crontab modification. 

The source can be downloaded from [https://github.com/cronie-crond/cronie/releases]

Cronie is packaged by these distributions:
- Fedora [http://koji.fedoraproject.org/koji/packageinfo?packageID=5724]
- Mandriva [http://sophie.zarb.org/srpm/Mandriva,cooker,/cronie/history]
- Gentoo [http://packages.gentoo.org/package/sys-process/cronie]
- Source Mage [http://dbg.download.sourcemage.org/grimoire/codex/stable/utils/cronie/]
- OpenSUSE replacement of default with cronie [http://lists.suse.com/opensuse-features/2010-09/msg00217.html]
- Arch Linux [https://www.archlinux.org/packages/core/x86_64/cronie/]

# Contact

Mailing list: `cronie-devel AT lists.fedorahosted DOT org`

Bugs can be filled either into the issue tracker at this site or into [https://bugzilla.redhat.com/] Fedora product, component cronie. 
