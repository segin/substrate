#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
CORPUS_DIR="$ROOT/tests/usr.bin/as/corpus"
TMP=${TMPDIR:-/tmp}/as-x86-32-intel-corpus-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

to_intel_roundtrip() {
    src=$1
    obj=$2
    intel=$3

    "$AS" -32 -o "$obj" "$src"
    objdump -d -Mintel "$obj" > "$TMP/objdump.txt"
    python3 - "$TMP/objdump.txt" "$intel" <<'PY'
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
    }
    tokens = inst.split()
    mnemonic = ''
    for tok in tokens:
        low = tok.lower()
        if low in prefixes:
            continue
        mnemonic = low
        break
    if not mnemonic:
        return False
    return mnemonic == 'call' or mnemonic == 'jmp' or mnemonic == 'xbegin' or \
        mnemonic == 'jecxz' or mnemonic == 'loop' or mnemonic == 'loope' or \
        mnemonic == 'loopz' or mnemonic == 'loopne' or mnemonic == 'loopnz' or \
        mnemonic.startswith('j')

def first_mnemonic(inst):
    prefixes = {
        'lock', 'rep', 'repe', 'repz', 'repne', 'repnz', 'data16', 'addr16',
        'cs', 'ds', 'es', 'fs', 'gs', 'ss', 'bnd', 'notrack', 'xacquire', 'xrelease',
    }
    for tok in inst.split():
        low = tok.lower()
        if low in prefixes:
            continue
        return low
    return ''

def is_skip_32bit_intel_roundtrip(inst):
    mnem = first_mnemonic(inst)
    if mnem in {'swapgs'}:
        return True
    if mnem == 'bswap':
        toks = inst.replace(',', ' ').split()
        regs16 = {'ax', 'bx', 'cx', 'dx', 'sp', 'bp', 'si', 'di'}
        for tok in toks[1:]:
            if tok.lower() in regs16:
                return True
    return False

def rewrite_targets(inst):
    ok = True
    def repl(mm):
        nonlocal ok
        target = int(mm.group(1), 16)
        if target in addrs:
            return f'.L{target:04x}'
        if is_branch_like(inst):
            ok = False
        return '0x' + mm.group(1)
    return branch_target_re.sub(repl, inst), ok

with open(dst, 'w', encoding='utf-8') as fout:
    fout.write('.intel_syntax noprefix\n')
    fout.write('.text\n')
    for addr, inst in entries:
        if is_skip_32bit_intel_roundtrip(inst):
            continue
        inst, ok = rewrite_targets(inst)
        if not ok:
            continue
        fout.write(f'.L{addr:04x}:\n')
        fout.write(inst)
        fout.write('\n')
PY
}

check_corpus() {
    name=$1
    att_src=$2
    att_obj="$TMP/${name}.att.o"
    intel_src="$TMP/${name}.intel.s"
    intel_obj="$TMP/${name}.intel.o"

    to_intel_roundtrip "$att_src" "$att_obj" "$intel_src"
    "$AS" -32 -o "$intel_obj" "$intel_src"
    objdump -dr "$intel_obj" >/dev/null
}

check_corpus valid "$CORPUS_DIR/x86_32_gas_all_valid_assembles.s"
check_corpus opcodes "$CORPUS_DIR/x86_32_gas_all_opcodes_assembles.s"

echo "ok: x86-32 corpus Intel reassembly"
