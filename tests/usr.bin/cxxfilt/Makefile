.PHONY: all clean run

all: run

run:
	$(MAKE) -C ../../../usr.lib/demangle NATIVE_BUILD=1
	$(MAKE) -C ../../../usr.bin/cxxfilt NATIVE_BUILD=1
	./test_cxxfilt.sh

clean:
	$(MAKE) -C ../../../usr.bin/cxxfilt NATIVE_BUILD=1 clean
