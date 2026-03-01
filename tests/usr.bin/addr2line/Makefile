.PHONY: all clean run

all: run

run:
	$(MAKE) -C ../../../usr.lib/elfobj NATIVE_BUILD=1
	$(MAKE) -C ../../../usr.lib/demangle NATIVE_BUILD=1
	$(MAKE) -C ../../../usr.bin/addr2line NATIVE_BUILD=1
	./test_addr2line.sh

clean:
	$(MAKE) -C ../../../usr.bin/addr2line NATIVE_BUILD=1 clean
