TOP ?= $(abspath ../../..)

.PHONY: all clean test

all: test

test:
	$(MAKE) -C $(TOP)/usr.bin/cc NATIVE_BUILD=1
	# Compile and run test_ast2ir
	$(CC) $(CFLAGS) -I$(TOP)/usr.bin/cc/include test_ast2ir.c $(TOP)/usr.bin/cc/cmd/pipeline.c $(TOP)/usr.bin/cc/frontend/preproc.c $(TOP)/usr.bin/cc/frontend/lexer.c $(TOP)/usr.bin/cc/frontend/parser.c $(TOP)/usr.bin/cc/frontend/sema.c $(TOP)/usr.bin/cc/frontend/builtin.c $(TOP)/usr.bin/cc/middle/ast2ir.c $(TOP)/usr.bin/cc/middle/legalize.c $(TOP)/usr.bin/cc/middle/passes/opt.c $(TOP)/usr.bin/cc/middle/ssa/module.c $(TOP)/usr.bin/cc/middle/ssa/func.c $(TOP)/usr.bin/cc/middle/ssa/bblock.c $(TOP)/usr.bin/cc/middle/ssa/instr_set.c $(TOP)/usr.bin/cc/backend/select.c $(TOP)/usr.bin/cc/backend/regalloc.c $(TOP)/usr.bin/cc/backend/frame.c $(TOP)/usr.bin/cc/backend/emit_s.c -o test_ast2ir_bin || true
	./test_ast2ir_bin
	$(TOP)/usr.bin/cc/ir-verifier valid.ir
	! $(TOP)/usr.bin/cc/ir-verifier invalid_missing_term.ir
	! $(TOP)/usr.bin/cc/ir-verifier invalid_use_before_def.ir
	! $(TOP)/usr.bin/cc/ir-verifier invalid_phi_arity.ir
	$(TOP)/usr.bin/cc/ir-normalize valid.ir > /tmp/cc_valid.norm
	$(TOP)/usr.bin/cc/ir-diff valid_equiv_a.ir valid_equiv_b.ir
	$(TOP)/usr.bin/cc/cc -E -DTESTVAL=7 driver_pp_input.c -o /tmp/cc_driver.pp
	grep -q "int x = 7;" /tmp/cc_driver.pp
	! $(TOP)/usr.bin/cc/cc -c driver_pp_input.c -o /tmp/cc_driver_fail.o
	$(TOP)/usr.bin/cc/cc --bootstrap-gcc -c driver_bootstrap_input.c -o /tmp/cc_driver_bootstrap.o
	test -f /tmp/cc_driver_bootstrap.o
	$(TOP)/usr.bin/cc/cc -S native_arith.c -o /tmp/cc_native_arith.s
	grep -q "imulq" /tmp/cc_native_arith.s
	grep -q "addq" /tmp/cc_native_arith.s
	$(TOP)/usr.bin/cc/cc native_main.c -o /tmp/cc_native_main
	/tmp/cc_native_main
	$(TOP)/usr.bin/cc/cc native_phase1.c -o /tmp/cc_native_phase1
	/tmp/cc_native_phase1
	$(TOP)/usr.bin/cc/cc native_call_stmt.c -o /tmp/cc_native_call_stmt
	/tmp/cc_native_call_stmt
	$(TOP)/usr.bin/cc/cc native_double.c -o /tmp/cc_native_double
	/tmp/cc_native_double
	$(TOP)/usr.bin/cc/cc native_void.c -o /tmp/cc_native_void
	/tmp/cc_native_void
	$(TOP)/usr.bin/cc/cc native_c99_empty_void.c -o /tmp/cc_native_c99_empty_void
	/tmp/cc_native_c99_empty_void
	$(TOP)/usr.bin/cc/cc native_c99_empty_stmt2.c -o /tmp/cc_native_c99_empty_stmt2
	/tmp/cc_native_c99_empty_stmt2
	$(TOP)/usr.bin/cc/cc native_variadic_decl.c -o /tmp/cc_native_variadic
	/tmp/cc_native_variadic
	$(TOP)/usr.bin/cc/cc native_c99_variadic_runtime.c -o /tmp/cc_native_c99_variadic_runtime
	/tmp/cc_native_c99_variadic_runtime
	$(TOP)/usr.bin/cc/cc native_abi_stack_int.c -o /tmp/cc_native_abi_stack_int
	/tmp/cc_native_abi_stack_int
	$(TOP)/usr.bin/cc/cc native_abi_stack_fp.c -o /tmp/cc_native_abi_stack_fp
	/tmp/cc_native_abi_stack_fp
	$(TOP)/usr.bin/cc/cc native_abi_mix.c -o /tmp/cc_native_abi_mix
	/tmp/cc_native_abi_mix
	$(TOP)/usr.bin/cc/cc native_if_else.c -o /tmp/cc_native_if_else
	/tmp/cc_native_if_else
	$(TOP)/usr.bin/cc/cc native_if_abs.c -o /tmp/cc_native_if_abs
	/tmp/cc_native_if_abs
	$(TOP)/usr.bin/cc/cc native_c95_digraph.c -o /tmp/cc_native_c95_digraph
	/tmp/cc_native_c95_digraph
	$(TOP)/usr.bin/cc/cc native_c95_trigraph.c -o /tmp/cc_native_c95_trigraph
	/tmp/cc_native_c95_trigraph
	$(TOP)/usr.bin/cc/cc native_c99_phase12_trigraph_splice.c -o /tmp/cc_native_c99_phase12_trigraph_splice
	/tmp/cc_native_c99_phase12_trigraph_splice
	$(TOP)/usr.bin/cc/cc -E -std=c23 native_c99_phase12_trigraph_splice.c -o /tmp/cc_native_c99_phase12_trigraph_splice_c23.pp
	grep -Fq "??=define TG_RET 0" /tmp/cc_native_c99_phase12_trigraph_splice_c23.pp
	$(TOP)/usr.bin/cc/cc native_c99_ucn_ident.c -o /tmp/cc_native_c99_ucn_ident
	/tmp/cc_native_c99_ucn_ident
	$(TOP)/usr.bin/cc/cc native_c99_tokenization_ppnum.c -o /tmp/cc_native_c99_tokenization_ppnum
	/tmp/cc_native_c99_tokenization_ppnum
	$(TOP)/usr.bin/cc/cc native_c99_line_comment.c -o /tmp/cc_native_c99_line_comment
	/tmp/cc_native_c99_line_comment
	$(TOP)/usr.bin/cc/cc native_c99_integer_literals.c -o /tmp/cc_native_c99_integer_literals
	/tmp/cc_native_c99_integer_literals
	$(TOP)/usr.bin/cc/cc native_c99_hex_float_suffix.c -o /tmp/cc_native_c99_hex_float_suffix
	/tmp/cc_native_c99_hex_float_suffix
	$(TOP)/usr.bin/cc/cc native_c99_wide_literals.c -o /tmp/cc_native_c99_wide_literals
	/tmp/cc_native_c99_wide_literals
	$(TOP)/usr.bin/cc/cc native_do_while.c -o /tmp/cc_native_do_while
	/tmp/cc_native_do_while
	$(TOP)/usr.bin/cc/cc native_switch.c -o /tmp/cc_native_switch
	/tmp/cc_native_switch
	$(TOP)/usr.bin/cc/cc native_c99_switch_constexpr.c -o /tmp/cc_native_c99_switch_constexpr
	/tmp/cc_native_c99_switch_constexpr
	$(TOP)/usr.bin/cc/cc native_c99_enum_constexpr.c -o /tmp/cc_native_c99_enum_constexpr
	/tmp/cc_native_c99_enum_constexpr
	$(TOP)/usr.bin/cc/cc native_c99_for_decl.c -o /tmp/cc_native_c99_for_decl
	/tmp/cc_native_c99_for_decl
	$(TOP)/usr.bin/cc/cc native_c99_for_multi_decl.c -o /tmp/cc_native_c99_for_multi_decl
	/tmp/cc_native_c99_for_multi_decl
	$(TOP)/usr.bin/cc/cc native_c99_types.c -o /tmp/cc_native_c99_types
	/tmp/cc_native_c99_types
	$(TOP)/usr.bin/cc/cc native_c99_const_qual.c -o /tmp/cc_native_c99_const_qual
	/tmp/cc_native_c99_const_qual
	$(TOP)/usr.bin/cc/cc native_c99_composite_types.c -o /tmp/cc_native_c99_composite_types
	/tmp/cc_native_c99_composite_types
	$(TOP)/usr.bin/cc/cc -std=c99 native_c99_inline_semantics.c -o /tmp/cc_native_c99_inline_semantics
	/tmp/cc_native_c99_inline_semantics
	$(TOP)/usr.bin/cc/cc -std=c99 -S native_c99_inline_semantics.c -o /tmp/cc_native_c99_inline_semantics.s
	! grep -q "^\\.globl add_local$$" /tmp/cc_native_c99_inline_semantics.s
	grep -q "^\\.globl add_ext$$" /tmp/cc_native_c99_inline_semantics.s
	$(TOP)/usr.bin/cc/cc native_c99_complex_decl.c -o /tmp/cc_native_c99_complex_decl
	/tmp/cc_native_c99_complex_decl
	$(TOP)/usr.bin/cc/cc native_c99_imaginary_decl.c -o /tmp/cc_native_c99_imaginary_decl
	/tmp/cc_native_c99_imaginary_decl
	$(TOP)/usr.bin/cc/cc native_c99_complex_types.c -o /tmp/cc_native_c99_complex_types
	/tmp/cc_native_c99_complex_types
	$(TOP)/usr.bin/cc/cc native_c99_param_array_qual.c -o /tmp/cc_native_c99_param_array_qual
	/tmp/cc_native_c99_param_array_qual
	$(TOP)/usr.bin/cc/cc native_c99_restrict_ptr.c -o /tmp/cc_native_c99_restrict_ptr
	/tmp/cc_native_c99_restrict_ptr
	$(TOP)/usr.bin/cc/cc native_c99_vla_decl.c -o /tmp/cc_native_c99_vla_decl
	/tmp/cc_native_c99_vla_decl
	$(TOP)/usr.bin/cc/cc -std=c90 native_c90_oldstyle.c -o /tmp/cc_native_c90_oldstyle
	/tmp/cc_native_c90_oldstyle
	$(TOP)/usr.bin/cc/cc -std=c90 -S native_c90_unproto_call_promotions.c -o /tmp/cc_native_c90_unproto_call_promotions.s
	grep -q "call ext" /tmp/cc_native_c90_unproto_call_promotions.s
	$(TOP)/usr.bin/cc/cc native_c99_typedef.c -o /tmp/cc_native_c99_typedef
	/tmp/cc_native_c99_typedef
	$(TOP)/usr.bin/cc/cc native_c99_typedef_attr.c -o /tmp/cc_native_c99_typedef_attr
	/tmp/cc_native_c99_typedef_attr
	$(TOP)/usr.bin/cc/cc native_c99_typedef_fnptr.c -o /tmp/cc_native_c99_typedef_fnptr
	/tmp/cc_native_c99_typedef_fnptr
	$(TOP)/usr.bin/cc/cc native_c99_typedef_fnptr_retptr.c -o /tmp/cc_native_c99_typedef_fnptr_retptr
	/tmp/cc_native_c99_typedef_fnptr_retptr
	$(TOP)/usr.bin/cc/cc native_c99_attr_codegen.c -o /tmp/cc_native_c99_attr_codegen
	/tmp/cc_native_c99_attr_codegen
	$(TOP)/usr.bin/cc/cc -S native_c99_attr_codegen.c -o /tmp/cc_native_c99_attr_codegen.s
	grep -Eq "^[[:space:]]*\\.section \\.hotdata" /tmp/cc_native_c99_attr_codegen.s
	grep -Eq "^[[:space:]]*\\.section \\.text\\.panic" /tmp/cc_native_c99_attr_codegen.s
	grep -q "^\\.align 32$$" /tmp/cc_native_c99_attr_codegen.s
	grep -q "^\\.align 16$$" /tmp/cc_native_c99_attr_codegen.s
	$(TOP)/usr.bin/cc/cc native_c99_gnu_declspec_aliases.c -o /tmp/cc_native_c99_gnu_declspec_aliases
	/tmp/cc_native_c99_gnu_declspec_aliases
	$(TOP)/usr.bin/cc/cc native_c99_multi_decl.c -o /tmp/cc_native_c99_multi_decl
	/tmp/cc_native_c99_multi_decl
	$(TOP)/usr.bin/cc/cc native_c99_char_hex.c -o /tmp/cc_native_c99_char_hex
	/tmp/cc_native_c99_char_hex
	$(TOP)/usr.bin/cc/cc native_c99_char_cast_sign.c -o /tmp/cc_native_c99_char_cast_sign
	/tmp/cc_native_c99_char_cast_sign
	$(TOP)/usr.bin/cc/cc native_c99_signed_char_type.c -o /tmp/cc_native_c99_signed_char_type
	/tmp/cc_native_c99_signed_char_type
	$(TOP)/usr.bin/cc/cc native_c99_logic.c -o /tmp/cc_native_c99_logic
	/tmp/cc_native_c99_logic
	$(TOP)/usr.bin/cc/cc native_c99_inc_expr.c -o /tmp/cc_native_c99_inc_expr
	/tmp/cc_native_c99_inc_expr
	$(TOP)/usr.bin/cc/cc native_c99_update.c -o /tmp/cc_native_c99_update
	/tmp/cc_native_c99_update
	$(TOP)/usr.bin/cc/cc native_c99_bitwise.c -o /tmp/cc_native_c99_bitwise
	/tmp/cc_native_c99_bitwise
	$(TOP)/usr.bin/cc/cc native_c99_comma.c -o /tmp/cc_native_c99_comma
	/tmp/cc_native_c99_comma
	$(TOP)/usr.bin/cc/cc native_c99_goto.c -o /tmp/cc_native_c99_goto
	/tmp/cc_native_c99_goto
	$(TOP)/usr.bin/cc/cc native_c99_goto_loop.c -o /tmp/cc_native_c99_goto_loop
	/tmp/cc_native_c99_goto_loop
	$(TOP)/usr.bin/cc/cc native_c99_computed_goto.c -o /tmp/cc_native_c99_computed_goto
	/tmp/cc_native_c99_computed_goto
	$(TOP)/usr.bin/cc/cc native_c99_ternary.c -o /tmp/cc_native_c99_ternary
	/tmp/cc_native_c99_ternary
	$(TOP)/usr.bin/cc/cc native_c99_cast_sizeof.c -o /tmp/cc_native_c99_cast_sizeof
	/tmp/cc_native_c99_cast_sizeof
	$(TOP)/usr.bin/cc/cc native_c99_expr_conversions.c -o /tmp/cc_native_c99_expr_conversions
	/tmp/cc_native_c99_expr_conversions
	$(TOP)/usr.bin/cc/cc native_c99_sizeof_array.c -o /tmp/cc_native_c99_sizeof_array
	/tmp/cc_native_c99_sizeof_array
	LD=/usr/bin/ld $(TOP)/usr.bin/cc/cc native_c99_sizeof_unsigned.c -o /tmp/cc_native_c99_sizeof_unsigned
	/tmp/cc_native_c99_sizeof_unsigned
	$(TOP)/usr.bin/cc/cc native_c99_sizeof_local_array_cmp.c -o /tmp/cc_native_c99_sizeof_local_array_cmp
	/tmp/cc_native_c99_sizeof_local_array_cmp
	$(TOP)/usr.bin/cc/cc native_c99_float_cmp.c -o /tmp/cc_native_c99_float_cmp
	/tmp/cc_native_c99_float_cmp
	$(TOP)/usr.bin/cc/cc native_c99_long_double_type.c -o /tmp/cc_native_c99_long_double_type
	/tmp/cc_native_c99_long_double_type
	$(TOP)/usr.bin/cc/cc native_c99_unsigned.c -o /tmp/cc_native_c99_unsigned
	/tmp/cc_native_c99_unsigned
	$(TOP)/usr.bin/cc/cc native_c99_unsigned_literals.c -o /tmp/cc_native_c99_unsigned_literals
	/tmp/cc_native_c99_unsigned_literals
	$(TOP)/usr.bin/cc/cc native_c99_short.c -o /tmp/cc_native_c99_short
	/tmp/cc_native_c99_short
	$(TOP)/usr.bin/cc/cc native_c99_pointer.c -o /tmp/cc_native_c99_pointer
	/tmp/cc_native_c99_pointer
	$(TOP)/usr.bin/cc/cc native_c99_pointer_param.c -o /tmp/cc_native_c99_pointer_param
	/tmp/cc_native_c99_pointer_param
	$(TOP)/usr.bin/cc/cc native_c99_pointer_store.c -o /tmp/cc_native_c99_pointer_store
	/tmp/cc_native_c99_pointer_store
	$(TOP)/usr.bin/cc/cc native_c99_pointer_store_param.c -o /tmp/cc_native_c99_pointer_store_param
	/tmp/cc_native_c99_pointer_store_param
	$(TOP)/usr.bin/cc/cc native_c99_deref_compound.c -o /tmp/cc_native_c99_deref_compound
	/tmp/cc_native_c99_deref_compound
	$(TOP)/usr.bin/cc/cc native_c99_deref_prefix_update.c -o /tmp/cc_native_c99_deref_prefix_update
	/tmp/cc_native_c99_deref_prefix_update
	$(TOP)/usr.bin/cc/cc native_c99_deref_postfix_update.c -o /tmp/cc_native_c99_deref_postfix_update
	/tmp/cc_native_c99_deref_postfix_update
	$(TOP)/usr.bin/cc/cc native_c99_pointer_arith.c -o /tmp/cc_native_c99_pointer_arith
	/tmp/cc_native_c99_pointer_arith
	$(TOP)/usr.bin/cc/cc native_c99_index.c -o /tmp/cc_native_c99_index
	/tmp/cc_native_c99_index
	$(TOP)/usr.bin/cc/cc native_c99_pointer_diff.c -o /tmp/cc_native_c99_pointer_diff
	/tmp/cc_native_c99_pointer_diff
	$(TOP)/usr.bin/cc/cc native_c99_ptr_cast.c -o /tmp/cc_native_c99_ptr_cast
	/tmp/cc_native_c99_ptr_cast
	$(TOP)/usr.bin/cc/cc native_c99_ptr_size.c -o /tmp/cc_native_c99_ptr_size
	/tmp/cc_native_c99_ptr_size
	$(TOP)/usr.bin/cc/cc native_c99_ptrptr.c -o /tmp/cc_native_c99_ptrptr
	/tmp/cc_native_c99_ptrptr
	$(TOP)/usr.bin/cc/cc native_c99_ptrptrptr.c -o /tmp/cc_native_c99_ptrptrptr
	/tmp/cc_native_c99_ptrptrptr
	$(TOP)/usr.bin/cc/cc native_c99_ptr4.c -o /tmp/cc_native_c99_ptr4
	/tmp/cc_native_c99_ptr4
	$(TOP)/usr.bin/cc/cc native_c99_ptr_depth6.c -o /tmp/cc_native_c99_ptr_depth6
	/tmp/cc_native_c99_ptr_depth6
	$(TOP)/usr.bin/cc/cc native_c99_pointer_update.c -o /tmp/cc_native_c99_pointer_update
	/tmp/cc_native_c99_pointer_update
	$(TOP)/usr.bin/cc/cc native_c99_octal_char.c -o /tmp/cc_native_c99_octal_char
	/tmp/cc_native_c99_octal_char
	$(TOP)/usr.bin/cc/cc native_c99_string_concat.c -o /tmp/cc_native_c99_string_concat
	/tmp/cc_native_c99_string_concat
	$(TOP)/usr.bin/cc/cc native_c99_func_name.c -o /tmp/cc_native_c99_func_name
	/tmp/cc_native_c99_func_name
	$(TOP)/usr.bin/cc/cc native_c99_atomic_fetch.c -o /tmp/cc_native_c99_atomic_fetch
	/tmp/cc_native_c99_atomic_fetch
	$(TOP)/usr.bin/cc/cc -S native_c99_builtin_strlen.c -o /tmp/cc_native_c99_builtin_strlen.s
	grep -q "call strlen" /tmp/cc_native_c99_builtin_strlen.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -c native_gnu_builtin_prefetch.c -o /tmp/cc_native_gnu_builtin_prefetch.o
	$(TOP)/usr.bin/cc/cc -std=gnu11 -c native_gnu_static_inline_extern.c -o /tmp/cc_native_gnu_static_inline_extern.o
	$(TOP)/usr.bin/cc/cc native_c99_global_array_init.c -o /tmp/cc_native_c99_global_array_init
	/tmp/cc_native_c99_global_array_init
	$(TOP)/usr.bin/cc/cc native_c99_global_init_list.c -o /tmp/cc_native_c99_global_init_list
	/tmp/cc_native_c99_global_init_list
	$(TOP)/usr.bin/cc/cc native_c99_local_init_list.c -o /tmp/cc_native_c99_local_init_list
	/tmp/cc_native_c99_local_init_list
	$(TOP)/usr.bin/cc/cc native_c99_storage_tentative.c -o /tmp/cc_native_c99_storage_tentative
	/tmp/cc_native_c99_storage_tentative
	$(TOP)/usr.bin/cc/cc native_c99_designated_init.c -o /tmp/cc_native_c99_designated_init
	/tmp/cc_native_c99_designated_init
	$(TOP)/usr.bin/cc/cc native_c99_compound_literal.c -o /tmp/cc_native_c99_compound_literal
	/tmp/cc_native_c99_compound_literal
	$(TOP)/usr.bin/cc/cc native_c99_flexible_array.c -o /tmp/cc_native_c99_flexible_array
	/tmp/cc_native_c99_flexible_array
	$(TOP)/usr.bin/cc/cc native_c99_pragma.c -o /tmp/cc_native_c99_pragma
	/tmp/cc_native_c99_pragma
	$(TOP)/usr.bin/cc/cc native_c99_stdc_pragma.c -o /tmp/cc_native_c99_stdc_pragma
	/tmp/cc_native_c99_stdc_pragma
	$(TOP)/usr.bin/cc/cc native_c99_struct_assign.c -o /tmp/cc_native_c99_struct_assign
	/tmp/cc_native_c99_struct_assign
	$(TOP)/usr.bin/cc/cc native_c99_struct_ptr_arith.c -o /tmp/cc_native_c99_struct_ptr_arith
	/tmp/cc_native_c99_struct_ptr_arith
	$(TOP)/usr.bin/cc/cc native_c99_struct_member_array.c -o /tmp/cc_native_c99_struct_member_array
	/tmp/cc_native_c99_struct_member_array
	$(TOP)/usr.bin/cc/cc native_c99_struct_array_init.c -o /tmp/cc_native_c99_struct_array_init
	/tmp/cc_native_c99_struct_array_init
	$(TOP)/usr.bin/cc/cc native_c99_struct_copy_init.c -o /tmp/cc_native_c99_struct_copy_init
	/tmp/cc_native_c99_struct_copy_init
	$(TOP)/usr.bin/cc/cc native_c99_struct_pass_value.c -o /tmp/cc_native_c99_struct_pass_value
	/tmp/cc_native_c99_struct_pass_value
	$(TOP)/usr.bin/cc/cc native_c99_transparent_union_call.c -o /tmp/cc_native_c99_transparent_union_call
	/tmp/cc_native_c99_transparent_union_call
	$(TOP)/usr.bin/cc/cc native_c99_struct_return_value.c -o /tmp/cc_native_c99_struct_return_value
	/tmp/cc_native_c99_struct_return_value
	$(TOP)/usr.bin/cc/cc native_c99_struct_return_16.c -o /tmp/cc_native_c99_struct_return_16
	/tmp/cc_native_c99_struct_return_16
	$(TOP)/usr.bin/cc/cc native_c99_anonymous_struct_union.c -o /tmp/cc_native_c99_anonymous_struct_union
	/tmp/cc_native_c99_anonymous_struct_union
	$(TOP)/usr.bin/cc/cc native_c99_bitfield_basic.c -o /tmp/cc_native_c99_bitfield_basic
	/tmp/cc_native_c99_bitfield_basic
	$(TOP)/usr.bin/cc/cc native_c99_void_pointer.c -o /tmp/cc_native_c99_void_pointer
	/tmp/cc_native_c99_void_pointer
	$(TOP)/usr.bin/cc/cc native_c99_pointer_cmp.c -o /tmp/cc_native_c99_pointer_cmp
	/tmp/cc_native_c99_pointer_cmp
	$(TOP)/usr.bin/cc/cc native_c99_decay.c -o /tmp/cc_native_c99_decay
	/tmp/cc_native_c99_decay
	$(TOP)/usr.bin/cc/cc native_c99_local_array_decay_addr.c -o /tmp/cc_native_c99_local_array_decay_addr
	/tmp/cc_native_c99_local_array_decay_addr
	$(TOP)/usr.bin/cc/cc native_c99_block_scope_shadow.c -o /tmp/cc_native_c99_block_scope_shadow
	/tmp/cc_native_c99_block_scope_shadow
	$(TOP)/usr.bin/cc/cc native_c99_mixed_decl_stmt.c -o /tmp/cc_native_c99_mixed_decl_stmt
	/tmp/cc_native_c99_mixed_decl_stmt
	$(TOP)/usr.bin/cc/cc native_c99_return_conversion.c -o /tmp/cc_native_c99_return_conversion
	/tmp/cc_native_c99_return_conversion
	$(TOP)/usr.bin/cc/cc native_c99_unproto_decl_call.c -o /tmp/cc_native_c99_unproto_decl_call
	/tmp/cc_native_c99_unproto_decl_call
	$(TOP)/usr.bin/cc/cc -S native_c99_unproto_extern_call.c -o /tmp/cc_native_c99_unproto_extern_call.s
	grep -q "call ext" /tmp/cc_native_c99_unproto_extern_call.s
	$(TOP)/usr.bin/cc/cc native_c99_proto_call.c -o /tmp/cc_native_c99_proto_call
	/tmp/cc_native_c99_proto_call
	$(TOP)/usr.bin/cc/cc -S native_c99_proto_extern.c -o /tmp/cc_native_c99_proto_extern.s
	grep -q "call extmul" /tmp/cc_native_c99_proto_extern.s
	$(TOP)/usr.bin/cc/cc native_loops.c -o /tmp/cc_native_loops
	/tmp/cc_native_loops
	$(TOP)/usr.bin/cc/cc native_bad_if_assign.c -o /tmp/cc_native_if_assign
	/tmp/cc_native_if_assign
	$(TOP)/usr.bin/cc/cc native_variadic_call.c -o /tmp/cc_native_variadic_call
	/tmp/cc_native_variadic_call
	$(TOP)/usr.bin/cc/cc native_c99_variadic_fnptr.c -o /tmp/cc_native_c99_variadic_fnptr
	/tmp/cc_native_c99_variadic_fnptr
	$(TOP)/usr.bin/cc/cc native_c99_va_args.c -o /tmp/cc_native_c99_va_args
	/tmp/cc_native_c99_va_args
	$(TOP)/usr.bin/cc/cc native_c99_extension_expr.c -o /tmp/cc_native_c99_extension_expr
	/tmp/cc_native_c99_extension_expr
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_typeof_alt.c -o /tmp/cc_native_gnu_typeof_alt
	/tmp/cc_native_gnu_typeof_alt
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_builtins.c -o /tmp/cc_native_gnu_builtins
	/tmp/cc_native_gnu_builtins
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_function_macros.c -o /tmp/cc_native_gnu_function_macros
	/tmp/cc_native_gnu_function_macros
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_stmt_expr.c -o /tmp/cc_native_gnu_stmt_expr
	/tmp/cc_native_gnu_stmt_expr
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_stmt_expr.c -o /tmp/cc_native_gnu_stmt_expr.s
	grep -q "imulq" /tmp/cc_native_gnu_stmt_expr.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_stmt_expr.c -o /tmp/cc_native_gnu_stmt_expr_32.s
	grep -q "\\.code32" /tmp/cc_native_gnu_stmt_expr_32.s
	grep -q "imull" /tmp/cc_native_gnu_stmt_expr_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_label_decl.c -o /tmp/cc_native_gnu_label_decl
	/tmp/cc_native_gnu_label_decl
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_label_decl.c -o /tmp/cc_native_gnu_label_decl.s
	grep -Eq "leaq \\.Lmain_[0-9]+\\(%rip\\)" /tmp/cc_native_gnu_label_decl.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_label_decl.c -o /tmp/cc_native_gnu_label_decl_32.s
	grep -q "\\.code32" /tmp/cc_native_gnu_label_decl_32.s
	grep -Eq "movl \\$$\\.Lmain_[0-9]+, %eax" /tmp/cc_native_gnu_label_decl_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_elvis.c -o /tmp/cc_native_gnu_elvis
	/tmp/cc_native_gnu_elvis
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_elvis.c -o /tmp/cc_native_gnu_elvis.s
	test "$$(grep -c "cmpq" /tmp/cc_native_gnu_elvis.s)" -ge 2
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_elvis.c -o /tmp/cc_native_gnu_elvis_32.s
	grep -q "\\.code32" /tmp/cc_native_gnu_elvis_32.s
	test "$$(grep -c "cmpl" /tmp/cc_native_gnu_elvis_32.s)" -ge 2
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_case_range.c -o /tmp/cc_native_gnu_case_range
	/tmp/cc_native_gnu_case_range
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_case_range.c -o /tmp/cc_native_gnu_case_range.s
	grep -q "setge" /tmp/cc_native_gnu_case_range.s
	grep -q "setle" /tmp/cc_native_gnu_case_range.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_case_range.c -o /tmp/cc_native_gnu_case_range_32.s
	grep -q "\\.code32" /tmp/cc_native_gnu_case_range_32.s
	grep -q "setge" /tmp/cc_native_gnu_case_range_32.s
	grep -q "setle" /tmp/cc_native_gnu_case_range_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_types_choose.c -o /tmp/cc_native_gnu_types_choose
	/tmp/cc_native_gnu_types_choose
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_types_choose.c -o /tmp/cc_native_gnu_types_choose.s
	! grep -q "does_not_exist" /tmp/cc_native_gnu_types_choose.s
	! grep -q "does_not_exist2" /tmp/cc_native_gnu_types_choose.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_overflow.c -o /tmp/cc_native_gnu_overflow
	/tmp/cc_native_gnu_overflow
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_overflow.c -o /tmp/cc_native_gnu_overflow.s
	grep -q "setb" /tmp/cc_native_gnu_overflow.s
	grep -q "imulq" /tmp/cc_native_gnu_overflow.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_overflow.c -o /tmp/cc_native_gnu_overflow_32.s
	grep -q "\\.code32" /tmp/cc_native_gnu_overflow_32.s
	grep -q "setb" /tmp/cc_native_gnu_overflow_32.s
	grep -q "imull" /tmp/cc_native_gnu_overflow_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_object_size_chk.c -o /tmp/cc_native_gnu_object_size_chk
	/tmp/cc_native_gnu_object_size_chk
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_object_size_chk.c -o /tmp/cc_native_gnu_object_size_chk.s
	grep -q "ud2" /tmp/cc_native_gnu_object_size_chk.s
	grep -q "call memcpy" /tmp/cc_native_gnu_object_size_chk.s
	grep -q "call memmove" /tmp/cc_native_gnu_object_size_chk.s
	grep -q "call memset" /tmp/cc_native_gnu_object_size_chk.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_object_size_chk.c -o /tmp/cc_native_gnu_object_size_chk_32.s
	grep -q "\\.code32" /tmp/cc_native_gnu_object_size_chk_32.s
	grep -q "ud2" /tmp/cc_native_gnu_object_size_chk_32.s
	grep -q "call memcpy" /tmp/cc_native_gnu_object_size_chk_32.s
	grep -q "call memmove" /tmp/cc_native_gnu_object_size_chk_32.s
	grep -q "call memset" /tmp/cc_native_gnu_object_size_chk_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_trap_unreachable.c -o /tmp/cc_native_gnu_trap_unreachable
	/tmp/cc_native_gnu_trap_unreachable
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_trap_unreachable.c -o /tmp/cc_native_gnu_trap_unreachable.s
	grep -q "ud2" /tmp/cc_native_gnu_trap_unreachable.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_trap_unreachable.c -o /tmp/cc_native_gnu_trap_unreachable_32.s
	grep -q "\\.code32" /tmp/cc_native_gnu_trap_unreachable_32.s
	grep -q "ud2" /tmp/cc_native_gnu_trap_unreachable_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_nested_func.c -o /tmp/cc_native_gnu_nested_func
	/tmp/cc_native_gnu_nested_func
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_union_cast.c -o /tmp/cc_native_gnu_union_cast
	/tmp/cc_native_gnu_union_cast
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_zerolen_array.c -o /tmp/cc_native_gnu_zerolen_array
	/tmp/cc_native_gnu_zerolen_array
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_empty_struct.c -o /tmp/cc_native_gnu_empty_struct
	/tmp/cc_native_gnu_empty_struct
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_void_ptr_arith.c -o /tmp/cc_native_gnu_void_ptr_arith
	/tmp/cc_native_gnu_void_ptr_arith
	$(TOP)/usr.bin/cc/cc -std=c99 -pedantic -c native_gnu_void_ptr_arith.c -o /tmp/cc_native_gnu_void_ptr_arith_pedantic.o 2>/tmp/cc_native_gnu_void_ptr_arith_pedantic.log
	grep -q "warning: arithmetic on 'void \\*' is a GNU extension" /tmp/cc_native_gnu_void_ptr_arith_pedantic.log
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_static_nonconst_init.c -o /tmp/cc_native_gnu_static_nonconst_init
	/tmp/cc_native_gnu_static_nonconst_init
	$(TOP)/usr.bin/cc/cc -std=gnu11 -fgnu89-inline native_gnu89_inline.c -o /tmp/cc_native_gnu89_inline
	/tmp/cc_native_gnu89_inline
	$(TOP)/usr.bin/cc/cc -std=gnu11 -fgnu89-inline -S native_gnu89_inline.c -o /tmp/cc_native_gnu89_inline.s
	grep -q "^\\.globl add_inline" /tmp/cc_native_gnu89_inline.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_asm_basic.c -o /tmp/cc_native_gnu_asm_basic
	/tmp/cc_native_gnu_asm_basic
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_asm_basic.c -o /tmp/cc_native_gnu_asm_basic.s
	grep -q "nop" /tmp/cc_native_gnu_asm_basic.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_asm_ext.c -o /tmp/cc_native_gnu_asm_ext
	/tmp/cc_native_gnu_asm_ext
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_asm_ext.c -o /tmp/cc_native_gnu_asm_ext.s
	grep -Eq 'add .*[$$]5' /tmp/cc_native_gnu_asm_ext.s
	grep -Eq "add .*%rax|add .*%r[0-9]+" /tmp/cc_native_gnu_asm_ext.s
	grep -q "mov" /tmp/cc_native_gnu_asm_ext.s
	grep -q "# asm clobber memory" /tmp/cc_native_gnu_asm_ext.s
	grep -q "# asm clobber cc" /tmp/cc_native_gnu_asm_ext.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_asm_ext.c -o /tmp/cc_native_gnu_asm_ext_32.s
	grep -q "\\.code32" /tmp/cc_native_gnu_asm_ext_32.s
	grep -q "add" /tmp/cc_native_gnu_asm_ext_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_asm_goto.c -o /tmp/cc_native_gnu_asm_goto
	/tmp/cc_native_gnu_asm_goto
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_asm_goto_outputs.c -o /tmp/cc_native_gnu_asm_goto_outputs
	/tmp/cc_native_gnu_asm_goto_outputs
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_asm_goto.c -o /tmp/cc_native_gnu_asm_goto.s
	grep -Eq "jz \\.L[a-zA-Z0-9_]+_[0-9]+" /tmp/cc_native_gnu_asm_goto.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_gnu_asm_goto.c -o /tmp/cc_native_gnu_asm_goto_32.s
	grep -q "\\.code32" /tmp/cc_native_gnu_asm_goto_32.s
	grep -Eq "jz \\.L[a-zA-Z0-9_]+_[0-9]+" /tmp/cc_native_gnu_asm_goto_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_asm_early_clobber.c -o /tmp/cc_native_gnu_asm_early_clobber
	/tmp/cc_native_gnu_asm_early_clobber
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_asm_volatile_barrier.c -O2 -o /tmp/cc_native_gnu_asm_volatile_barrier.s
	grep -q "# volatile asm" /tmp/cc_native_gnu_asm_volatile_barrier.s
	grep -q "# asm clobber memory" /tmp/cc_native_gnu_asm_volatile_barrier.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 -O2 native_gnu_asm_side_effect.c -o /tmp/cc_native_gnu_asm_side_effect
	/tmp/cc_native_gnu_asm_side_effect
	$(TOP)/usr.bin/cc/cc -std=gnu11 -O2 -S native_gnu_asm_side_effect.c -o /tmp/cc_native_gnu_asm_side_effect.s
	grep -Eq "add .*[$$]1" /tmp/cc_native_gnu_asm_side_effect.s
	grep -q "# volatile asm" /tmp/cc_native_gnu_asm_side_effect.s
	grep -q "# asm clobber memory" /tmp/cc_native_gnu_asm_side_effect.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_fn_codegen.c -o /tmp/cc_native_gnu_attr_fn_codegen
	/tmp/cc_native_gnu_attr_fn_codegen
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_attr_fn_codegen.c -o /tmp/cc_native_gnu_attr_fn_codegen.s
	grep -Eq "^\\.weak hot_fn$$" /tmp/cc_native_gnu_attr_fn_codegen.s
	grep -Eq "^\\.weak cold_fn$$" /tmp/cc_native_gnu_attr_fn_codegen.s
	grep -Eq "^[[:space:]]*\\.section \\.text\\.hot" /tmp/cc_native_gnu_attr_fn_codegen.s
	grep -Eq "^[[:space:]]*\\.section \\.text\\.unlikely" /tmp/cc_native_gnu_attr_fn_codegen.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_var_codegen.c -o /tmp/cc_native_gnu_attr_var_codegen
	/tmp/cc_native_gnu_attr_var_codegen
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_attr_var_codegen.c -o /tmp/cc_native_gnu_attr_var_codegen.s
	grep -Eq "^[[:space:]]*\\.section \\.attr\\.data" /tmp/cc_native_gnu_attr_var_codegen.s
	grep -q "^\\.align 32$$" /tmp/cc_native_gnu_attr_var_codegen.s
	grep -Eq "^\\.weak vis_obj$$" /tmp/cc_native_gnu_attr_var_codegen.s
	grep -Eq "^\\.hidden vis_obj$$" /tmp/cc_native_gnu_attr_var_codegen.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_type_parse.c -o /tmp/cc_native_gnu_attr_type_parse
	/tmp/cc_native_gnu_attr_type_parse
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_alias.c -o /tmp/cc_native_gnu_attr_alias
	/tmp/cc_native_gnu_attr_alias
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_gnu_attr_alias.c -o /tmp/cc_native_gnu_attr_alias.s
	grep -Eq "^\\.set alias_fn, target_fn$$" /tmp/cc_native_gnu_attr_alias.s
	grep -Eq "^\\.set alias_obj, target_obj$$" /tmp/cc_native_gnu_attr_alias.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_gnu_attr_label_enum_stmt.c -o /tmp/cc_native_gnu_attr_label_enum_stmt
	/tmp/cc_native_gnu_attr_label_enum_stmt
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_clang_blocks_decl.c -o /tmp/cc_native_clang_blocks_decl
	/tmp/cc_native_clang_blocks_decl
	$(TOP)/usr.bin/cc/cc -std=gnu11 -m32 -S native_clang_blocks_decl.c -o /tmp/cc_native_clang_blocks_decl_32.s
	grep -q "\\.code32" /tmp/cc_native_clang_blocks_decl_32.s
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_clang_ext_vector_attr.c -o /tmp/cc_native_clang_ext_vector_attr
	/tmp/cc_native_clang_ext_vector_attr
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_clang_builtin_aliases.c -o /tmp/cc_native_clang_builtin_aliases
	/tmp/cc_native_clang_builtin_aliases
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_clang_attr_placement.c -o /tmp/cc_native_clang_attr_placement
	/tmp/cc_native_clang_attr_placement
	$(TOP)/usr.bin/cc/cc -std=gnu11 native_clang_asm_goto_quirks.c -o /tmp/cc_native_clang_asm_goto_quirks
	/tmp/cc_native_clang_asm_goto_quirks
	$(TOP)/usr.bin/cc/cc -std=gnu11 -S native_clang_asm_goto_quirks.c -o /tmp/cc_native_clang_asm_goto_quirks.s
	grep -Eq "jz \\.L[a-zA-Z0-9_]+_[0-9]+" /tmp/cc_native_clang_asm_goto_quirks.s
	$(TOP)/usr.bin/cc/cc -O2 native_opt_fold.c -o /tmp/cc_native_opt_fold
	/tmp/cc_native_opt_fold
	$(TOP)/usr.bin/cc/cc -O2 -S native_opt_fold.c -o /tmp/cc_native_opt_fold.s
	! grep -q "imulq" /tmp/cc_native_opt_fold.s
	$(TOP)/usr.bin/cc/cc -S native_slot_pressure.c -o /tmp/cc_native_slot_pressure.s
	awk '/subq \$$[0-9]+, %rsp/ { n = $$2; gsub(/[^0-9]/, "", n); if ((n + 0) <= 256) ok = 1 } END { exit ok ? 0 : 1 }' /tmp/cc_native_slot_pressure.s
	$(TOP)/usr.bin/cc/cc -S native_variadic_call.c -o /tmp/cc_native_variadic_call.s
	grep -Fq 'movb $$2, %al' /tmp/cc_native_variadic_call.s
	$(TOP)/usr.bin/cc/cc -S native_c99_va_args.c -o /tmp/cc_native_c99_va_args.s
	grep -Fq 'movl $$8, ' /tmp/cc_native_c99_va_args.s
	grep -q 'addq %rcx, %rax' /tmp/cc_native_c99_va_args.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_arith.c -o /tmp/cc_native_arith_32.s
	grep -q "\\.code32" /tmp/cc_native_arith_32.s
	grep -q "movl" /tmp/cc_native_arith_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_if_else.c -o /tmp/cc_native_if_else_32.s
	grep -q "jne" /tmp/cc_native_if_else_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c95_digraph.c -o /tmp/cc_native_c95_digraph_32.s
	grep -q "\\.code32" /tmp/cc_native_c95_digraph_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c95_trigraph.c -o /tmp/cc_native_c95_trigraph_32.s
	grep -q "jne" /tmp/cc_native_c95_trigraph_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_phase12_trigraph_splice.c -o /tmp/cc_native_c99_phase12_trigraph_splice_32.s
	grep -q "^main:" /tmp/cc_native_c99_phase12_trigraph_splice_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ucn_ident.c -o /tmp/cc_native_c99_ucn_ident_32.s
	grep -q "^main:" /tmp/cc_native_c99_ucn_ident_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_tokenization_ppnum.c -o /tmp/cc_native_c99_tokenization_ppnum_32.s
	grep -Eq "ucomisd|fucompp" /tmp/cc_native_c99_tokenization_ppnum_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_line_comment.c -o /tmp/cc_native_c99_line_comment_32.s
	grep -q '\$$3' /tmp/cc_native_c99_line_comment_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_integer_literals.c -o /tmp/cc_native_c99_integer_literals_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_integer_literals_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_hex_float_suffix.c -o /tmp/cc_native_c99_hex_float_suffix_32.s
	grep -Eq "ucomisd|fucompp" /tmp/cc_native_c99_hex_float_suffix_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_wide_literals.c -o /tmp/cc_native_c99_wide_literals_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_wide_literals_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_do_while.c -o /tmp/cc_native_do_while_32.s
	grep -q "jne" /tmp/cc_native_do_while_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_empty_stmt2.c -o /tmp/cc_native_c99_empty_stmt2_32.s
	grep -q "jne" /tmp/cc_native_c99_empty_stmt2_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_switch.c -o /tmp/cc_native_switch_32.s
	grep -q "sete" /tmp/cc_native_switch_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_for_decl.c -o /tmp/cc_native_c99_for_decl_32.s
	grep -q "jne" /tmp/cc_native_c99_for_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_for_multi_decl.c -o /tmp/cc_native_c99_for_multi_decl_32.s
	grep -q "jne" /tmp/cc_native_c99_for_multi_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_types.c -o /tmp/cc_native_c99_types_32.s
	grep -q "call add_ll" /tmp/cc_native_c99_types_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_const_qual.c -o /tmp/cc_native_c99_const_qual_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_const_qual_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_composite_types.c -o /tmp/cc_native_c99_composite_types_32.s
	grep -q "call sum" /tmp/cc_native_c99_composite_types_32.s
	$(TOP)/usr.bin/cc/cc -m32 -std=c99 -S native_c99_inline_semantics.c -o /tmp/cc_native_c99_inline_semantics_32.s
	! grep -q "^\\.globl add_local$$" /tmp/cc_native_c99_inline_semantics_32.s
	grep -q "^\\.globl add_ext$$" /tmp/cc_native_c99_inline_semantics_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_complex_decl.c -o /tmp/cc_native_c99_complex_decl_32.s
	grep -Eq "cvttsd2sil|fistpl" /tmp/cc_native_c99_complex_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_imaginary_decl.c -o /tmp/cc_native_c99_imaginary_decl_32.s
	grep -Eq "cvtts[sd]2sil|fistpl" /tmp/cc_native_c99_imaginary_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_param_array_qual.c -o /tmp/cc_native_c99_param_array_qual_32.s
	grep -q "main:" /tmp/cc_native_c99_param_array_qual_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_restrict_ptr.c -o /tmp/cc_native_c99_restrict_ptr_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_restrict_ptr_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_vla_decl.c -o /tmp/cc_native_c99_vla_decl_32.s
	grep -q "main:" /tmp/cc_native_c99_vla_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 -std=c90 -S native_c90_oldstyle.c -o /tmp/cc_native_c90_oldstyle_32.s
	grep -q "add:" /tmp/cc_native_c90_oldstyle_32.s
	$(TOP)/usr.bin/cc/cc -m32 -std=c90 -S native_c90_unproto_call_promotions.c -o /tmp/cc_native_c90_unproto_call_promotions_32.s
	grep -q "call ext" /tmp/cc_native_c90_unproto_call_promotions_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_typedef.c -o /tmp/cc_native_c99_typedef_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_typedef_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_multi_decl.c -o /tmp/cc_native_c99_multi_decl_32.s
	grep -q "movl" /tmp/cc_native_c99_multi_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_logic.c -o /tmp/cc_native_c99_logic_32.s
	grep -q "jne" /tmp/cc_native_c99_logic_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_update.c -o /tmp/cc_native_c99_update_32.s
	grep -q "idivl" /tmp/cc_native_c99_update_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_bitwise.c -o /tmp/cc_native_c99_bitwise_32.s
	grep -q "xorl" /tmp/cc_native_c99_bitwise_32.s
	grep -q "shll" /tmp/cc_native_c99_bitwise_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_comma.c -o /tmp/cc_native_c99_comma_32.s
	grep -q "movl" /tmp/cc_native_c99_comma_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_goto.c -o /tmp/cc_native_c99_goto_32.s
	grep -q "jmp" /tmp/cc_native_c99_goto_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_goto_loop.c -o /tmp/cc_native_c99_goto_loop_32.s
	grep -q "jmp" /tmp/cc_native_c99_goto_loop_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ternary.c -o /tmp/cc_native_c99_ternary_32.s
	grep -q "jne" /tmp/cc_native_c99_ternary_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_cast_sizeof.c -o /tmp/cc_native_c99_cast_sizeof_32.s
	grep -Eq "cvttsd2sil|fistpl" /tmp/cc_native_c99_cast_sizeof_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_expr_conversions.c -o /tmp/cc_native_c99_expr_conversions_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_expr_conversions_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_sizeof_array.c -o /tmp/cc_native_c99_sizeof_array_32.s
	grep -A8 "main:" /tmp/cc_native_c99_sizeof_array_32.s | grep -Eq '[$$](28|20)'
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_sizeof_unsigned.c -o /tmp/cc_native_c99_sizeof_unsigned_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_sizeof_unsigned_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_float_cmp.c -o /tmp/cc_native_c99_float_cmp_32.s
	grep -Eq "ucomisd|fucompp" /tmp/cc_native_c99_float_cmp_32.s
	grep -q "setnp" /tmp/cc_native_c99_float_cmp_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_unsigned.c -o /tmp/cc_native_c99_unsigned_32.s
	grep -q "divl" /tmp/cc_native_c99_unsigned_32.s
	grep -q "shrl" /tmp/cc_native_c99_unsigned_32.s
	grep -q "seta" /tmp/cc_native_c99_unsigned_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_unsigned_literals.c -o /tmp/cc_native_c99_unsigned_literals_32.s
	grep -q "seta" /tmp/cc_native_c99_unsigned_literals_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_short.c -o /tmp/cc_native_c99_short_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_short_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_pointer.c -o /tmp/cc_native_c99_pointer_32.s
	grep -q "leal" /tmp/cc_native_c99_pointer_32.s
	grep -q "(%eax)" /tmp/cc_native_c99_pointer_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_pointer_store.c -o /tmp/cc_native_c99_pointer_store_32.s
	grep -Eq "movl %e[a-z]{2}, \\(%e[a-z]{2}\\)" /tmp/cc_native_c99_pointer_store_32.s
	$(TOP)/usr.bin/cc/cc -S native_c99_pointer_arith.c -o /tmp/cc_native_c99_pointer_arith.s
	grep -q "imulq" /tmp/cc_native_c99_pointer_arith.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_pointer_arith.c -o /tmp/cc_native_c99_pointer_arith_32.s
	grep -q "imull" /tmp/cc_native_c99_pointer_arith_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_index.c -o /tmp/cc_native_c99_index_32.s
	grep -q "imull" /tmp/cc_native_c99_index_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_pointer_diff.c -o /tmp/cc_native_c99_pointer_diff_32.s
	grep -q "idivl" /tmp/cc_native_c99_pointer_diff_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ptr_cast.c -o /tmp/cc_native_c99_ptr_cast_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_ptr_cast_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ptr_size.c -o /tmp/cc_native_c99_ptr_size_32.s
	grep -A6 "ptr_size:" /tmp/cc_native_c99_ptr_size_32.s | grep -q '\$$4'
	grep -A6 "ptrptr_size:" /tmp/cc_native_c99_ptr_size_32.s | grep -q '\$$4'
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ptrptr.c -o /tmp/cc_native_c99_ptrptr_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_ptrptr_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ptrptrptr.c -o /tmp/cc_native_c99_ptrptrptr_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_ptrptrptr_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_ptr4.c -o /tmp/cc_native_c99_ptr4_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_ptr4_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_pointer_update.c -o /tmp/cc_native_c99_pointer_update_32.s
	grep -q "addl" /tmp/cc_native_c99_pointer_update_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_global_array_init.c -o /tmp/cc_native_c99_global_array_init_32.s
	grep -q "movb" /tmp/cc_native_c99_global_array_init_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_global_init_list.c -o /tmp/cc_native_c99_global_init_list_32.s
	grep -q "\\.long" /tmp/cc_native_c99_global_init_list_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_local_init_list.c -o /tmp/cc_native_c99_local_init_list_32.s
	grep -q "movl" /tmp/cc_native_c99_local_init_list_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_storage_tentative.c -o /tmp/cc_native_c99_storage_tentative_32.s
	grep -q "\\.bss" /tmp/cc_native_c99_storage_tentative_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_designated_init.c -o /tmp/cc_native_c99_designated_init_32.s
	grep -q "movl" /tmp/cc_native_c99_designated_init_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_compound_literal.c -o /tmp/cc_native_c99_compound_literal_32.s
	grep -Eq "movl %e[a-z]{2}, \\(%e[a-z]{2}\\)" /tmp/cc_native_c99_compound_literal_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_flexible_array.c -o /tmp/cc_native_c99_flexible_array_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_flexible_array_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_assign.c -o /tmp/cc_native_c99_struct_assign_32.s
	grep -q "call memcpy" /tmp/cc_native_c99_struct_assign_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_ptr_arith.c -o /tmp/cc_native_c99_struct_ptr_arith_32.s
	grep -q "imull" /tmp/cc_native_c99_struct_ptr_arith_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_member_array.c -o /tmp/cc_native_c99_struct_member_array_32.s
	grep -Eq "movl %e[a-z]{2}, \\(%e[a-z]{2}\\)" /tmp/cc_native_c99_struct_member_array_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_array_init.c -o /tmp/cc_native_c99_struct_array_init_32.s
	grep -Eq "movl %e[a-z]{2}, \\(%e[a-z]{2}\\)" /tmp/cc_native_c99_struct_array_init_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_copy_init.c -o /tmp/cc_native_c99_struct_copy_init_32.s
	grep -q "call memcpy" /tmp/cc_native_c99_struct_copy_init_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_pass_value.c -o /tmp/cc_native_c99_struct_pass_value_32.s
	grep -q "call sum_pair" /tmp/cc_native_c99_struct_pass_value_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_return_value.c -o /tmp/cc_native_c99_struct_return_value_32.s
	grep -q "call mk_pair" /tmp/cc_native_c99_struct_return_value_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_bitfield_basic.c -o /tmp/cc_native_c99_bitfield_basic_32.s
	grep -q "andl" /tmp/cc_native_c99_bitfield_basic_32.s
	grep -q "shll" /tmp/cc_native_c99_bitfield_basic_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_deref_postfix_update.c -o /tmp/cc_native_c99_deref_postfix_update_32.s
	grep -q "addl" /tmp/cc_native_c99_deref_postfix_update_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_proto_extern.c -o /tmp/cc_native_c99_proto_extern_32.s
	grep -q "call extmul" /tmp/cc_native_c99_proto_extern_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_decay.c -o /tmp/cc_native_c99_decay_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_decay_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_block_scope_shadow.c -o /tmp/cc_native_c99_block_scope_shadow_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_block_scope_shadow_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_mixed_decl_stmt.c -o /tmp/cc_native_c99_mixed_decl_stmt_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_mixed_decl_stmt_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_return_conversion.c -o /tmp/cc_native_c99_return_conversion_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_return_conversion_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_stdc_pragma.c -o /tmp/cc_native_c99_stdc_pragma_32.s
	grep -q "\\.code32" /tmp/cc_native_c99_stdc_pragma_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_unproto_decl_call.c -o /tmp/cc_native_c99_unproto_decl_call_32.s
	grep -q "call addmix" /tmp/cc_native_c99_unproto_decl_call_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_unproto_extern_call.c -o /tmp/cc_native_c99_unproto_extern_call_32.s
	grep -q "call ext" /tmp/cc_native_c99_unproto_extern_call_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_va_args.c -o /tmp/cc_native_c99_va_args_32.s
	grep -q "leal 12(%ebp), %eax" /tmp/cc_native_c99_va_args_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_loops.c -o /tmp/cc_native_loops_32.s
	grep -q "jmp" /tmp/cc_native_loops_32.s
	$(TOP)/usr.bin/cc/cc -m32 -c native_arith.c -o /tmp/cc_native_arith_32.o
	file /tmp/cc_native_arith_32.o | grep -q "ELF 32-bit"
	$(TOP)/usr.bin/cc/cc -m32 -S native_double.c -o /tmp/cc_native_double_32.s
	grep -Eq "movsd|fldl" /tmp/cc_native_double_32.s
	grep -q "fldl" /tmp/cc_native_double_32.s
	$(TOP)/usr.bin/cc/cc -m32 -c native_double.c -o /tmp/cc_native_double_32.o
	file /tmp/cc_native_double_32.o | grep -q "ELF 32-bit"
	$(TOP)/usr.bin/cc/cc -g -S native_double.c -o /tmp/cc_native_double_dbg.s
	grep -q "\\.cfi_startproc" /tmp/cc_native_double_dbg.s
	grep -q "\\.file" /tmp/cc_native_double_dbg.s
	grep -q "addsd" /tmp/cc_native_double_dbg.s
	grep -q "mulsd" /tmp/cc_native_double_dbg.s
	$(TOP)/usr.bin/cc/cc -Wall -c native_warn_assign_cond.c -o /tmp/cc_native_warn_assign_cond.o 2>/tmp/cc_native_warn_assign_cond.log
	grep -q "warning: assignment used as condition in if" /tmp/cc_native_warn_assign_cond.log
	! $(TOP)/usr.bin/cc/cc -Wall -Werror -c native_warn_assign_cond.c -o /tmp/cc_native_warn_assign_cond_werror.o
	$(TOP)/usr.bin/cc/cc -std=c99 -pedantic -c native_c99_computed_goto.c -o /tmp/cc_native_c99_computed_goto_pedantic.o 2>/tmp/cc_native_c99_computed_goto_pedantic.log
	grep -q "warning: computed goto is a GNU extension (non-C99)" /tmp/cc_native_c99_computed_goto_pedantic.log
	! $(TOP)/usr.bin/cc/cc -std=c99 -pedantic-errors -c native_c99_computed_goto.c -o /tmp/cc_native_c99_computed_goto_pedantic_errors.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_ptr_add_ptr.c -o /tmp/cc_native_bad_ptr_add_ptr_loc.o 2>/tmp/cc_native_bad_ptr_add_ptr_loc.log
	grep -Eq '^[^:]+:[0-9]+:[0-9]+: error: ' /tmp/cc_native_bad_ptr_add_ptr_loc.log
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_source_line.c -o /tmp/cc_native_bad_source_line.o 2>/tmp/cc_native_bad_source_line.log
	grep -q "return missing + ;" /tmp/cc_native_bad_source_line.log
	grep -Eq '^[[:space:]]*\^$$' /tmp/cc_native_bad_source_line.log
	sh run_conformance_c99.sh
	sh run_diff_c99.sh
	sh run_postc99_c11_c17.sh
	sh run_postc99_c23.sh
	sh run_postc99_c23_attrs.sh
	sh run_postc99_mode_matrix.sh
	sh run_postc99_diff_modes.sh
	sh run_postc99_abi_modes.sh
	sh run_pp_postc99.sh
	sh run_pp_c99_baseline.sh
	sh run_pp_fuzz_smoke.sh
	sh run_pp_gnu_ext.sh
	sh run_pp_clang_feature_probe.sh
	sh run_pp_clang_pragma.sh
	sh run_pp_cmd_injection_test.sh
	printf '' > /tmp/cc_pp_std_probe.c
	$(TOP)/usr.bin/cc/cc -E -dM -std=c11 /tmp/cc_pp_std_probe.c -o /tmp/cc_pp_strict_c11.i
	grep -q "^#define __STRICT_ANSI__ 1$$" /tmp/cc_pp_strict_c11.i
	$(TOP)/usr.bin/cc/cc -E -dM -std=gnu11 /tmp/cc_pp_std_probe.c -o /tmp/cc_pp_strict_gnu11.i
	! grep -q "^#define __STRICT_ANSI__" /tmp/cc_pp_strict_gnu11.i
	sh run_simd_intrinsics.sh
	! $(TOP)/usr.bin/cc/cc -c native_bad.c -o /tmp/cc_native_bad.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_assign.c -o /tmp/cc_native_bad_assign.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_break.c -o /tmp/cc_native_bad_break.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_continue.c -o /tmp/cc_native_bad_continue.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_case.c -o /tmp/cc_native_bad_case.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_default.c -o /tmp/cc_native_bad_default.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_switch_dup_case.c -o /tmp/cc_native_bad_switch_dup_case.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_switch_dup_default.c -o /tmp/cc_native_bad_switch_dup_default.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_for_scope.c -o /tmp/cc_native_bad_for_scope.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_inc.c -o /tmp/cc_native_bad_inc.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_goto.c -o /tmp/cc_native_bad_goto.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_label_dup.c -o /tmp/cc_native_bad_label_dup.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_bitwise_float.c -o /tmp/cc_native_bad_bitwise_float.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_sizeof_void.c -o /tmp/cc_native_bad_sizeof_void.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_proto_conflict.c -o /tmp/cc_native_bad_proto_conflict.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_signed_unsigned.c -o /tmp/cc_native_bad_signed_unsigned.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_short_long.c -o /tmp/cc_native_bad_short_long.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_typedef_init.c -o /tmp/cc_native_bad_typedef_init.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_deref_nonptr.c -o /tmp/cc_native_bad_deref_nonptr.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_addr_expr.c -o /tmp/cc_native_bad_addr_expr.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_deref_compound.c -o /tmp/cc_native_bad_deref_compound.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_deref_postfix_update.c -o /tmp/cc_native_bad_deref_postfix_update.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_ptr_add_ptr.c -o /tmp/cc_native_bad_ptr_add_ptr.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_index_nonptr.c -o /tmp/cc_native_bad_index_nonptr.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_ptr_add_float.c -o /tmp/cc_native_bad_ptr_add_float.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_ptr_sub_incompat.c -o /tmp/cc_native_bad_ptr_sub_incompat.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_ptr_cmp_incompat.c -o /tmp/cc_native_bad_ptr_cmp_incompat.o
	$(TOP)/usr.bin/cc/cc -c native_bad_ptr_depth5.c -o /tmp/cc_native_bad_ptr_depth5.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_cast_ptr_float.c -o /tmp/cc_native_bad_cast_ptr_float.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_computed_goto_float.c -o /tmp/cc_native_bad_computed_goto_float.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_nested_capture.c -o /tmp/cc_native_bad_gnu_nested_capture.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_asm_constraint.c -o /tmp/cc_native_bad_gnu_asm_constraint.o 2>/tmp/cc_native_bad_gnu_asm_constraint.log
	grep -q "asm operand constraint contains unsupported character" /tmp/cc_native_bad_gnu_asm_constraint.log
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_asm_match_idx.c -o /tmp/cc_native_bad_gnu_asm_match_idx.o 2>/tmp/cc_native_bad_gnu_asm_match_idx.log
	grep -q "asm matching constraint references invalid output index" /tmp/cc_native_bad_gnu_asm_match_idx.log
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_asm_template_mismatch.c -o /tmp/cc_native_bad_gnu_asm_template_mismatch.o 2>/tmp/cc_native_bad_gnu_asm_template_mismatch.log
	grep -q "asm template operand index is out of range" /tmp/cc_native_bad_gnu_asm_template_mismatch.log
	$(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_asm_early_clobber_tie.c -o /tmp/cc_native_bad_gnu_asm_early_clobber_tie.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_gnu_asm_early_clobber_fixed.c -o /tmp/cc_native_bad_gnu_asm_early_clobber_fixed.o 2>/tmp/cc_native_bad_gnu_asm_early_clobber_fixed.log
	grep -q "asm input constraint conflicts with early-clobber output register" /tmp/cc_native_bad_gnu_asm_early_clobber_fixed.log
	! $(TOP)/usr.bin/cc/cc -c native_bad_attr_noreturn_return.c -o /tmp/cc_native_bad_attr_noreturn_return.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_attr_noreturn_nonvoid.c -o /tmp/cc_native_bad_attr_noreturn_nonvoid.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_attr_inline_conflict.c -o /tmp/cc_native_bad_attr_inline_conflict.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_attr_hot_cold_conflict.c -o /tmp/cc_native_bad_attr_hot_cold_conflict.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_attr_alias_init.c -o /tmp/cc_native_bad_attr_alias_init.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_attr_malloc_nonptr.c -o /tmp/cc_native_bad_attr_malloc_nonptr.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_attr_format_nonvariadic.c -o /tmp/cc_native_bad_attr_format_nonvariadic.o
	! $(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_attr_nonnull_empty.c -o /tmp/cc_native_bad_attr_nonnull_empty.o
	$(TOP)/usr.bin/cc/cc -std=gnu11 -c native_bad_attr_nonnull_call.c -o /tmp/cc_native_bad_attr_nonnull_call.o 2>/tmp/cc_native_bad_attr_nonnull_call.log
	grep -q "warning: nonnull argument 1 in call to takes_ptr is null" /tmp/cc_native_bad_attr_nonnull_call.log
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_implicit_function.c -o /tmp/cc_native_bad_c99_implicit_function.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_no_implicit_int.c -o /tmp/cc_native_bad_no_implicit_int.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_const_assign.c -o /tmp/cc_native_bad_c99_const_assign.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_const_param_assign.c -o /tmp/cc_native_bad_c99_const_param_assign.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_restrict_nonptr.c -o /tmp/cc_native_bad_c99_restrict_nonptr.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_file_scope_auto.c -o /tmp/cc_native_bad_c99_file_scope_auto.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_extern_local_init.c -o /tmp/cc_native_bad_c99_extern_local_init.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_flexible_array_init.c -o /tmp/cc_native_bad_c99_flexible_array_init.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_union_init.c -o /tmp/cc_native_bad_c99_union_init.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_array_assign.c -o /tmp/cc_native_bad_c99_array_assign.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_array_update.c -o /tmp/cc_native_bad_c99_array_update.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_bitfield_addr.c -o /tmp/cc_native_bad_c99_bitfield_addr.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_bitfield_sizeof.c -o /tmp/cc_native_bad_c99_bitfield_sizeof.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_bitfield_named_zero.c -o /tmp/cc_native_bad_c99_bitfield_named_zero.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_return_void_value.c -o /tmp/cc_native_bad_c99_return_void_value.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_return_missing_value.c -o /tmp/cc_native_bad_c99_return_missing_value.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_variadic_too_few.c -o /tmp/cc_native_bad_c99_variadic_too_few.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_bad_c99_pragma_stdc_bad.c -o /tmp/cc_native_bad_c99_pragma_stdc_bad.o
	! $(TOP)/usr.bin/cc/cc -std=c99 -c native_c90_oldstyle.c -o /tmp/cc_native_c90_oldstyle_c99_bad.o
	$(TOP)/usr.bin/cc/cc -c native_bad_void_ptr_arith.c -o /tmp/cc_native_bad_void_ptr_arith.o
	$(TOP)/usr.bin/cc/cc -c driver_hello.s -o /tmp/cc_driver_hello.o
	$(TOP)/usr.bin/cc/cc -nostdlib /tmp/cc_driver_hello.o -o /tmp/cc_driver_hello
	/tmp/cc_driver_hello | grep -q "cc-driver-asm-ok"

clean:
	rm -f /tmp/cc_valid.norm /tmp/cc_driver.pp /tmp/cc_driver_fail.o /tmp/cc_native_c99_phase12_trigraph_splice_c23.pp \
	      /tmp/cc_driver_bootstrap.o /tmp/cc_driver_hello.o /tmp/cc_driver_hello \
	      /tmp/cc_native_arith.s /tmp/cc_native_main /tmp/cc_native_bad.o \
	      /tmp/cc_native_phase1 /tmp/cc_native_call_stmt /tmp/cc_native_bad_assign.o \
	      /tmp/cc_native_bad_break.o /tmp/cc_native_bad_continue.o /tmp/cc_native_double /tmp/cc_native_void /tmp/cc_native_c99_empty_void /tmp/cc_native_variadic \
	      /tmp/cc_native_bad_case.o /tmp/cc_native_bad_default.o /tmp/cc_native_bad_switch_dup_case.o /tmp/cc_native_bad_switch_dup_default.o /tmp/cc_native_bad_for_scope.o \
	      /tmp/cc_native_abi_stack_int /tmp/cc_native_abi_stack_fp /tmp/cc_native_abi_mix \
	      /tmp/cc_native_if_else /tmp/cc_native_if_abs /tmp/cc_native_c95_digraph /tmp/cc_native_c95_trigraph /tmp/cc_native_c99_phase12_trigraph_splice /tmp/cc_native_c99_ucn_ident /tmp/cc_native_c99_tokenization_ppnum /tmp/cc_native_c99_line_comment /tmp/cc_native_do_while /tmp/cc_native_switch /tmp/cc_native_c99_switch_constexpr /tmp/cc_native_c99_for_decl /tmp/cc_native_c99_for_multi_decl /tmp/cc_native_c99_types /tmp/cc_native_c99_const_qual /tmp/cc_native_c99_composite_types /tmp/cc_native_c99_inline_semantics /tmp/cc_native_c99_inline_semantics.s /tmp/cc_native_c99_typedef /tmp/cc_native_c99_multi_decl /tmp/cc_native_c99_char_hex /tmp/cc_native_c99_char_cast_sign /tmp/cc_native_c99_signed_char_type /tmp/cc_native_c99_logic /tmp/cc_native_c99_inc_expr /tmp/cc_native_c99_update /tmp/cc_native_c99_bitwise /tmp/cc_native_c99_comma /tmp/cc_native_c99_goto /tmp/cc_native_c99_goto_loop /tmp/cc_native_c99_computed_goto /tmp/cc_native_c99_ternary /tmp/cc_native_c99_cast_sizeof /tmp/cc_native_c99_expr_conversions /tmp/cc_native_c99_sizeof_array /tmp/cc_native_c99_sizeof_unsigned /tmp/cc_native_c99_sizeof_local_array_cmp /tmp/cc_native_c99_float_cmp /tmp/cc_native_c99_long_double_type /tmp/cc_native_c99_unsigned /tmp/cc_native_c99_unsigned_literals /tmp/cc_native_c99_short /tmp/cc_native_c99_pointer /tmp/cc_native_c99_pointer_param /tmp/cc_native_c99_pointer_store /tmp/cc_native_c99_pointer_store_param /tmp/cc_native_c99_deref_compound /tmp/cc_native_c99_deref_prefix_update /tmp/cc_native_c99_deref_postfix_update /tmp/cc_native_c99_pointer_arith /tmp/cc_native_c99_index /tmp/cc_native_c99_pointer_diff /tmp/cc_native_c99_ptr_cast /tmp/cc_native_c99_ptr_size /tmp/cc_native_c99_ptrptr /tmp/cc_native_c99_ptrptrptr /tmp/cc_native_c99_ptr4 /tmp/cc_native_c99_pointer_update /tmp/cc_native_c99_octal_char /tmp/cc_native_c99_string_concat /tmp/cc_native_c99_func_name /tmp/cc_native_c99_atomic_fetch /tmp/cc_native_c99_builtin_strlen.s /tmp/cc_native_gnu_static_inline_extern.o /tmp/cc_native_c99_global_array_init /tmp/cc_native_c99_storage_tentative /tmp/cc_native_c99_designated_init /tmp/cc_native_c99_compound_literal /tmp/cc_native_c99_flexible_array /tmp/cc_native_c99_pragma /tmp/cc_native_c99_stdc_pragma /tmp/cc_native_c99_struct_assign /tmp/cc_native_c99_struct_ptr_arith /tmp/cc_native_c99_struct_member_array /tmp/cc_native_c99_anonymous_struct_union /tmp/cc_native_c99_bitfield_basic /tmp/cc_native_c99_void_pointer /tmp/cc_native_c99_pointer_cmp /tmp/cc_native_c99_decay /tmp/cc_native_c99_local_array_decay_addr /tmp/cc_native_c99_block_scope_shadow /tmp/cc_native_c99_mixed_decl_stmt /tmp/cc_native_c99_return_conversion /tmp/cc_native_c99_unproto_decl_call /tmp/cc_native_c99_unproto_extern_call.s /tmp/cc_native_c99_proto_call /tmp/cc_native_c99_proto_extern.s /tmp/cc_native_c99_empty_stmt2 /tmp/cc_native_loops /tmp/cc_native_if_assign /tmp/cc_native_variadic_call /tmp/cc_native_c99_variadic_fnptr /tmp/cc_native_variadic_call.s \
	      /tmp/cc_native_opt_fold /tmp/cc_native_opt_fold.s /tmp/cc_native_slot_pressure.s \
	      /tmp/cc_native_arith_32.s /tmp/cc_native_if_else_32.s /tmp/cc_native_c95_digraph_32.s /tmp/cc_native_c95_trigraph_32.s /tmp/cc_native_c99_phase12_trigraph_splice_32.s /tmp/cc_native_c99_ucn_ident_32.s /tmp/cc_native_c99_tokenization_ppnum_32.s /tmp/cc_native_c99_line_comment_32.s /tmp/cc_native_do_while_32.s /tmp/cc_native_c99_empty_stmt2_32.s /tmp/cc_native_switch_32.s /tmp/cc_native_c99_for_decl_32.s /tmp/cc_native_c99_for_multi_decl_32.s /tmp/cc_native_c99_types_32.s /tmp/cc_native_c99_const_qual_32.s /tmp/cc_native_c99_composite_types_32.s /tmp/cc_native_c99_inline_semantics_32.s /tmp/cc_native_c99_typedef_32.s /tmp/cc_native_c99_multi_decl_32.s /tmp/cc_native_c99_logic_32.s /tmp/cc_native_c99_update_32.s /tmp/cc_native_c99_bitwise_32.s /tmp/cc_native_c99_comma_32.s /tmp/cc_native_c99_goto_32.s /tmp/cc_native_c99_goto_loop_32.s /tmp/cc_native_c99_ternary_32.s /tmp/cc_native_c99_cast_sizeof_32.s /tmp/cc_native_c99_sizeof_array_32.s /tmp/cc_native_c99_sizeof_unsigned_32.s /tmp/cc_native_c99_float_cmp_32.s /tmp/cc_native_c99_unsigned_32.s /tmp/cc_native_c99_unsigned_literals_32.s /tmp/cc_native_c99_short_32.s /tmp/cc_native_c99_pointer_32.s /tmp/cc_native_c99_pointer_store_32.s /tmp/cc_native_c99_pointer_arith.s /tmp/cc_native_c99_pointer_arith_32.s /tmp/cc_native_c99_index_32.s /tmp/cc_native_c99_pointer_diff_32.s /tmp/cc_native_c99_ptr_cast_32.s /tmp/cc_native_c99_ptr_size_32.s /tmp/cc_native_c99_ptrptr_32.s /tmp/cc_native_c99_ptrptrptr_32.s /tmp/cc_native_c99_ptr4_32.s /tmp/cc_native_c99_pointer_update_32.s /tmp/cc_native_c99_global_array_init_32.s /tmp/cc_native_c99_storage_tentative_32.s /tmp/cc_native_c99_designated_init_32.s /tmp/cc_native_c99_compound_literal_32.s /tmp/cc_native_c99_flexible_array_32.s /tmp/cc_native_c99_struct_assign_32.s /tmp/cc_native_c99_struct_ptr_arith_32.s /tmp/cc_native_c99_struct_member_array_32.s /tmp/cc_native_c99_bitfield_basic_32.s /tmp/cc_native_c99_deref_postfix_update_32.s /tmp/cc_native_c99_proto_extern_32.s /tmp/cc_native_c99_block_scope_shadow_32.s /tmp/cc_native_c99_mixed_decl_stmt_32.s /tmp/cc_native_c99_return_conversion_32.s /tmp/cc_native_c99_stdc_pragma_32.s /tmp/cc_native_c99_unproto_decl_call_32.s /tmp/cc_native_c99_unproto_extern_call_32.s /tmp/cc_native_c90_unproto_call_promotions_32.s /tmp/cc_native_loops_32.s /tmp/cc_native_arith_32.o /tmp/cc_native_double_32.s /tmp/cc_native_double_32.o \
	      /tmp/cc_native_bad_inc.o /tmp/cc_native_bad_goto.o /tmp/cc_native_bad_label_dup.o /tmp/cc_native_bad_bitwise_float.o /tmp/cc_native_bad_sizeof_void.o /tmp/cc_native_bad_proto_conflict.o /tmp/cc_native_bad_signed_unsigned.o /tmp/cc_native_bad_short_long.o /tmp/cc_native_bad_typedef_init.o /tmp/cc_native_bad_deref_nonptr.o /tmp/cc_native_bad_addr_expr.o /tmp/cc_native_bad_deref_compound.o /tmp/cc_native_bad_deref_postfix_update.o /tmp/cc_native_bad_ptr_add_ptr.o /tmp/cc_native_bad_index_nonptr.o /tmp/cc_native_bad_ptr_add_float.o /tmp/cc_native_bad_ptr_sub_incompat.o /tmp/cc_native_bad_ptr_cmp_incompat.o /tmp/cc_native_bad_ptr_depth5.o /tmp/cc_native_bad_cast_ptr_float.o /tmp/cc_native_bad_computed_goto_float.o /tmp/cc_native_bad_void_ptr_arith.o /tmp/cc_native_bad_attr_noreturn_return.o /tmp/cc_native_bad_attr_noreturn_nonvoid.o \
	      /tmp/cc_native_c99_typedef_fnptr_retptr /tmp/cc_native_c99_attr_codegen /tmp/cc_native_c99_attr_codegen.s /tmp/cc_native_double_dbg.s \
	      /tmp/cc_native_gnu_typeof_alt /tmp/cc_native_gnu_builtins /tmp/cc_native_gnu_builtin_prefetch.o /tmp/cc_native_gnu_stmt_expr /tmp/cc_native_gnu_label_decl \
	      /tmp/cc_native_gnu_elvis /tmp/cc_native_gnu_case_range /tmp/cc_native_gnu_types_choose /tmp/cc_native_gnu_overflow \
	      /tmp/cc_native_gnu_stmt_expr.s /tmp/cc_native_gnu_stmt_expr_32.s /tmp/cc_native_gnu_label_decl.s /tmp/cc_native_gnu_label_decl_32.s \
	      /tmp/cc_native_gnu_elvis.s /tmp/cc_native_gnu_elvis_32.s /tmp/cc_native_gnu_case_range.s /tmp/cc_native_gnu_case_range_32.s \
	      /tmp/cc_native_gnu_types_choose.s /tmp/cc_native_gnu_overflow.s /tmp/cc_native_gnu_overflow_32.s \
	      /tmp/cc_native_gnu_object_size_chk /tmp/cc_native_gnu_object_size_chk.s /tmp/cc_native_gnu_object_size_chk_32.s \
	      /tmp/cc_native_gnu_trap_unreachable /tmp/cc_native_gnu_trap_unreachable.s /tmp/cc_native_gnu_trap_unreachable_32.s \
	      /tmp/cc_native_gnu_nested_func /tmp/cc_native_gnu_union_cast /tmp/cc_native_gnu_zerolen_array \
	      /tmp/cc_native_gnu_empty_struct /tmp/cc_native_gnu_static_nonconst_init /tmp/cc_native_gnu89_inline \
	      /tmp/cc_native_gnu89_inline.s /tmp/cc_native_gnu_attr_fn_codegen /tmp/cc_native_gnu_attr_fn_codegen.s \
	      /tmp/cc_native_gnu_attr_var_codegen /tmp/cc_native_gnu_attr_var_codegen.s \
	      /tmp/cc_native_gnu_attr_type_parse /tmp/cc_native_gnu_attr_alias /tmp/cc_native_gnu_attr_alias.s \
	      /tmp/cc_native_gnu_attr_label_enum_stmt /tmp/cc_native_clang_blocks_decl /tmp/cc_native_clang_blocks_decl_32.s \
	      /tmp/cc_native_clang_ext_vector_attr /tmp/cc_native_clang_builtin_aliases \
	      /tmp/cc_native_clang_attr_placement /tmp/cc_native_clang_asm_goto_quirks /tmp/cc_native_clang_asm_goto_quirks.s \
	      /tmp/cc_native_bad_gnu_nested_capture.o \
	      /tmp/cc_native_gnu_asm_basic /tmp/cc_native_gnu_asm_basic.s /tmp/cc_native_gnu_asm_ext \
	      /tmp/cc_native_gnu_asm_ext.s /tmp/cc_native_gnu_asm_ext_32.s \
	      /tmp/cc_native_gnu_asm_goto /tmp/cc_native_gnu_asm_goto.s /tmp/cc_native_gnu_asm_goto_32.s /tmp/cc_native_gnu_asm_goto_outputs \
	      /tmp/cc_native_gnu_asm_early_clobber /tmp/cc_native_gnu_asm_volatile_barrier.s \
	      /tmp/cc_native_gnu_asm_side_effect /tmp/cc_native_gnu_asm_side_effect.s \
	      /tmp/cc_native_gnu_void_ptr_arith /tmp/cc_native_gnu_void_ptr_arith_pedantic.o /tmp/cc_native_gnu_void_ptr_arith_pedantic.log \
	      /tmp/cc_native_bad_gnu_asm_constraint.o /tmp/cc_native_bad_gnu_asm_constraint.log \
	      /tmp/cc_native_bad_gnu_asm_match_idx.o /tmp/cc_native_bad_gnu_asm_match_idx.log \
	      /tmp/cc_native_bad_gnu_asm_template_mismatch.o /tmp/cc_native_bad_gnu_asm_template_mismatch.log \
	      /tmp/cc_native_bad_gnu_asm_early_clobber_tie.o /tmp/cc_native_bad_gnu_asm_early_clobber_tie.log \
	      /tmp/cc_native_bad_gnu_asm_early_clobber_fixed.o /tmp/cc_native_bad_gnu_asm_early_clobber_fixed.log \
	      /tmp/cc_native_bad_attr_inline_conflict.o /tmp/cc_native_bad_attr_hot_cold_conflict.o \
	      /tmp/cc_native_bad_attr_alias_init.o /tmp/cc_native_bad_attr_malloc_nonptr.o \
	      /tmp/cc_native_bad_attr_format_nonvariadic.o /tmp/cc_native_bad_attr_nonnull_empty.o \
	      /tmp/cc_native_bad_attr_nonnull_call.o
	rm -f /tmp/cc_native_c99_integer_literals /tmp/cc_native_c99_hex_float_suffix /tmp/cc_native_c99_wide_literals \
	      /tmp/cc_native_c99_complex_decl /tmp/cc_native_c99_imaginary_decl /tmp/cc_native_c99_complex_types /tmp/cc_native_c99_composite_types /tmp/cc_native_c99_param_array_qual /tmp/cc_native_c99_restrict_ptr /tmp/cc_native_c99_vla_decl /tmp/cc_native_c90_oldstyle /tmp/cc_native_c90_unproto_call_promotions /tmp/cc_native_c90_unproto_call_promotions.s \
	      /tmp/cc_native_c99_integer_literals_32.s /tmp/cc_native_c99_hex_float_suffix_32.s /tmp/cc_native_c99_wide_literals_32.s \
	      /tmp/cc_native_c99_complex_decl_32.s /tmp/cc_native_c99_imaginary_decl_32.s /tmp/cc_native_c99_composite_types_32.s /tmp/cc_native_c99_param_array_qual_32.s /tmp/cc_native_c99_restrict_ptr_32.s /tmp/cc_native_c99_vla_decl_32.s /tmp/cc_native_c90_oldstyle_32.s /tmp/cc_native_c90_unproto_call_promotions_32.s \
	      /tmp/cc_native_bad_c99_implicit_function.o /tmp/cc_native_bad_no_implicit_int.o /tmp/cc_native_bad_c99_const_assign.o /tmp/cc_native_bad_c99_const_param_assign.o /tmp/cc_native_bad_c99_restrict_nonptr.o /tmp/cc_native_bad_c99_file_scope_auto.o /tmp/cc_native_bad_c99_extern_local_init.o /tmp/cc_native_bad_c99_flexible_array_init.o /tmp/cc_native_bad_c99_union_init.o /tmp/cc_native_bad_c99_array_assign.o /tmp/cc_native_bad_c99_array_update.o /tmp/cc_native_bad_c99_bitfield_addr.o /tmp/cc_native_bad_c99_bitfield_sizeof.o /tmp/cc_native_bad_c99_bitfield_named_zero.o /tmp/cc_native_bad_c99_return_void_value.o /tmp/cc_native_bad_c99_return_missing_value.o /tmp/cc_native_bad_c99_variadic_too_few.o /tmp/cc_native_bad_c99_pragma_stdc_bad.o /tmp/cc_native_c90_oldstyle_c99_bad.o
	rm -f /tmp/cc_native_warn_assign_cond.o /tmp/cc_native_warn_assign_cond.log /tmp/cc_native_warn_assign_cond_werror.o \
	      /tmp/cc_native_c99_computed_goto_pedantic.o /tmp/cc_native_c99_computed_goto_pedantic.log /tmp/cc_native_c99_computed_goto_pedantic_errors.o \
	      /tmp/cc_native_bad_ptr_add_ptr_loc.o /tmp/cc_native_bad_ptr_add_ptr_loc.log /tmp/cc_native_bad_source_line.o /tmp/cc_native_bad_source_line.log \
	      /tmp/cc_conformance_pass.log /tmp/cc_conformance_xfail.log /tmp/cc_diff_host.log /tmp/cc_diff_ours.log
	rm -f /tmp/cc_native_c11_static_assert /tmp/cc_native_c11_generic /tmp/cc_native_c11_align_thread_noreturn \
	      /tmp/cc_native_c11_atomic /tmp/cc_native_c11_atomic_wrapper_type /tmp/cc_native_c11_atomic_fence /tmp/cc_native_c11_atomic_fence.s /tmp/cc_native_c11_atomic_fence_32.s /tmp/cc_native_c11_unicode_literals /tmp/cc_native_bad_c11_static_assert_fail.o \
	      /tmp/cc_native_bad_c11_thread_local_local.o /tmp/cc_native_c17_register /tmp/cc_native_c11_pp_version.i \
	      /tmp/cc_native_c17_pp_version.i \
	      /tmp/cc_native_c23_keywords /tmp/cc_native_c23_nullptr /tmp/cc_native_c23_typeof \
	      /tmp/cc_native_c23_bitint /tmp/cc_native_c23_bitint_size /tmp/cc_native_c23_binary_sep /tmp/cc_native_c23_empty_init \
	      /tmp/cc_native_c23_enum_underlying /tmp/cc_native_c23_label_decl /tmp/cc_native_c23_constexpr_auto_decimal /tmp/cc_native_c23_decimal_sizes \
	      /tmp/cc_native_c23_empty_param /tmp/cc_native_c23_asm_extension \
	      /tmp/cc_native_bad_c23_auto_no_init.o /tmp/cc_native_bad_c23_bitint_zero.o \
	      /tmp/cc_native_bad_c23_static_assert_single_fail.o /tmp/cc_native_bad_c23_empty_param_call.o /tmp/cc_native_c23_pp_version.i \
	      /tmp/cc_native_c23_attr_std /tmp/cc_native_c23_attr_std.log /tmp/cc_native_c23_attr_std_werror \
	      /tmp/cc_native_c23_attr_std_werror.log /tmp/cc_native_bad_c23_attr_fallthrough_outside.o \
	      /tmp/cc_native_c23_attr_has.i \
	      /tmp/cc_postc99_mode_c11.i /tmp/cc_postc99_mode_c17.i /tmp/cc_postc99_mode_c18.i /tmp/cc_postc99_mode_c23.i /tmp/cc_postc99_mode_c2x.i \
	      /tmp/cc_postc99_mode_c11 /tmp/cc_postc99_mode_c17 /tmp/cc_postc99_mode_c23 /tmp/cc_postc99_mode_c2x \
	      /tmp/cc_postc99_abi_c11 /tmp/cc_postc99_abi_c17 /tmp/cc_postc99_abi_c23 \
	      /tmp/cc_postc99_abi_c11_64.o /tmp/cc_postc99_abi_c17_64.o /tmp/cc_postc99_abi_c23_64.o \
	      /tmp/cc_postc99_abi_c11_32.o /tmp/cc_postc99_abi_c17_32.o /tmp/cc_postc99_abi_c23_32.o \
	      /tmp/cc_postc99_probe_bin /tmp/cc_postc99_probe_gcc_c11.c /tmp/cc_postc99_probe_gcc_c17.c \
	      /tmp/cc_postc99_probe_gcc_c23.c /tmp/cc_postc99_probe_clang_c11.c /tmp/cc_postc99_probe_clang_c17.c \
	      /tmp/cc_postc99_probe_clang_c23.c /tmp/cc_postc99_ours_gcc_c11 /tmp/cc_postc99_ours_gcc_c17 \
	      /tmp/cc_postc99_ours_gcc_c23 /tmp/cc_postc99_host_gcc_c11 /tmp/cc_postc99_host_gcc_c17 \
	      /tmp/cc_postc99_host_gcc_c23 /tmp/cc_postc99_ours_clang_c11 /tmp/cc_postc99_ours_clang_c17 \
	      /tmp/cc_postc99_ours_clang_c23 /tmp/cc_postc99_host_clang_c11 /tmp/cc_postc99_host_clang_c17 \
	      /tmp/cc_postc99_host_clang_c23
	rm -f /tmp/cc_postc99_abi_* /tmp/cc_postc99_ours_* /tmp/cc_postc99_host_*
	rm -f /tmp/cc_pp_s3.i /tmp/cc_pp_s3_p.i /tmp/cc_pp_s3_dM.i /tmp/cc_pp_s3_m.d /tmp/cc_pp_s3_mm.d /tmp/cc_pp_s3_md.d \
	      /tmp/cc_pp_s3_md.i /tmp/cc_pp_s3_v.i /tmp/cc_pp_s3_v.log /tmp/cc_pp_s3_depth.i /tmp/cc_pp_s3_macro.i /tmp/cc_pp_s3_macro_limit.i /tmp/cc_pp_s3_token_limit.i /tmp/cc_pp_s3_output_limit.i \
	      /tmp/cc_pp_s6_probe_gnu11.i /tmp/cc_pp_s6_probe_c99.i /tmp/cc_pp_s63_clang_pragma.i /tmp/cc_pp_s63_bad_clang_pragma.i /tmp/cc_pp_s63_bad_clang_section.i /tmp/cc_pp_s63_bad_clang_fp.i /tmp/cc_pp_s63_bad_clang_attribute.i /tmp/cc_pp_s63_bad_clang_loop.i \
	      /tmp/cc_pp_std_probe.c /tmp/cc_pp_strict_c11.i /tmp/cc_pp_strict_gnu11.i \
	      /tmp/cc_pp_s63_bad_clang_pragma.log /tmp/cc_pp_s63_bad_clang_section.log /tmp/cc_pp_s63_bad_clang_fp.log /tmp/cc_pp_s63_bad_clang_attribute.log /tmp/cc_pp_s63_bad_clang_loop.log \
	      /tmp/cc_pp_cpp_mode.i /tmp/cc_pp_cpp_mode_h.i /tmp/cc_pp_cpp_mode_input.h /tmp/cpp /tmp/cc_pp_fuzz_out.i /tmp/cc_pp_fuzz_seed.c /tmp/cc_pp_fuzz_seed.i \
	      /tmp/cc_pp_fuzz_0.c /tmp/cc_pp_fuzz_1.c /tmp/cc_pp_fuzz_2.c /tmp/cc_pp_fuzz_3.c /tmp/cc_pp_fuzz_4.c \
	      /tmp/cc_pp_fuzz_5.c /tmp/cc_pp_fuzz_6.c /tmp/cc_pp_fuzz_7.c /tmp/cc_pp_fuzz_8.c /tmp/cc_pp_fuzz_9.c \
	      /tmp/cc_pp_fuzz_10.c /tmp/cc_pp_fuzz_11.c /tmp/cc_pp_fuzz_12.c /tmp/cc_pp_fuzz_13.c /tmp/cc_pp_fuzz_14.c \
	      /tmp/cc_pp_fuzz_15.c /tmp/cc_pp_s4_elif_c23.i /tmp/cc_pp_s4_elif_c11.i /tmp/cc_pp_s4_warning_c23.i \
	      /tmp/cc_pp_s4_warning_c23.log /tmp/cc_pp_s4_warning_c11.i /tmp/cc_pp_s4_has_c23.i /tmp/cc_pp_s4_has_c11.i /tmp/cc_pp_s4_vaopt_c23.i \
	      /tmp/cc_pp_s4_vaopt_c11.i /tmp/cc_pp_s4_stdver_c99.i /tmp/cc_pp_s4_stdver_c11.i /tmp/cc_pp_s4_stdver_c17.i \
	      /tmp/cc_pp_s4_stdver_c23.i /tmp/cc_pp_s4_stdver_gnu11.i /tmp/cc_pp_s4_stdver_gnu17.i /tmp/cc_pp_s4_stdver_gnu23.i \
	      /tmp/cc_pp_s4_trigraph_c11.i /tmp/cc_pp_s4_trigraph_c23.i /tmp/cc_pp_s4_embed.bin /tmp/cc_pp_s4_embed.c /tmp/cc_pp_s4_embed.i /tmp/cc_pp_s4_embed_c11.i \
	      /tmp/cc_pp_s5_named_va.i /tmp/cc_pp_s5_named_va_c99.i /tmp/cc_pp_s5_dynmac.i /tmp/cc_pp_s5_include_next.i /tmp/cc_pp_s5_pragma.i \
	      /tmp/cc_pp_s4_host_gcc.i /tmp/cc_pp_s4_host_clang.i /tmp/cc_pp_s4_host_gcc.ab /tmp/cc_pp_s4_host_clang.ab \
	      /tmp/cc_pp_s4_ours.ab
	rm -f /tmp/cc_native_c99_simd_intrinsics /tmp/cc_native_c99_simd_intrinsics_32.o \
	      /tmp/cc_simd_macro_probe.c /tmp/cc_simd_macro_none.i /tmp/cc_simd_macro_avx.i /tmp/cc_simd_macro_avx2.i
	rm -f test_ast2ir_bin
