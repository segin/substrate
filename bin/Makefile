include ../Makefile.inc

# Subdirectories for core binaries
SUBDIRS = cat chmod cp echo false getty grep id kill ln login ls mkdir mknod more mount mv passwd pwd rm rmdir sh sleep sync true tty umount uptime vi wc whoami yes hostname

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering bin/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving bin/$@"

clean:
	@for dir in $(SUBDIRS); do \
		echo ">>> Cleaning bin/$$dir"; \
		$(MAKE) -C $$dir clean; \
	done

install:
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing bin/$$dir"; \
		$(MAKE) -C $$dir install; \
	done