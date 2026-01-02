include Makefile.inc

# Subdirectories to build
# Order matters: lib is usually a dependency for bin/usr.bin
SUBDIRS = lib sbin sys bin usr.lib usr.bin

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
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing $$dir"; \
		$(MAKE) -C $$dir install; \
	done

debug: all
	qemu-system-i386 -kernel sys/kernel.bin -nographic -serial file:serial.log