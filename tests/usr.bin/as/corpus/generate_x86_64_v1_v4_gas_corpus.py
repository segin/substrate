#!/usr/bin/env python3
"""generate_x86_64_v1_v4_gas_corpus.py

Creates GNU as (gas) AT&T-syntax corpora for AMD64/x86-64 micro-architecture levels:
  - x86-64-v1 (baseline)
  - x86-64-v2
  - x86-64-v3
  - x86-64-v4

For each level it writes two files:
  1) x86_64_<vN>_gas_all_valid_assembles.s
     - unique instruction lines that assemble under the level's feature set.

  2) x86_64_<vN>_gas_all_opcodes_assembles.s
     - one line per discovered encoding in the enumeration set that is accepted by gas
       under the level's feature set.

Method (pragmatic testing corpus, not a normative ISA definition):
  - Plant many candidate encodings into a fixed-size chunked binary.
  - Disassemble with objdump (AT&T syntax) to obtain canonical mnemonics.
  - Re-assemble candidate mnemonics under a constrained "-march=..." feature set.
  - Keep only the instructions that assemble cleanly under that feature set.

Enumeration:
  - legacy maps: 1-byte, 0F, 0F38, 0F3A with prefixes {none,66,F2,F3}
  - optional REX.W (0x48) included for legacy to expose 64-bit operand-size variants
  - VEX2/VEX3 included for v3/v4
  - EVEX included for v4

Requirements:
  - GNU binutils (as, objdump) in PATH.
"""

from __future__ import annotations

from dataclasses import dataclass
import io, os, re, subprocess

CHUNK = 16
NOP   = 0x90

@dataclass(frozen=True)
class Base:
    kind: str          # legacy/vex2/vex3/evex
    prefix: bytes      # legacy only: mandatory prefix + optional REX
    map_id: int
    enc_prefix: bytes  # legacy: map bytes; VEX/EVEX: prefix bytes
    opcode: int


def gen_bases(level: str):
    """Return a list of Base encodings to enumerate for the given level."""
    bases: list[Base] = []

    # Legacy encodings (always)
    legacy_prefixes = [b"", b"\x66", b"\xF2", b"\xF3"]
    rex_prefixes    = [b"", b"\x48"]  # include REX.W variants
    maps = {0: b"", 1: b"\x0F", 2: b"\x0F\x38", 3: b"\x0F\x3A"}

    for mand in legacy_prefixes:
        for rex in rex_prefixes:
            pref = mand + rex
            for map_id, mbytes in maps.items():
                for opc in range(256):
                    bases.append(Base("legacy", pref, map_id, mbytes, opc))

    if level in ("v3", "v4"):
        # VEX2: C5, pp(0..3), L(0..1), vvvv fixed to 1 (~vvvv = 0xE)
        for L in (0, 1):
            for pp in range(4):
                vex2 = 0x80 | (0xE << 3) | (L << 2) | pp
                for opc in range(256):
                    bases.append(Base("vex2", b"", 1, bytes([0xC5, vex2]), opc))

        # VEX3: C4 p0 p1, mmmm(1..3), W(0..1), L(0..1), pp(0..3), vvvv fixed to 1
        for mmmm in (1, 2, 3):
            p0 = 0xE0 | mmmm
            for W in (0, 1):
                for L in (0, 1):
                    for pp in range(4):
                        p1 = (W << 7) | (0xE << 3) | (L << 2) | pp
                        for opc in range(256):
                            bases.append(Base("vex3", b"", mmmm, bytes([0xC4, p0, p1]), opc))

    if level == "v4":
        # EVEX: 62 p0 p1 p2, mmmmm(1..3), W(0..1), pp(0..3), vvvv fixed to 1,
        #       vector length {128,256,512}, aaa=0, V'=1
        lenbits_list = [0x00, 0x20, 0x40]  # EVEX.L'L (approx)
        for mmmmm in (1, 2, 3):
            p0 = 0xF0 | mmmmm
            for W in (0, 1):
                for pp in range(4):
                    p1 = (W << 7) | (0xE << 3) | 0x04 | pp
                    for lenbits in lenbits_list:
                        p2 = 0x08 | lenbits  # V'=1, aaa=0
                        for opc in range(256):
                            bases.append(Base("evex", b"", mmmmm, bytes([0x62, p0, p1, p2]), opc))

    return bases


