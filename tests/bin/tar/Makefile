TOP = ../../..

.PHONY: all clean test

all: test

test:
	$(MAKE) -C $(TOP)/bin/tar NATIVE_BUILD=1
	bash ./test_tar.sh $(abspath $(TOP)/bin/tar/tar)

clean:
	rm -rf tmp
