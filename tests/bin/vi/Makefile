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
	python3 ./test_vi_frontend.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_replace_ctrl_o.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_exit_paths.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_interrupt.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_tag.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_zz_fail.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_utf8_stray.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_search_bol.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_utf8_ops.py $(abspath $(TOP)/bin/vi/vi); \
	python3 ./test_vi_hostile_bytes.py $(abspath $(TOP)/bin/vi/vi) $(abspath $(TOP)/bin/ex/ex)

oracle:
	@mkdir -p $(TOP)/.locks
	@while ! mkdir "$(USR_LIB_LOCKDIR)" 2>/dev/null; do sleep 1; done; \
	trap 'rmdir "$(USR_LIB_LOCKDIR)"' EXIT INT TERM; \
	$(MAKE) -C $(TOP)/usr.lib clean all NATIVE_BUILD=1; \
	$(MAKE) -C $(TOP)/bin/vi clean all NATIVE_BUILD=1; \
	python3 ./test_vi_oracle.py $(abspath $(TOP)/bin/vi/vi) $(ORACLE_ARGS)

clean:
