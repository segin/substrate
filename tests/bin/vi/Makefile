TOP = ../../..
ORACLE_ARGS ?=

include ../exvi-build.mk

# Every test, on the plain build and on the ASan one; the oracle, which
# compares with Vim and is slow, on the plain build only.  Each step is its
# own command, so the first failure fails the target.
VI_TESTS = \
	test_vi_pty.py test_vi_stress.py test_vi_frontend.py \
	test_vi_replace_ctrl_o.py test_vi_exit_paths.py test_vi_interrupt.py \
	test_vi_tag.py test_vi_zz_fail.py test_vi_utf8_stray.py \
	test_vi_search_bol.py test_vi_utf8_ops.py test_vi_nul.py \
	test_vi_signals.py test_vi_limits.py test_vi_registers.py \
	test_vi_exclusive.py test_vi_utf8_input.py test_vi_esc_burst.py \
	test_vi_counts.py test_vi_input_erase.py test_vi_words.py \
	test_vi_dot_ops.py test_vi_undo_line.py test_vi_percent.py \
	test_vi_empty_ops.py test_vi_change_lines.py test_vi_indent_keys.py \
	test_vi_ctrl_at.py test_vi_columns.py test_vi_large_counts.py

.PHONY: all clean test oracle

all: test

test: $(VI_PLAIN) $(VI_ASAN) $(EX_PLAIN) $(EX_ASAN)
	set -e; for vi in $(VI_PLAIN) $(VI_ASAN); do \
		for t in $(VI_TESTS); do python3 ./$$t $$vi; done; \
	done
	python3 ./test_vi_hostile_bytes.py $(VI_PLAIN) $(EX_PLAIN)
	python3 ./test_vi_hostile_bytes.py $(VI_ASAN) $(EX_ASAN)
	python3 ./test_vi_oracle.py $(VI_PLAIN) $(ORACLE_ARGS)

oracle: $(VI_PLAIN)
	python3 ./test_vi_oracle.py $(VI_PLAIN) $(ORACLE_ARGS)

clean: clean-build
