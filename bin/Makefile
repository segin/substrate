include ../Makefile.inc

# Subdirectories for core binaries
SUBDIRS = bas bc cal cat chmod cp dc echo ed false getty grep id join kill last ln login ls make mkdir mknod more mount mv newgrp nice nproc od passwd pgrep prof ps pwd rm rmdir sh size sleep sort split su sum sync tabs tail tar tc tee test time touch true tty umount uname uniq uptime vi w wall wc who whoami write yes hostname

ifndef NATIVE_BUILD
SUBDIRS += tests
endif

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