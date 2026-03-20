include ../Makefile.inc

.PHONY: all clean install

SUBDIRS = brandelf elfedit readelf as ld cc yacc lex compress pr uudecode pax cpio ar cxxfilt addr2line lsusb

all:
	@echo "Building User Binaries (usr.bin)..."
	@set -e; for dir in $(SUBDIRS); do \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir; fi; \
	done

clean:
	@echo "Cleaning User Binaries..."
	@set -e; for dir in $(SUBDIRS); do \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@echo "Installing User Binaries..."
	@set -e; for dir in $(SUBDIRS); do \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir DESTDIR="$(DESTDIR)" NATIVE_BUILD="$(NATIVE_BUILD)" install; fi; \
	done
