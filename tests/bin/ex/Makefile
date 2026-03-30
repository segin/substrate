TOP = ../../..
USR_LIB_LOCKDIR = $(TOP)/.locks/usr.lib-test.lock

.PHONY: all clean test

all: test

test:
	@mkdir -p $(TOP)/.locks
	@while ! mkdir "$(USR_LIB_LOCKDIR)" 2>/dev/null; do sleep 1; done; \
	trap 'rmdir "$(USR_LIB_LOCKDIR)"' EXIT INT TERM; \
	$(MAKE) -C $(TOP)/tests/usr.lib/exvi run; \
	$(MAKE) -C $(TOP)/usr.lib clean all; \
	$(MAKE) -C $(TOP)/lib/c; \
	$(MAKE) -C $(TOP)/bin/ex clean all; \
	$(MAKE) -C $(TOP)/bin/vi clean all; \
	$(MAKE) -C $(TOP)/usr.lib clean all NATIVE_BUILD=1; \
	$(MAKE) -C $(TOP)/bin/ex clean all NATIVE_BUILD=1; \
	$(MAKE) -C $(TOP)/bin/vi clean all NATIVE_BUILD=1; \
	bash ./test_ex.sh $(abspath $(TOP)/bin/ex/ex); \
	python3 ./test_ex_frontend.py $(abspath $(TOP)/bin/ex/ex)

clean:
