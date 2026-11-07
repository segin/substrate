TOP = ../../..

include ../exvi-build.mk

# Every test, on the plain build and on the ASan one.  Each step is its own
# command, so the first failure fails the target (they were one `;`-joined
# command, whose status was the last test's alone).
EX_SHELL_TESTS = \
	test_ex.sh test_ex_write_error.sh test_ex_exrc.sh test_ex_interrupt.sh \
	test_ex_tag.sh test_ex_recover.sh test_ex_read_error.sh test_ex_wq.sh \
	test_ex_limits.sh test_ex_patterns.sh test_ex_addresses.sh test_ex_set.sh \
	test_ex_writeany.sh test_ex_write_inplace.sh test_ex_pipe_write.sh \
	test_ex_batch_error.sh test_ex_copy.sh test_ex_force_variants.sh \
	test_ex_pattern_escape.sh test_ex_bre.sh test_ex_replacement.sh \
	test_ex_sub_bol.sh test_ex_shell.sh test_ex_global_undo.sh \
	test_ex_undo_state.sh test_ex_nul.sh test_ex_global.sh test_ex_word_bound.sh \
	test_ex_autoindent.sh
EX_PY_TESTS = test_ex_frontend.py test_ex_visual.py test_ex_undo_memory.py \
	test_ex_scaling.py

.PHONY: all clean test

all: test

test: $(EX_PLAIN) $(EX_ASAN)
	$(MAKE) -C $(TOP)/tests/usr.lib/exvi run
	set -e; for ex in $(EX_PLAIN) $(EX_ASAN); do \
		for t in $(EX_SHELL_TESTS); do bash ./$$t $$ex; done; \
		for t in $(EX_PY_TESTS); do python3 ./$$t $$ex; done; \
	done

clean: clean-build
