include ../Makefile.inc

SUBDIRS = bc regex demangle libuu elfobj exvi join

.PHONY: all clean install $(SUBDIRS) all64 clean64 install64 both install-both

# Like lib/, these build for either architecture from this one tree: the
# default, ARCH=i386, in place; ARCH=x86_64 under each library's
# obj-x86_64/, installed into /lib64 and /usr/lib64.
#
#   make -C usr.lib                  32-bit
#   make -C usr.lib ARCH=x86_64      64-bit     (same as: make -C usr.lib all64)
#   make -C usr.lib both             both
#   make -C usr.lib install-both     both, installed into one $(DESTDIR)
all: $(SUBDIRS)

all64 clean64 install64:
	$(MAKE) ARCH=x86_64 $(@:64=)

both: all all64

install-both: install install64

$(SUBDIRS):
	@echo ">>> Entering usr.lib/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving usr.lib/$@"

# libexvi.so.0 declares DT_NEEDED libregex.so.0 — regex must build
# first so that .so file exists when exvi links.
exvi: | regex

clean:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Cleaning usr.lib/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Installing usr.lib/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir DESTDIR="$(DESTDIR)" NATIVE_BUILD="$(NATIVE_BUILD)" install; fi; \
	done
