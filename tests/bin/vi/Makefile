TOP = ../../..

.PHONY: all clean test

all: test

test:
	$(MAKE) -C $(TOP)/usr.lib NATIVE_BUILD=1
	$(MAKE) -C $(TOP)/bin/vi clean all NATIVE_BUILD=1
	python3 ./test_vi_pty.py $(abspath $(TOP)/bin/vi/vi)

clean:
