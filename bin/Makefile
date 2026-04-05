include ../Makefile.inc

# Subdirectories for core binaries
SUBDIRS = bc cat chmod cp dc devtree echo ex false find getty grep head id kill ln login ls lspci mkdir mknod more mount mv passwd ps pwd rm rmdir sh sleep sum sync time true tty umount uptime vi wc whoami yes hostname ldtctl

.PHONY: all clean install $(SUBDIRS)

all: usr_libs $(SUBDIRS)

usr_libs:
	@echo ">>> Entering usr.lib"
	$(MAKE) -C ../usr.lib NATIVE_BUILD="$(NATIVE_BUILD)"
	@echo "<<< Leaving usr.lib"

$(SUBDIRS): usr_libs

$(SUBDIRS):
	@echo ">>> Entering bin/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving bin/$@"

clean:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Cleaning bin/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Installing bin/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir DESTDIR="$(DESTDIR)" NATIVE_BUILD="$(NATIVE_BUILD)" install; fi; \
	done
