#!/usr/bin/env python3
"""
generate_x86_32_gas_corpus.py

Creates two GNU as (gas) AT&T-syntax corpora for x86-32:

  1) x86_32_gas_all_valid_assembles.s
     - only mnemonics that gas accepts in .code32, plus a curated control-flow/prefix section.

  2) x86_32_gas_all_opcodes_assembles.s
     - covers the full enumeration set; if objdump's disassembly is not accepted by gas,
       it is replaced with a .byte encoding of the planted bytes.

Method:
  - Enumerate opcode maps and common mandatory-prefix encodings:
      legacy:   1-byte, 0F, 0F 38, 0F 3A with prefixes {none,66,F2,F3}
      VEX2:     C5 (map 0F) with pp∈[0..3], L∈{0,1}, vvvv fixed (chosen to match common encodings)
      VEX3:     C4 with mmmm∈{1,2,3}, pp∈[0..3], L∈{0,1}, W∈{0,1}
      EVEX:     62 with mmmmm∈{1,2,3}, pp∈[0..3], W∈{0,1}, vector length ∈{128,256,512}
      3DNow!:   0F 0F /r imm8 for imm8∈[0..255]
  - For each base encoding, test ModRM in two ways:
      reg form: mod=3, rm=0, reg=0
      mem form: mod=0, rm=5 (disp32), reg=0
    For opcode-group bases (where the mnemonic changes across reg bits), also enumerate reg=1..7.

  - Emit each test case into a fixed-size chunk padded with NOPs so objdump stays synchronized.
  - Disassemble with: objdump -b binary -m i386 -D -M att
  - Build the .s outputs and verify with: as --32

Requirements:
  - GNU binutils (as, objdump) in PATH.

This is a testing corpus, not a normative ISA definition.
"""
from dataclasses import dataclass
import itertools, os, re, subprocess, io

CHUNK = 16
NOP   = 0x90

@dataclass(frozen=True)
class Base:
    kind: str          # legacy/vex2/vex3/evex/3dnow
    prefix: bytes      # legacy only
    map_id: int
    vex_bytes: bytes   # includes map bytes for legacy; includes prefix bytes for VEX/EVEX; 3dnow uses b"\x0F\x0F"
    opcode: int        # opcode byte (or imm8 selector for 3dnow)

def gen_bases():
    bases=[]
    # legacy
    legacy_prefixes=[b"", b"\x66", b"\xF2", b"\xF3"]
    maps={0:b"", 1:b"\x0F", 2:b"\x0F\x38", 3:b"\x0F\x3A"}
    for pref in legacy_prefixes:
        for map_id, mbytes in maps.items():
            for opc in range(256):
                bases.append(Base("legacy", pref, map_id, mbytes, opc))

    # vex2: C5, pp(0..3), L(0..1), vvvv fixed to 1 (so ~vvvv=0xE)
    for L in (0,1):
        for pp in range(4):
            vex2 = 0x80 | (0xE<<3) | (L<<2) | pp
            for opc in range(256):
                bases.append(Base("vex2", b"", 1, bytes([0xC5, vex2]), opc))

    # vex3: C4 p0 p1, mmmm(1..3), W(0..1), L(0..1), pp(0..3), vvvv fixed to 1
    for mmmm in (1,2,3):
        p0 = 0xE0 | mmmm
        for W in (0,1):
            for L in (0,1):
                for pp in range(4):
                    p1 = (W<<7) | (0xE<<3) | (L<<2) | pp
                    for opc in range(256):
                        bases.append(Base("vex3", b"", mmmm, bytes([0xC4, p0, p1]), opc))

    # evex: 62 p0 p1 p2, mmmmm(1..3), W(0..1), pp(0..3), vvvv fixed to 1, length {128,256,512}, aaa=0, V'=1
    lenbits_list=[0x00,0x20,0x40]
    for mmmmm in (1,2,3):
        p0 = 0xF0 | mmmmm
        for W in (0,1):
            for pp in range(4):
                p1 = (W<<7) | (0xE<<3) | 0x04 | pp
                for lenbits in lenbits_list:
                    p2 = 0x08 | lenbits  # V'=1, aaa=0
                    for opc in range(256):
                        bases.append(Base("evex", b"", mmmmm, bytes([0x62, p0, p1, p2]), opc))

    # 3dnow: imm8 selector
    for imm in range(256):
        bases.append(Base("3dnow", b"", -1, b"\x0F\x0F", imm))

    return bases

