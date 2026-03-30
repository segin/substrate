TOP = ../../..

.PHONY: all clean test

all: test

test:
	$(MAKE) -C $(TOP)/tests/usr.lib/exvi run
	$(MAKE) -C $(TOP)/usr.lib
	$(MAKE) -C $(TOP)/lib/c
	$(MAKE) -C $(TOP)/bin/ex clean all
	$(MAKE) -C $(TOP)/bin/vi clean all
	$(MAKE) -C $(TOP)/usr.lib NATIVE_BUILD=1
	$(MAKE) -C $(TOP)/bin/ex clean all NATIVE_BUILD=1
	$(MAKE) -C $(TOP)/bin/vi clean all NATIVE_BUILD=1
	bash ./test_ex.sh $(abspath $(TOP)/bin/ex/ex)

clean:
