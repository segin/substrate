include Makefile.inc

# Subdirectories to build
# Order matters: lib is usually a dependency for bin/usr.bin
SUBDIRS = lib usr.lib sys sbin bin usr.bin usr.man

.PHONY: all clean install efi multiboot freebsd zimage debug host_dist host_dist_install host_install $(SUBDIRS)

all: $(SUBDIRS)

efi:
	$(MAKE) -C sys kernel.efi

multiboot:
	$(MAKE) -C sys kernel.multiboot

freebsd:
	$(MAKE) -C sys kernel.freebsd

zimage:
	$(MAKE) -C sys kernel.zimage

# Host tools build
HOSTCC ?= cc
HOSTCFLAGS ?= -O2 $(WARNFLAGS)
HOST_DIST_PREFIX ?= /opt/substrate
export HOSTCC HOSTCFLAGS

host_dist:
	@rm -rf host_dist
	@mkdir -p host_dist/bin
	@mkdir -p host_dist/sbin
	@mkdir -p host_dist/usr/lib
	@mkdir -p host_dist/usr/man
	@echo ">>> Building host tools..."
	-$(MAKE) -C usr.lib clean
	$(MAKE) -C usr.lib NATIVE_BUILD=1
	-$(MAKE) -C bin clean
	$(MAKE) -C bin NATIVE_BUILD=1 DESTDIR=$(TOP)/host_dist install
	-$(MAKE) -C sbin clean
	$(MAKE) -C sbin NATIVE_BUILD=1 DESTDIR=$(TOP)/host_dist install
	-$(MAKE) -C usr.bin/cc clean
	$(MAKE) -C usr.bin/cc NATIVE_BUILD=1 DESTDIR=$(TOP)/host_dist install
	-$(MAKE) -C usr.bin clean
	$(MAKE) -C usr.bin NATIVE_BUILD=1 DESTDIR=$(TOP)/host_dist install
	$(MAKE) -C usr.man DESTDIR=$(TOP)/host_dist install
	@test -x host_dist/usr/bin/cc
	@echo ">>> Host tools installed to host_dist"

host_dist_install: host_dist
	@test -n "$(HOST_DIST_PREFIX)"
	@test "$(HOST_DIST_PREFIX)" != "/"
	@mkdir -p "$(HOST_DIST_PREFIX)"
	@find "$(HOST_DIST_PREFIX)" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
	@cp -a host_dist/. "$(HOST_DIST_PREFIX)/"
	@echo ">>> Host tools synchronized to $(HOST_DIST_PREFIX)"

host_install: host_dist_install

# Recursive make for each subdirectory
$(SUBDIRS):
	@echo ">>> Entering $@"
	$(MAKE) -C $@
	@echo "<<< Leaving $@"


clean:
	@for dir in $(SUBDIRS); do \
		echo ">>> Cleaning $$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done
	rm -rf $(DESTDIR)

install:
	@mkdir -p $(DESTDIR)/bin
	@mkdir -p $(DESTDIR)/dev
	@mkdir -p $(DESTDIR)/etc
	@mkdir -p $(DESTDIR)/lib
	@mkdir -p $(DESTDIR)/mnt
	@mkdir -p $(DESTDIR)/proc
	@mkdir -p $(DESTDIR)/sbin
	@mkdir -p $(DESTDIR)/sys
	@mkdir -p $(DESTDIR)/tmp
	@mkdir -p $(DESTDIR)/usr/bin
	@mkdir -p $(DESTDIR)/usr/include
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/local
	@mkdir -p $(DESTDIR)/usr/man
	@mkdir -p $(DESTDIR)/var
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing $$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir install; fi; \
	done

dist: install

debug: all
	qemu-system-i386 -kernel sys/kernel.bin -nographic -serial file:serial.log
