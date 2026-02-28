include ../Makefile.inc

.PHONY: all clean install

SUBDIRS = brandelf as ld cc yacc compress pr uudecode pax cpio ar cxxfilt

all:
	@echo "Building User Binaries (usr.bin)..."
	@for dir in $(SUBDIRS); do \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir; fi; \
	done

clean:
	@echo "Cleaning User Binaries..."
	@for dir in $(SUBDIRS); do \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir clean; fi; \
	done

install:
	@echo "Installing User Binaries..."
	@for dir in $(SUBDIRS); do \
		if [ -f "$$dir/Makefile" ]; then $(MAKE) -C $$dir install; fi; \
	done
