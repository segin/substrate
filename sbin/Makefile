include ../Makefile.inc

# Subdirectories for system binaries
SUBDIRS = mkfs fsck ld.so init

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering sbin/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving sbin/$@"

clean:
	@for dir in $(SUBDIRS); do \
		echo ">>> Cleaning sbin/$$dir"; \
		$(MAKE) -C $@ clean; \
	done

install:
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing sbin/$$dir"; \
		$(MAKE) -C $@ install; \
	done

