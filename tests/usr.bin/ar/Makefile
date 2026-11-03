.PHONY: all clean run

all: run

run:
	$(MAKE) -C ../../../usr.lib/elfobj NATIVE_BUILD=1
	$(MAKE) -C ../../../usr.bin/ar NATIVE_BUILD=1
	./test_symtab.sh
	./test_thin.sh
	./test_corrupt.sh

clean:
	$(MAKE) -C ../../../usr.bin/ar NATIVE_BUILD=1 clean
