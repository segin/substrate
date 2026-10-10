#!/usr/bin/env python3
"""generate_i8086_gas_corpus.py

Creates GNU as (gas) AT&T-syntax corpora for i8086 (.arch i8086, .code16):

  1) i8086_gas_all_valid_assembles.s
     - mnemonics that gas accepts in .code16 + .arch i8086,
       plus a curated 16-bit control-flow section (labels keep rel16 ranges valid).

  2) i8086_gas_all_opcodes_assembles.s
     - broad opcode sweep; if objdump's disassembly isn't accepted by gas, or would
       assemble with out-of-range rel16 / disp16 targets, it is replaced with a .byte
       fallback of the planted bytes.

Method:
  - Enumerate prefix byte {none, LOCK, REPNE, REP, ES/CS/SS/DS overrides} and opcode 0..255.
  - For each base (prefix+opcode), probe ModRM in:
      reg form: mod=3 rm=0 reg={0..7}
      mem form: mod=0 rm=6 (disp16) reg={0..7}
    Group opcodes get reg=1..7 in addition to reg=0.
  - Chunk pad with NOPs, disassemble with: objdump -b binary -m i8086 -D -M att
  - Use gas as oracle to filter assembleable disassembly lines.

Notes:
  - This is a testing corpus, not a normative ISA definition.
  - It is not a Cartesian product over every ModRM/disp/immediate combination.
"""

from dataclasses import dataclass
import io
import os
import re
import subprocess
from typing import Dict, List, Tuple

CHUNK = 16
NOP = 0x90

PREFIXES = [
    b"",
    b"\xF0",
    b"\xF2",
    b"\xF3",
    b"\x26",
    b"\x2E",
    b"\x36",
    b"\x3E",
]

@dataclass(frozen=True)
class Base:
    prefix: bytes
    opcode: int

line_re = re.compile(r'^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}\s)+)\s*(.*)$')
nonimm_big_hex_re = re.compile(r'(?<!\$)\b0x[0-9a-fA-F]{5,}\b')


def parse_objdump_first_per_chunk(text: str, chunk_size: int) -> Dict[int, Tuple[str, str]]:
    out: Dict[int, Tuple[str, str]] = {}
    for line in io.StringIO(text):
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


def is_problematic_16bit_constants(inst: str) -> bool:
    return bool(nonimm_big_hex_re.search(inst))


def mnem(inst: str):
    if not is_valid_inst(inst):
        return None
    return inst.split()[0]


def chunk_bytes(base: Base, variant: str, reg: int) -> bytes | None:
    if variant == "reg":
        modrm = 0xC0 | (reg << 3) | 0x00
    elif variant == "mem":
        modrm = (reg << 3) | 0x06
    else:
        raise ValueError(variant)

    seq = bytearray()
    seq += base.prefix
    seq.append(base.opcode)
    seq.append(modrm)

    if len(seq) > CHUNK:
        return None
    seq += bytes([NOP]) * (CHUNK - len(seq))
    return bytes(seq)


def intended_bytes(base: Base, variant: str, reg: int) -> bytes:
    if variant == "reg":
        modrm = 0xC0 | (reg << 3) | 0x00
    elif variant == "mem":
        modrm = (reg << 3) | 0x06
    else:
        raise ValueError(variant)
    return base.prefix + bytes([base.opcode, modrm])


def fmt_bytes_hex(bs: bytes) -> str:
    return " ".join(f"{b:02x}" for b in bs)


def fmt_bytes_byte_directive(bs: bytes) -> str:
    return ",".join(f"0x{b:02x}" for b in bs)


