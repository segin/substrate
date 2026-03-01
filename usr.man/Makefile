include ../Makefile.inc

MAN_DIRS = man1 man2 man3 man4 man5 man7 man9
DESTDIR ?= ../dist

all:
	@echo "Nothing to build for man pages."

install:
	@echo "Installing Manual Pages to $(DESTDIR)/usr/man..."
	@mkdir -p $(DESTDIR)/usr/man
	@for dir in $(MAN_DIRS); do \
		if [ -d $$dir ]; then \
			mkdir -p $(DESTDIR)/usr/man/$$dir; \
			cp $$dir/* $(DESTDIR)/usr/man/$$dir/; \
		fi \
	done

clean:
	@echo "Nothing to clean for man pages."
