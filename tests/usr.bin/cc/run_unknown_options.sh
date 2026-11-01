#!/bin/sh
# An -f or -m option cc does not know is refused, as gcc refuses one it
# does not know: a build that probes for it (Linux's cc-option, a
# configure script) then finds it missing, where cc took every such
# option and acted on none -- Linux's retpolines, return thunks, stack
# canaries and ftrace calls went missing so.  Options gcc has whose
# effect cc has already are taken.  Each refused one is named; gcc
# refuses the made-up ones too; and every -f and -m option Linux passes
# to an x86-64 build unconditionally is taken, as are those of
# Substrate's own kernel.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
printf 'int f(int x) { return x + 1; }\n' >"$CC_TMP/b.c"

# refused, gcc's made-up ones and gcc's own that cc does not act on
for f in -fbogus-option -mbogus -fno-such-thing -fstack-check -fcommon -fzero-init-padding-bits=all \
	-fstrict-flex-arrays=3 -finline-stringops=memset -fzero-call-used-regs=used-gpr -fmacro-prefix-map=a=b; do
	if "$CC_BIN" -c -o "$CC_TMP/o.o" "$CC_TMP/b.c" "$f" 2>"$CC_TMP/c.err"; then
		echo "run_unknown_options: $f was taken"
		exit 1
	fi
	if ! grep -q -- "unrecognized command-line option '$f'" "$CC_TMP/c.err"; then
		echo "run_unknown_options: $f refused without naming it: $(head -1 "$CC_TMP/c.err")"
		exit 1
	fi
done
if command -v gcc >/dev/null 2>&1; then
	for f in -fbogus-option -mbogus -fno-such-thing; do
		if gcc -c -o "$CC_TMP/g.o" "$CC_TMP/b.c" "$f" 2>/dev/null; then
			echo "run_unknown_options: gcc took $f"
			exit 1
		fi
	done
fi
# taken: Linux's x86-64 build's own (Makefile, arch/x86/Makefile, without cc-option), and Substrate's kernel's
for f in -fno-strict-aliasing -fno-common -fms-extensions -fshort-wchar -funsigned-char -fno-PIE \
	-mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx -mno-sse4a -mno-80387 -mno-fp-ret-in-387 \
	-mskip-rax-setup -mno-red-zone -mcmodel=kernel -fno-delete-null-pointer-checks -fno-stack-check \
	-fconserve-stack -fno-builtin-wcslen -fno-builtin -ffreestanding -fno-zero-initialized-in-bss \
	-fvisibility=hidden -foptimize-sibling-calls -fno-function-sections -fno-data-sections -falign-jumps=1 \
	-falign-loops=1 -fdiagnostics-show-context=2 -fdiagnostics-show-inlining-chain -fno-allow-store-data-races \
	-fno-stack-clash-protection -fsched-pressure -fno-schedule-insns -mgeneral-regs-only -ffunction-sections \
	-fdata-sections -fno-omit-frame-pointer -fno-asynchronous-unwind-tables -fno-jump-tables -fno-stack-protector \
	-mindirect-branch=thunk-extern -mfunction-return=thunk-extern -mharden-sls=all -fpatchable-function-entry=16,16 \
	-fmin-function-alignment=16 -fstack-protector-strong -mstack-protector-guard-reg=gs \
	-mstack-protector-guard-symbol=__ref_stack_chk_guard -mrecord-mcount -mfentry -fcf-protection=branch \
	-mavx512f -msse4.2; do
	if ! "$CC_BIN" -c -o "$CC_TMP/o.o" "$CC_TMP/b.c" "$f" 2>"$CC_TMP/c.err"; then
		echo "run_unknown_options: $f refused: $(head -1 "$CC_TMP/c.err")"
		exit 1
	fi
done
echo "run_unknown_options: ok"
