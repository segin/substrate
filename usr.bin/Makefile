include ../Makefile.inc

.PHONY: all clean install

SUBDIRS = brandelf as ld yacc compress pr uuencode uudecode pax cpio ar

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
