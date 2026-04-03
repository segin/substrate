include ../Makefile.inc

SUBDIRS = bc regex demangle libuu elfobj link exvi

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering usr.lib/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving usr.lib/$@"

clean:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Cleaning usr.lib/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@set -e; for dir in $(SUBDIRS); do \
		echo ">>> Installing usr.lib/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir DESTDIR="$(DESTDIR)" NATIVE_BUILD="$(NATIVE_BUILD)" install; fi; \
	done
