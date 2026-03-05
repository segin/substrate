TOP = ../../..

.PHONY: all test build clean

all: test

build:
	$(MAKE) -C $(TOP)/usr.lib/bc NATIVE_BUILD=1
	$(MAKE) -C $(TOP)/bin/bc NATIVE_BUILD=1
	cp $(TOP)/bin/bc/bc $(TOP)/bin/bc/bc_host

test: build
	./test_undefined.sh

clean:
	rm -f $(TOP)/bin/bc/bc_host
	$(MAKE) -C $(TOP)/usr.lib/bc clean
	$(MAKE) -C $(TOP)/bin/bc clean
