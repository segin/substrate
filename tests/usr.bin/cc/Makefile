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

clean:
	rm -f /tmp/cc_valid.norm
