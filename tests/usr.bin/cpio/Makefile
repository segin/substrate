# cpio(1), built for the host and run through run_tests.sh.
.PHONY: all clean run

all: run

run:
	$(MAKE) -C ../../../usr.bin/cpio NATIVE_BUILD=1
	sh ./run_tests.sh ../../../usr.bin/cpio/cpio

clean:
	$(MAKE) -C ../../../usr.bin/cpio NATIVE_BUILD=1 clean