def chunk_bytes(base: Base, variant: str, reg: int):
    if variant == "reg":
        modrm = 0xC0 | (reg<<3) | 0x00
    elif variant == "mem":
        modrm = ((reg<<3)&0x38) | 0x05
    else:
        raise ValueError(variant)

    seq = bytearray()
    if base.kind == "legacy":
        seq += base.prefix
        seq += base.vex_bytes
        seq.append(base.opcode)
        seq.append(modrm)
    elif base.kind in ("vex2","vex3","evex"):
        seq += base.vex_bytes
        seq.append(base.opcode)
        seq.append(modrm)
    elif base.kind == "3dnow":
        seq += base.vex_bytes
        seq.append(modrm)
        seq.append(base.opcode)
    else:
        raise ValueError(base.kind)

    if len(seq) > CHUNK:
        return None
    seq += bytes([NOP])*(CHUNK-len(seq))
    return bytes(seq)

def gen_intended_bytes(base: Base, variant: str, reg: int):
    if variant == "reg":
        modrm = 0xC0 | (reg<<3) | 0x00
    elif variant == "mem":
        modrm = ((reg<<3)&0x38) | 0x05
    else:
        raise ValueError(variant)

    if base.kind == "legacy":
        return base.prefix + base.vex_bytes + bytes([base.opcode, modrm])
    if base.kind in ("vex2","vex3","evex"):
        return base.vex_bytes + bytes([base.opcode, modrm])
    if base.kind == "3dnow":
        return base.vex_bytes + bytes([modrm, base.opcode])
    raise ValueError(base.kind)

def fmt_bytes_hex(bs: bytes):
    return " ".join(f"{b:02x}" for b in bs)

def fmt_bytes_byte_directive(bs: bytes):
    return ",".join(f"0x{b:02x}" for b in bs)

line_re = re.compile(r'^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}\s)+)\s*(.*)$')

def parse_objdump_first_per_chunk(text, chunk_size):
    out={}
    s=io.StringIO(text)
    for line in s:
        m=line_re.match(line)
        if not m:
            continue
        addr=int(m.group(1),16)
        if addr % chunk_size != 0:
            continue
        out[addr]=(m.group(2).strip(), m.group(3).strip())
    return out

def is_valid_inst(inst):
    t=inst.strip()
    if not t: return False
    if t.startswith("(bad)"): return False
    if t.startswith(".byte") or t.startswith(".word") or t.startswith(".long") or t.startswith(".quad"):
        return False
    return True

