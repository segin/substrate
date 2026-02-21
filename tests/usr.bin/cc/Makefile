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
	$(TOP)/usr.bin/cc/cc native_c99_for_decl.c -o /tmp/cc_native_c99_for_decl
	/tmp/cc_native_c99_for_decl
	$(TOP)/usr.bin/cc/cc native_c99_types.c -o /tmp/cc_native_c99_types
	/tmp/cc_native_c99_types
	$(TOP)/usr.bin/cc/cc native_c99_logic.c -o /tmp/cc_native_c99_logic
	/tmp/cc_native_c99_logic
	$(TOP)/usr.bin/cc/cc native_c99_update.c -o /tmp/cc_native_c99_update
	/tmp/cc_native_c99_update
	$(TOP)/usr.bin/cc/cc native_loops.c -o /tmp/cc_native_loops
	/tmp/cc_native_loops
	$(TOP)/usr.bin/cc/cc native_bad_if_assign.c -o /tmp/cc_native_if_assign
	/tmp/cc_native_if_assign
	$(TOP)/usr.bin/cc/cc native_variadic_call.c -o /tmp/cc_native_variadic_call
	/tmp/cc_native_variadic_call
	$(TOP)/usr.bin/cc/cc -O2 native_opt_fold.c -o /tmp/cc_native_opt_fold
	/tmp/cc_native_opt_fold
	$(TOP)/usr.bin/cc/cc -O2 -S native_opt_fold.c -o /tmp/cc_native_opt_fold.s
	! grep -q "imulq" /tmp/cc_native_opt_fold.s
	$(TOP)/usr.bin/cc/cc -S native_slot_pressure.c -o /tmp/cc_native_slot_pressure.s
	grep -Fq 'subq $$32, %rsp' /tmp/cc_native_slot_pressure.s
	$(TOP)/usr.bin/cc/cc -S native_variadic_call.c -o /tmp/cc_native_variadic_call.s
	grep -Fq 'movb $$2, %al' /tmp/cc_native_variadic_call.s
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
	$(TOP)/usr.bin/cc/cc -m32 -S native_switch.c -o /tmp/cc_native_switch_32.s
	grep -q "sete" /tmp/cc_native_switch_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_for_decl.c -o /tmp/cc_native_c99_for_decl_32.s
	grep -q "jne" /tmp/cc_native_c99_for_decl_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_types.c -o /tmp/cc_native_c99_types_32.s
	grep -q "call add_ll" /tmp/cc_native_c99_types_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_logic.c -o /tmp/cc_native_c99_logic_32.s
	grep -q "jne" /tmp/cc_native_c99_logic_32.s
	$(TOP)/usr.bin/cc/cc -m32 -S native_c99_update.c -o /tmp/cc_native_c99_update_32.s
	grep -q "idivl" /tmp/cc_native_c99_update_32.s
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
	! $(TOP)/usr.bin/cc/cc -c native_bad_for_scope.c -o /tmp/cc_native_bad_for_scope.o
	! $(TOP)/usr.bin/cc/cc -c native_bad_inc.c -o /tmp/cc_native_bad_inc.o
	$(TOP)/usr.bin/cc/cc -c driver_hello.s -o /tmp/cc_driver_hello.o
	$(TOP)/usr.bin/cc/cc -nostdlib /tmp/cc_driver_hello.o -o /tmp/cc_driver_hello
	/tmp/cc_driver_hello | grep -q "cc-driver-asm-ok"

clean:
	rm -f /tmp/cc_valid.norm /tmp/cc_driver.pp /tmp/cc_driver_fail.o \
	      /tmp/cc_driver_bootstrap.o /tmp/cc_driver_hello.o /tmp/cc_driver_hello \
	      /tmp/cc_native_arith.s /tmp/cc_native_main /tmp/cc_native_bad.o \
	      /tmp/cc_native_phase1 /tmp/cc_native_call_stmt /tmp/cc_native_bad_assign.o \
	      /tmp/cc_native_bad_break.o /tmp/cc_native_bad_continue.o /tmp/cc_native_double /tmp/cc_native_void /tmp/cc_native_variadic \
	      /tmp/cc_native_bad_case.o /tmp/cc_native_bad_default.o /tmp/cc_native_bad_for_scope.o \
	      /tmp/cc_native_abi_stack_int /tmp/cc_native_abi_stack_fp /tmp/cc_native_abi_mix \
	      /tmp/cc_native_if_else /tmp/cc_native_if_abs /tmp/cc_native_c95_digraph /tmp/cc_native_c95_trigraph /tmp/cc_native_do_while /tmp/cc_native_switch /tmp/cc_native_c99_for_decl /tmp/cc_native_c99_types /tmp/cc_native_c99_logic /tmp/cc_native_c99_update /tmp/cc_native_loops /tmp/cc_native_if_assign /tmp/cc_native_variadic_call /tmp/cc_native_variadic_call.s \
	      /tmp/cc_native_opt_fold /tmp/cc_native_opt_fold.s /tmp/cc_native_slot_pressure.s \
	      /tmp/cc_native_arith_32.s /tmp/cc_native_if_else_32.s /tmp/cc_native_c95_digraph_32.s /tmp/cc_native_c95_trigraph_32.s /tmp/cc_native_do_while_32.s /tmp/cc_native_switch_32.s /tmp/cc_native_c99_for_decl_32.s /tmp/cc_native_c99_types_32.s /tmp/cc_native_c99_logic_32.s /tmp/cc_native_c99_update_32.s /tmp/cc_native_loops_32.s /tmp/cc_native_arith_32.o /tmp/cc_native_double_32.s /tmp/cc_native_double_32.o \
	      /tmp/cc_native_bad_inc.o \
	      /tmp/cc_native_double_dbg.s
