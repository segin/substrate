# pr(1), built for the host and run through test_pr.sh.
.PHONY: all clean run

all: run

run:
	$(MAKE) -C ../../../usr.bin/pr NATIVE_BUILD=1
	./test_pr.sh ../../../usr.bin/pr/pr

clean:
	$(MAKE) -C ../../../usr.bin/pr NATIVE_BUILD=1 clean
