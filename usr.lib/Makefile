include ../Makefile.inc

SUBDIRS = bc regex libuu

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering usr.lib/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving usr.lib/$@"

clean:
	@for dir in $(SUBDIRS); do \
		echo ">>> Cleaning usr.lib/$$dir"; \
		$(MAKE) -C $$dir clean; \
	done

install:
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing usr.lib/$$dir"; \
		$(MAKE) -C $$dir install; \
	done