def assembleable_filter(inst_lines: List[str]) -> List[str]:
    test_path = "_tmp_validate.s"
    hdr = [".text", ".code16", ".arch i8086", ".globl _start", "_start:"]
    with open(test_path, "w") as f:
        f.write("\n".join(hdr) + "\n")
        for ln in inst_lines:
            f.write(ln + "\n")

    p = subprocess.run(["as", "--32", test_path, "-o", "_tmp_validate.o"],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if p.returncode == 0 and not p.stderr.strip():
        return inst_lines

    pat = re.compile(r'^' + re.escape(test_path) + r':(\d+):\s+(Error|Warning):', re.M)
    bad = set()
    for mm in pat.finditer(p.stderr):
        line_no = int(mm.group(1))
        idx = line_no - (len(hdr) + 1)
        if 0 <= idx < len(inst_lines):
            bad.add(idx)
    return [ln for i, ln in enumerate(inst_lines) if i not in bad]


def gen_bases() -> List[Base]:
    return [Base(pref, opc) for pref in PREFIXES for opc in range(256)]


def disassemble_chunks(bin_path: str) -> Dict[int, Tuple[str, str]]:
    p = subprocess.run(["objdump", "-b", "binary", "-m", "i8086", "-D", "-M", "att", bin_path],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    return parse_objdump_first_per_chunk(p.stdout, CHUNK)


def control_flow_section() -> List[str]:
    return [
        "",
        "# --- 16-bit control-flow (range-safe) ---",
        "    jmp 1f",
        "1:  nop",
        "    call 1f",
        "    jmp 2f",
        "1:  ret",
        "2:  nop",
        "    ret",
        "    lret",
        "    iret",
        "    ljmp $0x1234,$0x5678",
        "    lcall $0x1234,$0x5678",
        "    int $0x10",
        "    into",
        "    jo 1f",
        "1:  nop",
        "    jno 1f",
        "1:  nop",
        "    jb 1f",
        "1:  nop",
        "    jnb 1f",
        "1:  nop",
        "    je 1f",
        "1:  nop",
        "    jne 1f",
        "1:  nop",
        "    jbe 1f",
        "1:  nop",
        "    ja 1f",
        "1:  nop",
        "    js 1f",
        "1:  nop",
        "    jns 1f",
        "1:  nop",
        "    jp 1f",
        "1:  nop",
        "    jnp 1f",
        "1:  nop",
        "    jl 1f",
        "1:  nop",
        "    jge 1f",
        "1:  nop",
        "    jle 1f",
        "1:  nop",
        "    jg 1f",
        "1:  nop",
        "    jcxz 1f",
        "1:  nop",
        "    loop 1f",
        "1:  nop",
        "    loope 1f",
        "1:  nop",
        "    loopne 1f",
        "1:  nop",
        "# --- end control-flow ---",
        "",
    ]


def main() -> None:
    bases = gen_bases()
    here = os.path.dirname(os.path.abspath(__file__))

    stage1_variants = [("reg", 0), ("reg", 7), ("mem", 0), ("mem", 7)]
    chunks: List[bytes] = []
    for b in bases:
        for var, reg in stage1_variants:
            ch = chunk_bytes(b, var, reg)
            if ch is not None:
                chunks.append(ch)

    stage1_bin = os.path.join(here, "i8086_stage1.bin")
    with open(stage1_bin, "wb") as f:
        for ch in chunks:
            f.write(ch)

    m1 = disassemble_chunks(stage1_bin)

    group = []
    for i, _b in enumerate(bases):
        mn = set()
        for v in range(4):
            addr = (i * 4 + v) * CHUNK
            mm = mnem(m1[addr][1])
            if mm:
                mn.add(mm)
        group.append(len(mn) > 1)

    chunks2: List[bytes] = []
    meta: List[Tuple[int, str, int]] = []
    for i, b in enumerate(bases):
        for var, reg in (("reg", 0), ("mem", 0)):
            ch = chunk_bytes(b, var, reg)
            if ch is None:
                continue
            chunks2.append(ch)
            meta.append((i, var, reg))
        if group[i]:
            for rr in range(1, 8):
                ch = chunk_bytes(b, "reg", rr)
                if ch is None:
                    continue
                chunks2.append(ch)
                meta.append((i, "reg", rr))
            for rr in range(1, 8):
                ch = chunk_bytes(b, "mem", rr)
                if ch is None:
                    continue
                chunks2.append(ch)
                meta.append((i, "mem", rr))

    stage2_bin = os.path.join(here, "i8086_stage2.bin")
    with open(stage2_bin, "wb") as f:
        for ch in chunks2:
            f.write(ch)

    m2 = disassemble_chunks(stage2_bin)

    header = [
        ".text",
        ".code16",
        ".arch i8086",
        ".globl _start",
        "_start:",
        "# Auto-generated i8086 opcode/mnemonic corpus (AT&T syntax) for GNU as (gas).",
        "",
    ]

    valid_raw: List[str] = []
    for idx, (_bi, _var, _reg) in enumerate(meta):
        addr = idx * CHUNK
        inst = m2[addr][1]
        if is_valid_inst(inst) and not is_problematic_16bit_constants(inst):
            valid_raw.append("    " + inst)

    valid_ok = assembleable_filter(valid_raw)
    ok_set = set(valid_ok)

    valid_lines: List[str] = []
    all_lines: List[str] = []

    for idx, (bi, var, reg) in enumerate(meta):
        addr = idx * CHUNK
        bstr, inst = m2[addr]
        intended = intended_bytes(bases[bi], var, reg)
        gen_comment = fmt_bytes_hex(intended)

        if (is_valid_inst(inst)
            and ("    " + inst) in ok_set
            and not is_problematic_16bit_constants(inst)):
            line = f"    {inst:<35} # gen={gen_comment}  dis={bstr}"
            valid_lines.append(line)
            all_lines.append(line)
        else:
            bdir = fmt_bytes_byte_directive(intended)
            all_lines.append(f"    .byte {bdir:<25} # fallback; gen={gen_comment}")

    seen = set()
    valid_dedup: List[str] = []
    for ln in valid_lines:
        if ln in seen:
            continue
        seen.add(ln)
        valid_dedup.append(ln)

    out_valid = os.path.join(here, "i8086_gas_all_valid_assembles.s")
    out_all = os.path.join(here, "i8086_gas_all_opcodes_assembles.s")
    out_valid_obj = os.path.join(here, "i8086_valid.o")
    out_all_obj = os.path.join(here, "i8086_all.o")

    with open(out_valid, "w") as f:
        f.write("\n".join(header) + "\n")
        f.write("\n".join(control_flow_section()))
        f.write("\n".join(valid_dedup) + "\n")

    with open(out_all, "w") as f:
        f.write("\n".join(header) + "\n")
        f.write("\n".join(all_lines) + "\n")

    subprocess.run(["as", "--32", out_valid, "-o", out_valid_obj], check=True)
    subprocess.run(["as", "--32", out_all, "-o", out_all_obj], check=True)

    for path in (stage1_bin, stage2_bin, os.path.join(here, "_tmp_validate.s"), os.path.join(here, "_tmp_validate.o")):
        if os.path.exists(path):
            os.remove(path)

    print("Wrote i8086 corpora:")
    print("  i8086_gas_all_valid_assembles.s")
    print("  i8086_gas_all_opcodes_assembles.s")


if __name__ == "__main__":
    main()
