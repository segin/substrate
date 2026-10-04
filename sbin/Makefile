include ../Makefile.inc

# Subdirectories for system binaries
ifdef NATIVE_BUILD
SUBDIRS = mkfs fsck
else
# Every directory here that ships, which is every one but sdm: sgreet is an
# Xlib client that needs the contrib X stack staged and the cross compiler
# (see sdm/Makefile), so build-rootfs.sh builds it separately.
SUBDIRS = \
	init \
	ld.so \
	mkfs \
	fsck \
	getty \
	echod \
	syslogd \
	radvd \
	telnetd \
	dhclient \
	ifconfig \
	halt \
	poweroff \
	reboot
endif


.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering sbin/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving sbin/$@"

clean:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Cleaning sbin/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Installing sbin/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir DESTDIR="$(DESTDIR)" NATIVE_BUILD="$(NATIVE_BUILD)" install; fi; \
	done
