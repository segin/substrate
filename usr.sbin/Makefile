include ../Makefile.inc

# System administration programs (/usr/sbin).  One name per line so adding
# a tool is a one-line diff.
#
# They link lib/at and lib/pwdb, so `make -C lib` comes first -- with the
# same ARCH, when building 64-bit.  ARCH, when given on the command line,
# reaches the sub-makes through MAKEFLAGS.
SUBDIRS = \
	atd \
	groupadd \
	groupdel \
	groupmod \
	useradd \
	userdel \
	usermod

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

# One target per directory, not a shell loop: `make -k` then carries on
# past a directory that fails, and -j builds them side by side.
$(SUBDIRS):
	@echo ">>> Entering usr.sbin/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving usr.sbin/$@"

clean:
	@echo "Cleaning System Binaries (usr.sbin)..."
	@set -e; for dir in $(SUBDIRS); do \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@echo "Installing System Binaries (usr.sbin)..."
	@set -e; for dir in $(SUBDIRS); do \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir DESTDIR="$(DESTDIR)" NATIVE_BUILD="$(NATIVE_BUILD)" install; fi; \
	done