def chunk_bytes(base: Base, variant: str, reg: int) -> bytes | None:
    """Return a fixed-size CHUNK byte sequence encoding base + ModRM variant."""
    if variant == "reg":
        # mod=3, reg=reg, rm=0
        modrm = 0xC0 | (reg << 3) | 0x00
        tail = bytes([modrm])
    elif variant == "mem":
        # mod=0, reg=reg, rm=0 => memory at (%rax)
        modrm = (reg << 3) | 0x00
        tail = bytes([modrm])
    else:
        raise ValueError(variant)

    seq = bytearray()
    if base.kind == "legacy":
        seq += base.prefix
        seq += base.enc_prefix
        seq.append(base.opcode)
        seq += tail
    elif base.kind in ("vex2", "vex3", "evex"):
        seq += base.enc_prefix
        seq.append(base.opcode)
        seq += tail
    else:
        raise ValueError(base.kind)

    if len(seq) > CHUNK:
        return None
    seq += bytes([NOP]) * (CHUNK - len(seq))
    return bytes(seq)


line_re = re.compile(r'^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}\s)+)\s*(.*)$')


def parse_objdump_first_per_chunk(text: str, chunk_size: int):
    out = {}
    s = io.StringIO(text)
    for line in s:
        m = line_re.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        if addr % chunk_size != 0:
            continue
        out[addr] = (m.group(2).strip(), m.group(3).strip())
    return out


def is_valid_inst(inst: str) -> bool:
    t = inst.strip()
    if not t:
        return False
    if t.startswith("(bad)"):
        return False
    if t.startswith(".byte") or t.startswith(".word") or t.startswith(".long") or t.startswith(".quad"):
        return False
    return True


def assembleable_filter(inst_lines: list[str], as_args: list[str]):
    test_path = "_tmp_validate_x86_64.s"
    hdr = [".text", ".code64", ".globl _start", "_start:"]
    with open(test_path, "w") as f:
        f.write("\n".join(hdr) + "\n")
        for ln in inst_lines:
            f.write(ln + "\n")

    cmd = ["as", "--64", *as_args, test_path, "-o", "_tmp_validate_x86_64.o"]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if p.returncode == 0 and not p.stderr.strip():
        return inst_lines

    # remove any line referenced by Error/Warning
    pat = re.compile(r'^' + re.escape(test_path) + r':(\d+):\s+(Error|Warning):', re.M)
    bad = set()
    for m in pat.finditer(p.stderr):
        line_no = int(m.group(1))
        idx = line_no - (len(hdr) + 1)
        if 0 <= idx < len(inst_lines):
            bad.add(idx)

    kept = [ln for i, ln in enumerate(inst_lines) if i not in bad]
    return kept


def as_args_for_level(level: str) -> list[str]:
    # Feature sets per AMD64 psABI micro-architecture levels (Table 3.1).
    # We use GNU as extension switches to approximate the same constraints.
    if level == "v1":
        return ["-march=generic64"]

    if level == "v2":
        # +SSE3 +SSSE3 +SSE4.1 +SSE4.2 +POPCNT +CX16
        return ["-march=generic64+sse3+ssse3+sse4.1+sse4.2+popcnt+cx16"]

    if level == "v3":
        # v2 + AVX + AVX2 + BMI1/2 + F16C + FMA + LZCNT + MOVBE + XSAVE (for xgetbv/xsetbv)
        return ["-march=generic64+sse3+ssse3+sse4.1+sse4.2+popcnt+cx16+avx+avx2+bmi+bmi2+f16c+fma+lzcnt+movbe+xsave"]

    if level == "v4":
        # v3 + AVX512F/BW/CD/DQ/VL
        return ["-march=generic64+sse3+ssse3+sse4.1+sse4.2+popcnt+cx16+avx+avx2+bmi+bmi2+f16c+fma+lzcnt+movbe+xsave+avx512f+avx512bw+avx512cd+avx512dq+avx512vl"]

    raise ValueError(level)


