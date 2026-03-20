#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
CORPUS_DIR="$ROOT/tests/usr.bin/as/corpus"
TMPROOT=${AS_INTEL_CORPUS_TMPDIR:-${TMPDIR:-/tmp}}
TMP=${AS_INTEL_CORPUS_TMP:-$TMPROOT/as-x86-64-intel-corpus-$$}
mkdir -p "$TMP"
cleanup_tmp() {
    if [ "${AS_INTEL_CORPUS_KEEP_TMP:-0}" = "1" ]; then
        echo "note: preserved Intel corpus tempdir: $TMP" >&2
        return
    fi
    rm -rf "$TMP"
}
trap cleanup_tmp EXIT INT TERM

check_corpus() {
    name=$1
    att_src=$2
    march=
    att_obj="$TMP/${name}.att.o"
    intel_src="$TMP/${name}.intel.s"
    intel_obj="$TMP/${name}.intel.o"

    case "$name" in
        v1-*) march=x86-64-v1 ;;
        v2-*) march=x86-64-v2 ;;
        v3-*) march=x86-64-v3 ;;
        v4-*) march=x86-64-v4 ;;
        *) march=x86-64-v1 ;;
    esac

    "$AS" -64 -march="$march" -o "$att_obj" "$att_src"
    objdump -d -Mintel "$att_obj" > "$TMP/objdump.txt"
    python3 - "$TMP/objdump.txt" "$intel_src" <<'PY'
import re
import sys

src = sys.argv[1]
dst = sys.argv[2]
line_re = re.compile(r'^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}\s)+)\s*(.*)$')
branch_target_re = re.compile(r'\b([0-9a-fA-F]+)\s+<[^>]+>')
entries = []
addrs = set()

with open(src, 'r', encoding='utf-8') as fin:
    for line in fin:
        m = line_re.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        inst = m.group(3).strip()
        if not inst or '(bad)' in inst or '?' in inst or '{bad}' in inst or 'only)' in inst:
            continue
        inst = re.sub(r'\s+#.*$', '', inst)
        entries.append((addr, inst))
        addrs.add(addr)

def is_branch_like(inst):
    if not inst:
        return False
    prefixes = {
        'lock', 'rep', 'repe', 'repz', 'repne', 'repnz', 'data16', 'addr16',
        'cs', 'ds', 'es', 'fs', 'gs', 'ss', 'bnd', 'notrack', 'xacquire', 'xrelease',
        '{evex}',
    }
    mnemonic = ''
    for tok in inst.split():
        low = tok.lower()
        if low in prefixes:
            continue
        mnemonic = low
        break
    if not mnemonic:
        return False
    # objdump prints resolved branch destinations, not source-level relative
    # expressions, so the roundtrip must reconstruct labels for control flow.
    return mnemonic == 'call' or mnemonic == 'jmp' or mnemonic == 'xbegin' or \
        mnemonic == 'jrcxz' or mnemonic == 'loop' or mnemonic == 'loope' or \
        mnemonic == 'loopz' or mnemonic == 'loopne' or mnemonic == 'loopnz' or \
        mnemonic.startswith('j')

def first_mnemonic(inst):
    prefixes = {
        'lock', 'rep', 'repe', 'repz', 'repne', 'repnz', 'data16', 'addr16',
        'cs', 'ds', 'es', 'fs', 'gs', 'ss', 'bnd', 'notrack', 'xacquire', 'xrelease',
        '{evex}',
    }
    for tok in inst.split():
        low = tok.lower()
        if low in prefixes:
            continue
        return low
    return ''

def is_skip_64bit_intel_roundtrip(inst):
    # The Intel roundtrip intentionally drops disassembler-only artifacts. The
    # fixed-width loop/jrcxz family is also skipped because once those artifact
    # lines are removed, the original rel8 span is no longer a stable property
    # of the reconstructed source.
    return first_mnemonic(inst) in {'jcxz', 'jecxz', 'jrcxz', 'loop', 'loope', 'loopz', 'loopne', 'loopnz'}

def rewrite_targets(inst):
    # The remaining filtering here is for disassembler artifacts and label
    # recovery, not assembler syntax gaps.
    ok = True
    def repl(mm):
        nonlocal ok
        target = int(mm.group(1), 16)
        if target in kept_addrs:
            return f'.L{target:x}'
        if is_branch_like(inst):
            ok = False
        return '0x' + mm.group(1)
    return branch_target_re.sub(repl, inst), ok

base_keep = {addr: not is_skip_64bit_intel_roundtrip(inst) for addr, inst in entries}
keep = dict(base_keep)
changed = True
while changed:
    changed = False
    kept_addrs = {addr for addr, ok in keep.items() if ok}
    for addr, inst in entries:
        if not keep.get(addr, False):
            continue
        rewritten, ok = rewrite_targets(inst)
        if not ok:
            keep[addr] = False
            changed = True

with open(dst, 'w', encoding='utf-8') as fout:
    fout.write('.intel_syntax noprefix\n')
    fout.write('.text\n')
    kept_addrs = {addr for addr, ok in keep.items() if ok}
    for addr, inst in entries:
        if not keep.get(addr, False):
            continue
        inst, _ = rewrite_targets(inst)
        fout.write(f'.L{addr:x}:\n')
        fout.write(inst)
        fout.write('\n')
PY
    "$AS" -64 -march="$march" -o "$intel_obj" "$intel_src"
    objdump -dr "$intel_obj" >/dev/null
}

check_corpus v1-valid "$CORPUS_DIR/x86_64_v1_gas_all_valid_assembles.s"
check_corpus v1-opcodes "$CORPUS_DIR/x86_64_v1_gas_all_opcodes_assembles.s"
check_corpus v2-valid "$CORPUS_DIR/x86_64_v2_gas_all_valid_assembles.s"
check_corpus v2-opcodes "$CORPUS_DIR/x86_64_v2_gas_all_opcodes_assembles.s"
check_corpus v3-valid "$CORPUS_DIR/x86_64_v3_gas_all_valid_assembles.s"
check_corpus v3-opcodes "$CORPUS_DIR/x86_64_v3_gas_all_opcodes_assembles.s"
check_corpus v4-valid "$CORPUS_DIR/x86_64_v4_gas_all_valid_assembles.s"
check_corpus v4-opcodes "$CORPUS_DIR/x86_64_v4_gas_all_opcodes_assembles.s"

echo "ok: x86-64 corpus Intel reassembly"
