# bin/xenix - launcher for SCO Xenix/286 programs.
#
# A shell script, not a compiled program: all it does is fix up the
# environment (chiefly TERM) before exec'ing the Xenix binary, which the
# PERS_SCO_X286 personality then runs natively.  See xenix.sh for why that
# is needed at all.

include ../../Makefile.inc

all:

install:
	install -d $(DESTDIR)/usr/bin
	install -m 0755 xenix.sh $(DESTDIR)/usr/bin/xenix

clean:

.PHONY: all install clean
