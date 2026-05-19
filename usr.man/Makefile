include ../Makefile.inc

MAN_DIRS = man1 man2 man3 man4 man5 man7 man8 man9
DESTDIR ?= ../dist
MANDIR  ?= $(DESTDIR)/usr/share/man

all:
	@echo "Nothing to build for man pages."

install:
	@echo "Installing Manual Pages to $(MANDIR)..."
	@mkdir -p $(MANDIR)
	@for dir in $(MAN_DIRS); do \
		if [ -d $$dir ]; then \
			mkdir -p $(MANDIR)/$$dir; \
			cp $$dir/* $(MANDIR)/$$dir/; \
		fi \
	done

clean:
	@echo "Nothing to clean for man pages."
