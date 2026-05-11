include ../Makefile.inc

SUBDIRS = bc regex demangle libuu elfobj exvi

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering usr.lib/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving usr.lib/$@"

# libexvi.so.0 declares DT_NEEDED libregex.so.0 — regex must build
# first so that .so file exists when exvi links.
exvi: | regex

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
