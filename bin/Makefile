include ../Makefile.inc

# Subdirectories for core binaries
SUBDIRS = bc cat chmod cp dc devtree echo ex false find getty grep head id kill ln login ls lspci mkdir mknod more mount mv passwd pwd rm rmdir sh sleep sum sync time true tty umount uptime vi wc whoami yes hostname ldtctl

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
