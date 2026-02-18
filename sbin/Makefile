include ../Makefile.inc

# Subdirectories for system binaries
ifdef NATIVE_BUILD
SUBDIRS = mkfs fsck
else
SUBDIRS = init mkfs fsck
endif 


.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering sbin/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving sbin/$@"

clean:
	@for dir in $(SUBDIRS); do \
		echo ">>> Cleaning sbin/$$dir"; \
		$(MAKE) -C $$dir clean; \
	done

install:
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing sbin/$$dir"; \
		$(MAKE) -C $$dir install; \
	done