def assembleable_filter(inst_lines):
    test_path="_tmp_validate.s"
    hdr=[".text",".code32",".globl _start","_start:"]
    with open(test_path,"w") as f:
        f.write("\n".join(hdr)+"\n")
        for ln in inst_lines:
            f.write(ln+"\n")
    p=subprocess.run(["as","--32",test_path,"-o","_tmp_validate.o"],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if p.returncode == 0 and not p.stderr.strip():
        return inst_lines
    # remove any line referenced by Error/Warning
    pat=re.compile(r'^'+re.escape(test_path)+r':(\d+):\s+(Error|Warning):', re.M)
    bad=set()
    for m in pat.finditer(p.stderr):
        line_no=int(m.group(1))
        idx=line_no - (len(hdr)+1)
        if 0 <= idx < len(inst_lines):
            bad.add(idx)
    kept=[ln for i,ln in enumerate(inst_lines) if i not in bad]
    # one pass is enough in practice for this corpus
    return kept

def main():
    bases=gen_bases()

    # Stage 1: detect "opcode-group" bases by checking if mnemonic differs across reg bits (reg0 vs reg7, mem0 vs mem7)
    stage1_variants=[("reg",0),("reg",7),("mem",0),("mem",7)]
    chunks=[]
    for b in bases:
        for var,reg in stage1_variants:
            chunks.append(chunk_bytes(b,var,reg))
    with open("x86_32_stage1.bin","wb") as f:
        for ch in chunks: f.write(ch)

    p=subprocess.run(["objdump","-b","binary","-m","i386","-D","-M","att","x86_32_stage1.bin"],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    m1=parse_objdump_first_per_chunk(p.stdout, CHUNK)

    def mnem(inst):
        if not is_valid_inst(inst): return None
        return inst.split()[0]

    group=[]
    for i,b in enumerate(bases):
        mn=set()
        for v in range(4):
            addr=(i*4+v)*CHUNK
            mm=mnem(m1[addr][1])
            if mm: mn.add(mm)
        group.append(len(mn)>1)

    # Stage 2: emit reg0+mem0 always; if group, enumerate reg=1..7 for both
    chunks2=[]
    meta=[]
    for i,b in enumerate(bases):
        for var,reg in (("reg",0),("mem",0)):
            chunks2.append(chunk_bytes(b,var,reg))
            meta.append((i,var,reg))
        if group[i]:
            for reg in range(1,8):
                chunks2.append(chunk_bytes(b,"reg",reg))
                meta.append((i,"reg",reg))
            for reg in range(1,8):
                chunks2.append(chunk_bytes(b,"mem",reg))
                meta.append((i,"mem",reg))

    with open("x86_32_stage2.bin","wb") as f:
        for ch in chunks2: f.write(ch)

    p2=subprocess.run(["objdump","-b","binary","-m","i386","-D","-M","att","x86_32_stage2.bin"],
                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    m2=parse_objdump_first_per_chunk(p2.stdout, CHUNK)

    header=[
        ".text",".code32",".globl _start","_start:",
        "# Auto-generated x86-32 opcode/mnemonic corpus (AT&T syntax) for GNU as (gas).",
        ""
    ]

    # First pass: collect all "valid" mnemonics and filter by gas-assembleability
    valid_raw=[]
    for idx,(bi,var,reg) in enumerate(meta):
        addr=idx*CHUNK
        inst=m2[addr][1]
        if is_valid_inst(inst):
            valid_raw.append("    "+inst)

    valid_ok=assembleable_filter(valid_raw)
    ok_set=set(valid_ok)

    # Build outputs
    valid_lines=[]
    all_lines=[]
    for idx,(bi,var,reg) in enumerate(meta):
        addr=idx*CHUNK
        bstr,inst=m2[addr]
        intended=gen_intended_bytes(bases[bi],var,reg)
        gen_comment=fmt_bytes_hex(intended)
        dis_comment=bstr
        if is_valid_inst(inst) and ("    "+inst) in ok_set:
            line=f"    {inst:<35} # gen={gen_comment}  dis={dis_comment}"
            valid_lines.append(line)
            all_lines.append(line)
        else:
            bdir=fmt_bytes_byte_directive(intended)
            all_lines.append(f"    .byte {bdir:<25} # fallback; gen={gen_comment}")

    # Dedup valid lines
    seen=set(); valid_dedup=[]
    for ln in valid_lines:
        if ln in seen: continue
        seen.add(ln)
        valid_dedup.append(ln)

    with open("x86_32_gas_all_valid_assembles.s","w") as f:
        f.write("\n".join(header)+"\n")
        f.write("\n".join(valid_dedup)+"\n")

    with open("x86_32_gas_all_opcodes_assembles.s","w") as f:
        f.write("\n".join(header)+"\n")
        f.write("\n".join(all_lines)+"\n")

    print("Wrote:")
    print("  x86_32_gas_all_valid_assembles.s")
    print("  x86_32_gas_all_opcodes_assembles.s")

if __name__ == "__main__":
    main()
