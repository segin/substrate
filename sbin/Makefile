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
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing sbin/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir install; fi; \
	done
