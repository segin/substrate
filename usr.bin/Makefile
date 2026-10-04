include ../Makefile.inc

# One name per line so adding a tool is a one-line diff.  brandelf leads:
# its host build is the $(BRANDELF) every other link here runs.
SUBDIRS = \
	brandelf \
	addr2line \
	ar \
	compress \
	cpio \
	cxxfilt \
	elfedit \
	env \
	ldd \
	lsusb \
	mktemp \
	nm \
	pax \
	pr \
	readelf \
	seq \
	size \
	slex \
	uudecode \
	yacc

# Target-only: at and batch need lib/at, which the host build (the
# top-level Makefile's `make -C usr.bin NATIVE_BUILD=1 install`) does not
# have; strings and strip are placeholders for the binutils ones and build
# nothing.
ifndef NATIVE_BUILD
SUBDIRS += \
	at \
	batch \
	strings \
	strip
endif

.PHONY: all clean install usr_libs $(SUBDIRS)

all: $(SUBDIRS)

# Several of these link usr.lib libraries and would each start their own
# sub-make for a missing one; build them once, first, so a parallel build
# does not run two makes in one library directory.  ARCH, when given on
# the command line, reaches the sub-makes through MAKEFLAGS.
usr_libs:
	@echo ">>> Entering usr.lib"
	$(MAKE) -C ../usr.lib NATIVE_BUILD="$(NATIVE_BUILD)"
	@echo "<<< Leaving usr.lib"

$(SUBDIRS): usr_libs

$(filter-out brandelf,$(SUBDIRS)): brandelf

# One target per directory, not a shell loop: `make -k` then carries on
# past a directory that fails, and -j builds them side by side.
$(SUBDIRS):
	@echo ">>> Entering usr.bin/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving usr.bin/$@"

# The host builds of nm and size each clean and rebuild usr.lib/elfobj,
# so they cannot run at the same time.
ifdef NATIVE_BUILD
.NOTPARALLEL:
endif

clean:
	@echo "Cleaning User Binaries..."
	@set -e; for dir in $(SUBDIRS); do \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@echo "Installing User Binaries..."
	@set -e; for dir in $(SUBDIRS); do \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir DESTDIR="$(DESTDIR)" NATIVE_BUILD="$(NATIVE_BUILD)" install; fi; \
	done
