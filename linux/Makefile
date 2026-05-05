SUBDIRS = runner

.PHONY: all clean install $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	@set -e; for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

install:
	@set -e; for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir install; \
	done
