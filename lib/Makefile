include ../Makefile.inc

# Subdirectories for core libraries
SUBDIRS = c m pthreads dl math

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@echo ">>> Entering lib/$@"
	$(MAKE) -C $@
	@echo "<<< Leaving lib/$@"

clean:
	@for dir in $(SUBDIRS); do \
		echo ">>> Cleaning lib/$$dir"; \
		$(MAKE) -C $$dir clean; \
	done

install:
	@for dir in $(SUBDIRS); do \
		echo ">>> Installing lib/$$dir"; \
		$(MAKE) -C $$dir install; \
	done