def run_level(level: str):
    bases = gen_bases(level)
    as_args = as_args_for_level(level)

    # Stage 1: detect opcode-group bases (mnemonic changes across reg bits)
    stage1_variants = [("reg", 0), ("reg", 7), ("mem", 0), ("mem", 7)]
    chunks = []
    for b in bases:
        for var, reg in stage1_variants:
            ch = chunk_bytes(b, var, reg)
            if ch is None:
                continue
            chunks.append(ch)

    bin1 = f"x86_64_{level}_stage1.bin"
    with open(bin1, "wb") as f:
        for ch in chunks:
            f.write(ch)

    p = subprocess.run(
        ["objdump", "-b", "binary", "-m", "i386:x86-64", "-D", "-M", "att", bin1],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True,
    )
    m1 = parse_objdump_first_per_chunk(p.stdout, CHUNK)

    def mnem(inst: str):
        if not is_valid_inst(inst):
            return None
        return inst.split()[0]

    group = []
    for i, _b in enumerate(bases):
        mn = set()
        for v in range(4):
            addr = (i * 4 + v) * CHUNK
            mm = mnem(m1.get(addr, ("", ""))[1])
            if mm:
                mn.add(mm)
        group.append(len(mn) > 1)

    # Stage 2: emit reg0+mem0 always; if group, enumerate reg=1..7 for both
    chunks2 = []
    meta = []
    for i, b in enumerate(bases):
        for var, reg in (("reg", 0), ("mem", 0)):
            ch = chunk_bytes(b, var, reg)
            if ch is None:
                continue
            chunks2.append(ch)
            meta.append((i, var, reg))
        if group[i]:
            for reg in range(1, 8):
                ch = chunk_bytes(b, "reg", reg)
                if ch is None:
                    continue
                chunks2.append(ch)
                meta.append((i, "reg", reg))
            for reg in range(1, 8):
                ch = chunk_bytes(b, "mem", reg)
                if ch is None:
                    continue
                chunks2.append(ch)
                meta.append((i, "mem", reg))

    bin2 = f"x86_64_{level}_stage2.bin"
    with open(bin2, "wb") as f:
        for ch in chunks2:
            f.write(ch)

    p2 = subprocess.run(
        ["objdump", "-b", "binary", "-m", "i386:x86-64", "-D", "-M", "att", bin2],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True,
    )
    m2 = parse_objdump_first_per_chunk(p2.stdout, CHUNK)

    header = [
        ".text",
        ".code64",
        ".globl _start",
        "_start:",
        f"# Auto-generated x86-64 {level} corpus (AT&T syntax) for GNU as (gas).",
        f"# Assembler feature gate used: {' '.join(as_args)}",
        "",
    ]

    # Collect candidates and filter by assembleability under ISA gate
    candidates = []
    for idx, (_bi, _var, _reg) in enumerate(meta):
        addr = idx * CHUNK
        inst = m2.get(addr, ("", ""))[1]
        if is_valid_inst(inst):
            candidates.append("    " + inst)

    ok_lines = assembleable_filter(candidates, as_args)
    ok_set = set(ok_lines)

    valid_lines = []
    all_lines = []
    for idx, (_bi, _var, _reg) in enumerate(meta):
        addr = idx * CHUNK
        bstr, inst = m2.get(addr, ("", ""))
        if not is_valid_inst(inst):
            continue
        asm_line = "    " + inst
        if asm_line not in ok_set:
            continue

        # keep the line
        # include the disassembled instruction bytes as a comment (helps byte-level checking)
        line = f"    {inst:<35} # bytes={bstr}"
        all_lines.append(line)
        valid_lines.append(line)

    # Deduplicate valid lines
    seen = set()
    valid_dedup = []
    for ln in valid_lines:
        if ln in seen:
            continue
        seen.add(ln)
        valid_dedup.append(ln)

    out_valid = f"x86_64_{level}_gas_all_valid_assembles.s"
    out_all   = f"x86_64_{level}_gas_all_opcodes_assembles.s"

    with open(out_valid, "w") as f:
        f.write("\n".join(header) + "\n")
        f.write("\n".join(valid_dedup) + "\n")

    with open(out_all, "w") as f:
        f.write("\n".join(header) + "\n")
        f.write("\n".join(all_lines) + "\n")

    # Cleanup temp binaries
    for path in (bin1, bin2, "_tmp_validate_x86_64.s", "_tmp_validate_x86_64.o"):
        try:
            os.remove(path)
        except FileNotFoundError:
            pass

    print(f"Wrote {out_valid} ({len(valid_dedup)} unique lines)")
    print(f"Wrote {out_all}   ({len(all_lines)} encoding lines)")


def main():
    for lvl in ("v1", "v2", "v3", "v4"):
        run_level(lvl)


if __name__ == "__main__":
    main()
