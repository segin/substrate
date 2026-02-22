TOP ?= ../..

.PHONY: all clean test

all: test

test:
	$(MAKE) -C $(TOP)/usr.bin/cc NATIVE_BUILD=1
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
	$(TOP)/usr.bin/cc/cc native_do_while.c -o /tmp/cc_native_do_while
	/tmp/cc_native_do_while
	$(TOP)/usr.bin/cc/cc native_switch.c -o /tmp/cc_native_switch
	/tmp/cc_native_switch
	$(TOP)/usr.bin/cc/cc native_c99_switch_constexpr.c -o /tmp/cc_native_c99_switch_constexpr
	/tmp/cc_native_c99_switch_constexpr
	$(TOP)/usr.bin/cc/cc native_c99_for_decl.c -o /tmp/cc_native_c99_for_decl
	/tmp/cc_native_c99_for_decl
	$(TOP)/usr.bin/cc/cc native_c99_for_multi_decl.c -o /tmp/cc_native_c99_for_multi_decl
	/tmp/cc_native_c99_for_multi_decl
	$(TOP)/usr.bin/cc/cc native_c99_types.c -o /tmp/cc_native_c99_types
	/tmp/cc_native_c99_types
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
	grep -q "^\\.section \\.hotdata$$" /tmp/cc_native_c99_attr_codegen.s
	grep -q "^\\.section \\.text\\.panic$$" /tmp/cc_native_c99_attr_codegen.s
	grep -q "^\\.align 32$$" /tmp/cc_native_c99_attr_codegen.s
	grep -q "^\\.align 16$$" /tmp/cc_native_c99_attr_codegen.s
	grep -q "^\\.align 1$$" /tmp/cc_native_c99_attr_codegen.s
	$(TOP)/usr.bin/cc/cc native_c99_gnu_declspec_aliases.c -o /tmp/cc_native_c99_gnu_declspec_aliases
	/tmp/cc_native_c99_gnu_declspec_aliases
	$(TOP)/usr.bin/cc/cc native_c99_multi_decl.c -o /tmp/cc_native_c99_multi_decl
	/tmp/cc_native_c99_multi_decl
	$(TOP)/usr.bin/cc/cc native_c99_char_hex.c -o /tmp/cc_native_c99_char_hex
	/tmp/cc_native_c99_char_hex
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
	$(TOP)/usr.bin/cc/cc native_c99_float_cmp.c -o /tmp/cc_native_c99_float_cmp
	/tmp/cc_native_c99_float_cmp
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
	$(TOP)/usr.bin/cc/cc native_c99_global_array_init.c -o /tmp/cc_native_c99_global_array_init
	/tmp/cc_native_c99_global_array_init
	$(TOP)/usr.bin/cc/cc native_c99_global_init_list.c -o /tmp/cc_native_c99_global_init_list
	/tmp/cc_native_c99_global_init_list
	$(TOP)/usr.bin/cc/cc native_c99_local_init_list.c -o /tmp/cc_native_c99_local_init_list
	/tmp/cc_native_c99_local_init_list
	$(TOP)/usr.bin/cc/cc native_c99_pragma.c -o /tmp/cc_native_c99_pragma
	/tmp/cc_native_c99_pragma
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
	$(TOP)/usr.bin/cc/cc native_c99_void_pointer.c -o /tmp/cc_native_c99_void_pointer
	/tmp/cc_native_c99_void_pointer
	$(TOP)/usr.bin/cc/cc native_c99_pointer_cmp.c -o /tmp/cc_native_c99_pointer_cmp
	/tmp/cc_native_c99_pointer_cmp
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
	$(TOP)/usr.bin/cc/cc native_c99_va_args.c -o /tmp/cc_native_c99_va_args
	/tmp/cc_native_c99_va_args
	$(TOP)/usr.bin/cc/cc native_c99_extension_expr.c -o /tmp/cc_native_c99_extension_expr
	/tmp/cc_native_c99_extension_expr
	$(TOP)/usr.bin/cc/cc -O2 native_opt_fold.c -o /tmp/cc_native_opt_fold
	/tmp/cc_native_opt_fold
	$(TOP)/usr.bin/cc/cc -O2 -S native_opt_fold.c -o /tmp/cc_native_opt_fold.s
	! grep -q "imulq" /tmp/cc_native_opt_fold.s
	$(TOP)/usr.bin/cc/cc -S native_slot_pressure.c -o /tmp/cc_native_slot_pressure.s
	grep -Fq 'subq $$32, %rsp' /tmp/cc_native_slot_pressure.s
	$(TOP)/usr.bin/cc/cc -S native_variadic_call.c -o /tmp/cc_native_variadic_call.s
	grep -Fq 'movb $$2, %al' /tmp/cc_native_variadic_call.s
	$(TOP)/usr.bin/cc/cc -S native_c99_va_args.c -o /tmp/cc_native_c99_va_args.s
	grep -q "movq %r10" /tmp/cc_native_c99_va_args.s
	grep -q 'addq $$8, %rax' /tmp/cc_native_c99_va_args.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_arith.c -o /tmp/cc_native_arith_32.s
	grep -q "\\.code32" /tmp/cc_native_arith_32.s
	grep -q "movl" /tmp/cc_native_arith_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_if_else.c -o /tmp/cc_native_if_else_32.s
	grep -q "jne" /tmp/cc_native_if_else_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c95_digraph.c -o /tmp/cc_native_c95_digraph_32.s
	grep -q "\\.code32" /tmp/cc_native_c95_digraph_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c95_trigraph.c -o /tmp/cc_native_c95_trigraph_32.s
	grep -q "jne" /tmp/cc_native_c95_trigraph_32.s
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
	grep -q "cvttsd2sil" /tmp/cc_native_c99_cast_sizeof_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_float_cmp.c -o /tmp/cc_native_c99_float_cmp_32.s
	grep -q "ucomisd" /tmp/cc_native_c99_float_cmp_32.s
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
	grep -q "movl %edx, (%eax)" /tmp/cc_native_c99_pointer_store_32.s
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
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_assign.c -o /tmp/cc_native_c99_struct_assign_32.s
	grep -q "call memcpy" /tmp/cc_native_c99_struct_assign_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_ptr_arith.c -o /tmp/cc_native_c99_struct_ptr_arith_32.s
	grep -q "imull" /tmp/cc_native_c99_struct_ptr_arith_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_member_array.c -o /tmp/cc_native_c99_struct_member_array_32.s
	grep -q "movl %edx, (%eax)" /tmp/cc_native_c99_struct_member_array_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_array_init.c -o /tmp/cc_native_c99_struct_array_init_32.s
	grep -q "call calloc" /tmp/cc_native_c99_struct_array_init_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_struct_copy_init.c -o /tmp/cc_native_c99_struct_copy_init_32.s
	grep -q "call memcpy" /tmp/cc_native_c99_struct_copy_init_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_deref_postfix_update.c -o /tmp/cc_native_c99_deref_postfix_update_32.s
	grep -q "addl" /tmp/cc_native_c99_deref_postfix_update_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_proto_extern.c -o /tmp/cc_native_c99_proto_extern_32.s
	grep -q "call extmul" /tmp/cc_native_c99_proto_extern_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_va_args.c -o /tmp/cc_native_c99_va_args_32.s
	grep -q "leal 12(%ebp), %eax" /tmp/cc_native_c99_va_args_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_loops.c -o /tmp/cc_native_loops_32.s
	grep -q "jmp" /tmp/cc_native_loops_32.s
	$(TOP)/usr.bin/cc/cc -m32 -c native_arith.c -o /tmp/cc_native_arith_32.o
	file /tmp/cc_native_arith_32.o | grep -q "ELF 32-bit"
	$(TOP)/usr.bin/cc/cc -m32 -S native_double.c -o /tmp/cc_native_double_32.s
	grep -q "movsd" /tmp/cc_native_double_32.s
	grep -q "fldl" /tmp/cc_native_double_32.s
	$(TOP)/usr.bin/cc/cc -m32 -c native_double.c -o /tmp/cc_native_double_32.o
	file /tmp/cc_native_double_32.o | grep -q "ELF 32-bit"
	$(TOP)/usr.bin/cc/cc -g -S native_double.c -o /tmp/cc_native_double_dbg.s
	grep -q "\\.cfi_startproc" /tmp/cc_native_double_dbg.s
	grep -q "\\.file" /tmp/cc_native_double_dbg.s
	grep -q "addsd" /tmp/cc_native_double_dbg.s
	grep -q "mulsd" /tmp/cc_native_double_dbg.s
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
	! $(TOP)/usr.bin/cc/cc -c native_bad_ptr_depth5.c -o /tmp/cc_native_bad_ptr_depth5.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_cast_ptr_float.c -o /tmp/cc_native_bad_cast_ptr_float.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_computed_goto_float.c -o /tmp/cc_native_bad_computed_goto_float.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_attr_noreturn_return.c -o /tmp/cc_native_bad_attr_noreturn_return.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_attr_noreturn_nonvoid.c -o /tmp/cc_native_bad_attr_noreturn_nonvoid.o
	$(TOP)/usr.bin/cc/cc -c native_bad_void_ptr_arith.c -o /tmp/cc_native_bad_void_ptr_arith.o
	$(TOP)/usr.bin/cc/cc -c driver_hello.s -o /tmp/cc_driver_hello.o
	$(TOP)/usr.bin/cc/cc -nostdlib /tmp/cc_driver_hello.o -o /tmp/cc_driver_hello
	/tmp/cc_driver_hello | grep -q "cc-driver-asm-ok"

clean:
	rm -f /tmp/cc_valid.norm /tmp/cc_driver.pp /tmp/cc_driver_fail.o \
	      /tmp/cc_driver_bootstrap.o /tmp/cc_driver_hello.o /tmp/cc_driver_hello \
	      /tmp/cc_native_arith.s /tmp/cc_native_main /tmp/cc_native_bad.o \
	      /tmp/cc_native_phase1 /tmp/cc_native_call_stmt /tmp/cc_native_bad_assign.o \
	      /tmp/cc_native_bad_break.o /tmp/cc_native_bad_continue.o /tmp/cc_native_double /tmp/cc_native_void /tmp/cc_native_c99_empty_void /tmp/cc_native_variadic \
	      /tmp/cc_native_bad_case.o /tmp/cc_native_bad_default.o /tmp/cc_native_bad_switch_dup_case.o /tmp/cc_native_bad_switch_dup_default.o /tmp/cc_native_bad_for_scope.o \
	      /tmp/cc_native_abi_stack_int /tmp/cc_native_abi_stack_fp /tmp/cc_native_abi_mix \
	      /tmp/cc_native_if_else /tmp/cc_native_if_abs /tmp/cc_native_c95_digraph /tmp/cc_native_c95_trigraph /tmp/cc_native_do_while /tmp/cc_native_switch /tmp/cc_native_c99_switch_constexpr /tmp/cc_native_c99_for_decl /tmp/cc_native_c99_for_multi_decl /tmp/cc_native_c99_types /tmp/cc_native_c99_typedef /tmp/cc_native_c99_multi_decl /tmp/cc_native_c99_char_hex /tmp/cc_native_c99_logic /tmp/cc_native_c99_inc_expr /tmp/cc_native_c99_update /tmp/cc_native_c99_bitwise /tmp/cc_native_c99_comma /tmp/cc_native_c99_goto /tmp/cc_native_c99_goto_loop /tmp/cc_native_c99_computed_goto /tmp/cc_native_c99_ternary /tmp/cc_native_c99_cast_sizeof /tmp/cc_native_c99_float_cmp /tmp/cc_native_c99_unsigned /tmp/cc_native_c99_unsigned_literals /tmp/cc_native_c99_short /tmp/cc_native_c99_pointer /tmp/cc_native_c99_pointer_param /tmp/cc_native_c99_pointer_store /tmp/cc_native_c99_pointer_store_param /tmp/cc_native_c99_deref_compound /tmp/cc_native_c99_deref_prefix_update /tmp/cc_native_c99_deref_postfix_update /tmp/cc_native_c99_pointer_arith /tmp/cc_native_c99_index /tmp/cc_native_c99_pointer_diff /tmp/cc_native_c99_ptr_cast /tmp/cc_native_c99_ptr_size /tmp/cc_native_c99_ptrptr /tmp/cc_native_c99_ptrptrptr /tmp/cc_native_c99_ptr4 /tmp/cc_native_c99_pointer_update /tmp/cc_native_c99_octal_char /tmp/cc_native_c99_string_concat /tmp/cc_native_c99_func_name /tmp/cc_native_c99_atomic_fetch /tmp/cc_native_c99_global_array_init /tmp/cc_native_c99_pragma /tmp/cc_native_c99_struct_assign /tmp/cc_native_c99_struct_ptr_arith /tmp/cc_native_c99_struct_member_array /tmp/cc_native_c99_void_pointer /tmp/cc_native_c99_pointer_cmp /tmp/cc_native_c99_proto_call /tmp/cc_native_c99_proto_extern.s /tmp/cc_native_c99_empty_stmt2 /tmp/cc_native_loops /tmp/cc_native_if_assign /tmp/cc_native_variadic_call /tmp/cc_native_variadic_call.s \
	      /tmp/cc_native_opt_fold /tmp/cc_native_opt_fold.s /tmp/cc_native_slot_pressure.s \
	      /tmp/cc_native_arith_32.s /tmp/cc_native_if_else_32.s /tmp/cc_native_c95_digraph_32.s /tmp/cc_native_c95_trigraph_32.s /tmp/cc_native_do_while_32.s /tmp/cc_native_c99_empty_stmt2_32.s /tmp/cc_native_switch_32.s /tmp/cc_native_c99_for_decl_32.s /tmp/cc_native_c99_for_multi_decl_32.s /tmp/cc_native_c99_types_32.s /tmp/cc_native_c99_typedef_32.s /tmp/cc_native_c99_multi_decl_32.s /tmp/cc_native_c99_logic_32.s /tmp/cc_native_c99_update_32.s /tmp/cc_native_c99_bitwise_32.s /tmp/cc_native_c99_comma_32.s /tmp/cc_native_c99_goto_32.s /tmp/cc_native_c99_goto_loop_32.s /tmp/cc_native_c99_ternary_32.s /tmp/cc_native_c99_cast_sizeof_32.s /tmp/cc_native_c99_float_cmp_32.s /tmp/cc_native_c99_unsigned_32.s /tmp/cc_native_c99_unsigned_literals_32.s /tmp/cc_native_c99_short_32.s /tmp/cc_native_c99_pointer_32.s /tmp/cc_native_c99_pointer_store_32.s /tmp/cc_native_c99_pointer_arith.s /tmp/cc_native_c99_pointer_arith_32.s /tmp/cc_native_c99_index_32.s /tmp/cc_native_c99_pointer_diff_32.s /tmp/cc_native_c99_ptr_cast_32.s /tmp/cc_native_c99_ptr_size_32.s /tmp/cc_native_c99_ptrptr_32.s /tmp/cc_native_c99_ptrptrptr_32.s /tmp/cc_native_c99_ptr4_32.s /tmp/cc_native_c99_pointer_update_32.s /tmp/cc_native_c99_global_array_init_32.s /tmp/cc_native_c99_struct_assign_32.s /tmp/cc_native_c99_struct_ptr_arith_32.s /tmp/cc_native_c99_struct_member_array_32.s /tmp/cc_native_c99_deref_postfix_update_32.s /tmp/cc_native_c99_proto_extern_32.s /tmp/cc_native_loops_32.s /tmp/cc_native_arith_32.o /tmp/cc_native_double_32.s /tmp/cc_native_double_32.o \
	      /tmp/cc_native_bad_inc.o /tmp/cc_native_bad_goto.o /tmp/cc_native_bad_label_dup.o /tmp/cc_native_bad_bitwise_float.o /tmp/cc_native_bad_sizeof_void.o /tmp/cc_native_bad_proto_conflict.o /tmp/cc_native_bad_signed_unsigned.o /tmp/cc_native_bad_short_long.o /tmp/cc_native_bad_typedef_init.o /tmp/cc_native_bad_deref_nonptr.o /tmp/cc_native_bad_addr_expr.o /tmp/cc_native_bad_deref_compound.o /tmp/cc_native_bad_deref_postfix_update.o /tmp/cc_native_bad_ptr_add_ptr.o /tmp/cc_native_bad_index_nonptr.o /tmp/cc_native_bad_ptr_add_float.o /tmp/cc_native_bad_ptr_sub_incompat.o /tmp/cc_native_bad_ptr_cmp_incompat.o /tmp/cc_native_bad_ptr_depth5.o /tmp/cc_native_bad_cast_ptr_float.o /tmp/cc_native_bad_computed_goto_float.o /tmp/cc_native_bad_void_ptr_arith.o /tmp/cc_native_bad_attr_noreturn_return.o /tmp/cc_native_bad_attr_noreturn_nonvoid.o \
	      /tmp/cc_native_c99_typedef_fnptr_retptr /tmp/cc_native_c99_attr_codegen /tmp/cc_native_c99_attr_codegen.s /tmp/cc_native_double_dbg.s
