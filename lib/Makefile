include ../Makefile.inc

# Subdirectories for core libraries.  Build order matters: libm
# must come BEFORE libc.so.0 because libc references libm
# functions (feraiseexcept and friends) and declares DT_NEEDED
# libm.so.0 — the libc.so.0 link rule pulls in
# $(TOP)/lib/m/libm.so.0 to satisfy the soname embedding, which
# requires the file to already exist on disk.
SUBDIRS = m c pthreads dl sys edit usb yacc

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering lib/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving lib/$@"

# Dependency ordering:
#   - libm has no dependencies, builds first.
#   - libc references libm symbols and embeds DT_NEEDED libm.so.0,
#     so libm.so.0 must exist before libc.so.0 links.
#   - Everything else (pthreads, dl, edit, ...) depends on libc.
c: | m
pthreads dl sys edit usb yacc: | c

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
