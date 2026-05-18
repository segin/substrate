include ../Makefile.inc

# Subdirectories for core libraries.  Build order matters at the
# .so link layer: libc.so.0 declares DT_NEEDED libm.so.0 and
# DT_NEEDED libsys.so.0, and the link line names the .so files
# directly with -l:libm.so.0 / -l:libsys.so.0 — those files have
# to exist before libc.so.0 links, so libm and libsys come first.
#
# kvm and proc depend on libsys + libc; placed after both are
# guaranteed to be linkable.
SUBDIRS = m sys c pthread dl edit usb yacc at dbm kvm proc

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering lib/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving lib/$@"

# Dependency ordering:
#   - libm and libsys have no inter-deps, build first.
#   - libc references libm (feraiseexcept &c) and is the formal
#     consumer of libsys's raw syscall surface; both .so files
#     must exist before libc.so.0 links.
#   - Everything else (pthreads, dl, edit, ...) depends on libc.
c: | m sys
pthread dl edit usb yacc at dbm: | c
# libkvm and libproc wrap libsys directly (and pull libc for stdlib).
kvm proc: | c sys

clean:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Cleaning lib/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Installing lib/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir DESTDIR="$(DESTDIR)" NATIVE_BUILD="$(NATIVE_BUILD)" install; fi; \
	done
