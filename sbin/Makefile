include ../Makefile.inc

# Subdirectories for system binaries
ifdef NATIVE_BUILD
SUBDIRS = mkfs fsck
else
SUBDIRS = init ld.so mkfs fsck getty echod syslogd radvd telnetd
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
