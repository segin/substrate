include ../Makefile.inc

# Subdirectories for core libraries
SUBDIRS = c m pthreads dl sys edit usb yacc

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering lib/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving lib/$@"

# Dependency ordering: libc must be built before other libraries.
m pthreads dl sys edit usb yacc: | c

clean:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Cleaning lib/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Installing lib/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir DESTDIR="$(DESTDIR)" NATIVE_BUILD="$(NATIVE_BUILD)" install; fi; \
	done
