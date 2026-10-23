TOP ?= $(abspath ../../..)

# The suite runs the native (host) build of cc.  Name the host's as and
# ld: otherwise cc runs the ones beside it in the tree, which may be
# substrate binaries that cannot run here.
export AS = as
export LD = ld

# Each run writes its files to a directory of its own, so two runs (two CI
# jobs on one host, or two users) cannot overwrite each other's.  A run
# that passes removes it; one that fails leaves it for a look, and
# `make clean CC_TMP=<dir>` removes the files from it.  The scripts take
# the same CC_TMP.
ifndef CC_TMP
ifeq ($(filter clean,$(MAKECMDGOALS)),)
CC_TMP := $(shell mktemp -d "$${TMPDIR:-/tmp}/cc_suite.XXXXXX")
CC_TMP_OWNED := 1
else
CC_TMP := /nonexistent/cc_suite
endif
endif
export CC_TMP

.PHONY: all clean test

all: test

test:
	test -d "$(CC_TMP)"
	$(MAKE) -C $(TOP)/usr.bin/cc NATIVE_BUILD=1
	# Compile and run test_ast2ir; a failed build must fail the suite,
	# not leave an earlier binary to run
	rm -f test_ast2ir_bin
	$(CC) $(CFLAGS) -I$(TOP)/usr.bin/cc/include test_ast2ir.c $(TOP)/usr.bin/cc/cmd/pipeline.c $(TOP)/usr.bin/cc/common/asm_constraint.c $(TOP)/usr.bin/cc/common/diag.c $(TOP)/usr.bin/cc/common/lang.c $(TOP)/usr.bin/cc/common/options.c $(TOP)/usr.bin/cc/frontend/consteval.c $(TOP)/usr.bin/cc/frontend/ctype.c $(TOP)/usr.bin/cc/common/target.c $(TOP)/usr.bin/cc/common/util.c $(TOP)/usr.bin/cc/frontend/types.c $(TOP)/usr.bin/cc/frontend/nested.c $(TOP)/usr.bin/cc/frontend/decimal.c $(TOP)/usr.bin/cc/frontend/binary128.c $(TOP)/usr.bin/cc/frontend/preproc.c $(TOP)/usr.bin/cc/frontend/lexer.c $(TOP)/usr.bin/cc/frontend/parser.c $(TOP)/usr.bin/cc/frontend/parse_sema.c $(TOP)/usr.bin/cc/frontend/sema.c $(TOP)/usr.bin/cc/frontend/sema_call.c $(TOP)/usr.bin/cc/frontend/builtin.c $(TOP)/usr.bin/cc/middle/ast2ir.c $(TOP)/usr.bin/cc/middle/lower_call.c $(TOP)/usr.bin/cc/middle/irbuild.c $(TOP)/usr.bin/cc/middle/abi_x86_64.c $(TOP)/usr.bin/cc/middle/abi_i386.c $(TOP)/usr.bin/cc/middle/legalize.c $(TOP)/usr.bin/cc/middle/legalize_i386.c $(TOP)/usr.bin/cc/middle/ssa_dump.c $(TOP)/usr.bin/cc/middle/passes/opt.c $(TOP)/usr.bin/cc/middle/passes/inline.c $(TOP)/usr.bin/cc/middle/passes/mem.c $(TOP)/usr.bin/cc/middle/ssa/module.c $(TOP)/usr.bin/cc/middle/ssa/func.c $(TOP)/usr.bin/cc/middle/ssa/bblock.c $(TOP)/usr.bin/cc/middle/ssa/instr_set.c $(TOP)/usr.bin/cc/backend/regalloc.c $(TOP)/usr.bin/cc/backend/frame.c $(TOP)/usr.bin/cc/backend/x86/common.c $(TOP)/usr.bin/cc/backend/x86/x86_64.c $(TOP)/usr.bin/cc/backend/x86/i386.c $(TOP)/usr.bin/cc/backend/x86/regalloc_i386.c -o test_ast2ir_bin
	./test_ast2ir_bin
	rm -f test_ctype_bin
	$(CC) $(CFLAGS) -I$(TOP)/usr.bin/cc/include test_ctype.c $(TOP)/usr.bin/cc/frontend/ctype.c $(TOP)/usr.bin/cc/frontend/types.c $(TOP)/usr.bin/cc/common/target.c $(TOP)/usr.bin/cc/common/options.c $(TOP)/usr.bin/cc/common/lang.c $(TOP)/usr.bin/cc/middle/abi_x86_64.c $(TOP)/usr.bin/cc/middle/abi_i386.c $(TOP)/usr.bin/cc/middle/irbuild.c $(TOP)/usr.bin/cc/common/diag.c $(TOP)/usr.bin/cc/common/util.c $(TOP)/usr.bin/cc/middle/ssa/func.c $(TOP)/usr.bin/cc/middle/ssa/instr_set.c $(TOP)/usr.bin/cc/middle/ssa/bblock.c $(TOP)/usr.bin/cc/middle/legalize_i386.c -o test_ctype_bin
	./test_ctype_bin
	$(TOP)/usr.bin/cc/ir-verifier valid.ir
	! $(TOP)/usr.bin/cc/ir-verifier invalid_missing_term.ir
	! $(TOP)/usr.bin/cc/ir-verifier invalid_use_before_def.ir
	! $(TOP)/usr.bin/cc/ir-verifier invalid_phi_arity.ir
	! $(TOP)/usr.bin/cc/ir-verifier invalid_type_class.ir 2>$(CC_TMP)/cc_ir_type.log
	grep -q "fadd: operand %a has the wrong class of type" $(CC_TMP)/cc_ir_type.log
	! $(TOP)/usr.bin/cc/ir-verifier invalid_unknown_opcode.ir 2>$(CC_TMP)/cc_ir_op.log
	grep -q "unknown opcode 'frobnicate'" $(CC_TMP)/cc_ir_op.log
	! $(TOP)/usr.bin/cc/ir-verifier invalid_operand_count.ir 2>$(CC_TMP)/cc_ir_count.log
	grep -q "icmp takes two operands" $(CC_TMP)/cc_ir_count.log
	$(TOP)/usr.bin/cc/ir-verifier valid_width.ir
	! $(TOP)/usr.bin/cc/ir-verifier invalid_width_narrowing.ir 2>$(CC_TMP)/cc_ir_narrow.log
	grep -q "add: operand %a is wider than the operation" $(CC_TMP)/cc_ir_narrow.log
	! $(TOP)/usr.bin/cc/ir-verifier invalid_width_crossing.ir 2>$(CC_TMP)/cc_ir_cross.log
	grep -q "add: operand %b is narrower than the word-crossing operation" $(CC_TMP)/cc_ir_cross.log
	$(TOP)/usr.bin/cc/ir-normalize valid.ir > $(CC_TMP)/cc_valid.norm
	$(TOP)/usr.bin/cc/ir-diff valid_equiv_a.ir valid_equiv_b.ir
	$(TOP)/usr.bin/cc/cc -E -DTESTVAL=7 driver_pp_input.c -o $(CC_TMP)/cc_driver.pp
	grep -q "int x = 7;" $(CC_TMP)/cc_driver.pp
	! $(TOP)/usr.bin/cc/cc -c driver_pp_input.c -o $(CC_TMP)/cc_driver_fail.o 2>$(CC_TMP)/cc_driver_fail.log
	grep -q "use of undeclared identifier: TESTVAL" $(CC_TMP)/cc_driver_fail.log
	$(TOP)/usr.bin/cc/cc --bootstrap-gcc -c driver_bootstrap_input.c -o $(CC_TMP)/cc_driver_bootstrap.o
	test -f $(CC_TMP)/cc_driver_bootstrap.o
	$(TOP)/usr.bin/cc/cc -S native_arith.c -o $(CC_TMP)/cc_native_arith.s
	grep -q "imulq" $(CC_TMP)/cc_native_arith.s
	grep -q "addq" $(CC_TMP)/cc_native_arith.s
	$(TOP)/usr.bin/cc/cc native_array_of_array_pointers_size.c -o $(CC_TMP)/cc_native_aoap
	$(CC_TMP)/cc_native_aoap
	$(TOP)/usr.bin/cc/cc -c native_array_of_array_pointers_size.c -o $(CC_TMP)/cc_native_aoap.o
	test "$$(objdump -h $(CC_TMP)/cc_native_aoap.o | awk '$$2 == ".data" { print $$3 }')" = 00000010
	$(TOP)/usr.bin/cc/cc -std=gnu23 native_atomic_bitint_global_size.c -o $(CC_TMP)/cc_native_abgs
	$(CC_TMP)/cc_native_abgs
	$(TOP)/usr.bin/cc/cc -std=gnu23 -c native_atomic_bitint_global_size.c -o $(CC_TMP)/cc_native_abgs.o
	test "$$(objdump -h $(CC_TMP)/cc_native_abgs.o | awk '$$2 == ".data" { print $$3 }')" = 00000008
	$(TOP)/usr.bin/cc/cc native_pointer_difference_type.c -o $(CC_TMP)/cc_native_pdt
	$(CC_TMP)/cc_native_pdt
	$(TOP)/usr.bin/cc/cc native_c99_array_rank_unlimited.c -o $(CC_TMP)/cc_native_aru
	$(CC_TMP)/cc_native_aru
	$(TOP)/usr.bin/cc/cc native_main.c -o $(CC_TMP)/cc_native_main
	$(CC_TMP)/cc_native_main
	$(TOP)/usr.bin/cc/cc native_phase1.c -o $(CC_TMP)/cc_native_phase1
	$(CC_TMP)/cc_native_phase1
	$(TOP)/usr.bin/cc/cc native_call_stmt.c -o $(CC_TMP)/cc_native_call_stmt
	$(CC_TMP)/cc_native_call_stmt
	$(TOP)/usr.bin/cc/cc native_double.c -o $(CC_TMP)/cc_native_double
	$(CC_TMP)/cc_native_double
	$(TOP)/usr.bin/cc/cc native_void.c -o $(CC_TMP)/cc_native_void
	$(CC_TMP)/cc_native_void
	$(TOP)/usr.bin/cc/cc native_c99_empty_void.c -o $(CC_TMP)/cc_native_c99_empty_void
	$(CC_TMP)/cc_native_c99_empty_void
	$(TOP)/usr.bin/cc/cc native_c99_empty_stmt2.c -o $(CC_TMP)/cc_native_c99_empty_stmt2
	$(CC_TMP)/cc_native_c99_empty_stmt2
	$(TOP)/usr.bin/cc/cc native_variadic_decl.c -o $(CC_TMP)/cc_native_variadic
	$(CC_TMP)/cc_native_variadic
	$(TOP)/usr.bin/cc/cc native_c99_variadic_runtime.c -o $(CC_TMP)/cc_native_c99_variadic_runtime
	$(CC_TMP)/cc_native_c99_variadic_runtime
	$(TOP)/usr.bin/cc/cc native_abi_stack_int.c -o $(CC_TMP)/cc_native_abi_stack_int
	$(CC_TMP)/cc_native_abi_stack_int
	$(TOP)/usr.bin/cc/cc native_abi_stack_fp.c -o $(CC_TMP)/cc_native_abi_stack_fp
	$(CC_TMP)/cc_native_abi_stack_fp
	$(TOP)/usr.bin/cc/cc native_abi_mix.c -o $(CC_TMP)/cc_native_abi_mix
	$(CC_TMP)/cc_native_abi_mix
	$(TOP)/usr.bin/cc/cc native_if_else.c -o $(CC_TMP)/cc_native_if_else
	$(CC_TMP)/cc_native_if_else
	$(TOP)/usr.bin/cc/cc native_if_abs.c -o $(CC_TMP)/cc_native_if_abs
	$(CC_TMP)/cc_native_if_abs
	$(TOP)/usr.bin/cc/cc native_c95_digraph.c -o $(CC_TMP)/cc_native_c95_digraph
	$(CC_TMP)/cc_native_c95_digraph
	$(TOP)/usr.bin/cc/cc -std=c99 native_c95_trigraph.c -o $(CC_TMP)/cc_native_c95_trigraph
	$(CC_TMP)/cc_native_c95_trigraph
	$(TOP)/usr.bin/cc/cc -std=c99 native_c99_phase12_trigraph_splice.c -o $(CC_TMP)/cc_native_c99_phase12_trigraph_splice
	$(CC_TMP)/cc_native_c99_phase12_trigraph_splice
	$(TOP)/usr.bin/cc/cc -E -std=c23 native_c99_phase12_trigraph_splice.c -o $(CC_TMP)/cc_native_c99_phase12_trigraph_splice_c23.pp
	grep -Fq "??=define TG_RET 0" $(CC_TMP)/cc_native_c99_phase12_trigraph_splice_c23.pp
	$(TOP)/usr.bin/cc/cc native_c99_ucn_ident.c -o $(CC_TMP)/cc_native_c99_ucn_ident
	$(CC_TMP)/cc_native_c99_ucn_ident
	$(TOP)/usr.bin/cc/cc native_c99_tokenization_ppnum.c -o $(CC_TMP)/cc_native_c99_tokenization_ppnum
	$(CC_TMP)/cc_native_c99_tokenization_ppnum
	$(TOP)/usr.bin/cc/cc native_c99_line_comment.c -o $(CC_TMP)/cc_native_c99_line_comment
	$(CC_TMP)/cc_native_c99_line_comment
	$(TOP)/usr.bin/cc/cc -E -std=gnu11 native_c99_pp_defined_suffix_ident.c -o $(CC_TMP)/cc_native_c99_pp_defined_suffix_ident.i
	grep -q '^int bad;' $(CC_TMP)/cc_native_c99_pp_defined_suffix_ident.i
	$(TOP)/usr.bin/cc/cc native_c99_integer_literals.c -o $(CC_TMP)/cc_native_c99_integer_literals
	$(CC_TMP)/cc_native_c99_integer_literals
	$(TOP)/usr.bin/cc/cc native_c99_hex_float_suffix.c -o $(CC_TMP)/cc_native_c99_hex_float_suffix
	$(CC_TMP)/cc_native_c99_hex_float_suffix
	$(TOP)/usr.bin/cc/cc native_c99_wide_literals.c -o $(CC_TMP)/cc_native_c99_wide_literals
	$(CC_TMP)/cc_native_c99_wide_literals
	$(TOP)/usr.bin/cc/cc native_do_while.c -o $(CC_TMP)/cc_native_do_while
	$(CC_TMP)/cc_native_do_while
	$(TOP)/usr.bin/cc/cc native_switch.c -o $(CC_TMP)/cc_native_switch
	$(CC_TMP)/cc_native_switch
	$(TOP)/usr.bin/cc/cc native_c99_switch_constexpr.c -o $(CC_TMP)/cc_native_c99_switch_constexpr
	$(CC_TMP)/cc_native_c99_switch_constexpr
	$(TOP)/usr.bin/cc/cc native_c99_enum_constexpr.c -o $(CC_TMP)/cc_native_c99_enum_constexpr
	$(CC_TMP)/cc_native_c99_enum_constexpr
	$(TOP)/usr.bin/cc/cc native_c99_for_decl.c -o $(CC_TMP)/cc_native_c99_for_decl
	$(CC_TMP)/cc_native_c99_for_decl
	$(TOP)/usr.bin/cc/cc native_c99_for_multi_decl.c -o $(CC_TMP)/cc_native_c99_for_multi_decl
	$(CC_TMP)/cc_native_c99_for_multi_decl
	$(TOP)/usr.bin/cc/cc native_c99_types.c -o $(CC_TMP)/cc_native_c99_types
	$(CC_TMP)/cc_native_c99_types
	$(TOP)/usr.bin/cc/cc native_host_stdint_inttypes.c -o $(CC_TMP)/cc_native_host_stdint_inttypes
	$(CC_TMP)/cc_native_host_stdint_inttypes
	$(TOP)/usr.bin/cc/cc -E native_host_stdint_inttypes.c -o $(CC_TMP)/cc_native_host_stdint_inttypes.i
	! grep -q "__INTMAX_TYPE__" $(CC_TMP)/cc_native_host_stdint_inttypes.i
	! grep -q "__INT8_TYPE__" $(CC_TMP)/cc_native_host_stdint_inttypes.i
	$(TOP)/usr.bin/cc/cc -m64 -E -dM -std=gnu11 /dev/null -o $(CC_TMP)/cc_pp_gnu11_predefs.i
	grep -q "^#define __SIZEOF_INT128__ 16" $(CC_TMP)/cc_pp_gnu11_predefs.i
	$(TOP)/usr.bin/cc/cc -m32 -E -dM -std=gnu11 /dev/null -o $(CC_TMP)/cc_pp_gnu11_predefs.i
	! grep -q "^#define __SIZEOF_INT128__" $(CC_TMP)/cc_pp_gnu11_predefs.i
	! $(TOP)/usr.bin/cc/cc -E -std=bogus /dev/null -o $(CC_TMP)/cc_pp_std_bogus.i 2>$(CC_TMP)/cc_pp_std_bogus.err
	grep -q -- "-std=bogus" $(CC_TMP)/cc_pp_std_bogus.err
	$(TOP)/usr.bin/cc/cc -E -dM -std=c89 /dev/null -o $(CC_TMP)/cc_pp_c89_predefs.i
	! grep -q "__STDC_VERSION__" $(CC_TMP)/cc_pp_c89_predefs.i
	$(TOP)/usr.bin/cc/cc -E -dM -ansi /dev/null -o $(CC_TMP)/cc_pp_ansi_predefs.i
	grep -q "^#define __STRICT_ANSI__ 1" $(CC_TMP)/cc_pp_ansi_predefs.i
	! grep -q "__STDC_VERSION__" $(CC_TMP)/cc_pp_ansi_predefs.i
	$(TOP)/usr.bin/cc/cc -E -dM -std=iso9899:199409 /dev/null -o $(CC_TMP)/cc_pp_c94_predefs.i
	grep -q "^#define __STDC_VERSION__ 199409L" $(CC_TMP)/cc_pp_c94_predefs.i
	rm -f ./-
	$(TOP)/usr.bin/cc/cc -E native_c99_const_qual.c -o - > $(CC_TMP)/cc_stdout_e.i
	$(TOP)/usr.bin/cc/cc -S native_c99_const_qual.c -o - > $(CC_TMP)/cc_stdout_s.s
	test -s $(CC_TMP)/cc_stdout_e.i
	grep -q "main:" $(CC_TMP)/cc_stdout_s.s
	test ! -e ./-
	$(TOP)/usr.bin/cc/cc -### native_c99_const_qual.c -L /tmp -l m -o$(CC_TMP)/cc_drv_prog > $(CC_TMP)/cc_drv_sep.txt 2>&1
	grep -q -- "-o $(CC_TMP)/cc_drv_prog .*-L /tmp -l m" $(CC_TMP)/cc_drv_sep.txt
	! $(TOP)/usr.bin/cc/cc -E -fpp-max-tokens=2 native_c99_const_qual.c -o $(CC_TMP)/cc_pp_max_tokens.i
	$(TOP)/usr.bin/cc/cc -S -O native_c99_const_qual.c -o $(CC_TMP)/cc_opt_bare.s
	$(TOP)/usr.bin/cc/cc -S -O1 native_c99_const_qual.c -o $(CC_TMP)/cc_opt_1.s
	$(TOP)/usr.bin/cc/cc -S -O0 native_c99_const_qual.c -o $(CC_TMP)/cc_opt_0.s
	cmp -s $(CC_TMP)/cc_opt_bare.s $(CC_TMP)/cc_opt_1.s
	! cmp -s $(CC_TMP)/cc_opt_bare.s $(CC_TMP)/cc_opt_0.s
	$(TOP)/usr.bin/cc/cc native_c99_const_qual.c -o $(CC_TMP)/cc_native_c99_const_qual
	$(CC_TMP)/cc_native_c99_const_qual
	$(TOP)/usr.bin/cc/cc native_c99_composite_types.c -o $(CC_TMP)/cc_native_c99_composite_types
	$(CC_TMP)/cc_native_c99_composite_types
	$(TOP)/usr.bin/cc/cc -std=c99 native_c99_inline_semantics.c -o $(CC_TMP)/cc_native_c99_inline_semantics
	$(CC_TMP)/cc_native_c99_inline_semantics
	$(TOP)/usr.bin/cc/cc -std=c99 -S native_c99_inline_semantics.c -o $(CC_TMP)/cc_native_c99_inline_semantics.s
	! grep -q "^\\.globl add_local$$" $(CC_TMP)/cc_native_c99_inline_semantics.s
	grep -q "^\\.globl add_ext$$" $(CC_TMP)/cc_native_c99_inline_semantics.s
	grep -q "^\\.globl add_both$$" $(CC_TMP)/cc_native_c99_inline_semantics.s
	$(TOP)/usr.bin/cc/cc native_c99_complex_decl.c -o $(CC_TMP)/cc_native_c99_complex_decl
	$(CC_TMP)/cc_native_c99_complex_decl
	$(TOP)/usr.bin/cc/cc native_c99_imaginary_decl.c -o $(CC_TMP)/cc_native_c99_imaginary_decl
	$(CC_TMP)/cc_native_c99_imaginary_decl
	$(TOP)/usr.bin/cc/cc native_c99_complex_types.c -o $(CC_TMP)/cc_native_c99_complex_types
	$(CC_TMP)/cc_native_c99_complex_types
	$(TOP)/usr.bin/cc/cc native_c99_param_array_qual.c -o $(CC_TMP)/cc_native_c99_param_array_qual
	$(CC_TMP)/cc_native_c99_param_array_qual
	$(TOP)/usr.bin/cc/cc native_c99_restrict_ptr.c -o $(CC_TMP)/cc_native_c99_restrict_ptr
	$(CC_TMP)/cc_native_c99_restrict_ptr
	$(TOP)/usr.bin/cc/cc native_c99_vla_decl.c -o $(CC_TMP)/cc_native_c99_vla_decl
	$(CC_TMP)/cc_native_c99_vla_decl
	$(TOP)/usr.bin/cc/cc -std=c90 native_c90_oldstyle.c -o $(CC_TMP)/cc_native_c90_oldstyle
	$(CC_TMP)/cc_native_c90_oldstyle
	$(TOP)/usr.bin/cc/cc -std=c90 -S native_c90_unproto_call_promotions.c -o $(CC_TMP)/cc_native_c90_unproto_call_promotions.s
	grep -q "call ext" $(CC_TMP)/cc_native_c90_unproto_call_promotions.s
	$(TOP)/usr.bin/cc/cc native_c99_typedef.c -o $(CC_TMP)/cc_native_c99_typedef
	$(CC_TMP)/cc_native_c99_typedef
	$(TOP)/usr.bin/cc/cc native_c99_typedef_array_member_layout.c -o $(CC_TMP)/cc_native_c99_typedef_array_member_layout
	$(CC_TMP)/cc_native_c99_typedef_array_member_layout
	$(TOP)/usr.bin/cc/cc native_c99_bitfield_init_slots.c -o $(CC_TMP)/cc_native_c99_bitfield_init_slots
	$(CC_TMP)/cc_native_c99_bitfield_init_slots
	$(TOP)/usr.bin/cc/cc native_c99_typedef_attr.c -o $(CC_TMP)/cc_native_c99_typedef_attr
	$(CC_TMP)/cc_native_c99_typedef_attr
	$(TOP)/usr.bin/cc/cc native_c99_typedef_fnptr.c -o $(CC_TMP)/cc_native_c99_typedef_fnptr
	$(CC_TMP)/cc_native_c99_typedef_fnptr
	$(TOP)/usr.bin/cc/cc native_c99_typedef_fnptr_retptr.c -o $(CC_TMP)/cc_native_c99_typedef_fnptr_retptr
	$(CC_TMP)/cc_native_c99_typedef_fnptr_retptr
	$(TOP)/usr.bin/cc/cc native_c99_indirect_call_deref.c -o $(CC_TMP)/cc_native_c99_indirect_call_deref
	$(CC_TMP)/cc_native_c99_indirect_call_deref
	$(TOP)/usr.bin/cc/cc native_c99_indirect_small_aggregate_arg.c -o $(CC_TMP)/cc_native_c99_indirect_small_aggregate_arg
	$(CC_TMP)/cc_native_c99_indirect_small_aggregate_arg
	$(TOP)/usr.bin/cc/cc native_c99_grouped_fn_param_decay.c -o $(CC_TMP)/cc_native_c99_grouped_fn_param_decay
	$(CC_TMP)/cc_native_c99_grouped_fn_param_decay
	$(TOP)/usr.bin/cc/cc native_c99_attr_codegen.c -o $(CC_TMP)/cc_native_c99_attr_codegen
	$(CC_TMP)/cc_native_c99_attr_codegen
	$(TOP)/usr.bin/cc/cc -S native_c99_attr_codegen.c -o $(CC_TMP)/cc_native_c99_attr_codegen.s
	grep -Eq "^[[:space:]]*\\.section \\.hotdata" $(CC_TMP)/cc_native_c99_attr_codegen.s
	grep -A4 -E "^[[:space:]]*\\.section \\.ptrdata" $(CC_TMP)/cc_native_c99_attr_codegen.s | grep -q "^hot_ptr:"
	grep -A2 -E "^[[:space:]]*\\.section \\.text\\.ptrfn" $(CC_TMP)/cc_native_c99_attr_codegen.s | grep -q "^ptr_fn:"
	grep -Eq "^[[:space:]]*\\.section \\.text\\.panic" $(CC_TMP)/cc_native_c99_attr_codegen.s
	grep -q "^\\.align 32$$" $(CC_TMP)/cc_native_c99_attr_codegen.s
	grep -q "^\\.align 16$$" $(CC_TMP)/cc_native_c99_attr_codegen.s
	LD=/usr/bin/ld $(TOP)/usr.bin/cc/cc native_c99_ctor_dtor_attr.c -o $(CC_TMP)/cc_native_c99_ctor_dtor_attr
	$(TOP)/usr.bin/cc/cc -S native_c99_ctor_dtor_attr.c -o $(CC_TMP)/cc_native_c99_ctor_dtor_attr.s
	grep -Eq "^[[:space:]]*\\.section \\.init_array" $(CC_TMP)/cc_native_c99_ctor_dtor_attr.s
	grep -Eq "^[[:space:]]*\\.(quad|long) ctor$$" $(CC_TMP)/cc_native_c99_ctor_dtor_attr.s
	grep -Eq "^[[:space:]]*\\.section \\.fini_array" $(CC_TMP)/cc_native_c99_ctor_dtor_attr.s
	grep -Eq "^[[:space:]]*\\.(quad|long) dtor$$" $(CC_TMP)/cc_native_c99_ctor_dtor_attr.s
	$(CC_TMP)/cc_native_c99_ctor_dtor_attr 2>$(CC_TMP)/cc_native_c99_ctor_dtor_attr.err
	grep -q '^D$$' $(CC_TMP)/cc_native_c99_ctor_dtor_attr.err
	$(TOP)/usr.bin/cc/cc native_c99_gnu_declspec_aliases.c -o $(CC_TMP)/cc_native_c99_gnu_declspec_aliases
	$(CC_TMP)/cc_native_c99_gnu_declspec_aliases
	$(TOP)/usr.bin/cc/cc native_c99_multi_decl.c -o $(CC_TMP)/cc_native_c99_multi_decl
	$(CC_TMP)/cc_native_c99_multi_decl
	$(TOP)/usr.bin/cc/cc native_c99_char_hex.c -o $(CC_TMP)/cc_native_c99_char_hex
	$(CC_TMP)/cc_native_c99_char_hex
	$(TOP)/usr.bin/cc/cc native_c99_char_cast_sign.c -o $(CC_TMP)/cc_native_c99_char_cast_sign
	$(CC_TMP)/cc_native_c99_char_cast_sign
	$(TOP)/usr.bin/cc/cc native_c99_signed_char_type.c -o $(CC_TMP)/cc_native_c99_signed_char_type
	$(CC_TMP)/cc_native_c99_signed_char_type
	$(TOP)/usr.bin/cc/cc native_c99_logic.c -o $(CC_TMP)/cc_native_c99_logic
	$(CC_TMP)/cc_native_c99_logic
	$(TOP)/usr.bin/cc/cc native_c99_inc_expr.c -o $(CC_TMP)/cc_native_c99_inc_expr
	$(CC_TMP)/cc_native_c99_inc_expr
	$(TOP)/usr.bin/cc/cc native_c99_update.c -o $(CC_TMP)/cc_native_c99_update
	$(CC_TMP)/cc_native_c99_update
	$(TOP)/usr.bin/cc/cc native_c99_bitwise.c -o $(CC_TMP)/cc_native_c99_bitwise
	$(CC_TMP)/cc_native_c99_bitwise
	$(TOP)/usr.bin/cc/cc native_c99_builtin_mul_overflow_signed_to_unsigned.c -o $(CC_TMP)/cc_native_c99_builtin_mul_overflow_signed_to_unsigned
	$(CC_TMP)/cc_native_c99_builtin_mul_overflow_signed_to_unsigned
	$(TOP)/usr.bin/cc/cc native_c99_comma.c -o $(CC_TMP)/cc_native_c99_comma
	$(CC_TMP)/cc_native_c99_comma
	$(TOP)/usr.bin/cc/cc native_c99_goto.c -o $(CC_TMP)/cc_native_c99_goto
	$(CC_TMP)/cc_native_c99_goto
	$(TOP)/usr.bin/cc/cc native_c99_goto_loop.c -o $(CC_TMP)/cc_native_c99_goto_loop
	$(CC_TMP)/cc_native_c99_goto_loop
	$(TOP)/usr.bin/cc/cc native_c99_computed_goto.c -o $(CC_TMP)/cc_native_c99_computed_goto
	$(CC_TMP)/cc_native_c99_computed_goto
	$(TOP)/usr.bin/cc/cc native_c99_ternary.c -o $(CC_TMP)/cc_native_c99_ternary
	$(CC_TMP)/cc_native_c99_ternary
	$(TOP)/usr.bin/cc/cc native_c99_ternary_signed_to_size_t.c -o $(CC_TMP)/cc_native_c99_ternary_signed_to_size_t
	$(CC_TMP)/cc_native_c99_ternary_signed_to_size_t
	$(TOP)/usr.bin/cc/cc native_c99_cast_sizeof.c -o $(CC_TMP)/cc_native_c99_cast_sizeof
	$(CC_TMP)/cc_native_c99_cast_sizeof
	$(TOP)/usr.bin/cc/cc native_c99_expr_conversions.c -o $(CC_TMP)/cc_native_c99_expr_conversions
	$(CC_TMP)/cc_native_c99_expr_conversions
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_c99_unary_integer_promotion.c -o $(CC_TMP)/cc_native_c99_unary_integer_promotion
	$(CC_TMP)/cc_native_c99_unary_integer_promotion
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_c99_static_local_string_pointer_array.c -o $(CC_TMP)/cc_native_c99_static_local_string_pointer_array
	$(CC_TMP)/cc_native_c99_static_local_string_pointer_array
	$(TOP)/usr.bin/cc/cc native_c99_sizeof_array.c -o $(CC_TMP)/cc_native_c99_sizeof_array
	$(CC_TMP)/cc_native_c99_sizeof_array
	$(TOP)/usr.bin/cc/cc native_c99_sizeof_prev_struct_array_bound.c -o $(CC_TMP)/cc_native_c99_sizeof_prev_struct_array_bound
	$(CC_TMP)/cc_native_c99_sizeof_prev_struct_array_bound
	LD=/usr/bin/ld $(TOP)/usr.bin/cc/cc native_c99_sizeof_unsigned.c -o $(CC_TMP)/cc_native_c99_sizeof_unsigned
	$(CC_TMP)/cc_native_c99_sizeof_unsigned
	$(TOP)/usr.bin/cc/cc native_c99_sizeof_local_array_cmp.c -o $(CC_TMP)/cc_native_c99_sizeof_local_array_cmp
	$(CC_TMP)/cc_native_c99_sizeof_local_array_cmp
	$(TOP)/usr.bin/cc/cc native_c99_float_cmp.c -o $(CC_TMP)/cc_native_c99_float_cmp
	$(CC_TMP)/cc_native_c99_float_cmp
	$(TOP)/usr.bin/cc/cc native_c99_math_classify_builtins.c -o $(CC_TMP)/cc_native_c99_math_classify_builtins
	$(CC_TMP)/cc_native_c99_math_classify_builtins
	$(TOP)/usr.bin/cc/cc -O2 native_c99_float_signbit_negzero_nan.c -o $(CC_TMP)/cc_native_c99_float_signbit_negzero_nan
	$(CC_TMP)/cc_native_c99_float_signbit_negzero_nan
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_c11_alignas_zero.c -o $(CC_TMP)/cc_native_c11_alignas_zero
	$(CC_TMP)/cc_native_c11_alignas_zero
	$(TOP)/usr.bin/cc/cc native_c99_long_double_type.c -o $(CC_TMP)/cc_native_c99_long_double_type
	$(CC_TMP)/cc_native_c99_long_double_type
	$(TOP)/usr.bin/cc/cc native_c99_long_double_f2i_round.c -o $(CC_TMP)/cc_native_c99_long_double_f2i_round
	$(CC_TMP)/cc_native_c99_long_double_f2i_round
	$(TOP)/usr.bin/cc/cc native_c99_long_double_ternary_return.c -o $(CC_TMP)/cc_native_c99_long_double_ternary_return
	$(CC_TMP)/cc_native_c99_long_double_ternary_return
	$(TOP)/usr.bin/cc/cc native_c99_long_double_hex_intmax_const.c -o $(CC_TMP)/cc_native_c99_long_double_hex_intmax_const
	$(CC_TMP)/cc_native_c99_long_double_hex_intmax_const
	$(TOP)/usr.bin/cc/cc native_c99_long_double_param_precision.c -o $(CC_TMP)/cc_native_c99_long_double_param_precision
	$(CC_TMP)/cc_native_c99_long_double_param_precision
	$(TOP)/usr.bin/cc/cc native_c99_long_double_limits.c -o $(CC_TMP)/cc_native_c99_long_double_limits
	$(CC_TMP)/cc_native_c99_long_double_limits
	$(TOP)/usr.bin/cc/cc native_c99_long_double_i2f_precision.c -o $(CC_TMP)/cc_native_c99_long_double_i2f_precision
	$(CC_TMP)/cc_native_c99_long_double_i2f_precision
	$(TOP)/usr.bin/cc/cc native_c99_long_double_unsigned_i2f.c -o $(CC_TMP)/cc_native_c99_long_double_unsigned_i2f
	$(CC_TMP)/cc_native_c99_long_double_unsigned_i2f
	$(TOP)/usr.bin/cc/cc native_c99_long_double_int_param.c -o $(CC_TMP)/cc_native_c99_long_double_int_param
	$(CC_TMP)/cc_native_c99_long_double_int_param
	$(TOP)/usr.bin/cc/cc -O2 native_c99_long_double_const_promote.c -o $(CC_TMP)/cc_native_c99_long_double_const_promote
	$(CC_TMP)/cc_native_c99_long_double_const_promote
	$(TOP)/usr.bin/cc/cc native_c99_unsigned.c -o $(CC_TMP)/cc_native_c99_unsigned
	$(CC_TMP)/cc_native_c99_unsigned
	$(TOP)/usr.bin/cc/cc native_c99_unsigned_literals.c -o $(CC_TMP)/cc_native_c99_unsigned_literals
	$(CC_TMP)/cc_native_c99_unsigned_literals
	$(TOP)/usr.bin/cc/cc native_c99_short.c -o $(CC_TMP)/cc_native_c99_short
	$(CC_TMP)/cc_native_c99_short
	$(TOP)/usr.bin/cc/cc native_c99_pointer.c -o $(CC_TMP)/cc_native_c99_pointer
	$(CC_TMP)/cc_native_c99_pointer
	$(TOP)/usr.bin/cc/cc native_c99_pointer_param.c -o $(CC_TMP)/cc_native_c99_pointer_param
	$(CC_TMP)/cc_native_c99_pointer_param
	$(TOP)/usr.bin/cc/cc native_c99_pointer_store.c -o $(CC_TMP)/cc_native_c99_pointer_store
	$(CC_TMP)/cc_native_c99_pointer_store
	$(TOP)/usr.bin/cc/cc native_c99_pointer_store_param.c -o $(CC_TMP)/cc_native_c99_pointer_store_param
	$(CC_TMP)/cc_native_c99_pointer_store_param
	$(TOP)/usr.bin/cc/cc native_c99_cast_pointer_subscript_stride.c -o $(CC_TMP)/cc_native_c99_cast_pointer_subscript_stride
	$(CC_TMP)/cc_native_c99_cast_pointer_subscript_stride
	$(TOP)/usr.bin/cc/cc native_c99_addr_taken_slot_reuse.c -o $(CC_TMP)/cc_native_c99_addr_taken_slot_reuse
	$(CC_TMP)/cc_native_c99_addr_taken_slot_reuse
	$(TOP)/usr.bin/cc/cc native_c99_deref_compound.c -o $(CC_TMP)/cc_native_c99_deref_compound
	$(CC_TMP)/cc_native_c99_deref_compound
	$(TOP)/usr.bin/cc/cc native_c99_deref_prefix_update.c -o $(CC_TMP)/cc_native_c99_deref_prefix_update
	$(CC_TMP)/cc_native_c99_deref_prefix_update
	$(TOP)/usr.bin/cc/cc native_c99_deref_postfix_update.c -o $(CC_TMP)/cc_native_c99_deref_postfix_update
	$(CC_TMP)/cc_native_c99_deref_postfix_update
	$(TOP)/usr.bin/cc/cc native_c99_pointer_arith.c -o $(CC_TMP)/cc_native_c99_pointer_arith
	$(CC_TMP)/cc_native_c99_pointer_arith
	$(TOP)/usr.bin/cc/cc native_c99_index.c -o $(CC_TMP)/cc_native_c99_index
	$(CC_TMP)/cc_native_c99_index
	$(TOP)/usr.bin/cc/cc native_c99_pointer_diff.c -o $(CC_TMP)/cc_native_c99_pointer_diff
	$(CC_TMP)/cc_native_c99_pointer_diff
	$(TOP)/usr.bin/cc/cc native_c99_ptr_cast.c -o $(CC_TMP)/cc_native_c99_ptr_cast
	$(CC_TMP)/cc_native_c99_ptr_cast
	$(TOP)/usr.bin/cc/cc native_c99_ptr_size.c -o $(CC_TMP)/cc_native_c99_ptr_size
	$(CC_TMP)/cc_native_c99_ptr_size
	$(TOP)/usr.bin/cc/cc native_c99_ptrptr.c -o $(CC_TMP)/cc_native_c99_ptrptr
	$(CC_TMP)/cc_native_c99_ptrptr
	$(TOP)/usr.bin/cc/cc native_c99_ptrptrptr.c -o $(CC_TMP)/cc_native_c99_ptrptrptr
	$(CC_TMP)/cc_native_c99_ptrptrptr
	$(TOP)/usr.bin/cc/cc native_c99_ptr4.c -o $(CC_TMP)/cc_native_c99_ptr4
	$(CC_TMP)/cc_native_c99_ptr4
	$(TOP)/usr.bin/cc/cc native_c99_ptr_depth6.c -o $(CC_TMP)/cc_native_c99_ptr_depth6
	$(CC_TMP)/cc_native_c99_ptr_depth6
	$(TOP)/usr.bin/cc/cc native_c99_pointer_update.c -o $(CC_TMP)/cc_native_c99_pointer_update
	$(CC_TMP)/cc_native_c99_pointer_update
	$(TOP)/usr.bin/cc/cc native_c99_pointer_array_steps.c -o $(CC_TMP)/cc_native_c99_pointer_array_steps
	$(CC_TMP)/cc_native_c99_pointer_array_steps
	$(TOP)/usr.bin/cc/cc native_c99_pointer_array_param.c -o $(CC_TMP)/cc_native_c99_pointer_array_param
	$(CC_TMP)/cc_native_c99_pointer_array_param | grep -q '^Jul$$'
	$(TOP)/usr.bin/cc/cc native_c99_param_array_constexpr_bound.c -o $(CC_TMP)/cc_native_c99_param_array_constexpr_bound
	$(CC_TMP)/cc_native_c99_param_array_constexpr_bound | grep -q '^Jul$$'
	$(TOP)/usr.bin/cc/cc native_c99_pointer_sub_array_decay.c -o $(CC_TMP)/cc_native_c99_pointer_sub_array_decay
	$(CC_TMP)/cc_native_c99_pointer_sub_array_decay
	$(TOP)/usr.bin/cc/cc native_c99_local_struct_array_2d.c -o $(CC_TMP)/cc_native_c99_local_struct_array_2d
	$(CC_TMP)/cc_native_c99_local_struct_array_2d | grep -q '^128$$'
	$(TOP)/usr.bin/cc/cc native_c99_octal_char.c -o $(CC_TMP)/cc_native_c99_octal_char
	$(CC_TMP)/cc_native_c99_octal_char
	$(TOP)/usr.bin/cc/cc native_c99_char_octal_signedness.c -o $(CC_TMP)/cc_native_c99_char_octal_signedness
	$(CC_TMP)/cc_native_c99_char_octal_signedness
	$(TOP)/usr.bin/cc/cc native_c99_string_concat.c -o $(CC_TMP)/cc_native_c99_string_concat
	$(CC_TMP)/cc_native_c99_string_concat
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_c99_string_concat_octal_boundary.c -o $(CC_TMP)/cc_native_c99_string_concat_octal_boundary
	$(CC_TMP)/cc_native_c99_string_concat_octal_boundary
	$(TOP)/usr.bin/cc/cc native_c99_global_string_embedded_nul.c -o $(CC_TMP)/cc_native_c99_global_string_embedded_nul
	$(CC_TMP)/cc_native_c99_global_string_embedded_nul
	$(TOP)/usr.bin/cc/cc native_c99_typeof_string_array.c -o $(CC_TMP)/cc_native_c99_typeof_string_array
	$(CC_TMP)/cc_native_c99_typeof_string_array
	$(TOP)/usr.bin/cc/cc native_c99_typeof_compound_array.c -o $(CC_TMP)/cc_native_c99_typeof_compound_array
	$(CC_TMP)/cc_native_c99_typeof_compound_array
	$(TOP)/usr.bin/cc/cc native_c99_compound_literal_array_decay.c -o $(CC_TMP)/cc_native_c99_compound_literal_array_decay
	$(CC_TMP)/cc_native_c99_compound_literal_array_decay
	$(TOP)/usr.bin/cc/cc native_c99_func_name.c -o $(CC_TMP)/cc_native_c99_func_name
	$(CC_TMP)/cc_native_c99_func_name
	$(TOP)/usr.bin/cc/cc native_c99_atomic_fetch.c -o $(CC_TMP)/cc_native_c99_atomic_fetch
	$(CC_TMP)/cc_native_c99_atomic_fetch
	$(TOP)/usr.bin/cc/cc -S native_c99_builtin_strlen.c -o $(CC_TMP)/cc_native_c99_builtin_strlen.s
	grep -q "call strlen" $(CC_TMP)/cc_native_c99_builtin_strlen.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -c native_gnu_builtin_prefetch.c -o $(CC_TMP)/cc_native_gnu_builtin_prefetch.o
	printf '%s\n' '#include <error.h>' 'int main(void) { return 0; }' > $(CC_TMP)/cc_host_error_h_probe.c
	$(TOP)/usr.bin/cc/cc -std=gnu11 -O2 -c $(CC_TMP)/cc_host_error_h_probe.c -o $(CC_TMP)/cc_host_error_h_probe.o
	$(TOP)/usr.bin/cc/cc -std=gnu11 -c native_gnu_static_inline_extern.c -o $(CC_TMP)/cc_native_gnu_static_inline_extern.o
	$(TOP)/usr.bin/cc/cc native_c99_global_array_init.c -o $(CC_TMP)/cc_native_c99_global_array_init
	$(CC_TMP)/cc_native_c99_global_array_init
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_c99_global_pointer_arith_init.c -o $(CC_TMP)/cc_native_c99_global_pointer_arith_init
	$(CC_TMP)/cc_native_c99_global_pointer_arith_init
	$(TOP)/usr.bin/cc/cc native_c99_global_init_list.c -o $(CC_TMP)/cc_native_c99_global_init_list
	$(CC_TMP)/cc_native_c99_global_init_list
	$(TOP)/usr.bin/cc/cc native_c99_local_init_list.c -o $(CC_TMP)/cc_native_c99_local_init_list
	$(CC_TMP)/cc_native_c99_local_init_list
	$(TOP)/usr.bin/cc/cc native_c99_storage_tentative.c -o $(CC_TMP)/cc_native_c99_storage_tentative
	$(CC_TMP)/cc_native_c99_storage_tentative
	$(TOP)/usr.bin/cc/cc native_c99_global_bool_addr.c -o $(CC_TMP)/cc_native_c99_global_bool_addr
	$(CC_TMP)/cc_native_c99_global_bool_addr
	$(TOP)/usr.bin/cc/cc native_c99_designated_init.c -o $(CC_TMP)/cc_native_c99_designated_init
	$(CC_TMP)/cc_native_c99_designated_init
	$(TOP)/usr.bin/cc/cc native_c99_compound_literal.c -o $(CC_TMP)/cc_native_c99_compound_literal
	$(CC_TMP)/cc_native_c99_compound_literal
	$(TOP)/usr.bin/cc/cc native_c99_flexible_array.c -o $(CC_TMP)/cc_native_c99_flexible_array
	$(CC_TMP)/cc_native_c99_flexible_array
	$(TOP)/usr.bin/cc/cc native_c99_flexible_array_global_init.c -o $(CC_TMP)/cc_native_c99_flexible_array_global_init
	$(CC_TMP)/cc_native_c99_flexible_array_global_init
	$(TOP)/usr.bin/cc/cc native_c99_pragma.c -o $(CC_TMP)/cc_native_c99_pragma
	$(CC_TMP)/cc_native_c99_pragma
	$(TOP)/usr.bin/cc/cc native_c99_stdc_pragma.c -o $(CC_TMP)/cc_native_c99_stdc_pragma
	$(CC_TMP)/cc_native_c99_stdc_pragma
	$(TOP)/usr.bin/cc/cc native_c99_struct_assign.c -o $(CC_TMP)/cc_native_c99_struct_assign
	$(CC_TMP)/cc_native_c99_struct_assign
	$(TOP)/usr.bin/cc/cc native_c99_struct_ptr_arith.c -o $(CC_TMP)/cc_native_c99_struct_ptr_arith
	$(CC_TMP)/cc_native_c99_struct_ptr_arith
	$(TOP)/usr.bin/cc/cc native_c99_struct_member_array.c -o $(CC_TMP)/cc_native_c99_struct_member_array
	$(CC_TMP)/cc_native_c99_struct_member_array
	$(TOP)/usr.bin/cc/cc native_c99_struct_array_init.c -o $(CC_TMP)/cc_native_c99_struct_array_init
	$(CC_TMP)/cc_native_c99_struct_array_init
	$(TOP)/usr.bin/cc/cc native_c99_struct_copy_init.c -o $(CC_TMP)/cc_native_c99_struct_copy_init
	$(CC_TMP)/cc_native_c99_struct_copy_init
	$(TOP)/usr.bin/cc/cc native_c99_struct_pass_value.c -o $(CC_TMP)/cc_native_c99_struct_pass_value
	$(CC_TMP)/cc_native_c99_struct_pass_value
	$(TOP)/usr.bin/cc/cc native_c99_struct_fnptr_pass_value.c -o $(CC_TMP)/cc_native_c99_struct_fnptr_pass_value
	$(CC_TMP)/cc_native_c99_struct_fnptr_pass_value
	$(TOP)/usr.bin/cc/cc native_c99_transparent_union_call.c -o $(CC_TMP)/cc_native_c99_transparent_union_call
	$(CC_TMP)/cc_native_c99_transparent_union_call
	$(TOP)/usr.bin/cc/cc native_c99_struct_return_value.c -o $(CC_TMP)/cc_native_c99_struct_return_value
	$(CC_TMP)/cc_native_c99_struct_return_value
	$(TOP)/usr.bin/cc/cc native_c99_struct_return_16.c -o $(CC_TMP)/cc_native_c99_struct_return_16
	$(CC_TMP)/cc_native_c99_struct_return_16
	$(TOP)/usr.bin/cc/cc native_c99_anonymous_struct_union.c -o $(CC_TMP)/cc_native_c99_anonymous_struct_union
	$(CC_TMP)/cc_native_c99_anonymous_struct_union
	$(TOP)/usr.bin/cc/cc native_c99_bitfield_basic.c -o $(CC_TMP)/cc_native_c99_bitfield_basic
	$(CC_TMP)/cc_native_c99_bitfield_basic
	$(TOP)/usr.bin/cc/cc native_c99_consteval_limits.c -o $(CC_TMP)/cc_native_c99_consteval_limits
	$(CC_TMP)/cc_native_c99_consteval_limits
	$(TOP)/usr.bin/cc/cc native_c99_void_pointer.c -o $(CC_TMP)/cc_native_c99_void_pointer
	$(CC_TMP)/cc_native_c99_void_pointer
	$(TOP)/usr.bin/cc/cc native_c99_pointer_cmp.c -o $(CC_TMP)/cc_native_c99_pointer_cmp
	$(CC_TMP)/cc_native_c99_pointer_cmp
	$(TOP)/usr.bin/cc/cc native_c99_decay.c -o $(CC_TMP)/cc_native_c99_decay
	$(CC_TMP)/cc_native_c99_decay
	$(TOP)/usr.bin/cc/cc native_c99_ptr_to_array_decay.c -o $(CC_TMP)/cc_native_c99_ptr_to_array_decay
	$(CC_TMP)/cc_native_c99_ptr_to_array_decay
	$(TOP)/usr.bin/cc/cc native_c99_local_array_decay_addr.c -o $(CC_TMP)/cc_native_c99_local_array_decay_addr
	$(CC_TMP)/cc_native_c99_local_array_decay_addr
	$(TOP)/usr.bin/cc/cc native_c99_block_scope_shadow.c -o $(CC_TMP)/cc_native_c99_block_scope_shadow
	$(CC_TMP)/cc_native_c99_block_scope_shadow
	$(TOP)/usr.bin/cc/cc native_c99_typedef_shadow_expr.c -o $(CC_TMP)/cc_native_c99_typedef_shadow_expr
	$(CC_TMP)/cc_native_c99_typedef_shadow_expr
	$(TOP)/usr.bin/cc/cc native_c99_mixed_decl_stmt.c -o $(CC_TMP)/cc_native_c99_mixed_decl_stmt
	$(CC_TMP)/cc_native_c99_mixed_decl_stmt
	$(TOP)/usr.bin/cc/cc native_c99_return_conversion.c -o $(CC_TMP)/cc_native_c99_return_conversion
	$(CC_TMP)/cc_native_c99_return_conversion
	$(TOP)/usr.bin/cc/cc native_c99_unproto_decl_call.c -o $(CC_TMP)/cc_native_c99_unproto_decl_call
	$(CC_TMP)/cc_native_c99_unproto_decl_call
	$(TOP)/usr.bin/cc/cc -S native_c99_unproto_extern_call.c -o $(CC_TMP)/cc_native_c99_unproto_extern_call.s
	grep -q "call ext" $(CC_TMP)/cc_native_c99_unproto_extern_call.s
	$(TOP)/usr.bin/cc/cc native_c99_proto_call.c -o $(CC_TMP)/cc_native_c99_proto_call
	$(CC_TMP)/cc_native_c99_proto_call
	$(TOP)/usr.bin/cc/cc -S native_c99_proto_extern.c -o $(CC_TMP)/cc_native_c99_proto_extern.s
	grep -q "call extmul" $(CC_TMP)/cc_native_c99_proto_extern.s
	$(TOP)/usr.bin/cc/cc native_loops.c -o $(CC_TMP)/cc_native_loops
	$(CC_TMP)/cc_native_loops
	$(TOP)/usr.bin/cc/cc native_bad_if_assign.c -o $(CC_TMP)/cc_native_if_assign
	$(CC_TMP)/cc_native_if_assign
	$(TOP)/usr.bin/cc/cc native_variadic_call.c -o $(CC_TMP)/cc_native_variadic_call
	$(CC_TMP)/cc_native_variadic_call
	$(TOP)/usr.bin/cc/cc native_c99_variadic_fnptr.c -o $(CC_TMP)/cc_native_c99_variadic_fnptr
	$(CC_TMP)/cc_native_c99_variadic_fnptr
	$(TOP)/usr.bin/cc/cc native_c99_va_args.c -o $(CC_TMP)/cc_native_c99_va_args
	$(CC_TMP)/cc_native_c99_va_args
	$(TOP)/usr.bin/cc/cc native_c99_extension_expr.c -o $(CC_TMP)/cc_native_c99_extension_expr
	$(CC_TMP)/cc_native_c99_extension_expr
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_typeof_alt.c -o $(CC_TMP)/cc_native_gnu_typeof_alt
	$(CC_TMP)/cc_native_gnu_typeof_alt
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_builtins.c -o $(CC_TMP)/cc_native_gnu_builtins
	$(CC_TMP)/cc_native_gnu_builtins
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_floatn_builtins.c -o $(CC_TMP)/cc_native_gnu_floatn_builtins
	$(CC_TMP)/cc_native_gnu_floatn_builtins
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_function_macros.c -o $(CC_TMP)/cc_native_gnu_function_macros
	$(CC_TMP)/cc_native_gnu_function_macros
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_stmt_expr.c -o $(CC_TMP)/cc_native_gnu_stmt_expr
	$(CC_TMP)/cc_native_gnu_stmt_expr
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_stmt_expr.c -o $(CC_TMP)/cc_native_gnu_stmt_expr.s
	grep -q "imulq" $(CC_TMP)/cc_native_gnu_stmt_expr.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_stmt_expr.c -o $(CC_TMP)/cc_native_gnu_stmt_expr_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 native_gnu_stmt_expr.c -o $(CC_TMP)/cc_native_gnu_stmt_expr_32
	$(CC_TMP)/cc_native_gnu_stmt_expr_32
	grep -q "imull" $(CC_TMP)/cc_native_gnu_stmt_expr_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_extension_cast_stmt_expr.c -o $(CC_TMP)/cc_native_gnu_extension_cast_stmt_expr
	$(CC_TMP)/cc_native_gnu_extension_cast_stmt_expr
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_label_decl.c -o $(CC_TMP)/cc_native_gnu_label_decl
	$(CC_TMP)/cc_native_gnu_label_decl
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_label_decl.c -o $(CC_TMP)/cc_native_gnu_label_decl.s
	grep -Eq "leaq \\.Lmain_[0-9]+\\(%rip\\)" $(CC_TMP)/cc_native_gnu_label_decl.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_label_decl.c -o $(CC_TMP)/cc_native_gnu_label_decl_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 native_gnu_label_decl.c -o $(CC_TMP)/cc_native_gnu_label_decl_32
	$(CC_TMP)/cc_native_gnu_label_decl_32
	grep -Eq "movl \\$$\\.Lmain_[0-9]+, %eax" $(CC_TMP)/cc_native_gnu_label_decl_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_elvis.c -o $(CC_TMP)/cc_native_gnu_elvis
	$(CC_TMP)/cc_native_gnu_elvis
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_elvis.c -o $(CC_TMP)/cc_native_gnu_elvis.s
	test "$$(grep -c "cmpq" $(CC_TMP)/cc_native_gnu_elvis.s)" -ge 2
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_elvis.c -o $(CC_TMP)/cc_native_gnu_elvis_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 native_gnu_elvis.c -o $(CC_TMP)/cc_native_gnu_elvis_32
	$(CC_TMP)/cc_native_gnu_elvis_32
	test "$$(grep -c "cmpl" $(CC_TMP)/cc_native_gnu_elvis_32.s)" -ge 2
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_case_range.c -o $(CC_TMP)/cc_native_gnu_case_range
	$(CC_TMP)/cc_native_gnu_case_range
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_case_range.c -o $(CC_TMP)/cc_native_gnu_case_range.s
	grep -q "setge" $(CC_TMP)/cc_native_gnu_case_range.s
	grep -q "setle" $(CC_TMP)/cc_native_gnu_case_range.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_case_range.c -o $(CC_TMP)/cc_native_gnu_case_range_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 native_gnu_case_range.c -o $(CC_TMP)/cc_native_gnu_case_range_32
	$(CC_TMP)/cc_native_gnu_case_range_32
	grep -q "setge" $(CC_TMP)/cc_native_gnu_case_range_32.s
	grep -q "setle" $(CC_TMP)/cc_native_gnu_case_range_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_types_choose.c -o $(CC_TMP)/cc_native_gnu_types_choose
	$(CC_TMP)/cc_native_gnu_types_choose
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_types_choose.c -o $(CC_TMP)/cc_native_gnu_types_choose.s
	! grep -q "never_called" $(CC_TMP)/cc_native_gnu_types_choose.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_overflow.c -o $(CC_TMP)/cc_native_gnu_overflow
	$(CC_TMP)/cc_native_gnu_overflow
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_overflow.c -o $(CC_TMP)/cc_native_gnu_overflow.s
	grep -q "setb" $(CC_TMP)/cc_native_gnu_overflow.s
	grep -q "imulq" $(CC_TMP)/cc_native_gnu_overflow.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_overflow.c -o $(CC_TMP)/cc_native_gnu_overflow_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 native_gnu_overflow.c -o $(CC_TMP)/cc_native_gnu_overflow_32
	$(CC_TMP)/cc_native_gnu_overflow_32
	grep -q "setb" $(CC_TMP)/cc_native_gnu_overflow_32.s
	grep -q "imull" $(CC_TMP)/cc_native_gnu_overflow_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_object_size_chk.c -o $(CC_TMP)/cc_native_gnu_object_size_chk
	$(CC_TMP)/cc_native_gnu_object_size_chk
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_object_size_chk.c -o $(CC_TMP)/cc_native_gnu_object_size_chk.s
	grep -q "ud2" $(CC_TMP)/cc_native_gnu_object_size_chk.s
	grep -q "call memcpy" $(CC_TMP)/cc_native_gnu_object_size_chk.s
	grep -q "call memmove" $(CC_TMP)/cc_native_gnu_object_size_chk.s
	grep -q "call memset" $(CC_TMP)/cc_native_gnu_object_size_chk.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_object_size_chk.c -o $(CC_TMP)/cc_native_gnu_object_size_chk_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 native_gnu_object_size_chk.c -o $(CC_TMP)/cc_native_gnu_object_size_chk_32
	$(CC_TMP)/cc_native_gnu_object_size_chk_32
	grep -q "ud2" $(CC_TMP)/cc_native_gnu_object_size_chk_32.s
	grep -q "call memcpy" $(CC_TMP)/cc_native_gnu_object_size_chk_32.s
	grep -q "call memmove" $(CC_TMP)/cc_native_gnu_object_size_chk_32.s
	grep -q "call memset" $(CC_TMP)/cc_native_gnu_object_size_chk_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_trap_unreachable.c -o $(CC_TMP)/cc_native_gnu_trap_unreachable
	$(CC_TMP)/cc_native_gnu_trap_unreachable
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_trap_unreachable.c -o $(CC_TMP)/cc_native_gnu_trap_unreachable.s
	grep -q "ud2" $(CC_TMP)/cc_native_gnu_trap_unreachable.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_trap_unreachable.c -o $(CC_TMP)/cc_native_gnu_trap_unreachable_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 native_gnu_trap_unreachable.c -o $(CC_TMP)/cc_native_gnu_trap_unreachable_32
	$(CC_TMP)/cc_native_gnu_trap_unreachable_32
	grep -q "ud2" $(CC_TMP)/cc_native_gnu_trap_unreachable_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_nested_func.c -o $(CC_TMP)/cc_native_gnu_nested_func
	$(CC_TMP)/cc_native_gnu_nested_func
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_union_cast.c -o $(CC_TMP)/cc_native_gnu_union_cast
	$(CC_TMP)/cc_native_gnu_union_cast
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_zerolen_array.c -o $(CC_TMP)/cc_native_gnu_zerolen_array
	$(CC_TMP)/cc_native_gnu_zerolen_array
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_empty_struct.c -o $(CC_TMP)/cc_native_gnu_empty_struct
	$(CC_TMP)/cc_native_gnu_empty_struct
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_void_ptr_arith.c -o $(CC_TMP)/cc_native_gnu_void_ptr_arith
	$(CC_TMP)/cc_native_gnu_void_ptr_arith
	$(TOP)/usr.bin/cc/cc -std=c99 -pedantic -c native_gnu_void_ptr_arith.c -o $(CC_TMP)/cc_native_gnu_void_ptr_arith_pedantic.o 2>$(CC_TMP)/cc_native_gnu_void_ptr_arith_pedantic.log
	grep -q "warning: arithmetic on 'void \\*' is a GNU extension" $(CC_TMP)/cc_native_gnu_void_ptr_arith_pedantic.log
	$(TOP)/usr.bin/cc/cc -std=gnu11 -fgnu89-inline native_gnu89_inline.c -o $(CC_TMP)/cc_native_gnu89_inline
	$(CC_TMP)/cc_native_gnu89_inline
	$(TOP)/usr.bin/cc/cc -std=gnu11 -fgnu89-inline -S native_gnu89_inline.c -o $(CC_TMP)/cc_native_gnu89_inline.s
	grep -q "^\\.globl add_inline$$" $(CC_TMP)/cc_native_gnu89_inline.s
	! grep -q "^\\.globl add_ext$$" $(CC_TMP)/cc_native_gnu89_inline.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_asm_basic.c -o $(CC_TMP)/cc_native_gnu_asm_basic
	$(CC_TMP)/cc_native_gnu_asm_basic
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_asm_basic.c -o $(CC_TMP)/cc_native_gnu_asm_basic.s
	grep -q "nop" $(CC_TMP)/cc_native_gnu_asm_basic.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_asm_ext.c -o $(CC_TMP)/cc_native_gnu_asm_ext
	$(CC_TMP)/cc_native_gnu_asm_ext
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_asm_ext.c -o $(CC_TMP)/cc_native_gnu_asm_ext.s
	grep -Eq 'add .*[$$]5' $(CC_TMP)/cc_native_gnu_asm_ext.s
	grep -Eq "add .*%rax|add .*%r[0-9]+" $(CC_TMP)/cc_native_gnu_asm_ext.s
	grep -q "mov" $(CC_TMP)/cc_native_gnu_asm_ext.s
	grep -q "# asm clobber memory" $(CC_TMP)/cc_native_gnu_asm_ext.s
	grep -q "# asm clobber cc" $(CC_TMP)/cc_native_gnu_asm_ext.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_asm_ext.c -o $(CC_TMP)/cc_native_gnu_asm_ext_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 native_gnu_asm_ext.c -o $(CC_TMP)/cc_native_gnu_asm_ext_32
	$(CC_TMP)/cc_native_gnu_asm_ext_32
	grep -q "add" $(CC_TMP)/cc_native_gnu_asm_ext_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_asm_goto.c -o $(CC_TMP)/cc_native_gnu_asm_goto
	$(CC_TMP)/cc_native_gnu_asm_goto
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_asm_goto_outputs.c -o $(CC_TMP)/cc_native_gnu_asm_goto_outputs
	$(CC_TMP)/cc_native_gnu_asm_goto_outputs
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_asm_goto.c -o $(CC_TMP)/cc_native_gnu_asm_goto.s
	grep -Eq "jz \\.L[a-zA-Z0-9_]+_[0-9]+" $(CC_TMP)/cc_native_gnu_asm_goto.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_asm_goto.c -o $(CC_TMP)/cc_native_gnu_asm_goto_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 native_gnu_asm_goto.c -o $(CC_TMP)/cc_native_gnu_asm_goto_32
	$(CC_TMP)/cc_native_gnu_asm_goto_32
	grep -Eq "jz \\.L[a-zA-Z0-9_]+_[0-9]+" $(CC_TMP)/cc_native_gnu_asm_goto_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_asm_early_clobber.c -o $(CC_TMP)/cc_native_gnu_asm_early_clobber
	$(CC_TMP)/cc_native_gnu_asm_early_clobber
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_asm_volatile_barrier.c -O2 -o $(CC_TMP)/cc_native_gnu_asm_volatile_barrier.s
	grep -q "# volatile asm" $(CC_TMP)/cc_native_gnu_asm_volatile_barrier.s
	grep -q "# asm clobber memory" $(CC_TMP)/cc_native_gnu_asm_volatile_barrier.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -O2 native_gnu_asm_side_effect.c -o $(CC_TMP)/cc_native_gnu_asm_side_effect
	$(CC_TMP)/cc_native_gnu_asm_side_effect
	$(TOP)/usr.bin/cc/cc -std=gnu11 -O2 -S native_gnu_asm_side_effect.c -o $(CC_TMP)/cc_native_gnu_asm_side_effect.s
	grep -Eq "add .*[$$]1" $(CC_TMP)/cc_native_gnu_asm_side_effect.s
	grep -q "# volatile asm" $(CC_TMP)/cc_native_gnu_asm_side_effect.s
	grep -q "# asm clobber memory" $(CC_TMP)/cc_native_gnu_asm_side_effect.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_fn_codegen.c -o $(CC_TMP)/cc_native_gnu_attr_fn_codegen
	$(CC_TMP)/cc_native_gnu_attr_fn_codegen
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_attr_fn_codegen.c -o $(CC_TMP)/cc_native_gnu_attr_fn_codegen.s
	grep -Eq "^\\.weak hot_fn$$" $(CC_TMP)/cc_native_gnu_attr_fn_codegen.s
	grep -Eq "^\\.weak cold_fn$$" $(CC_TMP)/cc_native_gnu_attr_fn_codegen.s
	grep -Eq "^[[:space:]]*\\.section \\.text\\.hot" $(CC_TMP)/cc_native_gnu_attr_fn_codegen.s
	grep -Eq "^[[:space:]]*\\.section \\.text\\.unlikely" $(CC_TMP)/cc_native_gnu_attr_fn_codegen.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_var_codegen.c -o $(CC_TMP)/cc_native_gnu_attr_var_codegen
	$(CC_TMP)/cc_native_gnu_attr_var_codegen
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_attr_var_codegen.c -o $(CC_TMP)/cc_native_gnu_attr_var_codegen.s
	grep -Eq "^[[:space:]]*\\.section \\.attr\\.data" $(CC_TMP)/cc_native_gnu_attr_var_codegen.s
	grep -q "^\\.align 32$$" $(CC_TMP)/cc_native_gnu_attr_var_codegen.s
	grep -Eq "^\\.weak vis_obj$$" $(CC_TMP)/cc_native_gnu_attr_var_codegen.s
	grep -Eq "^\\.hidden vis_obj$$" $(CC_TMP)/cc_native_gnu_attr_var_codegen.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_type_parse.c -o $(CC_TMP)/cc_native_gnu_attr_type_parse
	$(CC_TMP)/cc_native_gnu_attr_type_parse
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_alias.c -o $(CC_TMP)/cc_native_gnu_attr_alias
	$(CC_TMP)/cc_native_gnu_attr_alias
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_attr_alias.c -o $(CC_TMP)/cc_native_gnu_attr_alias.s
	grep -Eq "^\\.set alias_fn, target_fn$$" $(CC_TMP)/cc_native_gnu_attr_alias.s
	grep -Eq "^\\.set alias_obj, target_obj$$" $(CC_TMP)/cc_native_gnu_attr_alias.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_label_enum_stmt.c -o $(CC_TMP)/cc_native_gnu_attr_label_enum_stmt
	$(CC_TMP)/cc_native_gnu_attr_label_enum_stmt
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_clang_blocks_decl.c -o $(CC_TMP)/cc_native_clang_blocks_decl
	$(CC_TMP)/cc_native_clang_blocks_decl
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_clang_blocks_decl.c -o $(CC_TMP)/cc_native_clang_blocks_decl_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 native_clang_blocks_decl.c -o $(CC_TMP)/cc_native_clang_blocks_decl_32
	$(CC_TMP)/cc_native_clang_blocks_decl_32
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_clang_ext_vector_attr.c -o $(CC_TMP)/cc_native_clang_ext_vector_attr
	$(CC_TMP)/cc_native_clang_ext_vector_attr
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_clang_builtin_aliases.c -o $(CC_TMP)/cc_native_clang_builtin_aliases
	$(CC_TMP)/cc_native_clang_builtin_aliases
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_placement.c -o $(CC_TMP)/cc_native_gnu_attr_placement
	$(CC_TMP)/cc_native_gnu_attr_placement
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_clang_asm_goto_quirks.c -o $(CC_TMP)/cc_native_clang_asm_goto_quirks
	$(CC_TMP)/cc_native_clang_asm_goto_quirks
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_clang_asm_goto_quirks.c -o $(CC_TMP)/cc_native_clang_asm_goto_quirks.s
	grep -Eq "jz \\.L[a-zA-Z0-9_]+_[0-9]+" $(CC_TMP)/cc_native_clang_asm_goto_quirks.s
	$(TOP)/usr.bin/cc/cc -O2 native_opt_fold.c -o $(CC_TMP)/cc_native_opt_fold
	$(CC_TMP)/cc_native_opt_fold
	$(TOP)/usr.bin/cc/cc -O2 -S native_opt_fold.c -o $(CC_TMP)/cc_native_opt_fold.s
	! grep -q "imulq" $(CC_TMP)/cc_native_opt_fold.s
	$(TOP)/usr.bin/cc/cc -S native_slot_pressure.c -o $(CC_TMP)/cc_native_slot_pressure.s
	awk '/subq \$$[0-9]+, %rsp/ { n = $$2; gsub(/[^0-9]/, "", n); if ((n + 0) <= 256) ok = 1 } END { exit ok ? 0 : 1 }' $(CC_TMP)/cc_native_slot_pressure.s
	$(TOP)/usr.bin/cc/cc -S native_variadic_call.c -o $(CC_TMP)/cc_native_variadic_call.s
	grep -Fq 'movb $$2, %al' $(CC_TMP)/cc_native_variadic_call.s
	$(TOP)/usr.bin/cc/cc -S native_c99_va_args.c -o $(CC_TMP)/cc_native_c99_va_args.s
	grep -Fq 'movl $$8, ' $(CC_TMP)/cc_native_c99_va_args.s
	grep -q 'addq %rcx, %rax' $(CC_TMP)/cc_native_c99_va_args.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_arith.c -o $(CC_TMP)/cc_native_arith_32.s
	grep -q "imull" $(CC_TMP)/cc_native_arith_32.s
	grep -q "movl" $(CC_TMP)/cc_native_arith_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_array_of_array_pointers_size.c -o $(CC_TMP)/cc_native_aoap_32
	$(CC_TMP)/cc_native_aoap_32
	$(TOP)/usr.bin/cc/cc -m32 -c native_array_of_array_pointers_size.c -o $(CC_TMP)/cc_native_aoap_32.o
	test "$$(objdump -h $(CC_TMP)/cc_native_aoap_32.o | awk '$$2 == ".data" { print $$3 }')" = 00000008
	$(TOP)/usr.bin/cc/cc -m32 -std=gnu23 native_atomic_bitint_global_size.c -o $(CC_TMP)/cc_native_abgs_32
	$(CC_TMP)/cc_native_abgs_32
	$(TOP)/usr.bin/cc/cc -m32 -std=gnu23 -c native_atomic_bitint_global_size.c -o $(CC_TMP)/cc_native_abgs_32.o
	test "$$(objdump -h $(CC_TMP)/cc_native_abgs_32.o | awk '$$2 == ".data" { print $$3 }')" = 00000008
	$(TOP)/usr.bin/cc/cc -m32 native_pointer_difference_type.c -o $(CC_TMP)/cc_native_pdt_32
	$(CC_TMP)/cc_native_pdt_32
	$(TOP)/usr.bin/cc/cc -m32 native_c99_array_rank_unlimited.c -o $(CC_TMP)/cc_native_aru_32
	$(CC_TMP)/cc_native_aru_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_if_else.c -o $(CC_TMP)/cc_native_if_else_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_if_else.c -o $(CC_TMP)/cc_native_if_else_32
	$(CC_TMP)/cc_native_if_else_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c95_digraph.c -o $(CC_TMP)/cc_native_c95_digraph_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c95_digraph.c -o $(CC_TMP)/cc_native_c95_digraph_32
	$(CC_TMP)/cc_native_c95_digraph_32
	$(TOP)/usr.bin/cc/cc -std=c99 -m32 -S native_c95_trigraph.c -o $(CC_TMP)/cc_native_c95_trigraph_32.s
	$(TOP)/usr.bin/cc/cc -std=c99 -m32 native_c95_trigraph.c -o $(CC_TMP)/cc_native_c95_trigraph_32
	$(CC_TMP)/cc_native_c95_trigraph_32
	$(TOP)/usr.bin/cc/cc -std=c99 -m32 -S native_c99_phase12_trigraph_splice.c -o $(CC_TMP)/cc_native_c99_phase12_trigraph_splice_32.s
	grep -q "^main:" $(CC_TMP)/cc_native_c99_phase12_trigraph_splice_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ucn_ident.c -o $(CC_TMP)/cc_native_c99_ucn_ident_32.s
	grep -q "^main:" $(CC_TMP)/cc_native_c99_ucn_ident_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_tokenization_ppnum.c -o $(CC_TMP)/cc_native_c99_tokenization_ppnum_32.s
	grep -Eq "ucomisd|fucompp" $(CC_TMP)/cc_native_c99_tokenization_ppnum_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_line_comment.c -o $(CC_TMP)/cc_native_c99_line_comment_32.s
	grep -q '\$$3' $(CC_TMP)/cc_native_c99_line_comment_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_integer_literals.c -o $(CC_TMP)/cc_native_c99_integer_literals_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_integer_literals.c -o $(CC_TMP)/cc_native_c99_integer_literals_32
	$(CC_TMP)/cc_native_c99_integer_literals_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_hex_float_suffix.c -o $(CC_TMP)/cc_native_c99_hex_float_suffix_32.s
	grep -Eq "ucomisd|fucompp" $(CC_TMP)/cc_native_c99_hex_float_suffix_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_wide_literals.c -o $(CC_TMP)/cc_native_c99_wide_literals_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_wide_literals.c -o $(CC_TMP)/cc_native_c99_wide_literals_32
	$(CC_TMP)/cc_native_c99_wide_literals_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_do_while.c -o $(CC_TMP)/cc_native_do_while_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_do_while.c -o $(CC_TMP)/cc_native_do_while_32
	$(CC_TMP)/cc_native_do_while_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_empty_stmt2.c -o $(CC_TMP)/cc_native_c99_empty_stmt2_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_empty_stmt2.c -o $(CC_TMP)/cc_native_c99_empty_stmt2_32
	$(CC_TMP)/cc_native_c99_empty_stmt2_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_switch.c -o $(CC_TMP)/cc_native_switch_32.s
	grep -Eq "sete|je " $(CC_TMP)/cc_native_switch_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_for_decl.c -o $(CC_TMP)/cc_native_c99_for_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_for_decl.c -o $(CC_TMP)/cc_native_c99_for_decl_32
	$(CC_TMP)/cc_native_c99_for_decl_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_for_multi_decl.c -o $(CC_TMP)/cc_native_c99_for_multi_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_for_multi_decl.c -o $(CC_TMP)/cc_native_c99_for_multi_decl_32
	$(CC_TMP)/cc_native_c99_for_multi_decl_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_types.c -o $(CC_TMP)/cc_native_c99_types_32.s
	grep -q "call add_ll" $(CC_TMP)/cc_native_c99_types_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_const_qual.c -o $(CC_TMP)/cc_native_c99_const_qual_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_const_qual.c -o $(CC_TMP)/cc_native_c99_const_qual_32
	$(CC_TMP)/cc_native_c99_const_qual_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_composite_types.c -o $(CC_TMP)/cc_native_c99_composite_types_32.s
	grep -q "call sum" $(CC_TMP)/cc_native_c99_composite_types_32.s
	$(TOP)/usr.bin/cc/cc -m32 -std=c99 -S native_c99_inline_semantics.c -o $(CC_TMP)/cc_native_c99_inline_semantics_32.s
	! grep -q "^\\.globl add_local$$" $(CC_TMP)/cc_native_c99_inline_semantics_32.s
	grep -q "^\\.globl add_ext$$" $(CC_TMP)/cc_native_c99_inline_semantics_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_complex_decl.c -o $(CC_TMP)/cc_native_c99_complex_decl_32.s
	grep -Eq "cvttsd2sil|fistpl" $(CC_TMP)/cc_native_c99_complex_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_imaginary_decl.c -o $(CC_TMP)/cc_native_c99_imaginary_decl_32.s
	grep -Eq "cvtts[sd]2sil|fistpl" $(CC_TMP)/cc_native_c99_imaginary_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_param_array_qual.c -o $(CC_TMP)/cc_native_c99_param_array_qual_32.s
	grep -q "main:" $(CC_TMP)/cc_native_c99_param_array_qual_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_restrict_ptr.c -o $(CC_TMP)/cc_native_c99_restrict_ptr_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_restrict_ptr.c -o $(CC_TMP)/cc_native_c99_restrict_ptr_32
	$(CC_TMP)/cc_native_c99_restrict_ptr_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_vla_decl.c -o $(CC_TMP)/cc_native_c99_vla_decl_32.s
	grep -q "main:" $(CC_TMP)/cc_native_c99_vla_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 -std=c90 -S native_c90_oldstyle.c -o $(CC_TMP)/cc_native_c90_oldstyle_32.s
	grep -q "add:" $(CC_TMP)/cc_native_c90_oldstyle_32.s
	$(TOP)/usr.bin/cc/cc -m32 -std=c90 -S native_c90_unproto_call_promotions.c -o $(CC_TMP)/cc_native_c90_unproto_call_promotions_32.s
	grep -q "call ext" $(CC_TMP)/cc_native_c90_unproto_call_promotions_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_typedef.c -o $(CC_TMP)/cc_native_c99_typedef_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_typedef.c -o $(CC_TMP)/cc_native_c99_typedef_32
	$(CC_TMP)/cc_native_c99_typedef_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_multi_decl.c -o $(CC_TMP)/cc_native_c99_multi_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_multi_decl.c -o $(CC_TMP)/cc_native_c99_multi_decl_32
	$(CC_TMP)/cc_native_c99_multi_decl_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_logic.c -o $(CC_TMP)/cc_native_c99_logic_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_logic.c -o $(CC_TMP)/cc_native_c99_logic_32
	$(CC_TMP)/cc_native_c99_logic_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_update.c -o $(CC_TMP)/cc_native_c99_update_32.s
	grep -q "idivl" $(CC_TMP)/cc_native_c99_update_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_bitwise.c -o $(CC_TMP)/cc_native_c99_bitwise_32.s
	grep -q "xorl" $(CC_TMP)/cc_native_c99_bitwise_32.s
	grep -q "shll" $(CC_TMP)/cc_native_c99_bitwise_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_comma.c -o $(CC_TMP)/cc_native_c99_comma_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_comma.c -o $(CC_TMP)/cc_native_c99_comma_32
	$(CC_TMP)/cc_native_c99_comma_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_goto.c -o $(CC_TMP)/cc_native_c99_goto_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_goto.c -o $(CC_TMP)/cc_native_c99_goto_32
	$(CC_TMP)/cc_native_c99_goto_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_goto_loop.c -o $(CC_TMP)/cc_native_c99_goto_loop_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_goto_loop.c -o $(CC_TMP)/cc_native_c99_goto_loop_32
	$(CC_TMP)/cc_native_c99_goto_loop_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ternary.c -o $(CC_TMP)/cc_native_c99_ternary_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_ternary.c -o $(CC_TMP)/cc_native_c99_ternary_32
	$(CC_TMP)/cc_native_c99_ternary_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_cast_sizeof.c -o $(CC_TMP)/cc_native_c99_cast_sizeof_32.s
	grep -Eq "cvttsd2sil|fistpl" $(CC_TMP)/cc_native_c99_cast_sizeof_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_expr_conversions.c -o $(CC_TMP)/cc_native_c99_expr_conversions_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_expr_conversions.c -o $(CC_TMP)/cc_native_c99_expr_conversions_32
	$(CC_TMP)/cc_native_c99_expr_conversions_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_sizeof_array.c -o $(CC_TMP)/cc_native_c99_sizeof_array_32.s
	# the sizes are constants: as operands, or folded with the compares before the first call
	grep -A8 "main:" $(CC_TMP)/cc_native_c99_sizeof_array_32.s | grep -Eq '[$$](28|20)' || \
	  ! sed -n '/^main:/,/call param_size/p' $(CC_TMP)/cc_native_c99_sizeof_array_32.s | grep -q cmpl
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_sizeof_unsigned.c -o $(CC_TMP)/cc_native_c99_sizeof_unsigned_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_sizeof_unsigned.c -o $(CC_TMP)/cc_native_c99_sizeof_unsigned_32
	$(CC_TMP)/cc_native_c99_sizeof_unsigned_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_float_cmp.c -o $(CC_TMP)/cc_native_c99_float_cmp_32.s
	grep -Eq "ucomisd|fucompp" $(CC_TMP)/cc_native_c99_float_cmp_32.s
	grep -q "setnp" $(CC_TMP)/cc_native_c99_float_cmp_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_unsigned.c -o $(CC_TMP)/cc_native_c99_unsigned_32.s
	grep -q "divl" $(CC_TMP)/cc_native_c99_unsigned_32.s
	grep -q "shrl" $(CC_TMP)/cc_native_c99_unsigned_32.s
	grep -q "seta" $(CC_TMP)/cc_native_c99_unsigned_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_unsigned_literals.c -o $(CC_TMP)/cc_native_c99_unsigned_literals_32.s
	grep -q "seta" $(CC_TMP)/cc_native_c99_unsigned_literals_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_short.c -o $(CC_TMP)/cc_native_c99_short_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_short.c -o $(CC_TMP)/cc_native_c99_short_32
	$(CC_TMP)/cc_native_c99_short_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_pointer.c -o $(CC_TMP)/cc_native_c99_pointer_32.s
	grep -q "leal" $(CC_TMP)/cc_native_c99_pointer_32.s
	grep -q "(%eax)" $(CC_TMP)/cc_native_c99_pointer_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_pointer_store.c -o $(CC_TMP)/cc_native_c99_pointer_store_32.s
	grep -Eq "movl %e[a-z]{2}, \\(%e[a-z]{2}\\)" $(CC_TMP)/cc_native_c99_pointer_store_32.s
	$(TOP)/usr.bin/cc/cc -S native_c99_pointer_arith.c -o $(CC_TMP)/cc_native_c99_pointer_arith.s
	grep -q "imulq" $(CC_TMP)/cc_native_c99_pointer_arith.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_pointer_arith.c -o $(CC_TMP)/cc_native_c99_pointer_arith_32.s
	grep -q "imull" $(CC_TMP)/cc_native_c99_pointer_arith_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_index.c -o $(CC_TMP)/cc_native_c99_index_32.s
	grep -q "imull" $(CC_TMP)/cc_native_c99_index_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_pointer_diff.c -o $(CC_TMP)/cc_native_c99_pointer_diff_32.s
	grep -q "idivl" $(CC_TMP)/cc_native_c99_pointer_diff_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ptr_cast.c -o $(CC_TMP)/cc_native_c99_ptr_cast_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_ptr_cast.c -o $(CC_TMP)/cc_native_c99_ptr_cast_32
	$(CC_TMP)/cc_native_c99_ptr_cast_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ptr_size.c -o $(CC_TMP)/cc_native_c99_ptr_size_32.s
	grep -A6 "ptr_size:" $(CC_TMP)/cc_native_c99_ptr_size_32.s | grep -q '\$$4'
	grep -A6 "ptrptr_size:" $(CC_TMP)/cc_native_c99_ptr_size_32.s | grep -q '\$$4'
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ptrptr.c -o $(CC_TMP)/cc_native_c99_ptrptr_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_ptrptr.c -o $(CC_TMP)/cc_native_c99_ptrptr_32
	$(CC_TMP)/cc_native_c99_ptrptr_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ptrptrptr.c -o $(CC_TMP)/cc_native_c99_ptrptrptr_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_ptrptrptr.c -o $(CC_TMP)/cc_native_c99_ptrptrptr_32
	$(CC_TMP)/cc_native_c99_ptrptrptr_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ptr4.c -o $(CC_TMP)/cc_native_c99_ptr4_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_ptr4.c -o $(CC_TMP)/cc_native_c99_ptr4_32
	$(CC_TMP)/cc_native_c99_ptr4_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_pointer_update.c -o $(CC_TMP)/cc_native_c99_pointer_update_32.s
	grep -q "addl" $(CC_TMP)/cc_native_c99_pointer_update_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_global_array_init.c -o $(CC_TMP)/cc_native_c99_global_array_init_32.s
	grep -q "movb" $(CC_TMP)/cc_native_c99_global_array_init_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_global_init_list.c -o $(CC_TMP)/cc_native_c99_global_init_list_32.s
	grep -q "\\.long" $(CC_TMP)/cc_native_c99_global_init_list_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_local_init_list.c -o $(CC_TMP)/cc_native_c99_local_init_list_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_local_init_list.c -o $(CC_TMP)/cc_native_c99_local_init_list_32
	$(CC_TMP)/cc_native_c99_local_init_list_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_storage_tentative.c -o $(CC_TMP)/cc_native_c99_storage_tentative_32.s
	grep -q "\\.bss" $(CC_TMP)/cc_native_c99_storage_tentative_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_designated_init.c -o $(CC_TMP)/cc_native_c99_designated_init_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_designated_init.c -o $(CC_TMP)/cc_native_c99_designated_init_32
	$(CC_TMP)/cc_native_c99_designated_init_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_compound_literal.c -o $(CC_TMP)/cc_native_c99_compound_literal_32.s
	# a store into the object: through a register, or off the frame pointer
	grep -Eq "movl (%e[a-z]{2}|\\$$-?[0-9]+), (\\(%e[a-z]{2}\\)|-?[0-9]+\\(%ebp\\))" $(CC_TMP)/cc_native_c99_compound_literal_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_flexible_array.c -o $(CC_TMP)/cc_native_c99_flexible_array_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_flexible_array.c -o $(CC_TMP)/cc_native_c99_flexible_array_32
	$(CC_TMP)/cc_native_c99_flexible_array_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_assign.c -o $(CC_TMP)/cc_native_c99_struct_assign_32.s
	grep -q "call memcpy" $(CC_TMP)/cc_native_c99_struct_assign_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_ptr_arith.c -o $(CC_TMP)/cc_native_c99_struct_ptr_arith_32.s
	grep -q "imull" $(CC_TMP)/cc_native_c99_struct_ptr_arith_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_member_array.c -o $(CC_TMP)/cc_native_c99_struct_member_array_32.s
	grep -Eq "movl %e[a-z]{2}, \\(%e[a-z]{2}\\)" $(CC_TMP)/cc_native_c99_struct_member_array_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_array_init.c -o $(CC_TMP)/cc_native_c99_struct_array_init_32.s
	grep -Eq "movl (%e[a-z]{2}|\\$$-?[0-9]+), (\\(%e[a-z]{2}\\)|-?[0-9]+\\(%ebp\\))" $(CC_TMP)/cc_native_c99_struct_array_init_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_copy_init.c -o $(CC_TMP)/cc_native_c99_struct_copy_init_32.s
	grep -q "call memcpy" $(CC_TMP)/cc_native_c99_struct_copy_init_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_pass_value.c -o $(CC_TMP)/cc_native_c99_struct_pass_value_32.s
	grep -q "call sum_pair" $(CC_TMP)/cc_native_c99_struct_pass_value_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_return_value.c -o $(CC_TMP)/cc_native_c99_struct_return_value_32.s
	grep -q "call mk_pair" $(CC_TMP)/cc_native_c99_struct_return_value_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_bitfield_basic.c -o $(CC_TMP)/cc_native_c99_bitfield_basic_32.s
	grep -q "andl" $(CC_TMP)/cc_native_c99_bitfield_basic_32.s
	grep -q "shll" $(CC_TMP)/cc_native_c99_bitfield_basic_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_deref_postfix_update.c -o $(CC_TMP)/cc_native_c99_deref_postfix_update_32.s
	grep -q "addl" $(CC_TMP)/cc_native_c99_deref_postfix_update_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_proto_extern.c -o $(CC_TMP)/cc_native_c99_proto_extern_32.s
	grep -q "call extmul" $(CC_TMP)/cc_native_c99_proto_extern_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_decay.c -o $(CC_TMP)/cc_native_c99_decay_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_decay.c -o $(CC_TMP)/cc_native_c99_decay_32
	$(CC_TMP)/cc_native_c99_decay_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_block_scope_shadow.c -o $(CC_TMP)/cc_native_c99_block_scope_shadow_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_block_scope_shadow.c -o $(CC_TMP)/cc_native_c99_block_scope_shadow_32
	$(CC_TMP)/cc_native_c99_block_scope_shadow_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_mixed_decl_stmt.c -o $(CC_TMP)/cc_native_c99_mixed_decl_stmt_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_mixed_decl_stmt.c -o $(CC_TMP)/cc_native_c99_mixed_decl_stmt_32
	$(CC_TMP)/cc_native_c99_mixed_decl_stmt_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_return_conversion.c -o $(CC_TMP)/cc_native_c99_return_conversion_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_return_conversion.c -o $(CC_TMP)/cc_native_c99_return_conversion_32
	$(CC_TMP)/cc_native_c99_return_conversion_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_stdc_pragma.c -o $(CC_TMP)/cc_native_c99_stdc_pragma_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_c99_stdc_pragma.c -o $(CC_TMP)/cc_native_c99_stdc_pragma_32
	$(CC_TMP)/cc_native_c99_stdc_pragma_32
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_unproto_decl_call.c -o $(CC_TMP)/cc_native_c99_unproto_decl_call_32.s
	grep -q "call addmix" $(CC_TMP)/cc_native_c99_unproto_decl_call_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_unproto_extern_call.c -o $(CC_TMP)/cc_native_c99_unproto_extern_call_32.s
	grep -q "call ext" $(CC_TMP)/cc_native_c99_unproto_extern_call_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_va_args.c -o $(CC_TMP)/cc_native_c99_va_args_32.s
	grep -q "leal 12(%ebp), %eax" $(CC_TMP)/cc_native_c99_va_args_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_loops.c -o $(CC_TMP)/cc_native_loops_32.s
	$(TOP)/usr.bin/cc/cc -m32 native_loops.c -o $(CC_TMP)/cc_native_loops_32
	$(CC_TMP)/cc_native_loops_32
	$(TOP)/usr.bin/cc/cc -m32 -c native_arith.c -o $(CC_TMP)/cc_native_arith_32.o
	file $(CC_TMP)/cc_native_arith_32.o | grep -q "ELF 32-bit"
	$(TOP)/usr.bin/cc/cc -m32 -S native_double.c -o $(CC_TMP)/cc_native_double_32.s
	grep -Eq "movsd|fldl" $(CC_TMP)/cc_native_double_32.s
	grep -q "fldl" $(CC_TMP)/cc_native_double_32.s
	$(TOP)/usr.bin/cc/cc -m32 -c native_double.c -o $(CC_TMP)/cc_native_double_32.o
	file $(CC_TMP)/cc_native_double_32.o | grep -q "ELF 32-bit"
	$(TOP)/usr.bin/cc/cc -g -S native_double.c -o $(CC_TMP)/cc_native_double_dbg.s
	grep -q "\\.cfi_startproc" $(CC_TMP)/cc_native_double_dbg.s
	grep -q "\\.file" $(CC_TMP)/cc_native_double_dbg.s
	grep -q "addsd" $(CC_TMP)/cc_native_double_dbg.s
	grep -q "mulsd" $(CC_TMP)/cc_native_double_dbg.s
	$(TOP)/usr.bin/cc/cc -Wall -c native_warn_assign_cond.c -o $(CC_TMP)/cc_native_warn_assign_cond.o 2>$(CC_TMP)/cc_native_warn_assign_cond.log
	grep -q "^native_warn_assign_cond.c:3:2: warning: assignment used as condition in if" $(CC_TMP)/cc_native_warn_assign_cond.log
	! $(TOP)/usr.bin/cc/cc -Wall -Werror -c native_warn_assign_cond.c -o $(CC_TMP)/cc_native_warn_assign_cond_werror.o 2>$(CC_TMP)/cc_native_warn_assign_cond_werror.log
	grep -q "error: assignment used as condition in if" $(CC_TMP)/cc_native_warn_assign_cond_werror.log
	$(TOP)/usr.bin/cc/cc -std=c99 -pedantic -c native_c99_computed_goto.c -o $(CC_TMP)/cc_native_c99_computed_goto_pedantic.o 2>$(CC_TMP)/cc_native_c99_computed_goto_pedantic.log
	grep -q "warning: computed goto is a GNU extension (non-C99)" $(CC_TMP)/cc_native_c99_computed_goto_pedantic.log
	! $(TOP)/usr.bin/cc/cc -std=c99 -pedantic-errors -c native_c99_computed_goto.c -o $(CC_TMP)/cc_native_c99_computed_goto_pedantic_errors.o 2>$(CC_TMP)/cc_native_c99_computed_goto_pedantic_errors.log
	grep -q "error: computed goto is a GNU extension (non-C99)" $(CC_TMP)/cc_native_c99_computed_goto_pedantic_errors.log
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_ptr_add_ptr.c -o $(CC_TMP)/cc_native_bad_ptr_add_ptr_loc.o 2>$(CC_TMP)/cc_native_bad_ptr_add_ptr_loc.log
	grep -Eq '^[^:]+:[0-9]+:[0-9]+: error: ' $(CC_TMP)/cc_native_bad_ptr_add_ptr_loc.log
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_source_line.c -o $(CC_TMP)/cc_native_bad_source_line.o 2>$(CC_TMP)/cc_native_bad_source_line.log
	grep -q "return missing + ;" $(CC_TMP)/cc_native_bad_source_line.log
	grep -Eq '^[[:space:]]*\^$$' $(CC_TMP)/cc_native_bad_source_line.log
	sh run_conformance_c99.sh
	sh run_diff_c99.sh
	sh run_codegen_diff.sh
	sh run_codegen_random.sh
	sh run_codegen_abi.sh
	sh run_codegen_regparm.sh
	sh run_selfhost.sh
	sh run_crosshost.sh
	sh run_postc99_c11_c17.sh
	sh run_postc99_c23.sh
	sh run_postc99_c23_attrs.sh
	sh run_postc99_mode_matrix.sh
	sh run_postc99_diff_modes.sh
	sh run_postc99_abi_modes.sh
	$(TOP)/usr.bin/cc/cc -std=c23 native_c23_float16_storage.c -o $(CC_TMP)/cc_native_c23_float16_storage
	$(CC_TMP)/cc_native_c23_float16_storage
	$(TOP)/usr.bin/cc/cc native_global_ulong_div_init.c -o $(CC_TMP)/cc_native_global_ulong_div_init
	$(CC_TMP)/cc_native_global_ulong_div_init
	$(TOP)/usr.bin/cc/cc native_link_atexit.c -o $(CC_TMP)/cc_native_link_atexit
	$(CC_TMP)/cc_native_link_atexit
	$(TOP)/usr.bin/cc/cc native_x86_64_large_struct_byval.c -o $(CC_TMP)/cc_native_x86_64_large_struct_byval
	$(CC_TMP)/cc_native_x86_64_large_struct_byval
	sh run_pp_postc99.sh
	sh run_pp_c99_baseline.sh
	sh run_m16.sh
	sh run_gnu_inline.sh
	sh run_div64_pow2.sh
	sh run_inline_dead_call.sh
	sh run_pic_i386.sh
	sh run_object_symbols.sh
	sh run_trivial_auto_var_init.sh
	sh run_i386_regcache.sh
	sh run_i386_frame_shapes.sh
	sh run_alignof_std.sh
	sh run_static_local_tables.sh
	sh run_static_local_orphans.sh
	sh run_asm_imm_symbol.sh
	sh run_compiler_stamp.sh
	sh run_pp_fuzz_smoke.sh
	sh run_pp_gnu_ext.sh
	sh run_pp_clang_feature_probe.sh
	sh run_pp_clang_pragma.sh
	sh run_pp_cmd_injection_test.sh
	printf '' > $(CC_TMP)/cc_pp_std_probe.c
	$(TOP)/usr.bin/cc/cc -E -dM -std=c11 $(CC_TMP)/cc_pp_std_probe.c -o $(CC_TMP)/cc_pp_strict_c11.i
	grep -q "^#define __STRICT_ANSI__ 1$$" $(CC_TMP)/cc_pp_strict_c11.i
	$(TOP)/usr.bin/cc/cc -E -dM -std=gnu11 $(CC_TMP)/cc_pp_std_probe.c -o $(CC_TMP)/cc_pp_strict_gnu11.i
	! grep -q "^#define __STRICT_ANSI__" $(CC_TMP)/cc_pp_strict_gnu11.i
	sh run_simd_intrinsics.sh
	sh run_negative.sh
	sh run_ir_golden.sh
	$(TOP)/usr.bin/cc/cc native_gnu_sizeof_void.c -o $(CC_TMP)/cc_native_gnu_sizeof_void
	$(CC_TMP)/cc_native_gnu_sizeof_void
	$(TOP)/usr.bin/cc/cc -m32 native_gnu_sizeof_void.c -o $(CC_TMP)/cc_native_gnu_sizeof_void_32
	$(CC_TMP)/cc_native_gnu_sizeof_void_32
	$(TOP)/usr.bin/cc/cc -c native_bad_ptr_depth5.c -o $(CC_TMP)/cc_native_bad_ptr_depth5.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_negative_array_bound.c -o $(CC_TMP)/cc_native_bad_negative_array_bound.o 2>$(CC_TMP)/cc_native_bad_negative_array_bound.log
	grep -q "array bound must be positive" $(CC_TMP)/cc_native_bad_negative_array_bound.log
	! $(TOP)/usr.bin/cc/cc -m32 -std=gnu11 -c native_bad_gnu_int128.c -o $(CC_TMP)/cc_native_bad_gnu_int128.o 2>$(CC_TMP)/cc_native_bad_gnu_int128.log
	grep -q "__int128 is not supported on this target" $(CC_TMP)/cc_native_bad_gnu_int128.log
	$(TOP)/usr.bin/cc/cc -m64 -std=gnu11 native_bad_gnu_int128.c -o $(CC_TMP)/cc_native_gnu_int128_64 && $(CC_TMP)/cc_native_gnu_int128_64
	! AS=/nonexistent/cc_no_such_as $(TOP)/usr.bin/cc/cc -c native_c99_complex_decl.c -o $(CC_TMP)/cc_no_such_as.o 2>$(CC_TMP)/cc_no_such_as.log
	grep -q "cannot execute '/nonexistent/cc_no_such_as'" $(CC_TMP)/cc_no_such_as.log
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_asm_constraint.c -o $(CC_TMP)/cc_native_bad_gnu_asm_constraint.o 2>$(CC_TMP)/cc_native_bad_gnu_asm_constraint.log
	grep -q "asm operand constraint contains unsupported character" $(CC_TMP)/cc_native_bad_gnu_asm_constraint.log
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_asm_match_idx.c -o $(CC_TMP)/cc_native_bad_gnu_asm_match_idx.o 2>$(CC_TMP)/cc_native_bad_gnu_asm_match_idx.log
	grep -q "asm matching constraint references invalid output index" $(CC_TMP)/cc_native_bad_gnu_asm_match_idx.log
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_asm_template_mismatch.c -o $(CC_TMP)/cc_native_bad_gnu_asm_template_mismatch.o 2>$(CC_TMP)/cc_native_bad_gnu_asm_template_mismatch.log
	grep -q "asm template operand index is out of range" $(CC_TMP)/cc_native_bad_gnu_asm_template_mismatch.log
	$(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_asm_early_clobber_tie.c -o $(CC_TMP)/cc_native_bad_gnu_asm_early_clobber_tie.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_asm_early_clobber_fixed.c -o $(CC_TMP)/cc_native_bad_gnu_asm_early_clobber_fixed.o 2>$(CC_TMP)/cc_native_bad_gnu_asm_early_clobber_fixed.log
	grep -q "asm input constraint conflicts with early-clobber output register" $(CC_TMP)/cc_native_bad_gnu_asm_early_clobber_fixed.log
	$(TOP)/usr.bin/cc/cc -c native_attr_noreturn_nonvoid.c -o $(CC_TMP)/cc_native_attr_noreturn_nonvoid.o
	$(TOP)/usr.bin/cc/cc -std=gnu11 -c native_attr_nonnull_noparams.c -o $(CC_TMP)/cc_native_attr_nonnull_noparams.o
	$(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_attr_nonnull_call.c -o $(CC_TMP)/cc_native_bad_attr_nonnull_call.o 2>$(CC_TMP)/cc_native_bad_attr_nonnull_call.log
	grep -q "warning: nonnull argument 1 in call to takes_ptr is null" $(CC_TMP)/cc_native_bad_attr_nonnull_call.log
	$(TOP)/usr.bin/cc/cc -std=c99 native_c90_oldstyle.c -o $(CC_TMP)/cc_native_c90_oldstyle_c99
	$(CC_TMP)/cc_native_c90_oldstyle_c99
	$(TOP)/usr.bin/cc/cc -std=c23 native_c90_oldstyle.c -o $(CC_TMP)/cc_native_c90_oldstyle_c23
	$(CC_TMP)/cc_native_c90_oldstyle_c23
	$(TOP)/usr.bin/cc/cc -c native_bad_void_ptr_arith.c -o $(CC_TMP)/cc_native_bad_void_ptr_arith.o
	$(TOP)/usr.bin/cc/cc -c driver_hello.s -o $(CC_TMP)/cc_driver_hello.o
	$(TOP)/usr.bin/cc/cc -nostdlib $(CC_TMP)/cc_driver_hello.o -o $(CC_TMP)/cc_driver_hello
	$(CC_TMP)/cc_driver_hello | grep -q "cc-driver-asm-ok"
	if [ "$(CC_TMP_OWNED)" = 1 ]; then rm -rf "$(CC_TMP)"; fi

clean:
	rm -f $(CC_TMP)/cc_valid.norm $(CC_TMP)/cc_driver.pp $(CC_TMP)/cc_driver_fail.o $(CC_TMP)/cc_native_c99_phase12_trigraph_splice_c23.pp \
	      $(CC_TMP)/cc_driver_bootstrap.o $(CC_TMP)/cc_driver_hello.o $(CC_TMP)/cc_driver_hello \
	      $(CC_TMP)/cc_native_arith.s $(CC_TMP)/cc_native_main $(CC_TMP)/cc_native_bad.o \
	      $(CC_TMP)/cc_native_phase1 $(CC_TMP)/cc_native_call_stmt $(CC_TMP)/cc_native_bad_assign.o \
	      $(CC_TMP)/cc_native_bad_break.o $(CC_TMP)/cc_native_bad_continue.o $(CC_TMP)/cc_native_double $(CC_TMP)/cc_native_void $(CC_TMP)/cc_native_c99_empty_void $(CC_TMP)/cc_native_variadic \
	      $(CC_TMP)/cc_native_bad_case.o $(CC_TMP)/cc_native_bad_default.o $(CC_TMP)/cc_native_bad_switch_dup_case.o $(CC_TMP)/cc_native_bad_switch_dup_default.o $(CC_TMP)/cc_native_bad_for_scope.o \
	      $(CC_TMP)/cc_native_abi_stack_int $(CC_TMP)/cc_native_abi_stack_fp $(CC_TMP)/cc_native_abi_mix $(CC_TMP)/cc_native_host_stdint_inttypes $(CC_TMP)/cc_native_host_stdint_inttypes.i \
	      $(CC_TMP)/cc_native_if_else $(CC_TMP)/cc_native_if_abs $(CC_TMP)/cc_native_c95_digraph $(CC_TMP)/cc_native_c95_trigraph $(CC_TMP)/cc_native_c99_phase12_trigraph_splice $(CC_TMP)/cc_native_c99_ucn_ident $(CC_TMP)/cc_native_c99_tokenization_ppnum $(CC_TMP)/cc_native_c99_line_comment $(CC_TMP)/cc_native_do_while $(CC_TMP)/cc_native_switch $(CC_TMP)/cc_native_c99_switch_constexpr $(CC_TMP)/cc_native_c99_for_decl $(CC_TMP)/cc_native_c99_for_multi_decl $(CC_TMP)/cc_native_c99_types $(CC_TMP)/cc_native_c99_const_qual $(CC_TMP)/cc_native_c99_composite_types $(CC_TMP)/cc_native_c99_inline_semantics $(CC_TMP)/cc_native_c99_inline_semantics.s $(CC_TMP)/cc_native_c99_typedef $(CC_TMP)/cc_native_c99_multi_decl $(CC_TMP)/cc_native_c99_char_hex $(CC_TMP)/cc_native_c99_char_cast_sign $(CC_TMP)/cc_native_c99_signed_char_type $(CC_TMP)/cc_native_c99_logic $(CC_TMP)/cc_native_c99_inc_expr $(CC_TMP)/cc_native_c99_update $(CC_TMP)/cc_native_c99_bitwise $(CC_TMP)/cc_native_c99_comma $(CC_TMP)/cc_native_c99_goto $(CC_TMP)/cc_native_c99_goto_loop $(CC_TMP)/cc_native_c99_computed_goto $(CC_TMP)/cc_native_c99_ternary $(CC_TMP)/cc_native_c99_cast_sizeof $(CC_TMP)/cc_native_c99_expr_conversions $(CC_TMP)/cc_native_c99_unary_integer_promotion $(CC_TMP)/cc_native_c99_sizeof_array $(CC_TMP)/cc_native_c99_sizeof_unsigned $(CC_TMP)/cc_native_c99_sizeof_local_array_cmp $(CC_TMP)/cc_native_c99_float_cmp $(CC_TMP)/cc_native_c99_math_classify_builtins $(CC_TMP)/cc_native_c99_long_double_type $(CC_TMP)/cc_native_c99_unsigned $(CC_TMP)/cc_native_c99_unsigned_literals $(CC_TMP)/cc_native_c99_short $(CC_TMP)/cc_native_c99_pointer $(CC_TMP)/cc_native_c99_pointer_param $(CC_TMP)/cc_native_c99_pointer_store $(CC_TMP)/cc_native_c99_pointer_store_param $(CC_TMP)/cc_native_c99_deref_compound $(CC_TMP)/cc_native_c99_deref_prefix_update $(CC_TMP)/cc_native_c99_deref_postfix_update $(CC_TMP)/cc_native_c99_pointer_arith $(CC_TMP)/cc_native_c99_index $(CC_TMP)/cc_native_c99_pointer_diff $(CC_TMP)/cc_native_c99_ptr_cast $(CC_TMP)/cc_native_c99_ptr_size $(CC_TMP)/cc_native_c99_ptrptr $(CC_TMP)/cc_native_c99_ptrptrptr $(CC_TMP)/cc_native_c99_ptr4 $(CC_TMP)/cc_native_c99_pointer_update $(CC_TMP)/cc_native_c99_pointer_array_steps $(CC_TMP)/cc_native_c99_pointer_array_param $(CC_TMP)/cc_native_c99_param_array_constexpr_bound $(CC_TMP)/cc_native_c99_pointer_sub_array_decay $(CC_TMP)/cc_native_c99_local_struct_array_2d $(CC_TMP)/cc_native_c99_octal_char $(CC_TMP)/cc_native_c99_char_octal_signedness $(CC_TMP)/cc_native_c99_string_concat $(CC_TMP)/cc_native_c99_global_string_embedded_nul $(CC_TMP)/cc_native_c99_compound_literal_array_decay $(CC_TMP)/cc_native_c99_func_name $(CC_TMP)/cc_native_c99_atomic_fetch $(CC_TMP)/cc_native_c99_builtin_strlen.s $(CC_TMP)/cc_native_gnu_static_inline_extern.o $(CC_TMP)/cc_native_c99_global_array_init $(CC_TMP)/cc_native_c99_storage_tentative $(CC_TMP)/cc_native_c99_global_bool_addr $(CC_TMP)/cc_native_c99_designated_init $(CC_TMP)/cc_native_c99_compound_literal $(CC_TMP)/cc_native_c99_flexible_array $(CC_TMP)/cc_native_c99_flexible_array_global_init $(CC_TMP)/cc_native_c99_pragma $(CC_TMP)/cc_native_c99_stdc_pragma $(CC_TMP)/cc_native_c99_struct_assign $(CC_TMP)/cc_native_c99_struct_ptr_arith $(CC_TMP)/cc_native_c99_struct_member_array $(CC_TMP)/cc_native_c99_anonymous_struct_union $(CC_TMP)/cc_native_c99_bitfield_basic $(CC_TMP)/cc_native_c99_consteval_limits $(CC_TMP)/cc_native_c99_void_pointer $(CC_TMP)/cc_native_c99_pointer_cmp $(CC_TMP)/cc_native_c99_decay $(CC_TMP)/cc_native_c99_local_array_decay_addr $(CC_TMP)/cc_native_c99_block_scope_shadow $(CC_TMP)/cc_native_c99_typedef_shadow_expr $(CC_TMP)/cc_native_c99_mixed_decl_stmt $(CC_TMP)/cc_native_c99_return_conversion $(CC_TMP)/cc_native_c99_unproto_decl_call $(CC_TMP)/cc_native_c99_unproto_extern_call.s $(CC_TMP)/cc_native_c99_proto_call $(CC_TMP)/cc_native_c99_proto_extern.s $(CC_TMP)/cc_native_c99_empty_stmt2 $(CC_TMP)/cc_native_loops $(CC_TMP)/cc_native_if_assign $(CC_TMP)/cc_native_variadic_call $(CC_TMP)/cc_native_c99_variadic_fnptr $(CC_TMP)/cc_native_variadic_call.s $(CC_TMP)/cc_native_c99_struct_fnptr_pass_value $(CC_TMP)/cc_native_c99_grouped_fn_param_decay \
	      $(CC_TMP)/cc_native_opt_fold $(CC_TMP)/cc_native_opt_fold.s $(CC_TMP)/cc_native_slot_pressure.s \
	      $(CC_TMP)/cc_native_arith_32.s $(CC_TMP)/cc_native_if_else_32.s $(CC_TMP)/cc_native_c95_digraph_32.s $(CC_TMP)/cc_native_c95_trigraph_32.s $(CC_TMP)/cc_native_c99_phase12_trigraph_splice_32.s $(CC_TMP)/cc_native_c99_ucn_ident_32.s $(CC_TMP)/cc_native_c99_tokenization_ppnum_32.s $(CC_TMP)/cc_native_c99_line_comment_32.s $(CC_TMP)/cc_native_do_while_32.s $(CC_TMP)/cc_native_c99_empty_stmt2_32.s $(CC_TMP)/cc_native_switch_32.s $(CC_TMP)/cc_native_c99_for_decl_32.s $(CC_TMP)/cc_native_c99_for_multi_decl_32.s $(CC_TMP)/cc_native_c99_types_32.s $(CC_TMP)/cc_native_c99_const_qual_32.s $(CC_TMP)/cc_native_c99_composite_types_32.s $(CC_TMP)/cc_native_c99_inline_semantics_32.s $(CC_TMP)/cc_native_c99_typedef_32.s $(CC_TMP)/cc_native_c99_multi_decl_32.s $(CC_TMP)/cc_native_c99_logic_32.s $(CC_TMP)/cc_native_c99_update_32.s $(CC_TMP)/cc_native_c99_bitwise_32.s $(CC_TMP)/cc_native_c99_comma_32.s $(CC_TMP)/cc_native_c99_goto_32.s $(CC_TMP)/cc_native_c99_goto_loop_32.s $(CC_TMP)/cc_native_c99_ternary_32.s $(CC_TMP)/cc_native_c99_cast_sizeof_32.s $(CC_TMP)/cc_native_c99_sizeof_array_32.s $(CC_TMP)/cc_native_c99_sizeof_unsigned_32.s $(CC_TMP)/cc_native_c99_float_cmp_32.s $(CC_TMP)/cc_native_c99_unsigned_32.s $(CC_TMP)/cc_native_c99_unsigned_literals_32.s $(CC_TMP)/cc_native_c99_short_32.s $(CC_TMP)/cc_native_c99_pointer_32.s $(CC_TMP)/cc_native_c99_pointer_store_32.s $(CC_TMP)/cc_native_c99_pointer_arith.s $(CC_TMP)/cc_native_c99_pointer_arith_32.s $(CC_TMP)/cc_native_c99_index_32.s $(CC_TMP)/cc_native_c99_pointer_diff_32.s $(CC_TMP)/cc_native_c99_ptr_cast_32.s $(CC_TMP)/cc_native_c99_ptr_size_32.s $(CC_TMP)/cc_native_c99_ptrptr_32.s $(CC_TMP)/cc_native_c99_ptrptrptr_32.s $(CC_TMP)/cc_native_c99_ptr4_32.s $(CC_TMP)/cc_native_c99_pointer_update_32.s $(CC_TMP)/cc_native_c99_global_array_init_32.s $(CC_TMP)/cc_native_c99_storage_tentative_32.s $(CC_TMP)/cc_native_c99_designated_init_32.s $(CC_TMP)/cc_native_c99_compound_literal_32.s $(CC_TMP)/cc_native_c99_flexible_array_32.s $(CC_TMP)/cc_native_c99_struct_assign_32.s $(CC_TMP)/cc_native_c99_struct_ptr_arith_32.s $(CC_TMP)/cc_native_c99_struct_member_array_32.s $(CC_TMP)/cc_native_c99_bitfield_basic_32.s $(CC_TMP)/cc_native_c99_deref_postfix_update_32.s $(CC_TMP)/cc_native_c99_proto_extern_32.s $(CC_TMP)/cc_native_c99_block_scope_shadow_32.s $(CC_TMP)/cc_native_c99_mixed_decl_stmt_32.s $(CC_TMP)/cc_native_c99_return_conversion_32.s $(CC_TMP)/cc_native_c99_stdc_pragma_32.s $(CC_TMP)/cc_native_c99_unproto_decl_call_32.s $(CC_TMP)/cc_native_c99_unproto_extern_call_32.s $(CC_TMP)/cc_native_c90_unproto_call_promotions_32.s $(CC_TMP)/cc_native_loops_32.s $(CC_TMP)/cc_native_arith_32.o $(CC_TMP)/cc_native_double_32.s $(CC_TMP)/cc_native_double_32.o \
	      $(CC_TMP)/cc_native_bad_inc.o $(CC_TMP)/cc_native_bad_goto.o $(CC_TMP)/cc_native_bad_label_dup.o $(CC_TMP)/cc_native_bad_bitwise_float.o $(CC_TMP)/cc_native_gnu_sizeof_void $(CC_TMP)/cc_native_gnu_sizeof_void_32 $(CC_TMP)/cc_native_bad_proto_conflict.o $(CC_TMP)/cc_native_bad_signed_unsigned.o $(CC_TMP)/cc_native_bad_short_long.o $(CC_TMP)/cc_native_bad_typedef_init.o $(CC_TMP)/cc_native_bad_deref_nonptr.o $(CC_TMP)/cc_native_bad_addr_expr.o $(CC_TMP)/cc_native_bad_deref_compound.o $(CC_TMP)/cc_native_bad_deref_postfix_update.o $(CC_TMP)/cc_native_bad_ptr_add_ptr.o $(CC_TMP)/cc_native_bad_index_nonptr.o $(CC_TMP)/cc_native_bad_ptr_add_float.o $(CC_TMP)/cc_native_bad_ptr_sub_incompat.o $(CC_TMP)/cc_native_bad_ptr_cmp_incompat.o $(CC_TMP)/cc_native_bad_ptr_depth5.o $(CC_TMP)/cc_native_bad_cast_ptr_float.o $(CC_TMP)/cc_native_bad_computed_goto_float.o $(CC_TMP)/cc_native_bad_void_ptr_arith.o $(CC_TMP)/cc_native_bad_attr_noreturn_return.o \
	      $(CC_TMP)/cc_native_c99_typedef_fnptr_retptr $(CC_TMP)/cc_native_c99_attr_codegen $(CC_TMP)/cc_native_c99_attr_codegen.s $(CC_TMP)/cc_native_double_dbg.s \
	      $(CC_TMP)/cc_native_gnu_typeof_alt $(CC_TMP)/cc_native_gnu_builtins $(CC_TMP)/cc_native_gnu_floatn_builtins $(CC_TMP)/cc_native_gnu_builtin_prefetch.o $(CC_TMP)/cc_native_gnu_stmt_expr $(CC_TMP)/cc_native_gnu_extension_cast_stmt_expr $(CC_TMP)/cc_native_gnu_label_decl \
	      $(CC_TMP)/cc_native_gnu_elvis $(CC_TMP)/cc_native_gnu_case_range $(CC_TMP)/cc_native_gnu_types_choose $(CC_TMP)/cc_native_gnu_overflow \
	      $(CC_TMP)/cc_native_gnu_stmt_expr.s $(CC_TMP)/cc_native_gnu_stmt_expr_32.s $(CC_TMP)/cc_native_gnu_label_decl.s $(CC_TMP)/cc_native_gnu_label_decl_32.s \
	      $(CC_TMP)/cc_native_gnu_elvis.s $(CC_TMP)/cc_native_gnu_elvis_32.s $(CC_TMP)/cc_native_gnu_case_range.s $(CC_TMP)/cc_native_gnu_case_range_32.s \
	      $(CC_TMP)/cc_native_gnu_types_choose.s $(CC_TMP)/cc_native_gnu_overflow.s $(CC_TMP)/cc_native_gnu_overflow_32.s \
	      $(CC_TMP)/cc_native_gnu_object_size_chk $(CC_TMP)/cc_native_gnu_object_size_chk.s $(CC_TMP)/cc_native_gnu_object_size_chk_32.s \
	      $(CC_TMP)/cc_native_gnu_trap_unreachable $(CC_TMP)/cc_native_gnu_trap_unreachable.s $(CC_TMP)/cc_native_gnu_trap_unreachable_32.s \
	      $(CC_TMP)/cc_native_gnu_nested_func $(CC_TMP)/cc_native_gnu_union_cast $(CC_TMP)/cc_native_gnu_zerolen_array \
	      $(CC_TMP)/cc_native_gnu_empty_struct $(CC_TMP)/cc_native_gnu89_inline \
	      $(CC_TMP)/cc_native_gnu89_inline.s $(CC_TMP)/cc_native_gnu_attr_fn_codegen $(CC_TMP)/cc_native_gnu_attr_fn_codegen.s \
	      $(CC_TMP)/cc_native_gnu_attr_var_codegen $(CC_TMP)/cc_native_gnu_attr_var_codegen.s \
	      $(CC_TMP)/cc_native_gnu_attr_type_parse $(CC_TMP)/cc_native_gnu_attr_alias $(CC_TMP)/cc_native_gnu_attr_alias.s \
	      $(CC_TMP)/cc_native_gnu_attr_label_enum_stmt $(CC_TMP)/cc_native_clang_blocks_decl $(CC_TMP)/cc_native_clang_blocks_decl_32.s \
	      $(CC_TMP)/cc_native_clang_ext_vector_attr $(CC_TMP)/cc_native_clang_builtin_aliases \
	      $(CC_TMP)/cc_native_gnu_attr_placement $(CC_TMP)/cc_native_clang_asm_goto_quirks $(CC_TMP)/cc_native_clang_asm_goto_quirks.s \
	      $(CC_TMP)/cc_native_bad_negative_array_bound.o $(CC_TMP)/cc_native_bad_negative_array_bound.log \
	      $(CC_TMP)/cc_native_bad_gnu_int128.o $(CC_TMP)/cc_native_bad_gnu_int128.log $(CC_TMP)/cc_native_gnu_int128_64 \
	      $(CC_TMP)/cc_no_such_as.o $(CC_TMP)/cc_no_such_as.log \
	      $(CC_TMP)/cc_native_gnu_asm_basic $(CC_TMP)/cc_native_gnu_asm_basic.s $(CC_TMP)/cc_native_gnu_asm_ext \
	      $(CC_TMP)/cc_native_gnu_asm_ext.s $(CC_TMP)/cc_native_gnu_asm_ext_32.s \
	      $(CC_TMP)/cc_native_gnu_asm_goto $(CC_TMP)/cc_native_gnu_asm_goto.s $(CC_TMP)/cc_native_gnu_asm_goto_32.s $(CC_TMP)/cc_native_gnu_asm_goto_outputs \
	      $(CC_TMP)/cc_native_gnu_asm_early_clobber $(CC_TMP)/cc_native_gnu_asm_volatile_barrier.s \
	      $(CC_TMP)/cc_native_gnu_asm_side_effect $(CC_TMP)/cc_native_gnu_asm_side_effect.s \
	      $(CC_TMP)/cc_native_gnu_void_ptr_arith $(CC_TMP)/cc_native_gnu_void_ptr_arith_pedantic.o $(CC_TMP)/cc_native_gnu_void_ptr_arith_pedantic.log \
	      $(CC_TMP)/cc_native_bad_gnu_asm_constraint.o $(CC_TMP)/cc_native_bad_gnu_asm_constraint.log \
	      $(CC_TMP)/cc_native_bad_gnu_asm_match_idx.o $(CC_TMP)/cc_native_bad_gnu_asm_match_idx.log \
	      $(CC_TMP)/cc_native_bad_gnu_asm_template_mismatch.o $(CC_TMP)/cc_native_bad_gnu_asm_template_mismatch.log \
	      $(CC_TMP)/cc_native_bad_gnu_asm_early_clobber_tie.o $(CC_TMP)/cc_native_bad_gnu_asm_early_clobber_tie.log \
	      $(CC_TMP)/cc_native_bad_gnu_asm_early_clobber_fixed.o $(CC_TMP)/cc_native_bad_gnu_asm_early_clobber_fixed.log \
	      $(CC_TMP)/cc_native_bad_attr_inline_conflict.o $(CC_TMP)/cc_native_bad_attr_hot_cold_conflict.o \
	      $(CC_TMP)/cc_native_bad_attr_alias_init.o $(CC_TMP)/cc_native_bad_attr_malloc_nonptr.o \
	      $(CC_TMP)/cc_native_bad_attr_format_nonvariadic.o $(CC_TMP)/cc_native_attr_nonnull_noparams.o $(CC_TMP)/cc_native_attr_noreturn_nonvoid.o \
	      $(CC_TMP)/cc_native_bad_attr_nonnull_call.o
	rm -f $(CC_TMP)/cc_native_c99_integer_literals $(CC_TMP)/cc_native_c99_hex_float_suffix $(CC_TMP)/cc_native_c99_wide_literals \
	      $(CC_TMP)/cc_native_c99_complex_decl $(CC_TMP)/cc_native_c99_imaginary_decl $(CC_TMP)/cc_native_c99_complex_types $(CC_TMP)/cc_native_c99_composite_types $(CC_TMP)/cc_native_c99_param_array_qual $(CC_TMP)/cc_native_c99_restrict_ptr $(CC_TMP)/cc_native_c99_vla_decl $(CC_TMP)/cc_native_c90_oldstyle $(CC_TMP)/cc_native_c90_unproto_call_promotions $(CC_TMP)/cc_native_c90_unproto_call_promotions.s \
	      $(CC_TMP)/cc_native_c99_float_signbit_negzero_nan \
	      $(CC_TMP)/cc_native_c99_static_local_string_pointer_array \
	      $(CC_TMP)/cc_native_c99_string_concat_octal_boundary \
	      $(CC_TMP)/cc_native_c99_global_pointer_arith_init \
	      $(CC_TMP)/cc_native_c99_integer_literals_32.s $(CC_TMP)/cc_native_c99_hex_float_suffix_32.s $(CC_TMP)/cc_native_c99_wide_literals_32.s \
	      $(CC_TMP)/cc_native_c99_complex_decl_32.s $(CC_TMP)/cc_native_c99_imaginary_decl_32.s $(CC_TMP)/cc_native_c99_composite_types_32.s $(CC_TMP)/cc_native_c99_param_array_qual_32.s $(CC_TMP)/cc_native_c99_restrict_ptr_32.s $(CC_TMP)/cc_native_c99_vla_decl_32.s $(CC_TMP)/cc_native_c90_oldstyle_32.s $(CC_TMP)/cc_native_c90_unproto_call_promotions_32.s \
	      $(CC_TMP)/cc_native_c99_typedef_array_member_layout \
	      $(CC_TMP)/cc_native_bad_c99_implicit_function.o $(CC_TMP)/cc_native_bad_no_implicit_int.o $(CC_TMP)/cc_native_bad_c99_const_assign.o $(CC_TMP)/cc_native_bad_c99_const_param_assign.o $(CC_TMP)/cc_native_bad_c99_restrict_nonptr.o $(CC_TMP)/cc_native_bad_c99_file_scope_auto.o $(CC_TMP)/cc_native_bad_c99_extern_local_init.o $(CC_TMP)/cc_native_bad_c99_flexible_array_init.o $(CC_TMP)/cc_native_bad_c99_union_init.o $(CC_TMP)/cc_native_bad_c99_array_assign.o $(CC_TMP)/cc_native_bad_c99_array_update.o $(CC_TMP)/cc_native_bad_c99_bitfield_addr.o $(CC_TMP)/cc_native_bad_c99_bitfield_sizeof.o $(CC_TMP)/cc_native_bad_c99_bitfield_named_zero.o $(CC_TMP)/cc_native_bad_c99_return_void_value.o $(CC_TMP)/cc_native_bad_c99_return_missing_value.o $(CC_TMP)/cc_native_bad_c99_variadic_too_few.o $(CC_TMP)/cc_native_bad_c99_pragma_stdc_bad.o $(CC_TMP)/cc_native_c90_oldstyle_c99_bad.o
	rm -f $(CC_TMP)/cc_native_warn_assign_cond.o $(CC_TMP)/cc_native_warn_assign_cond.log $(CC_TMP)/cc_native_warn_assign_cond_werror.o \
	      $(CC_TMP)/cc_native_c99_computed_goto_pedantic.o $(CC_TMP)/cc_native_c99_computed_goto_pedantic.log $(CC_TMP)/cc_native_c99_computed_goto_pedantic_errors.o \
	      $(CC_TMP)/cc_native_bad_ptr_add_ptr_loc.o $(CC_TMP)/cc_native_bad_ptr_add_ptr_loc.log $(CC_TMP)/cc_native_bad_source_line.o $(CC_TMP)/cc_native_bad_source_line.log \
	      $(CC_TMP)/cc_conformance_pass.log $(CC_TMP)/cc_conformance_xfail.log $(CC_TMP)/cc_diff_host.log $(CC_TMP)/cc_diff_ours.log \
	      $(CC_TMP)/cc_pp_gnu11_predefs.i
	rm -f $(CC_TMP)/cc_native_c11_static_assert $(CC_TMP)/cc_native_c11_generic $(CC_TMP)/cc_native_c11_align_thread_noreturn \
	      $(CC_TMP)/cc_native_c11_atomic $(CC_TMP)/cc_native_c11_atomic_wrapper_type $(CC_TMP)/cc_native_c11_atomic_fence $(CC_TMP)/cc_native_c11_atomic_fence.s $(CC_TMP)/cc_native_c11_atomic_fence_32.s $(CC_TMP)/cc_native_c11_unicode_literals $(CC_TMP)/cc_native_bad_c11_static_assert_fail.o \
	      $(CC_TMP)/cc_native_bad_c11_thread_local_local.o $(CC_TMP)/cc_native_c17_register $(CC_TMP)/cc_native_c11_pp_version.i \
	      $(CC_TMP)/cc_native_c17_pp_version.i \
	      $(CC_TMP)/cc_native_c23_keywords $(CC_TMP)/cc_native_c23_nullptr $(CC_TMP)/cc_native_c23_typeof \
	      $(CC_TMP)/cc_native_c23_bitint $(CC_TMP)/cc_native_c23_bitint_size $(CC_TMP)/cc_native_c23_binary_sep $(CC_TMP)/cc_native_c23_empty_init \
	      $(CC_TMP)/cc_native_c23_enum_underlying $(CC_TMP)/cc_native_c23_label_decl $(CC_TMP)/cc_native_c23_constexpr_auto_decimal $(CC_TMP)/cc_native_c23_decimal_sizes \
	      $(CC_TMP)/cc_native_c23_empty_param $(CC_TMP)/cc_native_c23_asm_extension $(CC_TMP)/cc_native_c23_float16_storage \
	      $(CC_TMP)/cc_native_global_ulong_div_init $(CC_TMP)/cc_native_link_atexit $(CC_TMP)/cc_native_x86_64_large_struct_byval \
	      $(CC_TMP)/cc_native_bad_c23_auto_no_init.o $(CC_TMP)/cc_native_bad_c23_bitint_zero.o \
	      $(CC_TMP)/cc_native_bad_c23_static_assert_single_fail.o $(CC_TMP)/cc_native_bad_c23_empty_param_call.o $(CC_TMP)/cc_native_c23_pp_version.i \
	      $(CC_TMP)/cc_native_c23_attr_std $(CC_TMP)/cc_native_c23_attr_std.log $(CC_TMP)/cc_native_c23_attr_std_werror \
	      $(CC_TMP)/cc_native_c23_attr_std_werror.log $(CC_TMP)/cc_native_bad_c23_attr_fallthrough_outside.o \
	      $(CC_TMP)/cc_native_c23_attr_has.i \
	      $(CC_TMP)/cc_postc99_mode_c11.i $(CC_TMP)/cc_postc99_mode_c17.i $(CC_TMP)/cc_postc99_mode_c18.i $(CC_TMP)/cc_postc99_mode_c23.i $(CC_TMP)/cc_postc99_mode_c2x.i \
	      $(CC_TMP)/cc_postc99_mode_c11 $(CC_TMP)/cc_postc99_mode_c17 $(CC_TMP)/cc_postc99_mode_c23 $(CC_TMP)/cc_postc99_mode_c2x \
	      $(CC_TMP)/cc_postc99_abi_c11 $(CC_TMP)/cc_postc99_abi_c17 $(CC_TMP)/cc_postc99_abi_c23 \
	      $(CC_TMP)/cc_postc99_abi_c11_64.o $(CC_TMP)/cc_postc99_abi_c17_64.o $(CC_TMP)/cc_postc99_abi_c23_64.o \
	      $(CC_TMP)/cc_postc99_abi_c11_32.o $(CC_TMP)/cc_postc99_abi_c17_32.o $(CC_TMP)/cc_postc99_abi_c23_32.o \
	      $(CC_TMP)/cc_postc99_probe_bin $(CC_TMP)/cc_postc99_probe_gcc_c11.c $(CC_TMP)/cc_postc99_probe_gcc_c17.c \
	      $(CC_TMP)/cc_postc99_probe_gcc_c23.c $(CC_TMP)/cc_postc99_probe_clang_c11.c $(CC_TMP)/cc_postc99_probe_clang_c17.c \
	      $(CC_TMP)/cc_postc99_probe_clang_c23.c $(CC_TMP)/cc_postc99_ours_gcc_c11 $(CC_TMP)/cc_postc99_ours_gcc_c17 \
	      $(CC_TMP)/cc_postc99_ours_gcc_c23 $(CC_TMP)/cc_postc99_host_gcc_c11 $(CC_TMP)/cc_postc99_host_gcc_c17 \
	      $(CC_TMP)/cc_postc99_host_gcc_c23 $(CC_TMP)/cc_postc99_ours_clang_c11 $(CC_TMP)/cc_postc99_ours_clang_c17 \
	      $(CC_TMP)/cc_postc99_ours_clang_c23 $(CC_TMP)/cc_postc99_host_clang_c11 $(CC_TMP)/cc_postc99_host_clang_c17 \
	      $(CC_TMP)/cc_postc99_host_clang_c23
	rm -f $(CC_TMP)/cc_postc99_abi_* $(CC_TMP)/cc_postc99_ours_* $(CC_TMP)/cc_postc99_host_*
	rm -f $(CC_TMP)/cc_pp_s3.i $(CC_TMP)/cc_pp_s3_p.i $(CC_TMP)/cc_pp_s3_dM.i $(CC_TMP)/cc_pp_s3_m.d $(CC_TMP)/cc_pp_s3_mm.d $(CC_TMP)/cc_pp_s3_md.d \
	      $(CC_TMP)/cc_pp_s3_md.i $(CC_TMP)/cc_pp_s3_v.i $(CC_TMP)/cc_pp_s3_v.log $(CC_TMP)/cc_pp_s3_depth.i $(CC_TMP)/cc_pp_s3_macro.i $(CC_TMP)/cc_pp_s3_macro_limit.i $(CC_TMP)/cc_pp_s3_token_limit.i $(CC_TMP)/cc_pp_s3_output_limit.i \
	      $(CC_TMP)/cc_pp_s6_probe_gnu11.i $(CC_TMP)/cc_pp_s6_probe_c99.i $(CC_TMP)/cc_pp_s63_clang_pragma.i $(CC_TMP)/cc_pp_s63_bad_clang_pragma.i $(CC_TMP)/cc_pp_s63_bad_clang_section.i $(CC_TMP)/cc_pp_s63_bad_clang_fp.i $(CC_TMP)/cc_pp_s63_bad_clang_attribute.i $(CC_TMP)/cc_pp_s63_bad_clang_loop.i \
	      $(CC_TMP)/cc_pp_std_probe.c $(CC_TMP)/cc_pp_strict_c11.i $(CC_TMP)/cc_pp_strict_gnu11.i \
	      $(CC_TMP)/cc_pp_s63_bad_clang_pragma.log $(CC_TMP)/cc_pp_s63_bad_clang_section.log $(CC_TMP)/cc_pp_s63_bad_clang_fp.log $(CC_TMP)/cc_pp_s63_bad_clang_attribute.log $(CC_TMP)/cc_pp_s63_bad_clang_loop.log \
	      $(CC_TMP)/cc_pp_cpp_mode.i $(CC_TMP)/cc_pp_cpp_mode_h.i $(CC_TMP)/cc_pp_cpp_mode_input.h $(CC_TMP)/cpp $(CC_TMP)/cc_pp_fuzz_out.i $(CC_TMP)/cc_pp_fuzz_seed.c $(CC_TMP)/cc_pp_fuzz_seed.i \
	      $(CC_TMP)/cc_pp_fuzz_0.c $(CC_TMP)/cc_pp_fuzz_1.c $(CC_TMP)/cc_pp_fuzz_2.c $(CC_TMP)/cc_pp_fuzz_3.c $(CC_TMP)/cc_pp_fuzz_4.c \
	      $(CC_TMP)/cc_pp_fuzz_5.c $(CC_TMP)/cc_pp_fuzz_6.c $(CC_TMP)/cc_pp_fuzz_7.c $(CC_TMP)/cc_pp_fuzz_8.c $(CC_TMP)/cc_pp_fuzz_9.c \
	      $(CC_TMP)/cc_pp_fuzz_10.c $(CC_TMP)/cc_pp_fuzz_11.c $(CC_TMP)/cc_pp_fuzz_12.c $(CC_TMP)/cc_pp_fuzz_13.c $(CC_TMP)/cc_pp_fuzz_14.c \
	      $(CC_TMP)/cc_pp_fuzz_15.c $(CC_TMP)/cc_pp_s4_elif_c23.i $(CC_TMP)/cc_pp_s4_elif_c11.i $(CC_TMP)/cc_pp_s4_warning_c23.i \
	      $(CC_TMP)/cc_pp_s4_warning_c23.log $(CC_TMP)/cc_pp_s4_warning_c11.i $(CC_TMP)/cc_pp_s4_has_c23.i $(CC_TMP)/cc_pp_s4_has_c11.i $(CC_TMP)/cc_pp_s4_vaopt_c23.i \
	      $(CC_TMP)/cc_pp_s4_vaopt_c11.i $(CC_TMP)/cc_pp_s4_stdver_c99.i $(CC_TMP)/cc_pp_s4_stdver_c11.i $(CC_TMP)/cc_pp_s4_stdver_c17.i \
	      $(CC_TMP)/cc_pp_s4_stdver_c23.i $(CC_TMP)/cc_pp_s4_stdver_gnu11.i $(CC_TMP)/cc_pp_s4_stdver_gnu17.i $(CC_TMP)/cc_pp_s4_stdver_gnu23.i \
	      $(CC_TMP)/cc_pp_s4_trigraph_c11.i $(CC_TMP)/cc_pp_s4_trigraph_c23.i $(CC_TMP)/cc_pp_s4_trigraph_gnu17.i \
	      $(CC_TMP)/cc_pp_s4_trigraph_gnu17t.i $(CC_TMP)/cc_pp_s4_embed.bin $(CC_TMP)/cc_pp_s4_embed.c $(CC_TMP)/cc_pp_s4_embed.i $(CC_TMP)/cc_pp_s4_embed_c11.i \
	      $(CC_TMP)/cc_pp_s5_named_va.i $(CC_TMP)/cc_pp_s5_named_va_c99.i $(CC_TMP)/cc_pp_s5_dynmac.i $(CC_TMP)/cc_pp_s5_include_next.i $(CC_TMP)/cc_pp_s5_pragma.i \
	      $(CC_TMP)/cc_pp_s4_host_gcc.i $(CC_TMP)/cc_pp_s4_host_clang.i $(CC_TMP)/cc_pp_s4_host_gcc.ab $(CC_TMP)/cc_pp_s4_host_clang.ab \
	      $(CC_TMP)/cc_pp_s4_ours.ab
	rm -f $(CC_TMP)/cc_native_c99_simd_intrinsics $(CC_TMP)/cc_native_c99_simd_intrinsics_32.o \
	      $(CC_TMP)/cc_simd_macro_probe.c $(CC_TMP)/cc_simd_macro_none.i $(CC_TMP)/cc_simd_macro_avx.i $(CC_TMP)/cc_simd_macro_avx2.i \
	      $(CC_TMP)/cc_simd_macro_avx_m64.i $(CC_TMP)/cc_simd_macro_avx2_off.i $(CC_TMP)/cc_simd_macro_v3.i
	rm -f test_ast2ir_bin test_ctype_bin
