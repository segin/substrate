include Makefile.inc

# Subdirectories to build
# Order matters: lib is usually a dependency for bin/usr.bin
SUBDIRS = lib sbin sys bin usr.lib usr.bin man

.PHONY: all clean install efi multiboot freebsd zimage debug $(SUBDIRS)

all: $(SUBDIRS)

efi:
	$(MAKE) -C sys kernel.efi

multiboot:
	$(MAKE) -C sys kernel.multiboot

freebsd:
	$(MAKE) -C sys kernel.freebsd

zimage:
	$(MAKE) -C sys kernel.zimage

# Recursive make for each subdirectory
$(SUBDIRS):
	@echo ">>> Entering $@"
	$(MAKE) -C $@
	@echo "<<< Leaving $@"


clean:
	@for dir in $(SUBDIRS); do \
		echo ">>> Cleaning $$dir"; \
		$(MAKE) -C $$dir clean; \
	done

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
	@mkdir -p $(DESTDIR)/usr/share
	@mkdir -p $(DESTDIR)/var
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing $$dir"; \
		$(MAKE) -C $$dir install; \
	done

debug: all
	qemu-system-i386 -kernel sys/kernel.bin -nographic -serial file:serial.log