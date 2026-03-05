include ../Makefile.inc

# Subdirectories for core binaries
SUBDIRS = cat chmod cp echo false getty grep head id kill ln login ls mkdir mknod more mount mv passwd pwd rm rmdir sh sleep sum sync time true tty umount uptime vi wc whoami yes hostname

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering bin/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving bin/$@"

clean:
	@for dir in $(SUBDIRS); do \
		echo ">>> Cleaning bin/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing bin/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir install; fi; \
	done
