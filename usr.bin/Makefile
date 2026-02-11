include ../Makefile.inc

.PHONY: all clean install

SUBDIRS = brandelf yacc compress pr uuencode uudecode

all:
	@echo "Building User Binaries (usr.bin)..."
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir; \
	done

clean:
	@echo "Cleaning User Binaries..."
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

install:
	@echo "Installing User Binaries..."
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir install; \
	done
