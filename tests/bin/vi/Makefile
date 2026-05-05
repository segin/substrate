TOP = ../../..
USR_LIB_LOCKDIR = $(TOP)/.locks/usr.lib-test.lock
ORACLE_ARGS ?=

.PHONY: all clean test oracle

all: test

test:
	@mkdir -p $(TOP)/.locks
	@while ! mkdir "$(USR_LIB_LOCKDIR)" 2>/dev/null; do sleep 1; done; \
	trap 'rmdir "$(USR_LIB_LOCKDIR)"' EXIT INT TERM; \
	$(MAKE) -C $(TOP)/usr.lib clean all NATIVE_BUILD=1; \
	$(MAKE) -C $(TOP)/bin/vi clean all NATIVE_BUILD=1; \
	python3 ./test_vi_pty.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_oracle.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_stress.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_frontend.py $(abspath $(TOP)/bin/vi/vi)

oracle:
	@mkdir -p $(TOP)/.locks
	@while ! mkdir "$(USR_LIB_LOCKDIR)" 2>/dev/null; do sleep 1; done; \
	trap 'rmdir "$(USR_LIB_LOCKDIR)"' EXIT INT TERM; \
	$(MAKE) -C $(TOP)/usr.lib clean all NATIVE_BUILD=1; \
	$(MAKE) -C $(TOP)/bin/vi clean all NATIVE_BUILD=1; \
	python3 ./test_vi_oracle.py $(abspath $(TOP)/bin/vi/vi) $(ORACLE_ARGS)

clean:
