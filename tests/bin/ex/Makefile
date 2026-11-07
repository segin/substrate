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
	bash ./test_ex_write_error.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_exrc.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_interrupt.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_tag.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_recover.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_read_error.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_wq.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_writeany.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_write_inplace.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_pipe_write.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_batch_error.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_copy.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_force_variants.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_pattern_escape.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_bre.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_replacement.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_sub_bol.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_shell.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_global_undo.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_undo_state.sh $(abspath $(TOP)/bin/ex/ex); \
	bash ./test_ex_nul.sh $(abspath $(TOP)/bin/ex/ex); \
	python3 ./test_ex_frontend.py $(abspath $(TOP)/bin/ex/ex)

clean:
