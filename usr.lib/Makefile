include ../Makefile.inc

SUBDIRS = bc regex libuu elfobj

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering usr.lib/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving usr.lib/$@"

clean:
	@for dir in $(SUBDIRS); do \
		echo ">>> Cleaning usr.lib/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing usr.lib/$$dir"; \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir install; fi; \
	done
