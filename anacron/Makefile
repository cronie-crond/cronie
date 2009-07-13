#   Anacron - run commands periodically
#   Copyright (C) 1998  Itai Tzur <itzur@actcom.co.il>
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
#   The GNU General Public License can also be found in the file
#   `COPYING' that comes with the Anacron source distribution.


PREFIX = 
BINDIR = $(PREFIX)/usr/sbin
MANDIR = $(PREFIX)/usr/man
CFLAGS = -Wall -pedantic -O2
#CFLAGS = -Wall -O2 -g -DDEBUG

# If you change these, please update the man-pages too
# Only absolute paths here, please
SPOOLDIR = /var/spool/anacron
ANACRONTAB = /etc/anacrontab

RELEASE = 2.3
package_name = anacron-$(RELEASE)
distfiles = ChangeLog COPYING README TODO anacron.8 anacrontab.5 Makefile *.h *.c

SHELL = /bin/sh
INSTALL = install
INSTALL_PROGRAM = $(INSTALL)
INSTALL_DATA = $(INSTALL)
INSTALL_DIR = $(INSTALL) -d
GZIP = gzip -9 -f
ALL_CPPFLAGS = -DSPOOLDIR=\"$(SPOOLDIR)\" -DRELEASE=\"$(RELEASE)\" \
	-DANACRONTAB=\"$(ANACRONTAB)\" $(CPPFLAGS)

csources := $(wildcard *.c)
objects = $(csources:.c=.o)

.PHONY: all
all: anacron

# This makefile generates header file dependencies auto-magically
%.d: %.c
	$(SHELL) -ec "$(CC) -MM $(ALL_CPPFLAGS) $< \
	| sed '1s/^\(.*\)\.o[ :]*/\1.d &/1' > $@"

include $(csources:.c=.d)

anacron: $(objects)
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) -o $@

%.o : %.c
	$(CC) -c $(ALL_CPPFLAGS) $(CFLAGS) $< -o $@

.PHONY: installdirs
installdirs:
	$(INSTALL_DIR) $(BINDIR) $(PREFIX)$(SPOOLDIR) \
		$(MANDIR)/man5 $(MANDIR)/man8

.PHONY: install
install: installdirs
	$(INSTALL_PROGRAM) anacron $(BINDIR)/anacron
	$(INSTALL_DATA) anacrontab.5 $(MANDIR)/man5/anacrontab.5
	$(INSTALL_DATA) anacron.8 $(MANDIR)/man8/anacron.8

.PHONY: clean
clean:
	rm -f *.o *.d anacron

distclean: clean
	rm -f *~

.PHONY: dist
dist: $(package_name).tar.gz

$(package_name).tar.gz: $(distfiles)
	mkdir $(package_name)
	ln $(distfiles) $(package_name)
	chmod 0644 $(package_name)/*
	chmod 0755 $(package_name)
	tar cf $(package_name).tar $(package_name)
	$(GZIP) $(package_name).tar
	rm -r $(package_name)

release: distclean $(package_name).tar.gz
