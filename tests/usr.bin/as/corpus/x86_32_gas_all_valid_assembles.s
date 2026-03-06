.text
.code32
.globl _start
_start:
#
# Auto-generated x86-32 opcode/mnemonic corpus (AT&T syntax) for GNU as (gas).
# Generation method: enumerate opcode maps and common mandatory prefixes, disassemble with objdump,
# then emit either the decoded mnemonic or (for invalid encodings) a .byte fallback.
#
# Notes:
# - This is a corpus for assembler development/testing, not a normative ISA specification.
# - PC-relative instructions will assemble deterministically, but bytes may differ from the disassembler sample.
# - The 'gen=' comment is the byte sequence explicitly planted at the start of each chunk (before NOP padding).
#
    add    %al,%al                      # gen=00 c0  dis=00 c0
    add    %al,0x90909090               # gen=00 05  dis=00 05 90 90 90 90
    add    %eax,%eax                    # gen=01 c0  dis=01 c0
    add    %eax,0x90909090              # gen=01 05  dis=01 05 90 90 90 90
    add    0x90909090,%al               # gen=02 05  dis=02 05 90 90 90 90
    add    0x90909090,%eax              # gen=03 05  dis=03 05 90 90 90 90
    add    $0xc0,%al                    # gen=04 c0  dis=04 c0
    add    $0x5,%al                     # gen=04 05  dis=04 05
    add    $0x909090c0,%eax             # gen=05 c0  dis=05 c0 90 90 90
    add    $0x90909005,%eax             # gen=05 05  dis=05 05 90 90 90
    push   %es                          # gen=06 c0  dis=06
    pop    %es                          # gen=07 c0  dis=07
    or     %al,%al                      # gen=08 c0  dis=08 c0
    or     %al,0x90909090               # gen=08 05  dis=08 05 90 90 90 90
    or     %eax,%eax                    # gen=09 c0  dis=09 c0
    or     %eax,0x90909090              # gen=09 05  dis=09 05 90 90 90 90
    or     0x90909090,%al               # gen=0a 05  dis=0a 05 90 90 90 90
    or     0x90909090,%eax              # gen=0b 05  dis=0b 05 90 90 90 90
    or     $0xc0,%al                    # gen=0c c0  dis=0c c0
    or     $0x5,%al                     # gen=0c 05  dis=0c 05
    or     $0x909090c0,%eax             # gen=0d c0  dis=0d c0 90 90 90
    or     $0x90909005,%eax             # gen=0d 05  dis=0d 05 90 90 90
    push   %cs                          # gen=0e c0  dis=0e
    xadd   %dl,-0x6f6f6f70(%eax)        # gen=0f c0  dis=0f c0 90 90 90 90 90
    syscall                             # gen=0f 05  dis=0f 05
    bswap  %eax                         # gen=0f c8  dis=0f c8
    psubusb -0x6f6f6f70(%eax),%mm2      # gen=0f d8  dis=0f d8 90 90 90 90 90
    pavgb  -0x6f6f6f70(%eax),%mm2       # gen=0f e0  dis=0f e0 90 90 90 90 90
    psubsb -0x6f6f6f70(%eax),%mm2       # gen=0f e8  dis=0f e8 90 90 90 90 90
    psubb  -0x6f6f6f70(%eax),%mm2       # gen=0f f8  dis=0f f8 90 90 90 90 90
    prefetchwt1 -0x6f6f6f70(%eax)       # gen=0f 0d  dis=0f 0d 90 90 90 90 90
    unpckhps -0x6f6f6f70(%eax),%xmm2    # gen=0f 15  dis=0f 15 90 90 90 90 90
    nopl   -0x6f6f6f70(%eax)            # gen=0f 1d  dis=0f 1d 90 90 90 90 90
    cvtps2pi -0x6f6f6f70(%eax),%mm2     # gen=0f 2d  dis=0f 2d 90 90 90 90 90
    sysexit                             # gen=0f 35  dis=0f 35
    adc    %al,%al                      # gen=10 c0  dis=10 c0
    adc    %al,0x90909090               # gen=10 05  dis=10 05 90 90 90 90
    adc    %eax,%eax                    # gen=11 c0  dis=11 c0
    adc    %eax,0x90909090              # gen=11 05  dis=11 05 90 90 90 90
    adc    0x90909090,%al               # gen=12 05  dis=12 05 90 90 90 90
    adc    0x90909090,%eax              # gen=13 05  dis=13 05 90 90 90 90
    adc    $0xc0,%al                    # gen=14 c0  dis=14 c0
    adc    $0x5,%al                     # gen=14 05  dis=14 05
    adc    $0x909090c0,%eax             # gen=15 c0  dis=15 c0 90 90 90
    adc    $0x90909005,%eax             # gen=15 05  dis=15 05 90 90 90
    push   %ss                          # gen=16 c0  dis=16
    pop    %ss                          # gen=17 c0  dis=17
    sbb    %al,%al                      # gen=18 c0  dis=18 c0
    sbb    %al,0x90909090               # gen=18 05  dis=18 05 90 90 90 90
    sbb    %eax,%eax                    # gen=19 c0  dis=19 c0
    sbb    %eax,0x90909090              # gen=19 05  dis=19 05 90 90 90 90
    sbb    0x90909090,%al               # gen=1a 05  dis=1a 05 90 90 90 90
    sbb    0x90909090,%eax              # gen=1b 05  dis=1b 05 90 90 90 90
    sbb    $0xc0,%al                    # gen=1c c0  dis=1c c0
    sbb    $0x5,%al                     # gen=1c 05  dis=1c 05
    sbb    $0x909090c0,%eax             # gen=1d c0  dis=1d c0 90 90 90
    sbb    $0x90909005,%eax             # gen=1d 05  dis=1d 05 90 90 90
    push   %ds                          # gen=1e c0  dis=1e
    pop    %ds                          # gen=1f c0  dis=1f
    and    %al,%al                      # gen=20 c0  dis=20 c0
    and    %al,0x90909090               # gen=20 05  dis=20 05 90 90 90 90
    and    %eax,%eax                    # gen=21 c0  dis=21 c0
    and    %eax,0x90909090              # gen=21 05  dis=21 05 90 90 90 90
    and    0x90909090,%al               # gen=22 05  dis=22 05 90 90 90 90
    and    0x90909090,%eax              # gen=23 05  dis=23 05 90 90 90 90
    and    $0xc0,%al                    # gen=24 c0  dis=24 c0
    and    $0x5,%al                     # gen=24 05  dis=24 05
    and    $0x909090c0,%eax             # gen=25 c0  dis=25 c0 90 90 90
    and    $0x90909005,%eax             # gen=25 05  dis=25 05 90 90 90
    rclb   $0x90,%es:-0x6f6f6f70(%eax)  # gen=26 c0  dis=26 c0 90 90 90 90 90
    es add $0x90909090,%eax             # gen=26 05  dis=26 05 90 90 90 90
    es enter $0x9090,$0x90              # gen=26 c8  dis=26 c8 90 90 90
    rclb   %es:-0x6f6f6f70(%eax)        # gen=26 d0  dis=26 d0 90 90 90 90 90
    fcoms  %es:-0x6f6f6f70(%eax)        # gen=26 d8  dis=26 d8 90 90 90 90 90
    es clc                              # gen=26 f8  dis=26 f8
    es or  $0x90909090,%eax             # gen=26 0d  dis=26 0d 90 90 90 90
    es adc $0x90909090,%eax             # gen=26 15  dis=26 15 90 90 90 90
    es sbb $0x90909090,%eax             # gen=26 1d  dis=26 1d 90 90 90 90
    es and $0x90909090,%eax             # gen=26 25  dis=26 25 90 90 90 90
    es sub $0x90909090,%eax             # gen=26 2d  dis=26 2d 90 90 90 90
    es xor $0x90909090,%eax             # gen=26 35  dis=26 35 90 90 90 90
    es cmp $0x90909090,%eax             # gen=26 3d  dis=26 3d 90 90 90 90
    daa                                 # gen=27 c0  dis=27
    sub    %al,%al                      # gen=28 c0  dis=28 c0
    sub    %al,0x90909090               # gen=28 05  dis=28 05 90 90 90 90
    sub    %eax,%eax                    # gen=29 c0  dis=29 c0
    sub    %eax,0x90909090              # gen=29 05  dis=29 05 90 90 90 90
    sub    0x90909090,%al               # gen=2a 05  dis=2a 05 90 90 90 90
    sub    0x90909090,%eax              # gen=2b 05  dis=2b 05 90 90 90 90
    sub    $0xc0,%al                    # gen=2c c0  dis=2c c0
    sub    $0x5,%al                     # gen=2c 05  dis=2c 05
    sub    $0x909090c0,%eax             # gen=2d c0  dis=2d c0 90 90 90
    sub    $0x90909005,%eax             # gen=2d 05  dis=2d 05 90 90 90
    rclb   $0x90,%cs:-0x6f6f6f70(%eax)  # gen=2e c0  dis=2e c0 90 90 90 90 90
    cs add $0x90909090,%eax             # gen=2e 05  dis=2e 05 90 90 90 90
    cs enter $0x9090,$0x90              # gen=2e c8  dis=2e c8 90 90 90
    rclb   %cs:-0x6f6f6f70(%eax)        # gen=2e d0  dis=2e d0 90 90 90 90 90
    fcoms  %cs:-0x6f6f6f70(%eax)        # gen=2e d8  dis=2e d8 90 90 90 90 90
    cs clc                              # gen=2e f8  dis=2e f8
    cs or  $0x90909090,%eax             # gen=2e 0d  dis=2e 0d 90 90 90 90
    cs adc $0x90909090,%eax             # gen=2e 15  dis=2e 15 90 90 90 90
    cs sbb $0x90909090,%eax             # gen=2e 1d  dis=2e 1d 90 90 90 90
    cs and $0x90909090,%eax             # gen=2e 25  dis=2e 25 90 90 90 90
    cs sub $0x90909090,%eax             # gen=2e 2d  dis=2e 2d 90 90 90 90
    cs xor $0x90909090,%eax             # gen=2e 35  dis=2e 35 90 90 90 90
    cs cmp $0x90909090,%eax             # gen=2e 3d  dis=2e 3d 90 90 90 90
    das                                 # gen=2f c0  dis=2f
    xor    %al,%al                      # gen=30 c0  dis=30 c0
    xor    %al,0x90909090               # gen=30 05  dis=30 05 90 90 90 90
    xor    %eax,%eax                    # gen=31 c0  dis=31 c0
    xor    %eax,0x90909090              # gen=31 05  dis=31 05 90 90 90 90
    xor    0x90909090,%al               # gen=32 05  dis=32 05 90 90 90 90
    xor    0x90909090,%eax              # gen=33 05  dis=33 05 90 90 90 90
    xor    $0xc0,%al                    # gen=34 c0  dis=34 c0
    xor    $0x5,%al                     # gen=34 05  dis=34 05
    xor    $0x909090c0,%eax             # gen=35 c0  dis=35 c0 90 90 90
    xor    $0x90909005,%eax             # gen=35 05  dis=35 05 90 90 90
    rclb   $0x90,%ss:-0x6f6f6f70(%eax)  # gen=36 c0  dis=36 c0 90 90 90 90 90
    ss add $0x90909090,%eax             # gen=36 05  dis=36 05 90 90 90 90
    ss enter $0x9090,$0x90              # gen=36 c8  dis=36 c8 90 90 90
    rclb   %ss:-0x6f6f6f70(%eax)        # gen=36 d0  dis=36 d0 90 90 90 90 90
    fcoms  %ss:-0x6f6f6f70(%eax)        # gen=36 d8  dis=36 d8 90 90 90 90 90
    ss clc                              # gen=36 f8  dis=36 f8
    ss or  $0x90909090,%eax             # gen=36 0d  dis=36 0d 90 90 90 90
    ss adc $0x90909090,%eax             # gen=36 15  dis=36 15 90 90 90 90
    ss sbb $0x90909090,%eax             # gen=36 1d  dis=36 1d 90 90 90 90
    ss and $0x90909090,%eax             # gen=36 25  dis=36 25 90 90 90 90
    ss sub $0x90909090,%eax             # gen=36 2d  dis=36 2d 90 90 90 90
    ss xor $0x90909090,%eax             # gen=36 35  dis=36 35 90 90 90 90
    ss cmp $0x90909090,%eax             # gen=36 3d  dis=36 3d 90 90 90 90
    aaa                                 # gen=37 c0  dis=37
    cmp    %al,%al                      # gen=38 c0  dis=38 c0
    cmp    %al,0x90909090               # gen=38 05  dis=38 05 90 90 90 90
    cmp    %eax,%eax                    # gen=39 c0  dis=39 c0
    cmp    %eax,0x90909090              # gen=39 05  dis=39 05 90 90 90 90
    cmp    0x90909090,%al               # gen=3a 05  dis=3a 05 90 90 90 90
    cmp    0x90909090,%eax              # gen=3b 05  dis=3b 05 90 90 90 90
    cmp    $0xc0,%al                    # gen=3c c0  dis=3c c0
    cmp    $0x5,%al                     # gen=3c 05  dis=3c 05
    cmp    $0x909090c0,%eax             # gen=3d c0  dis=3d c0 90 90 90
    cmp    $0x90909005,%eax             # gen=3d 05  dis=3d 05 90 90 90
    rclb   $0x90,%ds:-0x6f6f6f70(%eax)  # gen=3e c0  dis=3e c0 90 90 90 90 90
    ds add $0x90909090,%eax             # gen=3e 05  dis=3e 05 90 90 90 90
    ds enter $0x9090,$0x90              # gen=3e c8  dis=3e c8 90 90 90
    rclb   %ds:-0x6f6f6f70(%eax)        # gen=3e d0  dis=3e d0 90 90 90 90 90
    fcoms  %ds:-0x6f6f6f70(%eax)        # gen=3e d8  dis=3e d8 90 90 90 90 90
    ds clc                              # gen=3e f8  dis=3e f8
    ds or  $0x90909090,%eax             # gen=3e 0d  dis=3e 0d 90 90 90 90
    ds adc $0x90909090,%eax             # gen=3e 15  dis=3e 15 90 90 90 90
    ds sbb $0x90909090,%eax             # gen=3e 1d  dis=3e 1d 90 90 90 90
    ds and $0x90909090,%eax             # gen=3e 25  dis=3e 25 90 90 90 90
    ds sub $0x90909090,%eax             # gen=3e 2d  dis=3e 2d 90 90 90 90
    ds xor $0x90909090,%eax             # gen=3e 35  dis=3e 35 90 90 90 90
    ds cmp $0x90909090,%eax             # gen=3e 3d  dis=3e 3d 90 90 90 90
    aas                                 # gen=3f c0  dis=3f
    inc    %eax                         # gen=40 c0  dis=40
    inc    %ecx                         # gen=41 c0  dis=41
    inc    %edx                         # gen=42 c0  dis=42
    inc    %ebx                         # gen=43 c0  dis=43
    inc    %esp                         # gen=44 c0  dis=44
    inc    %ebp                         # gen=45 c0  dis=45
    inc    %esi                         # gen=46 c0  dis=46
    inc    %edi                         # gen=47 c0  dis=47
    dec    %eax                         # gen=48 c0  dis=48
    dec    %ecx                         # gen=49 c0  dis=49
    dec    %edx                         # gen=4a c0  dis=4a
    dec    %ebx                         # gen=4b c0  dis=4b
    dec    %esp                         # gen=4c c0  dis=4c
    dec    %ebp                         # gen=4d c0  dis=4d
    dec    %esi                         # gen=4e c0  dis=4e
    dec    %edi                         # gen=4f c0  dis=4f
    push   %eax                         # gen=50 c0  dis=50
    push   %ecx                         # gen=51 c0  dis=51
    push   %edx                         # gen=52 c0  dis=52
    push   %ebx                         # gen=53 c0  dis=53
    push   %esp                         # gen=54 c0  dis=54
    push   %ebp                         # gen=55 c0  dis=55
    push   %esi                         # gen=56 c0  dis=56
    push   %edi                         # gen=57 c0  dis=57
    pop    %eax                         # gen=58 c0  dis=58
    pop    %ecx                         # gen=59 c0  dis=59
    pop    %edx                         # gen=5a c0  dis=5a
    pop    %ebx                         # gen=5b c0  dis=5b
    pop    %esp                         # gen=5c c0  dis=5c
    pop    %ebp                         # gen=5d c0  dis=5d
    pop    %esi                         # gen=5e c0  dis=5e
    pop    %edi                         # gen=5f c0  dis=5f
    pusha                               # gen=60 c0  dis=60
    popa                                # gen=61 c0  dis=61
    bound  %eax,0x90909090              # gen=62 05  dis=62 05 90 90 90 90
    arpl   %ax,%ax                      # gen=63 c0  dis=63 c0
    arpl   %ax,0x90909090               # gen=63 05  dis=63 05 90 90 90 90
    rclb   $0x90,%fs:-0x6f6f6f70(%eax)  # gen=64 c0  dis=64 c0 90 90 90 90 90
    fs add $0x90909090,%eax             # gen=64 05  dis=64 05 90 90 90 90
    fs enter $0x9090,$0x90              # gen=64 c8  dis=64 c8 90 90 90
    rclb   %fs:-0x6f6f6f70(%eax)        # gen=64 d0  dis=64 d0 90 90 90 90 90
    fcoms  %fs:-0x6f6f6f70(%eax)        # gen=64 d8  dis=64 d8 90 90 90 90 90
    fs clc                              # gen=64 f8  dis=64 f8
    fs or  $0x90909090,%eax             # gen=64 0d  dis=64 0d 90 90 90 90
    fs adc $0x90909090,%eax             # gen=64 15  dis=64 15 90 90 90 90
    fs sbb $0x90909090,%eax             # gen=64 1d  dis=64 1d 90 90 90 90
    fs and $0x90909090,%eax             # gen=64 25  dis=64 25 90 90 90 90
    fs sub $0x90909090,%eax             # gen=64 2d  dis=64 2d 90 90 90 90
    fs xor $0x90909090,%eax             # gen=64 35  dis=64 35 90 90 90 90
    fs cmp $0x90909090,%eax             # gen=64 3d  dis=64 3d 90 90 90 90
    rclb   $0x90,%gs:-0x6f6f6f70(%eax)  # gen=65 c0  dis=65 c0 90 90 90 90 90
    gs add $0x90909090,%eax             # gen=65 05  dis=65 05 90 90 90 90
    gs enter $0x9090,$0x90              # gen=65 c8  dis=65 c8 90 90 90
    rclb   %gs:-0x6f6f6f70(%eax)        # gen=65 d0  dis=65 d0 90 90 90 90 90
    fcoms  %gs:-0x6f6f6f70(%eax)        # gen=65 d8  dis=65 d8 90 90 90 90 90
    gs clc                              # gen=65 f8  dis=65 f8
    gs or  $0x90909090,%eax             # gen=65 0d  dis=65 0d 90 90 90 90
    gs adc $0x90909090,%eax             # gen=65 15  dis=65 15 90 90 90 90
    gs sbb $0x90909090,%eax             # gen=65 1d  dis=65 1d 90 90 90 90
    gs and $0x90909090,%eax             # gen=65 25  dis=65 25 90 90 90 90
    gs sub $0x90909090,%eax             # gen=65 2d  dis=65 2d 90 90 90 90
    gs xor $0x90909090,%eax             # gen=65 35  dis=65 35 90 90 90 90
    gs cmp $0x90909090,%eax             # gen=65 3d  dis=65 3d 90 90 90 90
    data16 rclb $0x90,-0x6f6f6f70(%eax) # gen=66 c0  dis=66 c0 90 90 90 90 90
    add    $0x9090,%ax                  # gen=66 05  dis=66 05 90 90
    enterw $0x9090,$0x90                # gen=66 c8  dis=66 c8 90 90 90
    data16 rclb -0x6f6f6f70(%eax)       # gen=66 d0  dis=66 d0 90 90 90 90 90
    data16 fcoms -0x6f6f6f70(%eax)      # gen=66 d8  dis=66 d8 90 90 90 90 90
    callw  0xa3d4                       # gen=66 e8  dis=66 e8 90 90
    data16 clc                          # gen=66 f8  dis=66 f8
    or     $0x9090,%ax                  # gen=66 0d  dis=66 0d 90 90
    adc    $0x9090,%ax                  # gen=66 15  dis=66 15 90 90
    sbb    $0x9090,%ax                  # gen=66 1d  dis=66 1d 90 90
    and    $0x9090,%ax                  # gen=66 25  dis=66 25 90 90
    sub    $0x9090,%ax                  # gen=66 2d  dis=66 2d 90 90
    xor    $0x9090,%ax                  # gen=66 35  dis=66 35 90 90
    cmp    $0x9090,%ax                  # gen=66 3d  dis=66 3d 90 90
    rclb   $0x90,-0x6f70(%bx,%si)       # gen=67 c0  dis=67 c0 90 90 90 90
    addr16 add $0x90909090,%eax         # gen=67 05  dis=67 05 90 90 90 90
    addr16 enter $0x9090,$0x90          # gen=67 c8  dis=67 c8 90 90 90
    rclb   -0x6f70(%bx,%si)             # gen=67 d0  dis=67 d0 90 90 90
    fcoms  -0x6f70(%bx,%si)             # gen=67 d8  dis=67 d8 90 90 90
    addr16 clc                          # gen=67 f8  dis=67 f8
    addr16 or $0x90909090,%eax          # gen=67 0d  dis=67 0d 90 90 90 90
    addr16 adc $0x90909090,%eax         # gen=67 15  dis=67 15 90 90 90 90
    addr16 sbb $0x90909090,%eax         # gen=67 1d  dis=67 1d 90 90 90 90
    addr16 and $0x90909090,%eax         # gen=67 25  dis=67 25 90 90 90 90
    addr16 sub $0x90909090,%eax         # gen=67 2d  dis=67 2d 90 90 90 90
    addr16 xor $0x90909090,%eax         # gen=67 35  dis=67 35 90 90 90 90
    addr16 cmp $0x90909090,%eax         # gen=67 3d  dis=67 3d 90 90 90 90
    push   $0x909090c0                  # gen=68 c0  dis=68 c0 90 90 90
    push   $0x90909005                  # gen=68 05  dis=68 05 90 90 90
    imul   $0x90909090,%eax,%eax        # gen=69 c0  dis=69 c0 90 90 90 90
    imul   $0x90909090,0x90909090,%eax  # gen=69 05  dis=69 05 90 90 90 90 90
    push   $0xffffffc0                  # gen=6a c0  dis=6a c0
    push   $0x5                         # gen=6a 05  dis=6a 05
    imul   $0xffffff90,%eax,%eax        # gen=6b c0  dis=6b c0 90
    imul   $0xffffff90,0x90909090,%eax  # gen=6b 05  dis=6b 05 90 90 90 90 90
    insb   (%dx),%es:(%edi)             # gen=6c c0  dis=6c
    insl   (%dx),%es:(%edi)             # gen=6d c0  dis=6d
    outsb  %ds:(%esi),(%dx)             # gen=6e c0  dis=6e
    outsl  %ds:(%esi),(%dx)             # gen=6f c0  dis=6f
    jo     0x15a2                       # gen=70 c0  dis=70 c0
    jo     0x15f7                       # gen=70 05  dis=70 05
    jno    0x15c2                       # gen=71 c0  dis=71 c0
    jno    0x1617                       # gen=71 05  dis=71 05
    jb     0x15e2                       # gen=72 c0  dis=72 c0
    jb     0x1637                       # gen=72 05  dis=72 05
    jae    0x1602                       # gen=73 c0  dis=73 c0
    jae    0x1657                       # gen=73 05  dis=73 05
    je     0x1622                       # gen=74 c0  dis=74 c0
    je     0x1677                       # gen=74 05  dis=74 05
    jne    0x1642                       # gen=75 c0  dis=75 c0
    jne    0x1697                       # gen=75 05  dis=75 05
    jbe    0x1662                       # gen=76 c0  dis=76 c0
    jbe    0x16b7                       # gen=76 05  dis=76 05
    ja     0x1682                       # gen=77 c0  dis=77 c0
    ja     0x16d7                       # gen=77 05  dis=77 05
    js     0x16a2                       # gen=78 c0  dis=78 c0
    js     0x16f7                       # gen=78 05  dis=78 05
    jns    0x16c2                       # gen=79 c0  dis=79 c0
    jns    0x1717                       # gen=79 05  dis=79 05
    jp     0x16e2                       # gen=7a c0  dis=7a c0
    jp     0x1737                       # gen=7a 05  dis=7a 05
    jnp    0x1702                       # gen=7b c0  dis=7b c0
    jnp    0x1757                       # gen=7b 05  dis=7b 05
    jl     0x1722                       # gen=7c c0  dis=7c c0
    jl     0x1777                       # gen=7c 05  dis=7c 05
    jge    0x1742                       # gen=7d c0  dis=7d c0
    jge    0x1797                       # gen=7d 05  dis=7d 05
    jle    0x1762                       # gen=7e c0  dis=7e c0
    jle    0x17b7                       # gen=7e 05  dis=7e 05
    jg     0x1782                       # gen=7f c0  dis=7f c0
    jg     0x17d7                       # gen=7f 05  dis=7f 05
    add    $0x90,%al                    # gen=80 c0  dis=80 c0 90
    addb   $0x90,0x90909090             # gen=80 05  dis=80 05 90 90 90 90 90
    or     $0x90,%al                    # gen=80 c8  dis=80 c8 90
    adc    $0x90,%al                    # gen=80 d0  dis=80 d0 90
    sbb    $0x90,%al                    # gen=80 d8  dis=80 d8 90
    and    $0x90,%al                    # gen=80 e0  dis=80 e0 90
    sub    $0x90,%al                    # gen=80 e8  dis=80 e8 90
    xor    $0x90,%al                    # gen=80 f0  dis=80 f0 90
    cmp    $0x90,%al                    # gen=80 f8  dis=80 f8 90
    orb    $0x90,0x90909090             # gen=80 0d  dis=80 0d 90 90 90 90 90
    adcb   $0x90,0x90909090             # gen=80 15  dis=80 15 90 90 90 90 90
    sbbb   $0x90,0x90909090             # gen=80 1d  dis=80 1d 90 90 90 90 90
    andb   $0x90,0x90909090             # gen=80 25  dis=80 25 90 90 90 90 90
    subb   $0x90,0x90909090             # gen=80 2d  dis=80 2d 90 90 90 90 90
    xorb   $0x90,0x90909090             # gen=80 35  dis=80 35 90 90 90 90 90
    cmpb   $0x90,0x90909090             # gen=80 3d  dis=80 3d 90 90 90 90 90
    add    $0x90909090,%eax             # gen=81 c0  dis=81 c0 90 90 90 90
    addl   $0x90909090,0x90909090       # gen=81 05  dis=81 05 90 90 90 90 90
    or     $0x90909090,%eax             # gen=81 c8  dis=81 c8 90 90 90 90
    adc    $0x90909090,%eax             # gen=81 d0  dis=81 d0 90 90 90 90
    sbb    $0x90909090,%eax             # gen=81 d8  dis=81 d8 90 90 90 90
    and    $0x90909090,%eax             # gen=81 e0  dis=81 e0 90 90 90 90
    sub    $0x90909090,%eax             # gen=81 e8  dis=81 e8 90 90 90 90
    xor    $0x90909090,%eax             # gen=81 f0  dis=81 f0 90 90 90 90
    cmp    $0x90909090,%eax             # gen=81 f8  dis=81 f8 90 90 90 90
    orl    $0x90909090,0x90909090       # gen=81 0d  dis=81 0d 90 90 90 90 90
    adcl   $0x90909090,0x90909090       # gen=81 15  dis=81 15 90 90 90 90 90
    sbbl   $0x90909090,0x90909090       # gen=81 1d  dis=81 1d 90 90 90 90 90
    andl   $0x90909090,0x90909090       # gen=81 25  dis=81 25 90 90 90 90 90
    subl   $0x90909090,0x90909090       # gen=81 2d  dis=81 2d 90 90 90 90 90
    xorl   $0x90909090,0x90909090       # gen=81 35  dis=81 35 90 90 90 90 90
    cmpl   $0x90909090,0x90909090       # gen=81 3d  dis=81 3d 90 90 90 90 90
    add    $0xffffff90,%eax             # gen=83 c0  dis=83 c0 90
    addl   $0xffffff90,0x90909090       # gen=83 05  dis=83 05 90 90 90 90 90
    or     $0xffffff90,%eax             # gen=83 c8  dis=83 c8 90
    adc    $0xffffff90,%eax             # gen=83 d0  dis=83 d0 90
    sbb    $0xffffff90,%eax             # gen=83 d8  dis=83 d8 90
    and    $0xffffff90,%eax             # gen=83 e0  dis=83 e0 90
    sub    $0xffffff90,%eax             # gen=83 e8  dis=83 e8 90
    xor    $0xffffff90,%eax             # gen=83 f0  dis=83 f0 90
    cmp    $0xffffff90,%eax             # gen=83 f8  dis=83 f8 90
    orl    $0xffffff90,0x90909090       # gen=83 0d  dis=83 0d 90 90 90 90 90
    adcl   $0xffffff90,0x90909090       # gen=83 15  dis=83 15 90 90 90 90 90
    sbbl   $0xffffff90,0x90909090       # gen=83 1d  dis=83 1d 90 90 90 90 90
    andl   $0xffffff90,0x90909090       # gen=83 25  dis=83 25 90 90 90 90 90
    subl   $0xffffff90,0x90909090       # gen=83 2d  dis=83 2d 90 90 90 90 90
    xorl   $0xffffff90,0x90909090       # gen=83 35  dis=83 35 90 90 90 90 90
    cmpl   $0xffffff90,0x90909090       # gen=83 3d  dis=83 3d 90 90 90 90 90
    test   %al,%al                      # gen=84 c0  dis=84 c0
    test   %al,0x90909090               # gen=84 05  dis=84 05 90 90 90 90
    test   %eax,%eax                    # gen=85 c0  dis=85 c0
    test   %eax,0x90909090              # gen=85 05  dis=85 05 90 90 90 90
    xchg   %al,%al                      # gen=86 c0  dis=86 c0
    xchg   %al,0x90909090               # gen=86 05  dis=86 05 90 90 90 90
    xchg   %eax,%eax                    # gen=87 c0  dis=87 c0
    xchg   %eax,0x90909090              # gen=87 05  dis=87 05 90 90 90 90
    mov    %al,%al                      # gen=88 c0  dis=88 c0
    mov    %al,0x90909090               # gen=88 05  dis=88 05 90 90 90 90
    mov    %eax,%eax                    # gen=89 c0  dis=89 c0
    mov    %eax,0x90909090              # gen=89 05  dis=89 05 90 90 90 90
    mov    0x90909090,%al               # gen=8a 05  dis=8a 05 90 90 90 90
    mov    0x90909090,%eax              # gen=8b 05  dis=8b 05 90 90 90 90
    mov    %es,%eax                     # gen=8c c0  dis=8c c0
    mov    %es,0x90909090               # gen=8c 05  dis=8c 05 90 90 90 90
    lea    0x90909090,%eax              # gen=8d 05  dis=8d 05 90 90 90 90
    mov    %eax,%es                     # gen=8e c0  dis=8e c0
    mov    0x90909090,%es               # gen=8e 05  dis=8e 05 90 90 90 90
    pop    0x90909090                   # gen=8f 05  dis=8f 05 90 90 90 90
    nop                                 # gen=90 c0  dis=90
    xchg   %eax,%ecx                    # gen=91 c0  dis=91
    xchg   %eax,%edx                    # gen=92 c0  dis=92
    xchg   %eax,%ebx                    # gen=93 c0  dis=93
    xchg   %eax,%esp                    # gen=94 c0  dis=94
    xchg   %eax,%ebp                    # gen=95 c0  dis=95
    xchg   %eax,%esi                    # gen=96 c0  dis=96
    xchg   %eax,%edi                    # gen=97 c0  dis=97
    cwtl                                # gen=98 c0  dis=98
    cltd                                # gen=99 c0  dis=99
    lcall  $0x9090,$0x909090c0          # gen=9a c0  dis=9a c0 90 90 90 90 90
    lcall  $0x9090,$0x90909005          # gen=9a 05  dis=9a 05 90 90 90 90 90
    fwait                               # gen=9b c0  dis=9b
    pushf                               # gen=9c c0  dis=9c
    popf                                # gen=9d c0  dis=9d
    sahf                                # gen=9e c0  dis=9e
    lahf                                # gen=9f c0  dis=9f
    mov    0x909090c0,%al               # gen=a0 c0  dis=a0 c0 90 90 90
    mov    0x90909005,%al               # gen=a0 05  dis=a0 05 90 90 90
    mov    0x909090c0,%eax              # gen=a1 c0  dis=a1 c0 90 90 90
    mov    0x90909005,%eax              # gen=a1 05  dis=a1 05 90 90 90
    mov    %al,0x909090c0               # gen=a2 c0  dis=a2 c0 90 90 90
    mov    %al,0x90909005               # gen=a2 05  dis=a2 05 90 90 90
    mov    %eax,0x909090c0              # gen=a3 c0  dis=a3 c0 90 90 90
    mov    %eax,0x90909005              # gen=a3 05  dis=a3 05 90 90 90
    movsb  %ds:(%esi),%es:(%edi)        # gen=a4 c0  dis=a4
    movsl  %ds:(%esi),%es:(%edi)        # gen=a5 c0  dis=a5
    cmpsb  %es:(%edi),%ds:(%esi)        # gen=a6 c0  dis=a6
    cmpsl  %es:(%edi),%ds:(%esi)        # gen=a7 c0  dis=a7
    test   $0xc0,%al                    # gen=a8 c0  dis=a8 c0
    test   $0x5,%al                     # gen=a8 05  dis=a8 05
    test   $0x909090c0,%eax             # gen=a9 c0  dis=a9 c0 90 90 90
    test   $0x90909005,%eax             # gen=a9 05  dis=a9 05 90 90 90
    stos   %al,%es:(%edi)               # gen=aa c0  dis=aa
    stos   %eax,%es:(%edi)              # gen=ab c0  dis=ab
    lods   %ds:(%esi),%al               # gen=ac c0  dis=ac
    lods   %ds:(%esi),%eax              # gen=ad c0  dis=ad
    scas   %es:(%edi),%al               # gen=ae c0  dis=ae
    scas   %es:(%edi),%eax              # gen=af c0  dis=af
    mov    $0xc0,%al                    # gen=b0 c0  dis=b0 c0
    mov    $0x5,%al                     # gen=b0 05  dis=b0 05
    mov    $0xc0,%cl                    # gen=b1 c0  dis=b1 c0
    mov    $0x5,%cl                     # gen=b1 05  dis=b1 05
    mov    $0xc0,%dl                    # gen=b2 c0  dis=b2 c0
    mov    $0x5,%dl                     # gen=b2 05  dis=b2 05
    mov    $0xc0,%bl                    # gen=b3 c0  dis=b3 c0
    mov    $0x5,%bl                     # gen=b3 05  dis=b3 05
    mov    $0xc0,%ah                    # gen=b4 c0  dis=b4 c0
    mov    $0x5,%ah                     # gen=b4 05  dis=b4 05
    mov    $0xc0,%ch                    # gen=b5 c0  dis=b5 c0
    mov    $0x5,%ch                     # gen=b5 05  dis=b5 05
    mov    $0xc0,%dh                    # gen=b6 c0  dis=b6 c0
    mov    $0x5,%dh                     # gen=b6 05  dis=b6 05
    mov    $0xc0,%bh                    # gen=b7 c0  dis=b7 c0
    mov    $0x5,%bh                     # gen=b7 05  dis=b7 05
    mov    $0x909090c0,%eax             # gen=b8 c0  dis=b8 c0 90 90 90
    mov    $0x90909005,%eax             # gen=b8 05  dis=b8 05 90 90 90
    mov    $0x909090c0,%ecx             # gen=b9 c0  dis=b9 c0 90 90 90
    mov    $0x90909005,%ecx             # gen=b9 05  dis=b9 05 90 90 90
    mov    $0x909090c0,%edx             # gen=ba c0  dis=ba c0 90 90 90
    mov    $0x90909005,%edx             # gen=ba 05  dis=ba 05 90 90 90
    mov    $0x909090c0,%ebx             # gen=bb c0  dis=bb c0 90 90 90
    mov    $0x90909005,%ebx             # gen=bb 05  dis=bb 05 90 90 90
    mov    $0x909090c0,%esp             # gen=bc c0  dis=bc c0 90 90 90
    mov    $0x90909005,%esp             # gen=bc 05  dis=bc 05 90 90 90
    mov    $0x909090c0,%ebp             # gen=bd c0  dis=bd c0 90 90 90
    mov    $0x90909005,%ebp             # gen=bd 05  dis=bd 05 90 90 90
    mov    $0x909090c0,%esi             # gen=be c0  dis=be c0 90 90 90
    mov    $0x90909005,%esi             # gen=be 05  dis=be 05 90 90 90
    mov    $0x909090c0,%edi             # gen=bf c0  dis=bf c0 90 90 90
    mov    $0x90909005,%edi             # gen=bf 05  dis=bf 05 90 90 90
    rol    $0x90,%al                    # gen=c0 c0  dis=c0 c0 90
    rolb   $0x90,0x90909090             # gen=c0 05  dis=c0 05 90 90 90 90 90
    ror    $0x90,%al                    # gen=c0 c8  dis=c0 c8 90
    rcl    $0x90,%al                    # gen=c0 d0  dis=c0 d0 90
    rcr    $0x90,%al                    # gen=c0 d8  dis=c0 d8 90
    shl    $0x90,%al                    # gen=c0 e0  dis=c0 e0 90
    shr    $0x90,%al                    # gen=c0 e8  dis=c0 e8 90
    sar    $0x90,%al                    # gen=c0 f8  dis=c0 f8 90
    rorb   $0x90,0x90909090             # gen=c0 0d  dis=c0 0d 90 90 90 90 90
    rclb   $0x90,0x90909090             # gen=c0 15  dis=c0 15 90 90 90 90 90
    rcrb   $0x90,0x90909090             # gen=c0 1d  dis=c0 1d 90 90 90 90 90
    shlb   $0x90,0x90909090             # gen=c0 25  dis=c0 25 90 90 90 90 90
    shrb   $0x90,0x90909090             # gen=c0 2d  dis=c0 2d 90 90 90 90 90
    sarb   $0x90,0x90909090             # gen=c0 3d  dis=c0 3d 90 90 90 90 90
    rol    $0x90,%eax                   # gen=c1 c0  dis=c1 c0 90
    roll   $0x90,0x90909090             # gen=c1 05  dis=c1 05 90 90 90 90 90
    ror    $0x90,%eax                   # gen=c1 c8  dis=c1 c8 90
    rcl    $0x90,%eax                   # gen=c1 d0  dis=c1 d0 90
    rcr    $0x90,%eax                   # gen=c1 d8  dis=c1 d8 90
    shl    $0x90,%eax                   # gen=c1 e0  dis=c1 e0 90
    shr    $0x90,%eax                   # gen=c1 e8  dis=c1 e8 90
    sar    $0x90,%eax                   # gen=c1 f8  dis=c1 f8 90
    rorl   $0x90,0x90909090             # gen=c1 0d  dis=c1 0d 90 90 90 90 90
    rcll   $0x90,0x90909090             # gen=c1 15  dis=c1 15 90 90 90 90 90
    rcrl   $0x90,0x90909090             # gen=c1 1d  dis=c1 1d 90 90 90 90 90
    shll   $0x90,0x90909090             # gen=c1 25  dis=c1 25 90 90 90 90 90
    shrl   $0x90,0x90909090             # gen=c1 2d  dis=c1 2d 90 90 90 90 90
    sarl   $0x90,0x90909090             # gen=c1 3d  dis=c1 3d 90 90 90 90 90
    ret    $0x90c0                      # gen=c2 c0  dis=c2 c0 90
    ret    $0x9005                      # gen=c2 05  dis=c2 05 90
    ret                                 # gen=c3 c0  dis=c3
    les    0x90909090,%eax              # gen=c4 05  dis=c4 05 90 90 90 90
    lds    0x90909090,%eax              # gen=c5 05  dis=c5 05 90 90 90 90
    kmovw  -0x6f6f6f70(%eax),%k2        # gen=c5 f8  dis=c5 f8 90 90 90 90 90
    lds    0x90909090,%ecx              # gen=c5 0d  dis=c5 0d 90 90 90 90
    lds    0x90909090,%edx              # gen=c5 15  dis=c5 15 90 90 90 90
    lds    0x90909090,%ebx              # gen=c5 1d  dis=c5 1d 90 90 90 90
    lds    0x90909090,%esp              # gen=c5 25  dis=c5 25 90 90 90 90
    lds    0x90909090,%ebp              # gen=c5 2d  dis=c5 2d 90 90 90 90
    lds    0x90909090,%esi              # gen=c5 35  dis=c5 35 90 90 90 90
    lds    0x90909090,%edi              # gen=c5 3d  dis=c5 3d 90 90 90 90
    mov    $0x90,%al                    # gen=c6 c0  dis=c6 c0 90
    movb   $0x90,0x90909090             # gen=c6 05  dis=c6 05 90 90 90 90 90
    xabort $0x90                        # gen=c6 f8  dis=c6 f8 90
    mov    $0x90909090,%eax             # gen=c7 c0  dis=c7 c0 90 90 90 90
    movl   $0x90909090,0x90909090       # gen=c7 05  dis=c7 05 90 90 90 90 90
    xbegin 0x9090b8d6                   # gen=c7 f8  dis=c7 f8 90 90 90 90
    enter  $0x90c0,$0x90                # gen=c8 c0  dis=c8 c0 90 90
    enter  $0x9005,$0x90                # gen=c8 05  dis=c8 05 90 90
    leave                               # gen=c9 c0  dis=c9
    lret   $0x90c0                      # gen=ca c0  dis=ca c0 90
    lret   $0x9005                      # gen=ca 05  dis=ca 05 90
    lret                                # gen=cb c0  dis=cb
    int3                                # gen=cc c0  dis=cc
    int    $0xc0                        # gen=cd c0  dis=cd c0
    int    $0x5                         # gen=cd 05  dis=cd 05
    into                                # gen=ce c0  dis=ce
    iret                                # gen=cf c0  dis=cf
    rol    %al                          # gen=d0 c0  dis=d0 c0
    rolb   0x90909090                   # gen=d0 05  dis=d0 05 90 90 90 90
    ror    %al                          # gen=d0 c8  dis=d0 c8
    rcl    %al                          # gen=d0 d0  dis=d0 d0
    rcr    %al                          # gen=d0 d8  dis=d0 d8
    shl    %al                          # gen=d0 e0  dis=d0 e0
    shr    %al                          # gen=d0 e8  dis=d0 e8
    sar    %al                          # gen=d0 f8  dis=d0 f8
    rorb   0x90909090                   # gen=d0 0d  dis=d0 0d 90 90 90 90
    rclb   0x90909090                   # gen=d0 15  dis=d0 15 90 90 90 90
    rcrb   0x90909090                   # gen=d0 1d  dis=d0 1d 90 90 90 90
    shlb   0x90909090                   # gen=d0 25  dis=d0 25 90 90 90 90
    shrb   0x90909090                   # gen=d0 2d  dis=d0 2d 90 90 90 90
    sarb   0x90909090                   # gen=d0 3d  dis=d0 3d 90 90 90 90
    rol    %eax                         # gen=d1 c0  dis=d1 c0
    roll   0x90909090                   # gen=d1 05  dis=d1 05 90 90 90 90
    ror    %eax                         # gen=d1 c8  dis=d1 c8
    rcl    %eax                         # gen=d1 d0  dis=d1 d0
    rcr    %eax                         # gen=d1 d8  dis=d1 d8
    shl    %eax                         # gen=d1 e0  dis=d1 e0
    shr    %eax                         # gen=d1 e8  dis=d1 e8
    sar    %eax                         # gen=d1 f8  dis=d1 f8
    rorl   0x90909090                   # gen=d1 0d  dis=d1 0d 90 90 90 90
    rcll   0x90909090                   # gen=d1 15  dis=d1 15 90 90 90 90
    rcrl   0x90909090                   # gen=d1 1d  dis=d1 1d 90 90 90 90
    shll   0x90909090                   # gen=d1 25  dis=d1 25 90 90 90 90
    shrl   0x90909090                   # gen=d1 2d  dis=d1 2d 90 90 90 90
    sarl   0x90909090                   # gen=d1 3d  dis=d1 3d 90 90 90 90
    rol    %cl,%al                      # gen=d2 c0  dis=d2 c0
    rolb   %cl,0x90909090               # gen=d2 05  dis=d2 05 90 90 90 90
    ror    %cl,%al                      # gen=d2 c8  dis=d2 c8
    rcl    %cl,%al                      # gen=d2 d0  dis=d2 d0
    rcr    %cl,%al                      # gen=d2 d8  dis=d2 d8
    shl    %cl,%al                      # gen=d2 e0  dis=d2 e0
    shr    %cl,%al                      # gen=d2 e8  dis=d2 e8
    sar    %cl,%al                      # gen=d2 f8  dis=d2 f8
    rorb   %cl,0x90909090               # gen=d2 0d  dis=d2 0d 90 90 90 90
    rclb   %cl,0x90909090               # gen=d2 15  dis=d2 15 90 90 90 90
    rcrb   %cl,0x90909090               # gen=d2 1d  dis=d2 1d 90 90 90 90
    shlb   %cl,0x90909090               # gen=d2 25  dis=d2 25 90 90 90 90
    shrb   %cl,0x90909090               # gen=d2 2d  dis=d2 2d 90 90 90 90
    sarb   %cl,0x90909090               # gen=d2 3d  dis=d2 3d 90 90 90 90
    rol    %cl,%eax                     # gen=d3 c0  dis=d3 c0
    roll   %cl,0x90909090               # gen=d3 05  dis=d3 05 90 90 90 90
    ror    %cl,%eax                     # gen=d3 c8  dis=d3 c8
    rcl    %cl,%eax                     # gen=d3 d0  dis=d3 d0
    rcr    %cl,%eax                     # gen=d3 d8  dis=d3 d8
    shl    %cl,%eax                     # gen=d3 e0  dis=d3 e0
    shr    %cl,%eax                     # gen=d3 e8  dis=d3 e8
    sar    %cl,%eax                     # gen=d3 f8  dis=d3 f8
    rorl   %cl,0x90909090               # gen=d3 0d  dis=d3 0d 90 90 90 90
    rcll   %cl,0x90909090               # gen=d3 15  dis=d3 15 90 90 90 90
    rcrl   %cl,0x90909090               # gen=d3 1d  dis=d3 1d 90 90 90 90
    shll   %cl,0x90909090               # gen=d3 25  dis=d3 25 90 90 90 90
    shrl   %cl,0x90909090               # gen=d3 2d  dis=d3 2d 90 90 90 90
    sarl   %cl,0x90909090               # gen=d3 3d  dis=d3 3d 90 90 90 90
    aam    $0xc0                        # gen=d4 c0  dis=d4 c0
    aam    $0x5                         # gen=d4 05  dis=d4 05
    aad    $0xc0                        # gen=d5 c0  dis=d5 c0
    aad    $0x5                         # gen=d5 05  dis=d5 05
    xlat   %ds:(%ebx)                   # gen=d7 c0  dis=d7
    fadd   %st(0),%st                   # gen=d8 c0  dis=d8 c0
    fadds  0x90909090                   # gen=d8 05  dis=d8 05 90 90 90 90
    fmul   %st(0),%st                   # gen=d8 c8  dis=d8 c8
    fcom   %st(0)                       # gen=d8 d0  dis=d8 d0
    fcomp  %st(0)                       # gen=d8 d8  dis=d8 d8
    fsub   %st(0),%st                   # gen=d8 e0  dis=d8 e0
    fsubr  %st(0),%st                   # gen=d8 e8  dis=d8 e8
    fdiv   %st(0),%st                   # gen=d8 f0  dis=d8 f0
    fdivr  %st(0),%st                   # gen=d8 f8  dis=d8 f8
    fmuls  0x90909090                   # gen=d8 0d  dis=d8 0d 90 90 90 90
    fcoms  0x90909090                   # gen=d8 15  dis=d8 15 90 90 90 90
    fcomps 0x90909090                   # gen=d8 1d  dis=d8 1d 90 90 90 90
    fsubs  0x90909090                   # gen=d8 25  dis=d8 25 90 90 90 90
    fsubrs 0x90909090                   # gen=d8 2d  dis=d8 2d 90 90 90 90
    fdivs  0x90909090                   # gen=d8 35  dis=d8 35 90 90 90 90
    fdivrs 0x90909090                   # gen=d8 3d  dis=d8 3d 90 90 90 90
    fld    %st(0)                       # gen=d9 c0  dis=d9 c0
    flds   0x90909090                   # gen=d9 05  dis=d9 05 90 90 90 90
    fxch   %st(0)                       # gen=d9 c8  dis=d9 c8
    fnop                                # gen=d9 d0  dis=d9 d0
    fchs                                # gen=d9 e0  dis=d9 e0
    fld1                                # gen=d9 e8  dis=d9 e8
    f2xm1                               # gen=d9 f0  dis=d9 f0
    fprem                               # gen=d9 f8  dis=d9 f8
    fsts   0x90909090                   # gen=d9 15  dis=d9 15 90 90 90 90
    fstps  0x90909090                   # gen=d9 1d  dis=d9 1d 90 90 90 90
    fldenv 0x90909090                   # gen=d9 25  dis=d9 25 90 90 90 90
    fldcw  0x90909090                   # gen=d9 2d  dis=d9 2d 90 90 90 90
    fnstenv 0x90909090                  # gen=d9 35  dis=d9 35 90 90 90 90
    fnstcw 0x90909090                   # gen=d9 3d  dis=d9 3d 90 90 90 90
    fcmovb %st(0),%st                   # gen=da c0  dis=da c0
    fiaddl 0x90909090                   # gen=da 05  dis=da 05 90 90 90 90
    fcmove %st(0),%st                   # gen=da c8  dis=da c8
    fcmovbe %st(0),%st                  # gen=da d0  dis=da d0
    fcmovu %st(0),%st                   # gen=da d8  dis=da d8
    fimull 0x90909090                   # gen=da 0d  dis=da 0d 90 90 90 90
    ficoml 0x90909090                   # gen=da 15  dis=da 15 90 90 90 90
    ficompl 0x90909090                  # gen=da 1d  dis=da 1d 90 90 90 90
    fisubl 0x90909090                   # gen=da 25  dis=da 25 90 90 90 90
    fisubrl 0x90909090                  # gen=da 2d  dis=da 2d 90 90 90 90
    fidivl 0x90909090                   # gen=da 35  dis=da 35 90 90 90 90
    fidivrl 0x90909090                  # gen=da 3d  dis=da 3d 90 90 90 90
    fcmovnb %st(0),%st                  # gen=db c0  dis=db c0
    fildl  0x90909090                   # gen=db 05  dis=db 05 90 90 90 90
    fcmovne %st(0),%st                  # gen=db c8  dis=db c8
    fcmovnbe %st(0),%st                 # gen=db d0  dis=db d0
    fcmovnu %st(0),%st                  # gen=db d8  dis=db d8
    fucomi %st(0),%st                   # gen=db e8  dis=db e8
    fcomi  %st(0),%st                   # gen=db f0  dis=db f0
    fisttpl 0x90909090                  # gen=db 0d  dis=db 0d 90 90 90 90
    fistl  0x90909090                   # gen=db 15  dis=db 15 90 90 90 90
    fistpl 0x90909090                   # gen=db 1d  dis=db 1d 90 90 90 90
    fldt   0x90909090                   # gen=db 2d  dis=db 2d 90 90 90 90
    fstpt  0x90909090                   # gen=db 3d  dis=db 3d 90 90 90 90
    fadd   %st,%st(0)                   # gen=dc c0  dis=dc c0
    faddl  0x90909090                   # gen=dc 05  dis=dc 05 90 90 90 90
    fmul   %st,%st(0)                   # gen=dc c8  dis=dc c8
    fsub   %st,%st(0)                   # gen=dc e0  dis=dc e0
    fsubr  %st,%st(0)                   # gen=dc e8  dis=dc e8
    fdiv   %st,%st(0)                   # gen=dc f0  dis=dc f0
    fdivr  %st,%st(0)                   # gen=dc f8  dis=dc f8
    fmull  0x90909090                   # gen=dc 0d  dis=dc 0d 90 90 90 90
    fcoml  0x90909090                   # gen=dc 15  dis=dc 15 90 90 90 90
    fcompl 0x90909090                   # gen=dc 1d  dis=dc 1d 90 90 90 90
    fsubl  0x90909090                   # gen=dc 25  dis=dc 25 90 90 90 90
    fsubrl 0x90909090                   # gen=dc 2d  dis=dc 2d 90 90 90 90
    fdivl  0x90909090                   # gen=dc 35  dis=dc 35 90 90 90 90
    fdivrl 0x90909090                   # gen=dc 3d  dis=dc 3d 90 90 90 90
    ffree  %st(0)                       # gen=dd c0  dis=dd c0
    fldl   0x90909090                   # gen=dd 05  dis=dd 05 90 90 90 90
    fst    %st(0)                       # gen=dd d0  dis=dd d0
    fstp   %st(0)                       # gen=dd d8  dis=dd d8
    fucom  %st(0)                       # gen=dd e0  dis=dd e0
    fucomp %st(0)                       # gen=dd e8  dis=dd e8
    fisttpll 0x90909090                 # gen=dd 0d  dis=dd 0d 90 90 90 90
    fstl   0x90909090                   # gen=dd 15  dis=dd 15 90 90 90 90
    fstpl  0x90909090                   # gen=dd 1d  dis=dd 1d 90 90 90 90
    frstor 0x90909090                   # gen=dd 25  dis=dd 25 90 90 90 90
    fnsave 0x90909090                   # gen=dd 35  dis=dd 35 90 90 90 90
    fnstsw 0x90909090                   # gen=dd 3d  dis=dd 3d 90 90 90 90
    faddp  %st,%st(0)                   # gen=de c0  dis=de c0
    fiadds 0x90909090                   # gen=de 05  dis=de 05 90 90 90 90
    fmulp  %st,%st(0)                   # gen=de c8  dis=de c8
    fsubp  %st,%st(0)                   # gen=de e0  dis=de e0
    fsubrp %st,%st(0)                   # gen=de e8  dis=de e8
    fdivp  %st,%st(0)                   # gen=de f0  dis=de f0
    fdivrp %st,%st(0)                   # gen=de f8  dis=de f8
    fimuls 0x90909090                   # gen=de 0d  dis=de 0d 90 90 90 90
    ficoms 0x90909090                   # gen=de 15  dis=de 15 90 90 90 90
    ficomps 0x90909090                  # gen=de 1d  dis=de 1d 90 90 90 90
    fisubs 0x90909090                   # gen=de 25  dis=de 25 90 90 90 90
    fisubrs 0x90909090                  # gen=de 2d  dis=de 2d 90 90 90 90
    fidivs 0x90909090                   # gen=de 35  dis=de 35 90 90 90 90
    fidivrs 0x90909090                  # gen=de 3d  dis=de 3d 90 90 90 90
    ffreep %st(0)                       # gen=df c0  dis=df c0
    filds  0x90909090                   # gen=df 05  dis=df 05 90 90 90 90
    fnstsw %ax                          # gen=df e0  dis=df e0
    fucomip %st(0),%st                  # gen=df e8  dis=df e8
    fcomip %st(0),%st                   # gen=df f0  dis=df f0
    fisttps 0x90909090                  # gen=df 0d  dis=df 0d 90 90 90 90
    fists  0x90909090                   # gen=df 15  dis=df 15 90 90 90 90
    fistps 0x90909090                   # gen=df 1d  dis=df 1d 90 90 90 90
    fbld   0x90909090                   # gen=df 25  dis=df 25 90 90 90 90
    fildll 0x90909090                   # gen=df 2d  dis=df 2d 90 90 90 90
    fbstp  0x90909090                   # gen=df 35  dis=df 35 90 90 90 90
    fistpll 0x90909090                  # gen=df 3d  dis=df 3d 90 90 90 90
    in     $0xc0,%al                    # gen=e4 c0  dis=e4 c0
    in     $0x5,%al                     # gen=e4 05  dis=e4 05
    in     $0xc0,%eax                   # gen=e5 c0  dis=e5 c0
    in     $0x5,%eax                    # gen=e5 05  dis=e5 05
    out    %al,$0xc0                    # gen=e6 c0  dis=e6 c0
    out    %al,$0x5                     # gen=e6 05  dis=e6 05
    out    %eax,$0xc0                   # gen=e7 c0  dis=e7 c0
    out    %eax,$0x5                    # gen=e7 05  dis=e7 05
    call   0x9090c805                   # gen=e8 c0  dis=e8 c0 90 90 90
    call   0x9090c75a                   # gen=e8 05  dis=e8 05 90 90 90
    jmp    0x9090c825                   # gen=e9 c0  dis=e9 c0 90 90 90
    jmp    0x9090c77a                   # gen=e9 05  dis=e9 05 90 90 90
    ljmp   $0x9090,$0x909090c0          # gen=ea c0  dis=ea c0 90 90 90 90 90
    ljmp   $0x9090,$0x90909005          # gen=ea 05  dis=ea 05 90 90 90 90 90
    jmp    0x3762                       # gen=eb c0  dis=eb c0
    jmp    0x37b7                       # gen=eb 05  dis=eb 05
    in     (%dx),%al                    # gen=ec c0  dis=ec
    in     (%dx),%eax                   # gen=ed c0  dis=ed
    out    %al,(%dx)                    # gen=ee c0  dis=ee
    out    %eax,(%dx)                   # gen=ef c0  dis=ef
    int1                                # gen=f1 c0  dis=f1
    hlt                                 # gen=f4 c0  dis=f4
    cmc                                 # gen=f5 c0  dis=f5
    test   $0x90,%al                    # gen=f6 c0  dis=f6 c0 90
    testb  $0x90,0x90909090             # gen=f6 05  dis=f6 05 90 90 90 90 90
    not    %al                          # gen=f6 d0  dis=f6 d0
    neg    %al                          # gen=f6 d8  dis=f6 d8
    mul    %al                          # gen=f6 e0  dis=f6 e0
    imul   %al                          # gen=f6 e8  dis=f6 e8
    div    %al                          # gen=f6 f0  dis=f6 f0
    idiv   %al                          # gen=f6 f8  dis=f6 f8
    notb   0x90909090                   # gen=f6 15  dis=f6 15 90 90 90 90
    negb   0x90909090                   # gen=f6 1d  dis=f6 1d 90 90 90 90
    mulb   0x90909090                   # gen=f6 25  dis=f6 25 90 90 90 90
    imulb  0x90909090                   # gen=f6 2d  dis=f6 2d 90 90 90 90
    divb   0x90909090                   # gen=f6 35  dis=f6 35 90 90 90 90
    idivb  0x90909090                   # gen=f6 3d  dis=f6 3d 90 90 90 90
    test   $0x90909090,%eax             # gen=f7 c0  dis=f7 c0 90 90 90 90
    testl  $0x90909090,0x90909090       # gen=f7 05  dis=f7 05 90 90 90 90 90
    not    %eax                         # gen=f7 d0  dis=f7 d0
    neg    %eax                         # gen=f7 d8  dis=f7 d8
    mul    %eax                         # gen=f7 e0  dis=f7 e0
    imul   %eax                         # gen=f7 e8  dis=f7 e8
    div    %eax                         # gen=f7 f0  dis=f7 f0
    idiv   %eax                         # gen=f7 f8  dis=f7 f8
    notl   0x90909090                   # gen=f7 15  dis=f7 15 90 90 90 90
    negl   0x90909090                   # gen=f7 1d  dis=f7 1d 90 90 90 90
    mull   0x90909090                   # gen=f7 25  dis=f7 25 90 90 90 90
    imull  0x90909090                   # gen=f7 2d  dis=f7 2d 90 90 90 90
    divl   0x90909090                   # gen=f7 35  dis=f7 35 90 90 90 90
    idivl  0x90909090                   # gen=f7 3d  dis=f7 3d 90 90 90 90
    clc                                 # gen=f8 c0  dis=f8
    stc                                 # gen=f9 c0  dis=f9
    cli                                 # gen=fa c0  dis=fa
    sti                                 # gen=fb c0  dis=fb
    cld                                 # gen=fc c0  dis=fc
    std                                 # gen=fd c0  dis=fd
    inc    %al                          # gen=fe c0  dis=fe c0
    incb   0x90909090                   # gen=fe 05  dis=fe 05 90 90 90 90
    dec    %al                          # gen=fe c8  dis=fe c8
    decb   0x90909090                   # gen=fe 0d  dis=fe 0d 90 90 90 90
    incl   0x90909090                   # gen=ff 05  dis=ff 05 90 90 90 90
    call   *%eax                        # gen=ff d0  dis=ff d0
    jmp    *%eax                        # gen=ff e0  dis=ff e0
    decl   0x90909090                   # gen=ff 0d  dis=ff 0d 90 90 90 90
    call   *0x90909090                  # gen=ff 15  dis=ff 15 90 90 90 90
    lcall  *0x90909090                  # gen=ff 1d  dis=ff 1d 90 90 90 90
    jmp    *0x90909090                  # gen=ff 25  dis=ff 25 90 90 90 90
    ljmp   *0x90909090                  # gen=ff 2d  dis=ff 2d 90 90 90 90
    push   0x90909090                   # gen=ff 35  dis=ff 35 90 90 90 90
    sldt   %eax                         # gen=0f 00 c0  dis=0f 00 c0
    sldt   0x90909090                   # gen=0f 00 05  dis=0f 00 05 90 90 90 90
    enclv                               # gen=0f 01 c0  dis=0f 01 c0
    sgdtl  0x90909090                   # gen=0f 01 05  dis=0f 01 05 90 90 90 90
    monitor %eax,%ecx,%edx              # gen=0f 01 c8  dis=0f 01 c8
    xgetbv                              # gen=0f 01 d0  dis=0f 01 d0
    vmrun                               # gen=0f 01 d8  dis=0f 01 d8
    smsw   %eax                         # gen=0f 01 e0  dis=0f 01 e0
    serialize                           # gen=0f 01 e8  dis=0f 01 e8
    lmsw   %ax                          # gen=0f 01 f0  dis=0f 01 f0
    sidtl  0x90909090                   # gen=0f 01 0d  dis=0f 01 0d 90 90 90 90
    lgdtl  0x90909090                   # gen=0f 01 15  dis=0f 01 15 90 90 90 90
    lidtl  0x90909090                   # gen=0f 01 1d  dis=0f 01 1d 90 90 90 90
    smsw   0x90909090                   # gen=0f 01 25  dis=0f 01 25 90 90 90 90
    lmsw   0x90909090                   # gen=0f 01 35  dis=0f 01 35 90 90 90 90
    invlpg 0x90909090                   # gen=0f 01 3d  dis=0f 01 3d 90 90 90 90
    lar    %eax,%eax                    # gen=0f 02 c0  dis=0f 02 c0
    lar    0x90909090,%eax              # gen=0f 02 05  dis=0f 02 05 90 90 90 90
    lsl    %eax,%eax                    # gen=0f 03 c0  dis=0f 03 c0
    lsl    0x90909090,%eax              # gen=0f 03 05  dis=0f 03 05 90 90 90 90
    clts                                # gen=0f 06 c0  dis=0f 06
    sysret                              # gen=0f 07 c0  dis=0f 07
    invd                                # gen=0f 08 c0  dis=0f 08
    wbinvd                              # gen=0f 09 c0  dis=0f 09
    ud2                                 # gen=0f 0b c0  dis=0f 0b
    prefetch (bad)                      # gen=0f 0d c0  dis=0f
    prefetch 0x90909090                 # gen=0f 0d 05  dis=0f 0d 05 90 90 90 90
    femms                               # gen=0f 0e c0  dis=0f 0e
    pfcmpge %mm0,%mm0                   # gen=0f 0f c0  dis=0f 0f c0 90
    pfcmpge 0x90909090,%mm0             # gen=0f 0f 05  dis=0f 0f 05 90 90 90 90
    movups %xmm0,%xmm0                  # gen=0f 10 c0  dis=0f 10 c0
    movups 0x90909090,%xmm0             # gen=0f 10 05  dis=0f 10 05 90 90 90 90
    movups %xmm0,0x90909090             # gen=0f 11 05  dis=0f 11 05 90 90 90 90
    movhlps %xmm0,%xmm0                 # gen=0f 12 c0  dis=0f 12 c0
    movlps 0x90909090,%xmm0             # gen=0f 12 05  dis=0f 12 05 90 90 90 90
    movhlps %xmm0,%xmm1                 # gen=0f 12 c8  dis=0f 12 c8
    movhlps %xmm0,%xmm2                 # gen=0f 12 d0  dis=0f 12 d0
    movhlps %xmm0,%xmm3                 # gen=0f 12 d8  dis=0f 12 d8
    movhlps %xmm0,%xmm4                 # gen=0f 12 e0  dis=0f 12 e0
    movhlps %xmm0,%xmm5                 # gen=0f 12 e8  dis=0f 12 e8
    movhlps %xmm0,%xmm6                 # gen=0f 12 f0  dis=0f 12 f0
    movhlps %xmm0,%xmm7                 # gen=0f 12 f8  dis=0f 12 f8
    movlps 0x90909090,%xmm1             # gen=0f 12 0d  dis=0f 12 0d 90 90 90 90
    movlps 0x90909090,%xmm2             # gen=0f 12 15  dis=0f 12 15 90 90 90 90
    movlps 0x90909090,%xmm3             # gen=0f 12 1d  dis=0f 12 1d 90 90 90 90
    movlps 0x90909090,%xmm4             # gen=0f 12 25  dis=0f 12 25 90 90 90 90
    movlps 0x90909090,%xmm5             # gen=0f 12 2d  dis=0f 12 2d 90 90 90 90
    movlps 0x90909090,%xmm6             # gen=0f 12 35  dis=0f 12 35 90 90 90 90
    movlps 0x90909090,%xmm7             # gen=0f 12 3d  dis=0f 12 3d 90 90 90 90
    movlps %xmm0,0x90909090             # gen=0f 13 05  dis=0f 13 05 90 90 90 90
    unpcklps %xmm0,%xmm0                # gen=0f 14 c0  dis=0f 14 c0
    unpcklps 0x90909090,%xmm0           # gen=0f 14 05  dis=0f 14 05 90 90 90 90
    unpckhps %xmm0,%xmm0                # gen=0f 15 c0  dis=0f 15 c0
    unpckhps 0x90909090,%xmm0           # gen=0f 15 05  dis=0f 15 05 90 90 90 90
    movlhps %xmm0,%xmm0                 # gen=0f 16 c0  dis=0f 16 c0
    movhps 0x90909090,%xmm0             # gen=0f 16 05  dis=0f 16 05 90 90 90 90
    movlhps %xmm0,%xmm1                 # gen=0f 16 c8  dis=0f 16 c8
    movlhps %xmm0,%xmm2                 # gen=0f 16 d0  dis=0f 16 d0
    movlhps %xmm0,%xmm3                 # gen=0f 16 d8  dis=0f 16 d8
    movlhps %xmm0,%xmm4                 # gen=0f 16 e0  dis=0f 16 e0
    movlhps %xmm0,%xmm5                 # gen=0f 16 e8  dis=0f 16 e8
    movlhps %xmm0,%xmm6                 # gen=0f 16 f0  dis=0f 16 f0
    movlhps %xmm0,%xmm7                 # gen=0f 16 f8  dis=0f 16 f8
    movhps 0x90909090,%xmm1             # gen=0f 16 0d  dis=0f 16 0d 90 90 90 90
    movhps 0x90909090,%xmm2             # gen=0f 16 15  dis=0f 16 15 90 90 90 90
    movhps 0x90909090,%xmm3             # gen=0f 16 1d  dis=0f 16 1d 90 90 90 90
    movhps 0x90909090,%xmm4             # gen=0f 16 25  dis=0f 16 25 90 90 90 90
    movhps 0x90909090,%xmm5             # gen=0f 16 2d  dis=0f 16 2d 90 90 90 90
    movhps 0x90909090,%xmm6             # gen=0f 16 35  dis=0f 16 35 90 90 90 90
    movhps 0x90909090,%xmm7             # gen=0f 16 3d  dis=0f 16 3d 90 90 90 90
    movhps %xmm0,0x90909090             # gen=0f 17 05  dis=0f 17 05 90 90 90 90
    nop    %eax                         # gen=0f 18 c0  dis=0f 18 c0
    prefetchnta 0x90909090              # gen=0f 18 05  dis=0f 18 05 90 90 90 90
    prefetcht0 0x90909090               # gen=0f 18 0d  dis=0f 18 0d 90 90 90 90
    prefetcht1 0x90909090               # gen=0f 18 15  dis=0f 18 15 90 90 90 90
    prefetcht2 0x90909090               # gen=0f 18 1d  dis=0f 18 1d 90 90 90 90
    nopl   0x90909090                   # gen=0f 18 25  dis=0f 18 25 90 90 90 90
    bndldx 0x90909090,%bnd0             # gen=0f 1a 05  dis=0f 1a 05 90 90 90 90
    bndldx 0x90909090,%bnd1             # gen=0f 1a 0d  dis=0f 1a 0d 90 90 90 90
    bndldx 0x90909090,%bnd2             # gen=0f 1a 15  dis=0f 1a 15 90 90 90 90
    bndldx 0x90909090,%bnd3             # gen=0f 1a 1d  dis=0f 1a 1d 90 90 90 90
    bndstx %bnd0,0x90909090             # gen=0f 1b 05  dis=0f 1b 05 90 90 90 90
    bndstx %bnd1,0x90909090             # gen=0f 1b 0d  dis=0f 1b 0d 90 90 90 90
    bndstx %bnd2,0x90909090             # gen=0f 1b 15  dis=0f 1b 15 90 90 90 90
    bndstx %bnd3,0x90909090             # gen=0f 1b 1d  dis=0f 1b 1d 90 90 90 90
    cldemote 0x90909090                 # gen=0f 1c 05  dis=0f 1c 05 90 90 90 90
    mov    %cr0,%eax                    # gen=0f 20 c0  dis=0f 20 c0
    mov    %cr0,%ebp                    # gen=0f 20 05  dis=0f 20 05
    mov    %db0,%eax                    # gen=0f 21 c0  dis=0f 21 c0
    mov    %db0,%ebp                    # gen=0f 21 05  dis=0f 21 05
    mov    %eax,%cr0                    # gen=0f 22 c0  dis=0f 22 c0
    mov    %ebp,%cr0                    # gen=0f 22 05  dis=0f 22 05
    mov    %eax,%db0                    # gen=0f 23 c0  dis=0f 23 c0
    mov    %ebp,%db0                    # gen=0f 23 05  dis=0f 23 05
    mov    %tr0,%eax                    # gen=0f 24 c0  dis=0f 24 c0
    mov    %tr0,%ebp                    # gen=0f 24 05  dis=0f 24 05
    mov    %eax,%tr0                    # gen=0f 26 c0  dis=0f 26 c0
    mov    %ebp,%tr0                    # gen=0f 26 05  dis=0f 26 05
    movaps %xmm0,%xmm0                  # gen=0f 28 c0  dis=0f 28 c0
    movaps 0x90909090,%xmm0             # gen=0f 28 05  dis=0f 28 05 90 90 90 90
    movaps %xmm0,0x90909090             # gen=0f 29 05  dis=0f 29 05 90 90 90 90
    cvtpi2ps %mm0,%xmm0                 # gen=0f 2a c0  dis=0f 2a c0
    cvtpi2ps 0x90909090,%xmm0           # gen=0f 2a 05  dis=0f 2a 05 90 90 90 90
    movntps %xmm0,0x90909090            # gen=0f 2b 05  dis=0f 2b 05 90 90 90 90
    cvttps2pi %xmm0,%mm0                # gen=0f 2c c0  dis=0f 2c c0
    cvttps2pi 0x90909090,%mm0           # gen=0f 2c 05  dis=0f 2c 05 90 90 90 90
    cvtps2pi %xmm0,%mm0                 # gen=0f 2d c0  dis=0f 2d c0
    cvtps2pi 0x90909090,%mm0            # gen=0f 2d 05  dis=0f 2d 05 90 90 90 90
    ucomiss %xmm0,%xmm0                 # gen=0f 2e c0  dis=0f 2e c0
    ucomiss 0x90909090,%xmm0            # gen=0f 2e 05  dis=0f 2e 05 90 90 90 90
    comiss %xmm0,%xmm0                  # gen=0f 2f c0  dis=0f 2f c0
    comiss 0x90909090,%xmm0             # gen=0f 2f 05  dis=0f 2f 05 90 90 90 90
    wrmsr                               # gen=0f 30 c0  dis=0f 30
    rdtsc                               # gen=0f 31 c0  dis=0f 31
    rdmsr                               # gen=0f 32 c0  dis=0f 32
    rdpmc                               # gen=0f 33 c0  dis=0f 33
    sysenter                            # gen=0f 34 c0  dis=0f 34
    getsec                              # gen=0f 37 c0  dis=0f 37
    phsubw -0x6f6f6f70(%eax),%mm2       # gen=0f 38 05  dis=0f 38 05 90 90 90 90
    cmovo  %eax,%eax                    # gen=0f 40 c0  dis=0f 40 c0
    cmovo  0x90909090,%eax              # gen=0f 40 05  dis=0f 40 05 90 90 90 90
    cmovno %eax,%eax                    # gen=0f 41 c0  dis=0f 41 c0
    cmovno 0x90909090,%eax              # gen=0f 41 05  dis=0f 41 05 90 90 90 90
    cmovb  %eax,%eax                    # gen=0f 42 c0  dis=0f 42 c0
    cmovb  0x90909090,%eax              # gen=0f 42 05  dis=0f 42 05 90 90 90 90
    cmovae %eax,%eax                    # gen=0f 43 c0  dis=0f 43 c0
    cmovae 0x90909090,%eax              # gen=0f 43 05  dis=0f 43 05 90 90 90 90
    cmove  %eax,%eax                    # gen=0f 44 c0  dis=0f 44 c0
    cmove  0x90909090,%eax              # gen=0f 44 05  dis=0f 44 05 90 90 90 90
    cmovne %eax,%eax                    # gen=0f 45 c0  dis=0f 45 c0
    cmovne 0x90909090,%eax              # gen=0f 45 05  dis=0f 45 05 90 90 90 90
    cmovbe %eax,%eax                    # gen=0f 46 c0  dis=0f 46 c0
    cmovbe 0x90909090,%eax              # gen=0f 46 05  dis=0f 46 05 90 90 90 90
    cmova  %eax,%eax                    # gen=0f 47 c0  dis=0f 47 c0
    cmova  0x90909090,%eax              # gen=0f 47 05  dis=0f 47 05 90 90 90 90
    cmovs  %eax,%eax                    # gen=0f 48 c0  dis=0f 48 c0
    cmovs  0x90909090,%eax              # gen=0f 48 05  dis=0f 48 05 90 90 90 90
    cmovns %eax,%eax                    # gen=0f 49 c0  dis=0f 49 c0
    cmovns 0x90909090,%eax              # gen=0f 49 05  dis=0f 49 05 90 90 90 90
    cmovp  %eax,%eax                    # gen=0f 4a c0  dis=0f 4a c0
    cmovp  0x90909090,%eax              # gen=0f 4a 05  dis=0f 4a 05 90 90 90 90
    cmovnp %eax,%eax                    # gen=0f 4b c0  dis=0f 4b c0
    cmovnp 0x90909090,%eax              # gen=0f 4b 05  dis=0f 4b 05 90 90 90 90
    cmovl  %eax,%eax                    # gen=0f 4c c0  dis=0f 4c c0
    cmovl  0x90909090,%eax              # gen=0f 4c 05  dis=0f 4c 05 90 90 90 90
    cmovge %eax,%eax                    # gen=0f 4d c0  dis=0f 4d c0
    cmovge 0x90909090,%eax              # gen=0f 4d 05  dis=0f 4d 05 90 90 90 90
    cmovle %eax,%eax                    # gen=0f 4e c0  dis=0f 4e c0
    cmovle 0x90909090,%eax              # gen=0f 4e 05  dis=0f 4e 05 90 90 90 90
    cmovg  %eax,%eax                    # gen=0f 4f c0  dis=0f 4f c0
    cmovg  0x90909090,%eax              # gen=0f 4f 05  dis=0f 4f 05 90 90 90 90
    movmskps %xmm0,%eax                 # gen=0f 50 c0  dis=0f 50 c0
    sqrtps %xmm0,%xmm0                  # gen=0f 51 c0  dis=0f 51 c0
    sqrtps 0x90909090,%xmm0             # gen=0f 51 05  dis=0f 51 05 90 90 90 90
    rsqrtps %xmm0,%xmm0                 # gen=0f 52 c0  dis=0f 52 c0
    rsqrtps 0x90909090,%xmm0            # gen=0f 52 05  dis=0f 52 05 90 90 90 90
    rcpps  %xmm0,%xmm0                  # gen=0f 53 c0  dis=0f 53 c0
    rcpps  0x90909090,%xmm0             # gen=0f 53 05  dis=0f 53 05 90 90 90 90
    andps  %xmm0,%xmm0                  # gen=0f 54 c0  dis=0f 54 c0
    andps  0x90909090,%xmm0             # gen=0f 54 05  dis=0f 54 05 90 90 90 90
    andnps %xmm0,%xmm0                  # gen=0f 55 c0  dis=0f 55 c0
    andnps 0x90909090,%xmm0             # gen=0f 55 05  dis=0f 55 05 90 90 90 90
    orps   %xmm0,%xmm0                  # gen=0f 56 c0  dis=0f 56 c0
    orps   0x90909090,%xmm0             # gen=0f 56 05  dis=0f 56 05 90 90 90 90
    xorps  %xmm0,%xmm0                  # gen=0f 57 c0  dis=0f 57 c0
    xorps  0x90909090,%xmm0             # gen=0f 57 05  dis=0f 57 05 90 90 90 90
    addps  %xmm0,%xmm0                  # gen=0f 58 c0  dis=0f 58 c0
    addps  0x90909090,%xmm0             # gen=0f 58 05  dis=0f 58 05 90 90 90 90
    mulps  %xmm0,%xmm0                  # gen=0f 59 c0  dis=0f 59 c0
    mulps  0x90909090,%xmm0             # gen=0f 59 05  dis=0f 59 05 90 90 90 90
    cvtps2pd %xmm0,%xmm0                # gen=0f 5a c0  dis=0f 5a c0
    cvtps2pd 0x90909090,%xmm0           # gen=0f 5a 05  dis=0f 5a 05 90 90 90 90
    cvtdq2ps %xmm0,%xmm0                # gen=0f 5b c0  dis=0f 5b c0
    cvtdq2ps 0x90909090,%xmm0           # gen=0f 5b 05  dis=0f 5b 05 90 90 90 90
    subps  %xmm0,%xmm0                  # gen=0f 5c c0  dis=0f 5c c0
    subps  0x90909090,%xmm0             # gen=0f 5c 05  dis=0f 5c 05 90 90 90 90
    minps  %xmm0,%xmm0                  # gen=0f 5d c0  dis=0f 5d c0
    minps  0x90909090,%xmm0             # gen=0f 5d 05  dis=0f 5d 05 90 90 90 90
    divps  %xmm0,%xmm0                  # gen=0f 5e c0  dis=0f 5e c0
    divps  0x90909090,%xmm0             # gen=0f 5e 05  dis=0f 5e 05 90 90 90 90
    maxps  %xmm0,%xmm0                  # gen=0f 5f c0  dis=0f 5f c0
    maxps  0x90909090,%xmm0             # gen=0f 5f 05  dis=0f 5f 05 90 90 90 90
    punpcklbw %mm0,%mm0                 # gen=0f 60 c0  dis=0f 60 c0
    punpcklbw 0x90909090,%mm0           # gen=0f 60 05  dis=0f 60 05 90 90 90 90
    punpcklwd %mm0,%mm0                 # gen=0f 61 c0  dis=0f 61 c0
    punpcklwd 0x90909090,%mm0           # gen=0f 61 05  dis=0f 61 05 90 90 90 90
    punpckldq %mm0,%mm0                 # gen=0f 62 c0  dis=0f 62 c0
    punpckldq 0x90909090,%mm0           # gen=0f 62 05  dis=0f 62 05 90 90 90 90
    packsswb %mm0,%mm0                  # gen=0f 63 c0  dis=0f 63 c0
    packsswb 0x90909090,%mm0            # gen=0f 63 05  dis=0f 63 05 90 90 90 90
    pcmpgtb %mm0,%mm0                   # gen=0f 64 c0  dis=0f 64 c0
    pcmpgtb 0x90909090,%mm0             # gen=0f 64 05  dis=0f 64 05 90 90 90 90
    pcmpgtw %mm0,%mm0                   # gen=0f 65 c0  dis=0f 65 c0
    pcmpgtw 0x90909090,%mm0             # gen=0f 65 05  dis=0f 65 05 90 90 90 90
    pcmpgtd %mm0,%mm0                   # gen=0f 66 c0  dis=0f 66 c0
    pcmpgtd 0x90909090,%mm0             # gen=0f 66 05  dis=0f 66 05 90 90 90 90
    packuswb %mm0,%mm0                  # gen=0f 67 c0  dis=0f 67 c0
    packuswb 0x90909090,%mm0            # gen=0f 67 05  dis=0f 67 05 90 90 90 90
    punpckhbw %mm0,%mm0                 # gen=0f 68 c0  dis=0f 68 c0
    punpckhbw 0x90909090,%mm0           # gen=0f 68 05  dis=0f 68 05 90 90 90 90
    punpckhwd %mm0,%mm0                 # gen=0f 69 c0  dis=0f 69 c0
    punpckhwd 0x90909090,%mm0           # gen=0f 69 05  dis=0f 69 05 90 90 90 90
    punpckhdq %mm0,%mm0                 # gen=0f 6a c0  dis=0f 6a c0
    punpckhdq 0x90909090,%mm0           # gen=0f 6a 05  dis=0f 6a 05 90 90 90 90
    packssdw %mm0,%mm0                  # gen=0f 6b c0  dis=0f 6b c0
    packssdw 0x90909090,%mm0            # gen=0f 6b 05  dis=0f 6b 05 90 90 90 90
    movd   %eax,%mm0                    # gen=0f 6e c0  dis=0f 6e c0
    movd   0x90909090,%mm0              # gen=0f 6e 05  dis=0f 6e 05 90 90 90 90
    movq   %mm0,%mm0                    # gen=0f 6f c0  dis=0f 6f c0
    movq   0x90909090,%mm0              # gen=0f 6f 05  dis=0f 6f 05 90 90 90 90
    pshufw $0x90,%mm0,%mm0              # gen=0f 70 c0  dis=0f 70 c0 90
    pshufw $0x90,0x90909090,%mm0        # gen=0f 70 05  dis=0f 70 05 90 90 90 90
    pcmpeqb %mm0,%mm0                   # gen=0f 74 c0  dis=0f 74 c0
    pcmpeqb 0x90909090,%mm0             # gen=0f 74 05  dis=0f 74 05 90 90 90 90
    pcmpeqw %mm0,%mm0                   # gen=0f 75 c0  dis=0f 75 c0
    pcmpeqw 0x90909090,%mm0             # gen=0f 75 05  dis=0f 75 05 90 90 90 90
    pcmpeqd %mm0,%mm0                   # gen=0f 76 c0  dis=0f 76 c0
    pcmpeqd 0x90909090,%mm0             # gen=0f 76 05  dis=0f 76 05 90 90 90 90
    emms                                # gen=0f 77 c0  dis=0f 77
    vmread %eax,%eax                    # gen=0f 78 c0  dis=0f 78 c0
    vmread %eax,0x90909090              # gen=0f 78 05  dis=0f 78 05 90 90 90 90
    vmwrite %eax,%eax                   # gen=0f 79 c0  dis=0f 79 c0
    vmwrite 0x90909090,%eax             # gen=0f 79 05  dis=0f 79 05 90 90 90 90
    movd   %mm0,%eax                    # gen=0f 7e c0  dis=0f 7e c0
    movd   %mm0,0x90909090              # gen=0f 7e 05  dis=0f 7e 05 90 90 90 90
    movq   %mm0,0x90909090              # gen=0f 7f 05  dis=0f 7f 05 90 90 90 90
    jo     0x9090e826                   # gen=0f 80 c0  dis=0f 80 c0 90 90 90
    jo     0x9090e77b                   # gen=0f 80 05  dis=0f 80 05 90 90 90
    jno    0x9090e846                   # gen=0f 81 c0  dis=0f 81 c0 90 90 90
    jno    0x9090e79b                   # gen=0f 81 05  dis=0f 81 05 90 90 90
    jb     0x9090e866                   # gen=0f 82 c0  dis=0f 82 c0 90 90 90
    jb     0x9090e7bb                   # gen=0f 82 05  dis=0f 82 05 90 90 90
    jae    0x9090e886                   # gen=0f 83 c0  dis=0f 83 c0 90 90 90
    jae    0x9090e7db                   # gen=0f 83 05  dis=0f 83 05 90 90 90
    je     0x9090e8a6                   # gen=0f 84 c0  dis=0f 84 c0 90 90 90
    je     0x9090e7fb                   # gen=0f 84 05  dis=0f 84 05 90 90 90
    jne    0x9090e8c6                   # gen=0f 85 c0  dis=0f 85 c0 90 90 90
    jne    0x9090e81b                   # gen=0f 85 05  dis=0f 85 05 90 90 90
    jbe    0x9090e8e6                   # gen=0f 86 c0  dis=0f 86 c0 90 90 90
    jbe    0x9090e83b                   # gen=0f 86 05  dis=0f 86 05 90 90 90
    ja     0x9090e906                   # gen=0f 87 c0  dis=0f 87 c0 90 90 90
    ja     0x9090e85b                   # gen=0f 87 05  dis=0f 87 05 90 90 90
    js     0x9090e926                   # gen=0f 88 c0  dis=0f 88 c0 90 90 90
    js     0x9090e87b                   # gen=0f 88 05  dis=0f 88 05 90 90 90
    jns    0x9090e946                   # gen=0f 89 c0  dis=0f 89 c0 90 90 90
    jns    0x9090e89b                   # gen=0f 89 05  dis=0f 89 05 90 90 90
    jp     0x9090e966                   # gen=0f 8a c0  dis=0f 8a c0 90 90 90
    jp     0x9090e8bb                   # gen=0f 8a 05  dis=0f 8a 05 90 90 90
    jnp    0x9090e986                   # gen=0f 8b c0  dis=0f 8b c0 90 90 90
    jnp    0x9090e8db                   # gen=0f 8b 05  dis=0f 8b 05 90 90 90
    jl     0x9090e9a6                   # gen=0f 8c c0  dis=0f 8c c0 90 90 90
    jl     0x9090e8fb                   # gen=0f 8c 05  dis=0f 8c 05 90 90 90
    jge    0x9090e9c6                   # gen=0f 8d c0  dis=0f 8d c0 90 90 90
    jge    0x9090e91b                   # gen=0f 8d 05  dis=0f 8d 05 90 90 90
    jle    0x9090e9e6                   # gen=0f 8e c0  dis=0f 8e c0 90 90 90
    jle    0x9090e93b                   # gen=0f 8e 05  dis=0f 8e 05 90 90 90
    jg     0x9090ea06                   # gen=0f 8f c0  dis=0f 8f c0 90 90 90
    jg     0x9090e95b                   # gen=0f 8f 05  dis=0f 8f 05 90 90 90
    seto   %al                          # gen=0f 90 c0  dis=0f 90 c0
    seto   0x90909090                   # gen=0f 90 05  dis=0f 90 05 90 90 90 90
    setno  %al                          # gen=0f 91 c0  dis=0f 91 c0
    setno  0x90909090                   # gen=0f 91 05  dis=0f 91 05 90 90 90 90
    setb   %al                          # gen=0f 92 c0  dis=0f 92 c0
    setb   0x90909090                   # gen=0f 92 05  dis=0f 92 05 90 90 90 90
    setae  %al                          # gen=0f 93 c0  dis=0f 93 c0
    setae  0x90909090                   # gen=0f 93 05  dis=0f 93 05 90 90 90 90
    sete   %al                          # gen=0f 94 c0  dis=0f 94 c0
    sete   0x90909090                   # gen=0f 94 05  dis=0f 94 05 90 90 90 90
    setne  %al                          # gen=0f 95 c0  dis=0f 95 c0
    setne  0x90909090                   # gen=0f 95 05  dis=0f 95 05 90 90 90 90
    setbe  %al                          # gen=0f 96 c0  dis=0f 96 c0
    setbe  0x90909090                   # gen=0f 96 05  dis=0f 96 05 90 90 90 90
    seta   %al                          # gen=0f 97 c0  dis=0f 97 c0
    seta   0x90909090                   # gen=0f 97 05  dis=0f 97 05 90 90 90 90
    sets   %al                          # gen=0f 98 c0  dis=0f 98 c0
    sets   0x90909090                   # gen=0f 98 05  dis=0f 98 05 90 90 90 90
    setns  %al                          # gen=0f 99 c0  dis=0f 99 c0
    setns  0x90909090                   # gen=0f 99 05  dis=0f 99 05 90 90 90 90
    setp   %al                          # gen=0f 9a c0  dis=0f 9a c0
    setp   0x90909090                   # gen=0f 9a 05  dis=0f 9a 05 90 90 90 90
    setnp  %al                          # gen=0f 9b c0  dis=0f 9b c0
    setnp  0x90909090                   # gen=0f 9b 05  dis=0f 9b 05 90 90 90 90
    setl   %al                          # gen=0f 9c c0  dis=0f 9c c0
    setl   0x90909090                   # gen=0f 9c 05  dis=0f 9c 05 90 90 90 90
    setge  %al                          # gen=0f 9d c0  dis=0f 9d c0
    setge  0x90909090                   # gen=0f 9d 05  dis=0f 9d 05 90 90 90 90
    setle  %al                          # gen=0f 9e c0  dis=0f 9e c0
    setle  0x90909090                   # gen=0f 9e 05  dis=0f 9e 05 90 90 90 90
    setg   %al                          # gen=0f 9f c0  dis=0f 9f c0
    setg   0x90909090                   # gen=0f 9f 05  dis=0f 9f 05 90 90 90 90
    push   %fs                          # gen=0f a0 c0  dis=0f a0
    pop    %fs                          # gen=0f a1 c0  dis=0f a1
    cpuid                               # gen=0f a2 c0  dis=0f a2
    bt     %eax,%eax                    # gen=0f a3 c0  dis=0f a3 c0
    bt     %eax,0x90909090              # gen=0f a3 05  dis=0f a3 05 90 90 90 90
    shld   $0x90,%eax,%eax              # gen=0f a4 c0  dis=0f a4 c0 90
    shld   $0x90,%eax,0x90909090        # gen=0f a4 05  dis=0f a4 05 90 90 90 90
    shld   %cl,%eax,%eax                # gen=0f a5 c0  dis=0f a5 c0
    shld   %cl,%eax,0x90909090          # gen=0f a5 05  dis=0f a5 05 90 90 90 90
    montmul                             # gen=0f a6 c0  dis=0f a6 c0
    xstore-rng                          # gen=0f a7 c0  dis=0f a7 c0
    push   %gs                          # gen=0f a8 c0  dis=0f a8
    pop    %gs                          # gen=0f a9 c0  dis=0f a9
    rsm                                 # gen=0f aa c0  dis=0f aa
    bts    %eax,%eax                    # gen=0f ab c0  dis=0f ab c0
    bts    %eax,0x90909090              # gen=0f ab 05  dis=0f ab 05 90 90 90 90
    shrd   $0x90,%eax,%eax              # gen=0f ac c0  dis=0f ac c0 90
    shrd   $0x90,%eax,0x90909090        # gen=0f ac 05  dis=0f ac 05 90 90 90 90
    shrd   %cl,%eax,%eax                # gen=0f ad c0  dis=0f ad c0
    shrd   %cl,%eax,0x90909090          # gen=0f ad 05  dis=0f ad 05 90 90 90 90
    fxsave 0x90909090                   # gen=0f ae 05  dis=0f ae 05 90 90 90 90
    lfence                              # gen=0f ae e8  dis=0f ae e8
    mfence                              # gen=0f ae f0  dis=0f ae f0
    sfence                              # gen=0f ae f8  dis=0f ae f8
    fxrstor 0x90909090                  # gen=0f ae 0d  dis=0f ae 0d 90 90 90 90
    ldmxcsr 0x90909090                  # gen=0f ae 15  dis=0f ae 15 90 90 90 90
    stmxcsr 0x90909090                  # gen=0f ae 1d  dis=0f ae 1d 90 90 90 90
    xsave  0x90909090                   # gen=0f ae 25  dis=0f ae 25 90 90 90 90
    xrstor 0x90909090                   # gen=0f ae 2d  dis=0f ae 2d 90 90 90 90
    xsaveopt 0x90909090                 # gen=0f ae 35  dis=0f ae 35 90 90 90 90
    clflush 0x90909090                  # gen=0f ae 3d  dis=0f ae 3d 90 90 90 90
    imul   %eax,%eax                    # gen=0f af c0  dis=0f af c0
    imul   0x90909090,%eax              # gen=0f af 05  dis=0f af 05 90 90 90 90
    cmpxchg %al,%al                     # gen=0f b0 c0  dis=0f b0 c0
    cmpxchg %al,0x90909090              # gen=0f b0 05  dis=0f b0 05 90 90 90 90
    cmpxchg %eax,%eax                   # gen=0f b1 c0  dis=0f b1 c0
    cmpxchg %eax,0x90909090             # gen=0f b1 05  dis=0f b1 05 90 90 90 90
    lss    0x90909090,%eax              # gen=0f b2 05  dis=0f b2 05 90 90 90 90
    btr    %eax,%eax                    # gen=0f b3 c0  dis=0f b3 c0
    btr    %eax,0x90909090              # gen=0f b3 05  dis=0f b3 05 90 90 90 90
    lfs    0x90909090,%eax              # gen=0f b4 05  dis=0f b4 05 90 90 90 90
    lgs    0x90909090,%eax              # gen=0f b5 05  dis=0f b5 05 90 90 90 90
    movzbl %al,%eax                     # gen=0f b6 c0  dis=0f b6 c0
    movzbl 0x90909090,%eax              # gen=0f b6 05  dis=0f b6 05 90 90 90 90
    movzwl %ax,%eax                     # gen=0f b7 c0  dis=0f b7 c0
    movzwl 0x90909090,%eax              # gen=0f b7 05  dis=0f b7 05 90 90 90 90
    ud1    %eax,%eax                    # gen=0f b9 c0  dis=0f b9 c0
    ud1    0x90909090,%eax              # gen=0f b9 05  dis=0f b9 05 90 90 90 90
    bt     $0x90,%eax                   # gen=0f ba e0  dis=0f ba e0 90
    bts    $0x90,%eax                   # gen=0f ba e8  dis=0f ba e8 90
    btr    $0x90,%eax                   # gen=0f ba f0  dis=0f ba f0 90
    btc    $0x90,%eax                   # gen=0f ba f8  dis=0f ba f8 90
    btl    $0x90,0x90909090             # gen=0f ba 25  dis=0f ba 25 90 90 90 90
    btsl   $0x90,0x90909090             # gen=0f ba 2d  dis=0f ba 2d 90 90 90 90
    btrl   $0x90,0x90909090             # gen=0f ba 35  dis=0f ba 35 90 90 90 90
    btcl   $0x90,0x90909090             # gen=0f ba 3d  dis=0f ba 3d 90 90 90 90
    btc    %eax,%eax                    # gen=0f bb c0  dis=0f bb c0
    btc    %eax,0x90909090              # gen=0f bb 05  dis=0f bb 05 90 90 90 90
    bsf    %eax,%eax                    # gen=0f bc c0  dis=0f bc c0
    bsf    0x90909090,%eax              # gen=0f bc 05  dis=0f bc 05 90 90 90 90
    bsr    %eax,%eax                    # gen=0f bd c0  dis=0f bd c0
    bsr    0x90909090,%eax              # gen=0f bd 05  dis=0f bd 05 90 90 90 90
    movsbl %al,%eax                     # gen=0f be c0  dis=0f be c0
    movsbl 0x90909090,%eax              # gen=0f be 05  dis=0f be 05 90 90 90 90
    movswl %ax,%eax                     # gen=0f bf c0  dis=0f bf c0
    movswl 0x90909090,%eax              # gen=0f bf 05  dis=0f bf 05 90 90 90 90
    xadd   %al,%al                      # gen=0f c0 c0  dis=0f c0 c0
    xadd   %al,0x90909090               # gen=0f c0 05  dis=0f c0 05 90 90 90 90
    xadd   %eax,%eax                    # gen=0f c1 c0  dis=0f c1 c0
    xadd   %eax,0x90909090              # gen=0f c1 05  dis=0f c1 05 90 90 90 90
    cmpps  $0x90,%xmm0,%xmm0            # gen=0f c2 c0  dis=0f c2 c0 90
    cmpps  $0x90,0x90909090,%xmm0       # gen=0f c2 05  dis=0f c2 05 90 90 90 90
    movnti %eax,0x90909090              # gen=0f c3 05  dis=0f c3 05 90 90 90 90
    pinsrw $0x90,%eax,%mm0              # gen=0f c4 c0  dis=0f c4 c0 90
    pinsrw $0x90,0x90909090,%mm0        # gen=0f c4 05  dis=0f c4 05 90 90 90 90
    pextrw $0x90,%mm0,%eax              # gen=0f c5 c0  dis=0f c5 c0 90
    shufps $0x90,%xmm0,%xmm0            # gen=0f c6 c0  dis=0f c6 c0 90
    shufps $0x90,0x90909090,%xmm0       # gen=0f c6 05  dis=0f c6 05 90 90 90 90
    cmpxchg8b (bad)                     # gen=0f c7 c8  dis=0f
    rdrand %eax                         # gen=0f c7 f0  dis=0f c7 f0
    rdseed %eax                         # gen=0f c7 f8  dis=0f c7 f8
    cmpxchg8b 0x90909090                # gen=0f c7 0d  dis=0f c7 0d 90 90 90 90
    xrstors 0x90909090                  # gen=0f c7 1d  dis=0f c7 1d 90 90 90 90
    xsavec 0x90909090                   # gen=0f c7 25  dis=0f c7 25 90 90 90 90
    xsaves 0x90909090                   # gen=0f c7 2d  dis=0f c7 2d 90 90 90 90
    vmptrld 0x90909090                  # gen=0f c7 35  dis=0f c7 35 90 90 90 90
    vmptrst 0x90909090                  # gen=0f c7 3d  dis=0f c7 3d 90 90 90 90
    bswap  %ecx                         # gen=0f c9 c0  dis=0f c9
    bswap  %edx                         # gen=0f ca c0  dis=0f ca
    bswap  %ebx                         # gen=0f cb c0  dis=0f cb
    bswap  %esp                         # gen=0f cc c0  dis=0f cc
    bswap  %ebp                         # gen=0f cd c0  dis=0f cd
    bswap  %esi                         # gen=0f ce c0  dis=0f ce
    bswap  %edi                         # gen=0f cf c0  dis=0f cf
    psrlw  %mm0,%mm0                    # gen=0f d1 c0  dis=0f d1 c0
    psrlw  0x90909090,%mm0              # gen=0f d1 05  dis=0f d1 05 90 90 90 90
    psrld  %mm0,%mm0                    # gen=0f d2 c0  dis=0f d2 c0
    psrld  0x90909090,%mm0              # gen=0f d2 05  dis=0f d2 05 90 90 90 90
    psrlq  %mm0,%mm0                    # gen=0f d3 c0  dis=0f d3 c0
    psrlq  0x90909090,%mm0              # gen=0f d3 05  dis=0f d3 05 90 90 90 90
    paddq  %mm0,%mm0                    # gen=0f d4 c0  dis=0f d4 c0
    paddq  0x90909090,%mm0              # gen=0f d4 05  dis=0f d4 05 90 90 90 90
    pmullw %mm0,%mm0                    # gen=0f d5 c0  dis=0f d5 c0
    pmullw 0x90909090,%mm0              # gen=0f d5 05  dis=0f d5 05 90 90 90 90
    pmovmskb %mm0,%eax                  # gen=0f d7 c0  dis=0f d7 c0
    psubusb %mm0,%mm0                   # gen=0f d8 c0  dis=0f d8 c0
    psubusb 0x90909090,%mm0             # gen=0f d8 05  dis=0f d8 05 90 90 90 90
    psubusw %mm0,%mm0                   # gen=0f d9 c0  dis=0f d9 c0
    psubusw 0x90909090,%mm0             # gen=0f d9 05  dis=0f d9 05 90 90 90 90
    pminub %mm0,%mm0                    # gen=0f da c0  dis=0f da c0
    pminub 0x90909090,%mm0              # gen=0f da 05  dis=0f da 05 90 90 90 90
    pand   %mm0,%mm0                    # gen=0f db c0  dis=0f db c0
    pand   0x90909090,%mm0              # gen=0f db 05  dis=0f db 05 90 90 90 90
    paddusb %mm0,%mm0                   # gen=0f dc c0  dis=0f dc c0
    paddusb 0x90909090,%mm0             # gen=0f dc 05  dis=0f dc 05 90 90 90 90
    paddusw %mm0,%mm0                   # gen=0f dd c0  dis=0f dd c0
    paddusw 0x90909090,%mm0             # gen=0f dd 05  dis=0f dd 05 90 90 90 90
    pmaxub %mm0,%mm0                    # gen=0f de c0  dis=0f de c0
    pmaxub 0x90909090,%mm0              # gen=0f de 05  dis=0f de 05 90 90 90 90
    pandn  %mm0,%mm0                    # gen=0f df c0  dis=0f df c0
    pandn  0x90909090,%mm0              # gen=0f df 05  dis=0f df 05 90 90 90 90
    pavgb  %mm0,%mm0                    # gen=0f e0 c0  dis=0f e0 c0
    pavgb  0x90909090,%mm0              # gen=0f e0 05  dis=0f e0 05 90 90 90 90
    psraw  %mm0,%mm0                    # gen=0f e1 c0  dis=0f e1 c0
    psraw  0x90909090,%mm0              # gen=0f e1 05  dis=0f e1 05 90 90 90 90
    psrad  %mm0,%mm0                    # gen=0f e2 c0  dis=0f e2 c0
    psrad  0x90909090,%mm0              # gen=0f e2 05  dis=0f e2 05 90 90 90 90
    pavgw  %mm0,%mm0                    # gen=0f e3 c0  dis=0f e3 c0
    pavgw  0x90909090,%mm0              # gen=0f e3 05  dis=0f e3 05 90 90 90 90
    pmulhuw %mm0,%mm0                   # gen=0f e4 c0  dis=0f e4 c0
    pmulhuw 0x90909090,%mm0             # gen=0f e4 05  dis=0f e4 05 90 90 90 90
    pmulhw %mm0,%mm0                    # gen=0f e5 c0  dis=0f e5 c0
    pmulhw 0x90909090,%mm0              # gen=0f e5 05  dis=0f e5 05 90 90 90 90
    movntq %mm0,(bad)                   # gen=0f e7 c0  dis=0f
    movntq %mm0,0x90909090              # gen=0f e7 05  dis=0f e7 05 90 90 90 90
    psubsb %mm0,%mm0                    # gen=0f e8 c0  dis=0f e8 c0
    psubsb 0x90909090,%mm0              # gen=0f e8 05  dis=0f e8 05 90 90 90 90
    psubsw %mm0,%mm0                    # gen=0f e9 c0  dis=0f e9 c0
    psubsw 0x90909090,%mm0              # gen=0f e9 05  dis=0f e9 05 90 90 90 90
    pminsw %mm0,%mm0                    # gen=0f ea c0  dis=0f ea c0
    pminsw 0x90909090,%mm0              # gen=0f ea 05  dis=0f ea 05 90 90 90 90
    por    %mm0,%mm0                    # gen=0f eb c0  dis=0f eb c0
    por    0x90909090,%mm0              # gen=0f eb 05  dis=0f eb 05 90 90 90 90
    paddsb %mm0,%mm0                    # gen=0f ec c0  dis=0f ec c0
    paddsb 0x90909090,%mm0              # gen=0f ec 05  dis=0f ec 05 90 90 90 90
    paddsw %mm0,%mm0                    # gen=0f ed c0  dis=0f ed c0
    paddsw 0x90909090,%mm0              # gen=0f ed 05  dis=0f ed 05 90 90 90 90
    pmaxsw %mm0,%mm0                    # gen=0f ee c0  dis=0f ee c0
    pmaxsw 0x90909090,%mm0              # gen=0f ee 05  dis=0f ee 05 90 90 90 90
    pxor   %mm0,%mm0                    # gen=0f ef c0  dis=0f ef c0
    pxor   0x90909090,%mm0              # gen=0f ef 05  dis=0f ef 05 90 90 90 90
    psllw  %mm0,%mm0                    # gen=0f f1 c0  dis=0f f1 c0
    psllw  0x90909090,%mm0              # gen=0f f1 05  dis=0f f1 05 90 90 90 90
    pslld  %mm0,%mm0                    # gen=0f f2 c0  dis=0f f2 c0
    pslld  0x90909090,%mm0              # gen=0f f2 05  dis=0f f2 05 90 90 90 90
    psllq  %mm0,%mm0                    # gen=0f f3 c0  dis=0f f3 c0
    psllq  0x90909090,%mm0              # gen=0f f3 05  dis=0f f3 05 90 90 90 90
    pmuludq %mm0,%mm0                   # gen=0f f4 c0  dis=0f f4 c0
    pmuludq 0x90909090,%mm0             # gen=0f f4 05  dis=0f f4 05 90 90 90 90
    pmaddwd %mm0,%mm0                   # gen=0f f5 c0  dis=0f f5 c0
    pmaddwd 0x90909090,%mm0             # gen=0f f5 05  dis=0f f5 05 90 90 90 90
    psadbw %mm0,%mm0                    # gen=0f f6 c0  dis=0f f6 c0
    psadbw 0x90909090,%mm0              # gen=0f f6 05  dis=0f f6 05 90 90 90 90
    maskmovq %mm0,%mm0                  # gen=0f f7 c0  dis=0f f7 c0
    psubb  %mm0,%mm0                    # gen=0f f8 c0  dis=0f f8 c0
    psubb  0x90909090,%mm0              # gen=0f f8 05  dis=0f f8 05 90 90 90 90
    psubw  %mm0,%mm0                    # gen=0f f9 c0  dis=0f f9 c0
    psubw  0x90909090,%mm0              # gen=0f f9 05  dis=0f f9 05 90 90 90 90
    psubd  %mm0,%mm0                    # gen=0f fa c0  dis=0f fa c0
    psubd  0x90909090,%mm0              # gen=0f fa 05  dis=0f fa 05 90 90 90 90
    psubq  %mm0,%mm0                    # gen=0f fb c0  dis=0f fb c0
    psubq  0x90909090,%mm0              # gen=0f fb 05  dis=0f fb 05 90 90 90 90
    paddb  %mm0,%mm0                    # gen=0f fc c0  dis=0f fc c0
    paddb  0x90909090,%mm0              # gen=0f fc 05  dis=0f fc 05 90 90 90 90
    paddw  %mm0,%mm0                    # gen=0f fd c0  dis=0f fd c0
    paddw  0x90909090,%mm0              # gen=0f fd 05  dis=0f fd 05 90 90 90 90
    paddd  %mm0,%mm0                    # gen=0f fe c0  dis=0f fe c0
    paddd  0x90909090,%mm0              # gen=0f fe 05  dis=0f fe 05 90 90 90 90
    ud0    %eax,%eax                    # gen=0f ff c0  dis=0f ff c0
    ud0    0x90909090,%eax              # gen=0f ff 05  dis=0f ff 05 90 90 90 90
    pshufb %mm0,%mm0                    # gen=0f 38 00 c0  dis=0f 38 00 c0
    pshufb 0x90909090,%mm0              # gen=0f 38 00 05  dis=0f 38 00 05 90 90 90
    phaddw %mm0,%mm0                    # gen=0f 38 01 c0  dis=0f 38 01 c0
    phaddw 0x90909090,%mm0              # gen=0f 38 01 05  dis=0f 38 01 05 90 90 90
    phaddd %mm0,%mm0                    # gen=0f 38 02 c0  dis=0f 38 02 c0
    phaddd 0x90909090,%mm0              # gen=0f 38 02 05  dis=0f 38 02 05 90 90 90
    phaddsw %mm0,%mm0                   # gen=0f 38 03 c0  dis=0f 38 03 c0
    phaddsw 0x90909090,%mm0             # gen=0f 38 03 05  dis=0f 38 03 05 90 90 90
    pmaddubsw %mm0,%mm0                 # gen=0f 38 04 c0  dis=0f 38 04 c0
    pmaddubsw 0x90909090,%mm0           # gen=0f 38 04 05  dis=0f 38 04 05 90 90 90
    phsubw %mm0,%mm0                    # gen=0f 38 05 c0  dis=0f 38 05 c0
    phsubw 0x90909090,%mm0              # gen=0f 38 05 05  dis=0f 38 05 05 90 90 90
    phsubd %mm0,%mm0                    # gen=0f 38 06 c0  dis=0f 38 06 c0
    phsubd 0x90909090,%mm0              # gen=0f 38 06 05  dis=0f 38 06 05 90 90 90
    phsubsw %mm0,%mm0                   # gen=0f 38 07 c0  dis=0f 38 07 c0
    phsubsw 0x90909090,%mm0             # gen=0f 38 07 05  dis=0f 38 07 05 90 90 90
    psignb %mm0,%mm0                    # gen=0f 38 08 c0  dis=0f 38 08 c0
    psignb 0x90909090,%mm0              # gen=0f 38 08 05  dis=0f 38 08 05 90 90 90
    psignw %mm0,%mm0                    # gen=0f 38 09 c0  dis=0f 38 09 c0
    psignw 0x90909090,%mm0              # gen=0f 38 09 05  dis=0f 38 09 05 90 90 90
    psignd %mm0,%mm0                    # gen=0f 38 0a c0  dis=0f 38 0a c0
    psignd 0x90909090,%mm0              # gen=0f 38 0a 05  dis=0f 38 0a 05 90 90 90
    pmulhrsw %mm0,%mm0                  # gen=0f 38 0b c0  dis=0f 38 0b c0
    pmulhrsw 0x90909090,%mm0            # gen=0f 38 0b 05  dis=0f 38 0b 05 90 90 90
    pabsb  %mm0,%mm0                    # gen=0f 38 1c c0  dis=0f 38 1c c0
    pabsb  0x90909090,%mm0              # gen=0f 38 1c 05  dis=0f 38 1c 05 90 90 90
    pabsw  %mm0,%mm0                    # gen=0f 38 1d c0  dis=0f 38 1d c0
    pabsw  0x90909090,%mm0              # gen=0f 38 1d 05  dis=0f 38 1d 05 90 90 90
    pabsd  %mm0,%mm0                    # gen=0f 38 1e c0  dis=0f 38 1e c0
    pabsd  0x90909090,%mm0              # gen=0f 38 1e 05  dis=0f 38 1e 05 90 90 90
    sha1nexte %xmm0,%xmm0               # gen=0f 38 c8 c0  dis=0f 38 c8 c0
    sha1nexte 0x90909090,%xmm0          # gen=0f 38 c8 05  dis=0f 38 c8 05 90 90 90
    sha1msg1 %xmm0,%xmm0                # gen=0f 38 c9 c0  dis=0f 38 c9 c0
    sha1msg1 0x90909090,%xmm0           # gen=0f 38 c9 05  dis=0f 38 c9 05 90 90 90
    sha1msg2 %xmm0,%xmm0                # gen=0f 38 ca c0  dis=0f 38 ca c0
    sha1msg2 0x90909090,%xmm0           # gen=0f 38 ca 05  dis=0f 38 ca 05 90 90 90
    sha256rnds2 %xmm0,%xmm0,%xmm0       # gen=0f 38 cb c0  dis=0f 38 cb c0
    sha256rnds2 %xmm0,0x90909090,%xmm0  # gen=0f 38 cb 05  dis=0f 38 cb 05 90 90 90
    sha256msg1 %xmm0,%xmm0              # gen=0f 38 cc c0  dis=0f 38 cc c0
    sha256msg1 0x90909090,%xmm0         # gen=0f 38 cc 05  dis=0f 38 cc 05 90 90 90
    sha256msg2 %xmm0,%xmm0              # gen=0f 38 cd c0  dis=0f 38 cd c0
    sha256msg2 0x90909090,%xmm0         # gen=0f 38 cd 05  dis=0f 38 cd 05 90 90 90
    movbe  (bad),%eax                   # gen=0f 38 f0 c0  dis=0f
    movbe  0x90909090,%eax              # gen=0f 38 f0 05  dis=0f 38 f0 05 90 90 90
    movbe  %eax,(bad)                   # gen=0f 38 f1 c0  dis=0f
    movbe  %eax,0x90909090              # gen=0f 38 f1 05  dis=0f 38 f1 05 90 90 90
    wrssd  %eax,0x90909090              # gen=0f 38 f6 05  dis=0f 38 f6 05 90 90 90
    movdiri %eax,0x90909090             # gen=0f 38 f9 05  dis=0f 38 f9 05 90 90 90
    aadd   %eax,(bad)                   # gen=0f 38 fc c0  dis=0f
    aadd   %eax,0x90909090              # gen=0f 38 fc 05  dis=0f 38 fc 05 90 90 90
    palignr $0x90,%mm0,%mm0             # gen=0f 3a 0f c0  dis=0f 3a 0f c0 90
    palignr $0x90,0x90909090,%mm0       # gen=0f 3a 0f 05  dis=0f 3a 0f 05 90 90 90
    sha1rnds4 $0x90,%xmm0,%xmm0         # gen=0f 3a cc c0  dis=0f 3a cc c0 90
    sha1rnds4 $0x90,0x90909090,%xmm0    # gen=0f 3a cc 05  dis=0f 3a cc 05 90 90 90
    data16 add %al,%al                  # gen=66 00 c0  dis=66 00 c0
    data16 add %al,0x90909090           # gen=66 00 05  dis=66 00 05 90 90 90 90
    add    %ax,%ax                      # gen=66 01 c0  dis=66 01 c0
    add    %ax,0x90909090               # gen=66 01 05  dis=66 01 05 90 90 90 90
    data16 add 0x90909090,%al           # gen=66 02 05  dis=66 02 05 90 90 90 90
    add    0x90909090,%ax               # gen=66 03 05  dis=66 03 05 90 90 90 90
    data16 add $0xc0,%al                # gen=66 04 c0  dis=66 04 c0
    data16 add $0x5,%al                 # gen=66 04 05  dis=66 04 05
    add    $0x90c0,%ax                  # gen=66 05 c0  dis=66 05 c0 90
    add    $0x9005,%ax                  # gen=66 05 05  dis=66 05 05 90
    pushw  %es                          # gen=66 06 c0  dis=66 06
    popw   %es                          # gen=66 07 c0  dis=66 07
    data16 or %al,%al                   # gen=66 08 c0  dis=66 08 c0
    data16 or %al,0x90909090            # gen=66 08 05  dis=66 08 05 90 90 90 90
    or     %ax,%ax                      # gen=66 09 c0  dis=66 09 c0
    or     %ax,0x90909090               # gen=66 09 05  dis=66 09 05 90 90 90 90
    data16 or 0x90909090,%al            # gen=66 0a 05  dis=66 0a 05 90 90 90 90
    or     0x90909090,%ax               # gen=66 0b 05  dis=66 0b 05 90 90 90 90
    data16 or $0xc0,%al                 # gen=66 0c c0  dis=66 0c c0
    data16 or $0x5,%al                  # gen=66 0c 05  dis=66 0c 05
    or     $0x90c0,%ax                  # gen=66 0d c0  dis=66 0d c0 90
    or     $0x9005,%ax                  # gen=66 0d 05  dis=66 0d 05 90
    pushw  %cs                          # gen=66 0e c0  dis=66 0e
    data16 xadd %dl,-0x6f6f6f70(%eax)   # gen=66 0f c0  dis=66 0f c0 90 90 90 90
    data16 syscall                      # gen=66 0f 05  dis=66 0f 05
    addsubpd -0x6f6f6f70(%eax),%xmm2    # gen=66 0f d0  dis=66 0f d0 90 90 90 90
    psubusb -0x6f6f6f70(%eax),%xmm2     # gen=66 0f d8  dis=66 0f d8 90 90 90 90
    pavgb  -0x6f6f6f70(%eax),%xmm2      # gen=66 0f e0  dis=66 0f e0 90 90 90 90
    psubsb -0x6f6f6f70(%eax),%xmm2      # gen=66 0f e8  dis=66 0f e8 90 90 90 90
    psubb  -0x6f6f6f70(%eax),%xmm2      # gen=66 0f f8  dis=66 0f f8 90 90 90 90
    data16 prefetchwt1 -0x6f6f6f70(%eax) # gen=66 0f 0d  dis=66 0f 0d 90 90 90 90
    unpckhpd -0x6f6f6f70(%eax),%xmm2    # gen=66 0f 15  dis=66 0f 15 90 90 90 90
    nopw   -0x6f6f6f70(%eax)            # gen=66 0f 1d  dis=66 0f 1d 90 90 90 90
    cvtpd2pi -0x6f6f6f70(%eax),%mm2     # gen=66 0f 2d  dis=66 0f 2d 90 90 90 90
    data16 sysexit                      # gen=66 0f 35  dis=66 0f 35
    data16 adc %al,%al                  # gen=66 10 c0  dis=66 10 c0
    data16 adc %al,0x90909090           # gen=66 10 05  dis=66 10 05 90 90 90 90
    adc    %ax,%ax                      # gen=66 11 c0  dis=66 11 c0
    adc    %ax,0x90909090               # gen=66 11 05  dis=66 11 05 90 90 90 90
    data16 adc 0x90909090,%al           # gen=66 12 05  dis=66 12 05 90 90 90 90
    adc    0x90909090,%ax               # gen=66 13 05  dis=66 13 05 90 90 90 90
    data16 adc $0xc0,%al                # gen=66 14 c0  dis=66 14 c0
    data16 adc $0x5,%al                 # gen=66 14 05  dis=66 14 05
    adc    $0x90c0,%ax                  # gen=66 15 c0  dis=66 15 c0 90
    adc    $0x9005,%ax                  # gen=66 15 05  dis=66 15 05 90
    pushw  %ss                          # gen=66 16 c0  dis=66 16
    popw   %ss                          # gen=66 17 c0  dis=66 17
    data16 sbb %al,%al                  # gen=66 18 c0  dis=66 18 c0
    data16 sbb %al,0x90909090           # gen=66 18 05  dis=66 18 05 90 90 90 90
    sbb    %ax,%ax                      # gen=66 19 c0  dis=66 19 c0
    sbb    %ax,0x90909090               # gen=66 19 05  dis=66 19 05 90 90 90 90
    data16 sbb 0x90909090,%al           # gen=66 1a 05  dis=66 1a 05 90 90 90 90
    sbb    0x90909090,%ax               # gen=66 1b 05  dis=66 1b 05 90 90 90 90
    data16 sbb $0xc0,%al                # gen=66 1c c0  dis=66 1c c0
    data16 sbb $0x5,%al                 # gen=66 1c 05  dis=66 1c 05
    sbb    $0x90c0,%ax                  # gen=66 1d c0  dis=66 1d c0 90
    sbb    $0x9005,%ax                  # gen=66 1d 05  dis=66 1d 05 90
    pushw  %ds                          # gen=66 1e c0  dis=66 1e
    popw   %ds                          # gen=66 1f c0  dis=66 1f
    data16 and %al,%al                  # gen=66 20 c0  dis=66 20 c0
    data16 and %al,0x90909090           # gen=66 20 05  dis=66 20 05 90 90 90 90
    and    %ax,%ax                      # gen=66 21 c0  dis=66 21 c0
    and    %ax,0x90909090               # gen=66 21 05  dis=66 21 05 90 90 90 90
    data16 and 0x90909090,%al           # gen=66 22 05  dis=66 22 05 90 90 90 90
    and    0x90909090,%ax               # gen=66 23 05  dis=66 23 05 90 90 90 90
    data16 and $0xc0,%al                # gen=66 24 c0  dis=66 24 c0
    data16 and $0x5,%al                 # gen=66 24 05  dis=66 24 05
    and    $0x90c0,%ax                  # gen=66 25 c0  dis=66 25 c0 90
    and    $0x9005,%ax                  # gen=66 25 05  dis=66 25 05 90
    data16 rclb $0x90,%es:-0x6f6f6f70(%eax) # gen=66 26 c0  dis=66 26 c0 90 90 90 90
    es add $0x9090,%ax                  # gen=66 26 05  dis=66 26 05 90 90
    es enterw $0x9090,$0x90             # gen=66 26 c8  dis=66 26 c8 90 90 90
    data16 rclb %es:-0x6f6f6f70(%eax)   # gen=66 26 d0  dis=66 26 d0 90 90 90 90
    data16 fcoms %es:-0x6f6f6f70(%eax)  # gen=66 26 d8  dis=66 26 d8 90 90 90 90
    data16 es clc                       # gen=66 26 f8  dis=66 26 f8
    es or  $0x9090,%ax                  # gen=66 26 0d  dis=66 26 0d 90 90
    es adc $0x9090,%ax                  # gen=66 26 15  dis=66 26 15 90 90
    es sbb $0x9090,%ax                  # gen=66 26 1d  dis=66 26 1d 90 90
    es and $0x9090,%ax                  # gen=66 26 25  dis=66 26 25 90 90
    es sub $0x9090,%ax                  # gen=66 26 2d  dis=66 26 2d 90 90
    es xor $0x9090,%ax                  # gen=66 26 35  dis=66 26 35 90 90
    es cmp $0x9090,%ax                  # gen=66 26 3d  dis=66 26 3d 90 90
    data16 daa                          # gen=66 27 c0  dis=66 27
    data16 sub %al,%al                  # gen=66 28 c0  dis=66 28 c0
    data16 sub %al,0x90909090           # gen=66 28 05  dis=66 28 05 90 90 90 90
    sub    %ax,%ax                      # gen=66 29 c0  dis=66 29 c0
    sub    %ax,0x90909090               # gen=66 29 05  dis=66 29 05 90 90 90 90
    data16 sub 0x90909090,%al           # gen=66 2a 05  dis=66 2a 05 90 90 90 90
    sub    0x90909090,%ax               # gen=66 2b 05  dis=66 2b 05 90 90 90 90
    data16 sub $0xc0,%al                # gen=66 2c c0  dis=66 2c c0
    data16 sub $0x5,%al                 # gen=66 2c 05  dis=66 2c 05
    sub    $0x90c0,%ax                  # gen=66 2d c0  dis=66 2d c0 90
    sub    $0x9005,%ax                  # gen=66 2d 05  dis=66 2d 05 90
    data16 rclb $0x90,%cs:-0x6f6f6f70(%eax) # gen=66 2e c0  dis=66 2e c0 90 90 90 90
    cs add $0x9090,%ax                  # gen=66 2e 05  dis=66 2e 05 90 90
    cs enterw $0x9090,$0x90             # gen=66 2e c8  dis=66 2e c8 90 90 90
    data16 rclb %cs:-0x6f6f6f70(%eax)   # gen=66 2e d0  dis=66 2e d0 90 90 90 90
    data16 fcoms %cs:-0x6f6f6f70(%eax)  # gen=66 2e d8  dis=66 2e d8 90 90 90 90
    data16 cs clc                       # gen=66 2e f8  dis=66 2e f8
    cs or  $0x9090,%ax                  # gen=66 2e 0d  dis=66 2e 0d 90 90
    cs adc $0x9090,%ax                  # gen=66 2e 15  dis=66 2e 15 90 90
    cs sbb $0x9090,%ax                  # gen=66 2e 1d  dis=66 2e 1d 90 90
    cs and $0x9090,%ax                  # gen=66 2e 25  dis=66 2e 25 90 90
    cs sub $0x9090,%ax                  # gen=66 2e 2d  dis=66 2e 2d 90 90
    cs xor $0x9090,%ax                  # gen=66 2e 35  dis=66 2e 35 90 90
    cs cmp $0x9090,%ax                  # gen=66 2e 3d  dis=66 2e 3d 90 90
    data16 das                          # gen=66 2f c0  dis=66 2f
    data16 xor %al,%al                  # gen=66 30 c0  dis=66 30 c0
    data16 xor %al,0x90909090           # gen=66 30 05  dis=66 30 05 90 90 90 90
    xor    %ax,%ax                      # gen=66 31 c0  dis=66 31 c0
    xor    %ax,0x90909090               # gen=66 31 05  dis=66 31 05 90 90 90 90
    data16 xor 0x90909090,%al           # gen=66 32 05  dis=66 32 05 90 90 90 90
    xor    0x90909090,%ax               # gen=66 33 05  dis=66 33 05 90 90 90 90
    data16 xor $0xc0,%al                # gen=66 34 c0  dis=66 34 c0
    data16 xor $0x5,%al                 # gen=66 34 05  dis=66 34 05
    xor    $0x90c0,%ax                  # gen=66 35 c0  dis=66 35 c0 90
    xor    $0x9005,%ax                  # gen=66 35 05  dis=66 35 05 90
    data16 rclb $0x90,%ss:-0x6f6f6f70(%eax) # gen=66 36 c0  dis=66 36 c0 90 90 90 90
    ss add $0x9090,%ax                  # gen=66 36 05  dis=66 36 05 90 90
    ss enterw $0x9090,$0x90             # gen=66 36 c8  dis=66 36 c8 90 90 90
    data16 rclb %ss:-0x6f6f6f70(%eax)   # gen=66 36 d0  dis=66 36 d0 90 90 90 90
    data16 fcoms %ss:-0x6f6f6f70(%eax)  # gen=66 36 d8  dis=66 36 d8 90 90 90 90
    data16 ss clc                       # gen=66 36 f8  dis=66 36 f8
    ss or  $0x9090,%ax                  # gen=66 36 0d  dis=66 36 0d 90 90
    ss adc $0x9090,%ax                  # gen=66 36 15  dis=66 36 15 90 90
    ss sbb $0x9090,%ax                  # gen=66 36 1d  dis=66 36 1d 90 90
    ss and $0x9090,%ax                  # gen=66 36 25  dis=66 36 25 90 90
    ss sub $0x9090,%ax                  # gen=66 36 2d  dis=66 36 2d 90 90
    ss xor $0x9090,%ax                  # gen=66 36 35  dis=66 36 35 90 90
    ss cmp $0x9090,%ax                  # gen=66 36 3d  dis=66 36 3d 90 90
    data16 aaa                          # gen=66 37 c0  dis=66 37
    data16 cmp %al,%al                  # gen=66 38 c0  dis=66 38 c0
    data16 cmp %al,0x90909090           # gen=66 38 05  dis=66 38 05 90 90 90 90
    cmp    %ax,%ax                      # gen=66 39 c0  dis=66 39 c0
    cmp    %ax,0x90909090               # gen=66 39 05  dis=66 39 05 90 90 90 90
    data16 cmp 0x90909090,%al           # gen=66 3a 05  dis=66 3a 05 90 90 90 90
    cmp    0x90909090,%ax               # gen=66 3b 05  dis=66 3b 05 90 90 90 90
    data16 cmp $0xc0,%al                # gen=66 3c c0  dis=66 3c c0
    data16 cmp $0x5,%al                 # gen=66 3c 05  dis=66 3c 05
    cmp    $0x90c0,%ax                  # gen=66 3d c0  dis=66 3d c0 90
    cmp    $0x9005,%ax                  # gen=66 3d 05  dis=66 3d 05 90
    data16 rclb $0x90,%ds:-0x6f6f6f70(%eax) # gen=66 3e c0  dis=66 3e c0 90 90 90 90
    ds add $0x9090,%ax                  # gen=66 3e 05  dis=66 3e 05 90 90
    ds enterw $0x9090,$0x90             # gen=66 3e c8  dis=66 3e c8 90 90 90
    data16 rclb %ds:-0x6f6f6f70(%eax)   # gen=66 3e d0  dis=66 3e d0 90 90 90 90
    data16 fcoms %ds:-0x6f6f6f70(%eax)  # gen=66 3e d8  dis=66 3e d8 90 90 90 90
    data16 ds clc                       # gen=66 3e f8  dis=66 3e f8
    ds or  $0x9090,%ax                  # gen=66 3e 0d  dis=66 3e 0d 90 90
    ds adc $0x9090,%ax                  # gen=66 3e 15  dis=66 3e 15 90 90
    ds sbb $0x9090,%ax                  # gen=66 3e 1d  dis=66 3e 1d 90 90
    ds and $0x9090,%ax                  # gen=66 3e 25  dis=66 3e 25 90 90
    ds sub $0x9090,%ax                  # gen=66 3e 2d  dis=66 3e 2d 90 90
    ds xor $0x9090,%ax                  # gen=66 3e 35  dis=66 3e 35 90 90
    ds cmp $0x9090,%ax                  # gen=66 3e 3d  dis=66 3e 3d 90 90
    data16 aas                          # gen=66 3f c0  dis=66 3f
    inc    %ax                          # gen=66 40 c0  dis=66 40
    inc    %cx                          # gen=66 41 c0  dis=66 41
    inc    %dx                          # gen=66 42 c0  dis=66 42
    inc    %bx                          # gen=66 43 c0  dis=66 43
    inc    %sp                          # gen=66 44 c0  dis=66 44
    inc    %bp                          # gen=66 45 c0  dis=66 45
    inc    %si                          # gen=66 46 c0  dis=66 46
    inc    %di                          # gen=66 47 c0  dis=66 47
    dec    %ax                          # gen=66 48 c0  dis=66 48
    dec    %cx                          # gen=66 49 c0  dis=66 49
    dec    %dx                          # gen=66 4a c0  dis=66 4a
    dec    %bx                          # gen=66 4b c0  dis=66 4b
    dec    %sp                          # gen=66 4c c0  dis=66 4c
    dec    %bp                          # gen=66 4d c0  dis=66 4d
    dec    %si                          # gen=66 4e c0  dis=66 4e
    dec    %di                          # gen=66 4f c0  dis=66 4f
    push   %ax                          # gen=66 50 c0  dis=66 50
    push   %cx                          # gen=66 51 c0  dis=66 51
    push   %dx                          # gen=66 52 c0  dis=66 52
    push   %bx                          # gen=66 53 c0  dis=66 53
    push   %sp                          # gen=66 54 c0  dis=66 54
    push   %bp                          # gen=66 55 c0  dis=66 55
    push   %si                          # gen=66 56 c0  dis=66 56
    push   %di                          # gen=66 57 c0  dis=66 57
    pop    %ax                          # gen=66 58 c0  dis=66 58
    pop    %cx                          # gen=66 59 c0  dis=66 59
    pop    %dx                          # gen=66 5a c0  dis=66 5a
    pop    %bx                          # gen=66 5b c0  dis=66 5b
    pop    %sp                          # gen=66 5c c0  dis=66 5c
    pop    %bp                          # gen=66 5d c0  dis=66 5d
    pop    %si                          # gen=66 5e c0  dis=66 5e
    pop    %di                          # gen=66 5f c0  dis=66 5f
    pushaw                              # gen=66 60 c0  dis=66 60
    popaw                               # gen=66 61 c0  dis=66 61
    bound  %ax,0x90909090               # gen=66 62 05  dis=66 62 05 90 90 90 90
    bound  %cx,0x90909090               # gen=66 62 0d  dis=66 62 0d 90 90 90 90
    bound  %dx,0x90909090               # gen=66 62 15  dis=66 62 15 90 90 90 90
    bound  %bx,0x90909090               # gen=66 62 1d  dis=66 62 1d 90 90 90 90
    bound  %sp,0x90909090               # gen=66 62 25  dis=66 62 25 90 90 90 90
    bound  %bp,0x90909090               # gen=66 62 2d  dis=66 62 2d 90 90 90 90
    bound  %si,0x90909090               # gen=66 62 35  dis=66 62 35 90 90 90 90
    bound  %di,0x90909090               # gen=66 62 3d  dis=66 62 3d 90 90 90 90
    data16 arpl %ax,%ax                 # gen=66 63 c0  dis=66 63 c0
    data16 arpl %ax,0x90909090          # gen=66 63 05  dis=66 63 05 90 90 90 90
    data16 rclb $0x90,%fs:-0x6f6f6f70(%eax) # gen=66 64 c0  dis=66 64 c0 90 90 90 90
    fs add $0x9090,%ax                  # gen=66 64 05  dis=66 64 05 90 90
    fs enterw $0x9090,$0x90             # gen=66 64 c8  dis=66 64 c8 90 90 90
    data16 rclb %fs:-0x6f6f6f70(%eax)   # gen=66 64 d0  dis=66 64 d0 90 90 90 90
    data16 fcoms %fs:-0x6f6f6f70(%eax)  # gen=66 64 d8  dis=66 64 d8 90 90 90 90
    data16 fs clc                       # gen=66 64 f8  dis=66 64 f8
    fs or  $0x9090,%ax                  # gen=66 64 0d  dis=66 64 0d 90 90
    fs adc $0x9090,%ax                  # gen=66 64 15  dis=66 64 15 90 90
    fs sbb $0x9090,%ax                  # gen=66 64 1d  dis=66 64 1d 90 90
    fs and $0x9090,%ax                  # gen=66 64 25  dis=66 64 25 90 90
    fs sub $0x9090,%ax                  # gen=66 64 2d  dis=66 64 2d 90 90
    fs xor $0x9090,%ax                  # gen=66 64 35  dis=66 64 35 90 90
    fs cmp $0x9090,%ax                  # gen=66 64 3d  dis=66 64 3d 90 90
    data16 rclb $0x90,%gs:-0x6f6f6f70(%eax) # gen=66 65 c0  dis=66 65 c0 90 90 90 90
    gs add $0x9090,%ax                  # gen=66 65 05  dis=66 65 05 90 90
    gs enterw $0x9090,$0x90             # gen=66 65 c8  dis=66 65 c8 90 90 90
    data16 rclb %gs:-0x6f6f6f70(%eax)   # gen=66 65 d0  dis=66 65 d0 90 90 90 90
    data16 fcoms %gs:-0x6f6f6f70(%eax)  # gen=66 65 d8  dis=66 65 d8 90 90 90 90
    data16 gs clc                       # gen=66 65 f8  dis=66 65 f8
    gs or  $0x9090,%ax                  # gen=66 65 0d  dis=66 65 0d 90 90
    gs adc $0x9090,%ax                  # gen=66 65 15  dis=66 65 15 90 90
    gs sbb $0x9090,%ax                  # gen=66 65 1d  dis=66 65 1d 90 90
    gs and $0x9090,%ax                  # gen=66 65 25  dis=66 65 25 90 90
    gs sub $0x9090,%ax                  # gen=66 65 2d  dis=66 65 2d 90 90
    gs xor $0x9090,%ax                  # gen=66 65 35  dis=66 65 35 90 90
    gs cmp $0x9090,%ax                  # gen=66 65 3d  dis=66 65 3d 90 90
    data16 rclb $0x90,-0x6f70(%bx,%si)  # gen=66 67 c0  dis=66 67 c0 90 90 90 90
    addr16 add $0x9090,%ax              # gen=66 67 05  dis=66 67 05 90 90
    addr16 enterw $0x9090,$0x90         # gen=66 67 c8  dis=66 67 c8 90 90 90
    data16 rclb -0x6f70(%bx,%si)        # gen=66 67 d0  dis=66 67 d0 90 90 90
    data16 fcoms -0x6f70(%bx,%si)       # gen=66 67 d8  dis=66 67 d8 90 90 90
    data16 addr16 clc                   # gen=66 67 f8  dis=66 67 f8
    addr16 or $0x9090,%ax               # gen=66 67 0d  dis=66 67 0d 90 90
    addr16 adc $0x9090,%ax              # gen=66 67 15  dis=66 67 15 90 90
    addr16 sbb $0x9090,%ax              # gen=66 67 1d  dis=66 67 1d 90 90
    addr16 and $0x9090,%ax              # gen=66 67 25  dis=66 67 25 90 90
    addr16 sub $0x9090,%ax              # gen=66 67 2d  dis=66 67 2d 90 90
    addr16 xor $0x9090,%ax              # gen=66 67 35  dis=66 67 35 90 90
    addr16 cmp $0x9090,%ax              # gen=66 67 3d  dis=66 67 3d 90 90
    pushw  $0x90c0                      # gen=66 68 c0  dis=66 68 c0 90
    pushw  $0x9005                      # gen=66 68 05  dis=66 68 05 90
    imul   $0x9090,%ax,%ax              # gen=66 69 c0  dis=66 69 c0 90 90
    imul   $0x9090,0x90909090,%ax       # gen=66 69 05  dis=66 69 05 90 90 90 90
    pushw  $0xffc0                      # gen=66 6a c0  dis=66 6a c0
    pushw  $0x5                         # gen=66 6a 05  dis=66 6a 05
    imul   $0xff90,%ax,%ax              # gen=66 6b c0  dis=66 6b c0 90
    imul   $0xff90,0x90909090,%ax       # gen=66 6b 05  dis=66 6b 05 90 90 90 90
    data16 insb (%dx),%es:(%edi)        # gen=66 6c c0  dis=66 6c
    insw   (%dx),%es:(%edi)             # gen=66 6d c0  dis=66 6d
    data16 outsb %ds:(%esi),(%dx)       # gen=66 6e c0  dis=66 6e
    outsw  %ds:(%esi),(%dx)             # gen=66 6f c0  dis=66 6f
    data16 jo 0xbfa3                    # gen=66 70 c0  dis=66 70 c0
    data16 jo 0xbff8                    # gen=66 70 05  dis=66 70 05
    data16 jno 0xbfc3                   # gen=66 71 c0  dis=66 71 c0
    data16 jno 0xc018                   # gen=66 71 05  dis=66 71 05
    data16 jb 0xbfe3                    # gen=66 72 c0  dis=66 72 c0
    data16 jb 0xc038                    # gen=66 72 05  dis=66 72 05
    data16 jae 0xc003                   # gen=66 73 c0  dis=66 73 c0
    data16 jae 0xc058                   # gen=66 73 05  dis=66 73 05
    data16 je 0xc023                    # gen=66 74 c0  dis=66 74 c0
    data16 je 0xc078                    # gen=66 74 05  dis=66 74 05
    data16 jne 0xc043                   # gen=66 75 c0  dis=66 75 c0
    data16 jne 0xc098                   # gen=66 75 05  dis=66 75 05
    data16 jbe 0xc063                   # gen=66 76 c0  dis=66 76 c0
    data16 jbe 0xc0b8                   # gen=66 76 05  dis=66 76 05
    data16 ja 0xc083                    # gen=66 77 c0  dis=66 77 c0
    data16 ja 0xc0d8                    # gen=66 77 05  dis=66 77 05
    data16 js 0xc0a3                    # gen=66 78 c0  dis=66 78 c0
    data16 js 0xc0f8                    # gen=66 78 05  dis=66 78 05
    data16 jns 0xc0c3                   # gen=66 79 c0  dis=66 79 c0
    data16 jns 0xc118                   # gen=66 79 05  dis=66 79 05
    data16 jp 0xc0e3                    # gen=66 7a c0  dis=66 7a c0
    data16 jp 0xc138                    # gen=66 7a 05  dis=66 7a 05
    data16 jnp 0xc103                   # gen=66 7b c0  dis=66 7b c0
    data16 jnp 0xc158                   # gen=66 7b 05  dis=66 7b 05
    data16 jl 0xc123                    # gen=66 7c c0  dis=66 7c c0
    data16 jl 0xc178                    # gen=66 7c 05  dis=66 7c 05
    data16 jge 0xc143                   # gen=66 7d c0  dis=66 7d c0
    data16 jge 0xc198                   # gen=66 7d 05  dis=66 7d 05
    data16 jle 0xc163                   # gen=66 7e c0  dis=66 7e c0
    data16 jle 0xc1b8                   # gen=66 7e 05  dis=66 7e 05
    data16 jg 0xc183                    # gen=66 7f c0  dis=66 7f c0
    data16 jg 0xc1d8                    # gen=66 7f 05  dis=66 7f 05
    data16 add $0x90,%al                # gen=66 80 c0  dis=66 80 c0 90
    data16 addb $0x90,0x90909090        # gen=66 80 05  dis=66 80 05 90 90 90 90
    addw   $0x9090,0x90909090           # gen=66 81 05  dis=66 81 05 90 90 90 90
    orw    $0x9090,0x90909090           # gen=66 81 0d  dis=66 81 0d 90 90 90 90
    adcw   $0x9090,0x90909090           # gen=66 81 15  dis=66 81 15 90 90 90 90
    sbbw   $0x9090,0x90909090           # gen=66 81 1d  dis=66 81 1d 90 90 90 90
    andw   $0x9090,0x90909090           # gen=66 81 25  dis=66 81 25 90 90 90 90
    subw   $0x9090,0x90909090           # gen=66 81 2d  dis=66 81 2d 90 90 90 90
    xorw   $0x9090,0x90909090           # gen=66 81 35  dis=66 81 35 90 90 90 90
    cmpw   $0x9090,0x90909090           # gen=66 81 3d  dis=66 81 3d 90 90 90 90
    add    $0xff90,%ax                  # gen=66 83 c0  dis=66 83 c0 90
    addw   $0xff90,0x90909090           # gen=66 83 05  dis=66 83 05 90 90 90 90
    or     $0xff90,%ax                  # gen=66 83 c8  dis=66 83 c8 90
    adc    $0xff90,%ax                  # gen=66 83 d0  dis=66 83 d0 90
    sbb    $0xff90,%ax                  # gen=66 83 d8  dis=66 83 d8 90
    and    $0xff90,%ax                  # gen=66 83 e0  dis=66 83 e0 90
    sub    $0xff90,%ax                  # gen=66 83 e8  dis=66 83 e8 90
    xor    $0xff90,%ax                  # gen=66 83 f0  dis=66 83 f0 90
    cmp    $0xff90,%ax                  # gen=66 83 f8  dis=66 83 f8 90
    orw    $0xff90,0x90909090           # gen=66 83 0d  dis=66 83 0d 90 90 90 90
    adcw   $0xff90,0x90909090           # gen=66 83 15  dis=66 83 15 90 90 90 90
    sbbw   $0xff90,0x90909090           # gen=66 83 1d  dis=66 83 1d 90 90 90 90
    andw   $0xff90,0x90909090           # gen=66 83 25  dis=66 83 25 90 90 90 90
    subw   $0xff90,0x90909090           # gen=66 83 2d  dis=66 83 2d 90 90 90 90
    xorw   $0xff90,0x90909090           # gen=66 83 35  dis=66 83 35 90 90 90 90
    cmpw   $0xff90,0x90909090           # gen=66 83 3d  dis=66 83 3d 90 90 90 90
    data16 test %al,%al                 # gen=66 84 c0  dis=66 84 c0
    data16 test %al,0x90909090          # gen=66 84 05  dis=66 84 05 90 90 90 90
    test   %ax,%ax                      # gen=66 85 c0  dis=66 85 c0
    test   %ax,0x90909090               # gen=66 85 05  dis=66 85 05 90 90 90 90
    data16 xchg %al,%al                 # gen=66 86 c0  dis=66 86 c0
    data16 xchg %al,0x90909090          # gen=66 86 05  dis=66 86 05 90 90 90 90
    xchg   %ax,%ax                      # gen=66 87 c0  dis=66 87 c0
    xchg   %ax,0x90909090               # gen=66 87 05  dis=66 87 05 90 90 90 90
    data16 mov %al,%al                  # gen=66 88 c0  dis=66 88 c0
    data16 mov %al,0x90909090           # gen=66 88 05  dis=66 88 05 90 90 90 90
    mov    %ax,%ax                      # gen=66 89 c0  dis=66 89 c0
    mov    %ax,0x90909090               # gen=66 89 05  dis=66 89 05 90 90 90 90
    data16 mov 0x90909090,%al           # gen=66 8a 05  dis=66 8a 05 90 90 90 90
    mov    0x90909090,%ax               # gen=66 8b 05  dis=66 8b 05 90 90 90 90
    mov    %es,%ax                      # gen=66 8c c0  dis=66 8c c0
    data16 mov %es,0x90909090           # gen=66 8c 05  dis=66 8c 05 90 90 90 90
    mov    %cs,%ax                      # gen=66 8c c8  dis=66 8c c8
    mov    %ss,%ax                      # gen=66 8c d0  dis=66 8c d0
    mov    %ds,%ax                      # gen=66 8c d8  dis=66 8c d8
    mov    %fs,%ax                      # gen=66 8c e0  dis=66 8c e0
    mov    %gs,%ax                      # gen=66 8c e8  dis=66 8c e8
    data16 mov %cs,0x90909090           # gen=66 8c 0d  dis=66 8c 0d 90 90 90 90
    data16 mov %ss,0x90909090           # gen=66 8c 15  dis=66 8c 15 90 90 90 90
    data16 mov %ds,0x90909090           # gen=66 8c 1d  dis=66 8c 1d 90 90 90 90
    data16 mov %fs,0x90909090           # gen=66 8c 25  dis=66 8c 25 90 90 90 90
    data16 mov %gs,0x90909090           # gen=66 8c 2d  dis=66 8c 2d 90 90 90 90
    lea    0x90909090,%ax               # gen=66 8d 05  dis=66 8d 05 90 90 90 90
    lea    0x90909090,%cx               # gen=66 8d 0d  dis=66 8d 0d 90 90 90 90
    lea    0x90909090,%dx               # gen=66 8d 15  dis=66 8d 15 90 90 90 90
    lea    0x90909090,%bx               # gen=66 8d 1d  dis=66 8d 1d 90 90 90 90
    lea    0x90909090,%sp               # gen=66 8d 25  dis=66 8d 25 90 90 90 90
    lea    0x90909090,%bp               # gen=66 8d 2d  dis=66 8d 2d 90 90 90 90
    lea    0x90909090,%si               # gen=66 8d 35  dis=66 8d 35 90 90 90 90
    lea    0x90909090,%di               # gen=66 8d 3d  dis=66 8d 3d 90 90 90 90
    mov    %ax,%es                      # gen=66 8e c0  dis=66 8e c0
    data16 mov 0x90909090,%es           # gen=66 8e 05  dis=66 8e 05 90 90 90 90
    mov    %ax,%cs                      # gen=66 8e c8  dis=66 8e c8
    mov    %ax,%ss                      # gen=66 8e d0  dis=66 8e d0
    mov    %ax,%ds                      # gen=66 8e d8  dis=66 8e d8
    mov    %ax,%fs                      # gen=66 8e e0  dis=66 8e e0
    mov    %ax,%gs                      # gen=66 8e e8  dis=66 8e e8
    data16 mov 0x90909090,%cs           # gen=66 8e 0d  dis=66 8e 0d 90 90 90 90
    data16 mov 0x90909090,%ss           # gen=66 8e 15  dis=66 8e 15 90 90 90 90
    data16 mov 0x90909090,%ds           # gen=66 8e 1d  dis=66 8e 1d 90 90 90 90
    data16 mov 0x90909090,%fs           # gen=66 8e 25  dis=66 8e 25 90 90 90 90
    data16 mov 0x90909090,%gs           # gen=66 8e 2d  dis=66 8e 2d 90 90 90 90
    popw   0x90909090                   # gen=66 8f 05  dis=66 8f 05 90 90 90 90
    xchg   %ax,%cx                      # gen=66 91 c0  dis=66 91
    xchg   %ax,%dx                      # gen=66 92 c0  dis=66 92
    xchg   %ax,%bx                      # gen=66 93 c0  dis=66 93
    xchg   %ax,%sp                      # gen=66 94 c0  dis=66 94
    xchg   %ax,%bp                      # gen=66 95 c0  dis=66 95
    xchg   %ax,%si                      # gen=66 96 c0  dis=66 96
    xchg   %ax,%di                      # gen=66 97 c0  dis=66 97
    cbtw                                # gen=66 98 c0  dis=66 98
    cwtd                                # gen=66 99 c0  dis=66 99
    lcallw $0x9090,$0x90c0              # gen=66 9a c0  dis=66 9a c0 90 90 90
    lcallw $0x9090,$0x9005              # gen=66 9a 05  dis=66 9a 05 90 90 90
    data16 fwait                        # gen=66 9b c0  dis=66 9b
    pushfw                              # gen=66 9c c0  dis=66 9c
    popfw                               # gen=66 9d c0  dis=66 9d
    data16 sahf                         # gen=66 9e c0  dis=66 9e
    data16 lahf                         # gen=66 9f c0  dis=66 9f
    data16 mov 0x909090c0,%al           # gen=66 a0 c0  dis=66 a0 c0 90 90 90
    data16 mov 0x90909005,%al           # gen=66 a0 05  dis=66 a0 05 90 90 90
    mov    0x909090c0,%ax               # gen=66 a1 c0  dis=66 a1 c0 90 90 90
    mov    0x90909005,%ax               # gen=66 a1 05  dis=66 a1 05 90 90 90
    data16 mov %al,0x909090c0           # gen=66 a2 c0  dis=66 a2 c0 90 90 90
    data16 mov %al,0x90909005           # gen=66 a2 05  dis=66 a2 05 90 90 90
    mov    %ax,0x909090c0               # gen=66 a3 c0  dis=66 a3 c0 90 90 90
    mov    %ax,0x90909005               # gen=66 a3 05  dis=66 a3 05 90 90 90
    data16 movsb %ds:(%esi),%es:(%edi)  # gen=66 a4 c0  dis=66 a4
    movsw  %ds:(%esi),%es:(%edi)        # gen=66 a5 c0  dis=66 a5
    data16 cmpsb %es:(%edi),%ds:(%esi)  # gen=66 a6 c0  dis=66 a6
    cmpsw  %es:(%edi),%ds:(%esi)        # gen=66 a7 c0  dis=66 a7
    data16 test $0xc0,%al               # gen=66 a8 c0  dis=66 a8 c0
    data16 test $0x5,%al                # gen=66 a8 05  dis=66 a8 05
    test   $0x90c0,%ax                  # gen=66 a9 c0  dis=66 a9 c0 90
    test   $0x9005,%ax                  # gen=66 a9 05  dis=66 a9 05 90
    data16 stos %al,%es:(%edi)          # gen=66 aa c0  dis=66 aa
    stos   %ax,%es:(%edi)               # gen=66 ab c0  dis=66 ab
    data16 lods %ds:(%esi),%al          # gen=66 ac c0  dis=66 ac
    lods   %ds:(%esi),%ax               # gen=66 ad c0  dis=66 ad
    data16 scas %es:(%edi),%al          # gen=66 ae c0  dis=66 ae
    scas   %es:(%edi),%ax               # gen=66 af c0  dis=66 af
    data16 mov $0xc0,%al                # gen=66 b0 c0  dis=66 b0 c0
    data16 mov $0x5,%al                 # gen=66 b0 05  dis=66 b0 05
    data16 mov $0xc0,%cl                # gen=66 b1 c0  dis=66 b1 c0
    data16 mov $0x5,%cl                 # gen=66 b1 05  dis=66 b1 05
    data16 mov $0xc0,%dl                # gen=66 b2 c0  dis=66 b2 c0
    data16 mov $0x5,%dl                 # gen=66 b2 05  dis=66 b2 05
    data16 mov $0xc0,%bl                # gen=66 b3 c0  dis=66 b3 c0
    data16 mov $0x5,%bl                 # gen=66 b3 05  dis=66 b3 05
    data16 mov $0xc0,%ah                # gen=66 b4 c0  dis=66 b4 c0
    data16 mov $0x5,%ah                 # gen=66 b4 05  dis=66 b4 05
    data16 mov $0xc0,%ch                # gen=66 b5 c0  dis=66 b5 c0
    data16 mov $0x5,%ch                 # gen=66 b5 05  dis=66 b5 05
    data16 mov $0xc0,%dh                # gen=66 b6 c0  dis=66 b6 c0
    data16 mov $0x5,%dh                 # gen=66 b6 05  dis=66 b6 05
    data16 mov $0xc0,%bh                # gen=66 b7 c0  dis=66 b7 c0
    data16 mov $0x5,%bh                 # gen=66 b7 05  dis=66 b7 05
    mov    $0x90c0,%ax                  # gen=66 b8 c0  dis=66 b8 c0 90
    mov    $0x9005,%ax                  # gen=66 b8 05  dis=66 b8 05 90
    mov    $0x90c0,%cx                  # gen=66 b9 c0  dis=66 b9 c0 90
    mov    $0x9005,%cx                  # gen=66 b9 05  dis=66 b9 05 90
    mov    $0x90c0,%dx                  # gen=66 ba c0  dis=66 ba c0 90
    mov    $0x9005,%dx                  # gen=66 ba 05  dis=66 ba 05 90
    mov    $0x90c0,%bx                  # gen=66 bb c0  dis=66 bb c0 90
    mov    $0x9005,%bx                  # gen=66 bb 05  dis=66 bb 05 90
    mov    $0x90c0,%sp                  # gen=66 bc c0  dis=66 bc c0 90
    mov    $0x9005,%sp                  # gen=66 bc 05  dis=66 bc 05 90
    mov    $0x90c0,%bp                  # gen=66 bd c0  dis=66 bd c0 90
    mov    $0x9005,%bp                  # gen=66 bd 05  dis=66 bd 05 90
    mov    $0x90c0,%si                  # gen=66 be c0  dis=66 be c0 90
    mov    $0x9005,%si                  # gen=66 be 05  dis=66 be 05 90
    mov    $0x90c0,%di                  # gen=66 bf c0  dis=66 bf c0 90
    mov    $0x9005,%di                  # gen=66 bf 05  dis=66 bf 05 90
    data16 rol $0x90,%al                # gen=66 c0 c0  dis=66 c0 c0 90
    data16 rolb $0x90,0x90909090        # gen=66 c0 05  dis=66 c0 05 90 90 90 90
    rol    $0x90,%ax                    # gen=66 c1 c0  dis=66 c1 c0 90
    rolw   $0x90,0x90909090             # gen=66 c1 05  dis=66 c1 05 90 90 90 90
    ror    $0x90,%ax                    # gen=66 c1 c8  dis=66 c1 c8 90
    rcl    $0x90,%ax                    # gen=66 c1 d0  dis=66 c1 d0 90
    rcr    $0x90,%ax                    # gen=66 c1 d8  dis=66 c1 d8 90
    shl    $0x90,%ax                    # gen=66 c1 e0  dis=66 c1 e0 90
    shr    $0x90,%ax                    # gen=66 c1 e8  dis=66 c1 e8 90
    sar    $0x90,%ax                    # gen=66 c1 f8  dis=66 c1 f8 90
    rorw   $0x90,0x90909090             # gen=66 c1 0d  dis=66 c1 0d 90 90 90 90
    rclw   $0x90,0x90909090             # gen=66 c1 15  dis=66 c1 15 90 90 90 90
    rcrw   $0x90,0x90909090             # gen=66 c1 1d  dis=66 c1 1d 90 90 90 90
    shlw   $0x90,0x90909090             # gen=66 c1 25  dis=66 c1 25 90 90 90 90
    shrw   $0x90,0x90909090             # gen=66 c1 2d  dis=66 c1 2d 90 90 90 90
    sarw   $0x90,0x90909090             # gen=66 c1 3d  dis=66 c1 3d 90 90 90 90
    retw   $0x90c0                      # gen=66 c2 c0  dis=66 c2 c0 90
    retw   $0x9005                      # gen=66 c2 05  dis=66 c2 05 90
    retw                                # gen=66 c3 c0  dis=66 c3
    les    0x90909090,%ax               # gen=66 c4 05  dis=66 c4 05 90 90 90 90
    les    0x90909090,%cx               # gen=66 c4 0d  dis=66 c4 0d 90 90 90 90
    les    0x90909090,%dx               # gen=66 c4 15  dis=66 c4 15 90 90 90 90
    les    0x90909090,%bx               # gen=66 c4 1d  dis=66 c4 1d 90 90 90 90
    les    0x90909090,%sp               # gen=66 c4 25  dis=66 c4 25 90 90 90 90
    les    0x90909090,%bp               # gen=66 c4 2d  dis=66 c4 2d 90 90 90 90
    les    0x90909090,%si               # gen=66 c4 35  dis=66 c4 35 90 90 90 90
    les    0x90909090,%di               # gen=66 c4 3d  dis=66 c4 3d 90 90 90 90
    lds    0x90909090,%ax               # gen=66 c5 05  dis=66 c5 05 90 90 90 90
    lds    0x90909090,%cx               # gen=66 c5 0d  dis=66 c5 0d 90 90 90 90
    lds    0x90909090,%dx               # gen=66 c5 15  dis=66 c5 15 90 90 90 90
    lds    0x90909090,%bx               # gen=66 c5 1d  dis=66 c5 1d 90 90 90 90
    lds    0x90909090,%sp               # gen=66 c5 25  dis=66 c5 25 90 90 90 90
    lds    0x90909090,%bp               # gen=66 c5 2d  dis=66 c5 2d 90 90 90 90
    lds    0x90909090,%si               # gen=66 c5 35  dis=66 c5 35 90 90 90 90
    lds    0x90909090,%di               # gen=66 c5 3d  dis=66 c5 3d 90 90 90 90
    data16 mov $0x90,%al                # gen=66 c6 c0  dis=66 c6 c0 90
    data16 movb $0x90,0x90909090        # gen=66 c6 05  dis=66 c6 05 90 90 90 90
    mov    $0x9090,%ax                  # gen=66 c7 c0  dis=66 c7 c0 90 90
    movw   $0x9090,0x90909090           # gen=66 c7 05  dis=66 c7 05 90 90 90 90
    enterw $0x90c0,$0x90                # gen=66 c8 c0  dis=66 c8 c0 90 90
    enterw $0x9005,$0x90                # gen=66 c8 05  dis=66 c8 05 90 90
    leavew                              # gen=66 c9 c0  dis=66 c9
    lretw  $0x90c0                      # gen=66 ca c0  dis=66 ca c0 90
    lretw  $0x9005                      # gen=66 ca 05  dis=66 ca 05 90
    lretw                               # gen=66 cb c0  dis=66 cb
    data16 int3                         # gen=66 cc c0  dis=66 cc
    data16 int $0xc0                    # gen=66 cd c0  dis=66 cd c0
    data16 int $0x5                     # gen=66 cd 05  dis=66 cd 05
    data16 into                         # gen=66 ce c0  dis=66 ce
    iretw                               # gen=66 cf c0  dis=66 cf
    data16 rol %al                      # gen=66 d0 c0  dis=66 d0 c0
    data16 rolb 0x90909090              # gen=66 d0 05  dis=66 d0 05 90 90 90 90
    rol    %ax                          # gen=66 d1 c0  dis=66 d1 c0
    rolw   0x90909090                   # gen=66 d1 05  dis=66 d1 05 90 90 90 90
    ror    %ax                          # gen=66 d1 c8  dis=66 d1 c8
    rcl    %ax                          # gen=66 d1 d0  dis=66 d1 d0
    rcr    %ax                          # gen=66 d1 d8  dis=66 d1 d8
    shl    %ax                          # gen=66 d1 e0  dis=66 d1 e0
    shr    %ax                          # gen=66 d1 e8  dis=66 d1 e8
    sar    %ax                          # gen=66 d1 f8  dis=66 d1 f8
    rorw   0x90909090                   # gen=66 d1 0d  dis=66 d1 0d 90 90 90 90
    rclw   0x90909090                   # gen=66 d1 15  dis=66 d1 15 90 90 90 90
    rcrw   0x90909090                   # gen=66 d1 1d  dis=66 d1 1d 90 90 90 90
    shlw   0x90909090                   # gen=66 d1 25  dis=66 d1 25 90 90 90 90
    shrw   0x90909090                   # gen=66 d1 2d  dis=66 d1 2d 90 90 90 90
    sarw   0x90909090                   # gen=66 d1 3d  dis=66 d1 3d 90 90 90 90
    data16 rol %cl,%al                  # gen=66 d2 c0  dis=66 d2 c0
    data16 rolb %cl,0x90909090          # gen=66 d2 05  dis=66 d2 05 90 90 90 90
    rol    %cl,%ax                      # gen=66 d3 c0  dis=66 d3 c0
    rolw   %cl,0x90909090               # gen=66 d3 05  dis=66 d3 05 90 90 90 90
    ror    %cl,%ax                      # gen=66 d3 c8  dis=66 d3 c8
    rcl    %cl,%ax                      # gen=66 d3 d0  dis=66 d3 d0
    rcr    %cl,%ax                      # gen=66 d3 d8  dis=66 d3 d8
    shl    %cl,%ax                      # gen=66 d3 e0  dis=66 d3 e0
    shr    %cl,%ax                      # gen=66 d3 e8  dis=66 d3 e8
    sar    %cl,%ax                      # gen=66 d3 f8  dis=66 d3 f8
    rorw   %cl,0x90909090               # gen=66 d3 0d  dis=66 d3 0d 90 90 90 90
    rclw   %cl,0x90909090               # gen=66 d3 15  dis=66 d3 15 90 90 90 90
    rcrw   %cl,0x90909090               # gen=66 d3 1d  dis=66 d3 1d 90 90 90 90
    shlw   %cl,0x90909090               # gen=66 d3 25  dis=66 d3 25 90 90 90 90
    shrw   %cl,0x90909090               # gen=66 d3 2d  dis=66 d3 2d 90 90 90 90
    sarw   %cl,0x90909090               # gen=66 d3 3d  dis=66 d3 3d 90 90 90 90
    data16 aam $0xc0                    # gen=66 d4 c0  dis=66 d4 c0
    data16 aam $0x5                     # gen=66 d4 05  dis=66 d4 05
    data16 aad $0xc0                    # gen=66 d5 c0  dis=66 d5 c0
    data16 aad $0x5                     # gen=66 d5 05  dis=66 d5 05
    data16 xlat %ds:(%ebx)              # gen=66 d7 c0  dis=66 d7
    data16 fadd %st(0),%st              # gen=66 d8 c0  dis=66 d8 c0
    data16 fadds 0x90909090             # gen=66 d8 05  dis=66 d8 05 90 90 90 90
    data16 fld %st(0)                   # gen=66 d9 c0  dis=66 d9 c0
    data16 flds 0x90909090              # gen=66 d9 05  dis=66 d9 05 90 90 90 90
    data16 fcmovb %st(0),%st            # gen=66 da c0  dis=66 da c0
    data16 fiaddl 0x90909090            # gen=66 da 05  dis=66 da 05 90 90 90 90
    data16 fcmovnb %st(0),%st           # gen=66 db c0  dis=66 db c0
    data16 fildl 0x90909090             # gen=66 db 05  dis=66 db 05 90 90 90 90
    data16 fadd %st,%st(0)              # gen=66 dc c0  dis=66 dc c0
    data16 faddl 0x90909090             # gen=66 dc 05  dis=66 dc 05 90 90 90 90
    data16 ffree %st(0)                 # gen=66 dd c0  dis=66 dd c0
    data16 fldl 0x90909090              # gen=66 dd 05  dis=66 dd 05 90 90 90 90
    data16 faddp %st,%st(0)             # gen=66 de c0  dis=66 de c0
    data16 fiadds 0x90909090            # gen=66 de 05  dis=66 de 05 90 90 90 90
    data16 ffreep %st(0)                # gen=66 df c0  dis=66 df c0
    data16 filds 0x90909090             # gen=66 df 05  dis=66 df 05 90 90 90 90
    data16 in $0xc0,%al                 # gen=66 e4 c0  dis=66 e4 c0
    data16 in $0x5,%al                  # gen=66 e4 05  dis=66 e4 05
    in     $0xc0,%ax                    # gen=66 e5 c0  dis=66 e5 c0
    in     $0x5,%ax                     # gen=66 e5 05  dis=66 e5 05
    data16 out %al,$0xc0                # gen=66 e6 c0  dis=66 e6 c0
    data16 out %al,$0x5                 # gen=66 e6 05  dis=66 e6 05
    out    %ax,$0xc0                    # gen=66 e7 c0  dis=66 e7 c0
    out    %ax,$0x5                     # gen=66 e7 05  dis=66 e7 05
    callw  0x6a24                       # gen=66 e8 c0  dis=66 e8 c0 90
    callw  0x6979                       # gen=66 e8 05  dis=66 e8 05 90
    ljmpw  $0x9090,$0x90c0              # gen=66 ea c0  dis=66 ea c0 90 90 90
    ljmpw  $0x9090,$0x9005              # gen=66 ea 05  dis=66 ea 05 90 90 90
    data16 jmp 0xd983                   # gen=66 eb c0  dis=66 eb c0
    data16 jmp 0xd9d8                   # gen=66 eb 05  dis=66 eb 05
    data16 in (%dx),%al                 # gen=66 ec c0  dis=66 ec
    in     (%dx),%ax                    # gen=66 ed c0  dis=66 ed
    data16 out %al,(%dx)                # gen=66 ee c0  dis=66 ee
    out    %ax,(%dx)                    # gen=66 ef c0  dis=66 ef
    data16 int1                         # gen=66 f1 c0  dis=66 f1
    bnd callw 0x6c75                    # gen=66 f2 e8  dis=66 f2 e8 90 90
    data16 hlt                          # gen=66 f4 c0  dis=66 f4
    data16 cmc                          # gen=66 f5 c0  dis=66 f5
    data16 test $0x90,%al               # gen=66 f6 c0  dis=66 f6 c0 90
    data16 testb $0x90,0x90909090       # gen=66 f6 05  dis=66 f6 05 90 90 90 90
    test   $0x9090,%ax                  # gen=66 f7 c0  dis=66 f7 c0 90 90
    testw  $0x9090,0x90909090           # gen=66 f7 05  dis=66 f7 05 90 90 90 90
    not    %ax                          # gen=66 f7 d0  dis=66 f7 d0
    neg    %ax                          # gen=66 f7 d8  dis=66 f7 d8
    mul    %ax                          # gen=66 f7 e0  dis=66 f7 e0
    imul   %ax                          # gen=66 f7 e8  dis=66 f7 e8
    div    %ax                          # gen=66 f7 f0  dis=66 f7 f0
    idiv   %ax                          # gen=66 f7 f8  dis=66 f7 f8
    notw   0x90909090                   # gen=66 f7 15  dis=66 f7 15 90 90 90 90
    negw   0x90909090                   # gen=66 f7 1d  dis=66 f7 1d 90 90 90 90
    mulw   0x90909090                   # gen=66 f7 25  dis=66 f7 25 90 90 90 90
    imulw  0x90909090                   # gen=66 f7 2d  dis=66 f7 2d 90 90 90 90
    divw   0x90909090                   # gen=66 f7 35  dis=66 f7 35 90 90 90 90
    idivw  0x90909090                   # gen=66 f7 3d  dis=66 f7 3d 90 90 90 90
    data16 stc                          # gen=66 f9 c0  dis=66 f9
    data16 cli                          # gen=66 fa c0  dis=66 fa
    data16 sti                          # gen=66 fb c0  dis=66 fb
    data16 cld                          # gen=66 fc c0  dis=66 fc
    data16 std                          # gen=66 fd c0  dis=66 fd
    data16 inc %al                      # gen=66 fe c0  dis=66 fe c0
    data16 incb 0x90909090              # gen=66 fe 05  dis=66 fe 05 90 90 90 90
    incw   0x90909090                   # gen=66 ff 05  dis=66 ff 05 90 90 90 90
    call   *%ax                         # gen=66 ff d0  dis=66 ff d0
    jmp    *%ax                         # gen=66 ff e0  dis=66 ff e0
    decw   0x90909090                   # gen=66 ff 0d  dis=66 ff 0d 90 90 90 90
    callw  *0x90909090                  # gen=66 ff 15  dis=66 ff 15 90 90 90 90
    lcallw *0x90909090                  # gen=66 ff 1d  dis=66 ff 1d 90 90 90 90
    jmpw   *0x90909090                  # gen=66 ff 25  dis=66 ff 25 90 90 90 90
    ljmpw  *0x90909090                  # gen=66 ff 2d  dis=66 ff 2d 90 90 90 90
    pushw  0x90909090                   # gen=66 ff 35  dis=66 ff 35 90 90 90 90
    sldt   %ax                          # gen=66 0f 00 c0  dis=66 0f 00 c0
    data16 sldt 0x90909090              # gen=66 0f 00 05  dis=66 0f 00 05 90 90 90
    str    %ax                          # gen=66 0f 00 c8  dis=66 0f 00 c8
    data16 lldt %ax                     # gen=66 0f 00 d0  dis=66 0f 00 d0
    data16 ltr %ax                      # gen=66 0f 00 d8  dis=66 0f 00 d8
    data16 verr %ax                     # gen=66 0f 00 e0  dis=66 0f 00 e0
    data16 verw %ax                     # gen=66 0f 00 e8  dis=66 0f 00 e8
    data16 str 0x90909090               # gen=66 0f 00 0d  dis=66 0f 00 0d 90 90 90
    data16 lldt 0x90909090              # gen=66 0f 00 15  dis=66 0f 00 15 90 90 90
    data16 ltr 0x90909090               # gen=66 0f 00 1d  dis=66 0f 00 1d 90 90 90
    data16 verr 0x90909090              # gen=66 0f 00 25  dis=66 0f 00 25 90 90 90
    data16 verw 0x90909090              # gen=66 0f 00 2d  dis=66 0f 00 2d 90 90 90
    data16 enclv                        # gen=66 0f 01 c0  dis=66 0f 01 c0
    sgdtw  0x90909090                   # gen=66 0f 01 05  dis=66 0f 01 05 90 90 90
    data16 monitor %eax,%ecx,%edx       # gen=66 0f 01 c8  dis=66 0f 01 c8
    data16 xgetbv                       # gen=66 0f 01 d0  dis=66 0f 01 d0
    data16 vmrun                        # gen=66 0f 01 d8  dis=66 0f 01 d8
    smsw   %ax                          # gen=66 0f 01 e0  dis=66 0f 01 e0
    data16 lmsw %ax                     # gen=66 0f 01 f0  dis=66 0f 01 f0
    sidtw  0x90909090                   # gen=66 0f 01 0d  dis=66 0f 01 0d 90 90 90
    lgdtw  0x90909090                   # gen=66 0f 01 15  dis=66 0f 01 15 90 90 90
    lidtw  0x90909090                   # gen=66 0f 01 1d  dis=66 0f 01 1d 90 90 90
    data16 smsw 0x90909090              # gen=66 0f 01 25  dis=66 0f 01 25 90 90 90
    data16 lmsw 0x90909090              # gen=66 0f 01 35  dis=66 0f 01 35 90 90 90
    data16 invlpg 0x90909090            # gen=66 0f 01 3d  dis=66 0f 01 3d 90 90 90
    lar    %ax,%ax                      # gen=66 0f 02 c0  dis=66 0f 02 c0
    lar    0x90909090,%ax               # gen=66 0f 02 05  dis=66 0f 02 05 90 90 90
    lsl    %ax,%ax                      # gen=66 0f 03 c0  dis=66 0f 03 c0
    lsl    0x90909090,%ax               # gen=66 0f 03 05  dis=66 0f 03 05 90 90 90
    data16 clts                         # gen=66 0f 06 c0  dis=66 0f 06
    data16 sysret                       # gen=66 0f 07 c0  dis=66 0f 07
    data16 invd                         # gen=66 0f 08 c0  dis=66 0f 08
    data16 ud2                          # gen=66 0f 0b c0  dis=66 0f 0b
    data16 prefetch (bad)               # gen=66 0f 0d c0  dis=66 0f
    data16 prefetch 0x90909090          # gen=66 0f 0d 05  dis=66 0f 0d 05 90 90 90
    data16 femms                        # gen=66 0f 0e c0  dis=66 0f 0e
    movupd %xmm0,%xmm0                  # gen=66 0f 10 c0  dis=66 0f 10 c0
    movupd 0x90909090,%xmm0             # gen=66 0f 10 05  dis=66 0f 10 05 90 90 90
    movupd %xmm0,0x90909090             # gen=66 0f 11 05  dis=66 0f 11 05 90 90 90
    movlpd 0x90909090,%xmm0             # gen=66 0f 12 05  dis=66 0f 12 05 90 90 90
    movlpd %xmm0,0x90909090             # gen=66 0f 13 05  dis=66 0f 13 05 90 90 90
    movlpd %xmm1,0x90909090             # gen=66 0f 13 0d  dis=66 0f 13 0d 90 90 90
    movlpd %xmm2,0x90909090             # gen=66 0f 13 15  dis=66 0f 13 15 90 90 90
    movlpd %xmm3,0x90909090             # gen=66 0f 13 1d  dis=66 0f 13 1d 90 90 90
    movlpd %xmm4,0x90909090             # gen=66 0f 13 25  dis=66 0f 13 25 90 90 90
    movlpd %xmm5,0x90909090             # gen=66 0f 13 2d  dis=66 0f 13 2d 90 90 90
    movlpd %xmm6,0x90909090             # gen=66 0f 13 35  dis=66 0f 13 35 90 90 90
    movlpd %xmm7,0x90909090             # gen=66 0f 13 3d  dis=66 0f 13 3d 90 90 90
    unpcklpd %xmm0,%xmm0                # gen=66 0f 14 c0  dis=66 0f 14 c0
    unpcklpd 0x90909090,%xmm0           # gen=66 0f 14 05  dis=66 0f 14 05 90 90 90
    unpckhpd %xmm0,%xmm0                # gen=66 0f 15 c0  dis=66 0f 15 c0
    unpckhpd 0x90909090,%xmm0           # gen=66 0f 15 05  dis=66 0f 15 05 90 90 90
    movhpd 0x90909090,%xmm0             # gen=66 0f 16 05  dis=66 0f 16 05 90 90 90
    movhpd %xmm0,0x90909090             # gen=66 0f 17 05  dis=66 0f 17 05 90 90 90
    movhpd %xmm1,0x90909090             # gen=66 0f 17 0d  dis=66 0f 17 0d 90 90 90
    movhpd %xmm2,0x90909090             # gen=66 0f 17 15  dis=66 0f 17 15 90 90 90
    movhpd %xmm3,0x90909090             # gen=66 0f 17 1d  dis=66 0f 17 1d 90 90 90
    movhpd %xmm4,0x90909090             # gen=66 0f 17 25  dis=66 0f 17 25 90 90 90
    movhpd %xmm5,0x90909090             # gen=66 0f 17 2d  dis=66 0f 17 2d 90 90 90
    movhpd %xmm6,0x90909090             # gen=66 0f 17 35  dis=66 0f 17 35 90 90 90
    movhpd %xmm7,0x90909090             # gen=66 0f 17 3d  dis=66 0f 17 3d 90 90 90
    nop    %ax                          # gen=66 0f 18 c0  dis=66 0f 18 c0
    data16 prefetchnta 0x90909090       # gen=66 0f 18 05  dis=66 0f 18 05 90 90 90
    data16 prefetcht0 0x90909090        # gen=66 0f 18 0d  dis=66 0f 18 0d 90 90 90
    data16 prefetcht1 0x90909090        # gen=66 0f 18 15  dis=66 0f 18 15 90 90 90
    data16 prefetcht2 0x90909090        # gen=66 0f 18 1d  dis=66 0f 18 1d 90 90 90
    nopw   0x90909090                   # gen=66 0f 18 25  dis=66 0f 18 25 90 90 90
    bndmov %bnd0,%bnd0                  # gen=66 0f 1a c0  dis=66 0f 1a c0
    bndmov 0x90909090,%bnd0             # gen=66 0f 1a 05  dis=66 0f 1a 05 90 90 90
    bndmov %bnd0,0x90909090             # gen=66 0f 1b 05  dis=66 0f 1b 05 90 90 90
    data16 mov %cr0,%eax                # gen=66 0f 20 c0  dis=66 0f 20 c0
    data16 mov %cr0,%ebp                # gen=66 0f 20 05  dis=66 0f 20 05
    data16 mov %db0,%eax                # gen=66 0f 21 c0  dis=66 0f 21 c0
    data16 mov %db0,%ebp                # gen=66 0f 21 05  dis=66 0f 21 05
    data16 mov %eax,%cr0                # gen=66 0f 22 c0  dis=66 0f 22 c0
    data16 mov %ebp,%cr0                # gen=66 0f 22 05  dis=66 0f 22 05
    data16 mov %eax,%db0                # gen=66 0f 23 c0  dis=66 0f 23 c0
    data16 mov %ebp,%db0                # gen=66 0f 23 05  dis=66 0f 23 05
    data16 mov %tr0,%eax                # gen=66 0f 24 c0  dis=66 0f 24 c0
    data16 mov %tr0,%ebp                # gen=66 0f 24 05  dis=66 0f 24 05
    data16 mov %eax,%tr0                # gen=66 0f 26 c0  dis=66 0f 26 c0
    data16 mov %ebp,%tr0                # gen=66 0f 26 05  dis=66 0f 26 05
    movapd %xmm0,%xmm0                  # gen=66 0f 28 c0  dis=66 0f 28 c0
    movapd 0x90909090,%xmm0             # gen=66 0f 28 05  dis=66 0f 28 05 90 90 90
    movapd %xmm0,0x90909090             # gen=66 0f 29 05  dis=66 0f 29 05 90 90 90
    cvtpi2pd %mm0,%xmm0                 # gen=66 0f 2a c0  dis=66 0f 2a c0
    cvtpi2pd 0x90909090,%xmm0           # gen=66 0f 2a 05  dis=66 0f 2a 05 90 90 90
    movntpd %xmm0,0x90909090            # gen=66 0f 2b 05  dis=66 0f 2b 05 90 90 90
    cvttpd2pi %xmm0,%mm0                # gen=66 0f 2c c0  dis=66 0f 2c c0
    cvttpd2pi 0x90909090,%mm0           # gen=66 0f 2c 05  dis=66 0f 2c 05 90 90 90
    cvtpd2pi %xmm0,%mm0                 # gen=66 0f 2d c0  dis=66 0f 2d c0
    cvtpd2pi 0x90909090,%mm0            # gen=66 0f 2d 05  dis=66 0f 2d 05 90 90 90
    ucomisd %xmm0,%xmm0                 # gen=66 0f 2e c0  dis=66 0f 2e c0
    ucomisd 0x90909090,%xmm0            # gen=66 0f 2e 05  dis=66 0f 2e 05 90 90 90
    comisd %xmm0,%xmm0                  # gen=66 0f 2f c0  dis=66 0f 2f c0
    comisd 0x90909090,%xmm0             # gen=66 0f 2f 05  dis=66 0f 2f 05 90 90 90
    data16 wrmsr                        # gen=66 0f 30 c0  dis=66 0f 30
    data16 rdtsc                        # gen=66 0f 31 c0  dis=66 0f 31
    data16 rdmsr                        # gen=66 0f 32 c0  dis=66 0f 32
    data16 rdpmc                        # gen=66 0f 33 c0  dis=66 0f 33
    data16 sysenter                     # gen=66 0f 34 c0  dis=66 0f 34
    data16 getsec                       # gen=66 0f 37 c0  dis=66 0f 37
    phsubw -0x6f6f6f70(%eax),%xmm2      # gen=66 0f 38 05  dis=66 0f 38 05 90 90 90
    movbe  -0x6f6f6f70(%eax),%dx        # gen=66 0f 38 f0  dis=66 0f 38 f0 90 90 90
    movdir64b -0x6f6f6f70(%eax),%edx    # gen=66 0f 38 f8  dis=66 0f 38 f8 90 90 90
    blendvpd %xmm0,-0x6f6f6f70(%eax),%xmm2 # gen=66 0f 38 15  dis=66 0f 38 15 90 90 90
    pabsw  -0x6f6f6f70(%eax),%xmm2      # gen=66 0f 38 1d  dis=66 0f 38 1d 90 90 90
    pmovsxdq -0x6f6f6f70(%eax),%xmm2    # gen=66 0f 38 25  dis=66 0f 38 25 90 90 90
    pmovzxdq -0x6f6f6f70(%eax),%xmm2    # gen=66 0f 38 35  dis=66 0f 38 35 90 90 90
    pmaxsd -0x6f6f6f70(%eax),%xmm2      # gen=66 0f 38 3d  dis=66 0f 38 3d 90 90 90
    cmovo  %ax,%ax                      # gen=66 0f 40 c0  dis=66 0f 40 c0
    cmovo  0x90909090,%ax               # gen=66 0f 40 05  dis=66 0f 40 05 90 90 90
    cmovno %ax,%ax                      # gen=66 0f 41 c0  dis=66 0f 41 c0
    cmovno 0x90909090,%ax               # gen=66 0f 41 05  dis=66 0f 41 05 90 90 90
    cmovb  %ax,%ax                      # gen=66 0f 42 c0  dis=66 0f 42 c0
    cmovb  0x90909090,%ax               # gen=66 0f 42 05  dis=66 0f 42 05 90 90 90
    cmovae %ax,%ax                      # gen=66 0f 43 c0  dis=66 0f 43 c0
    cmovae 0x90909090,%ax               # gen=66 0f 43 05  dis=66 0f 43 05 90 90 90
    cmove  %ax,%ax                      # gen=66 0f 44 c0  dis=66 0f 44 c0
    cmove  0x90909090,%ax               # gen=66 0f 44 05  dis=66 0f 44 05 90 90 90
    cmovne %ax,%ax                      # gen=66 0f 45 c0  dis=66 0f 45 c0
    cmovne 0x90909090,%ax               # gen=66 0f 45 05  dis=66 0f 45 05 90 90 90
    cmovbe %ax,%ax                      # gen=66 0f 46 c0  dis=66 0f 46 c0
    cmovbe 0x90909090,%ax               # gen=66 0f 46 05  dis=66 0f 46 05 90 90 90
    cmova  %ax,%ax                      # gen=66 0f 47 c0  dis=66 0f 47 c0
    cmova  0x90909090,%ax               # gen=66 0f 47 05  dis=66 0f 47 05 90 90 90
    cmovs  %ax,%ax                      # gen=66 0f 48 c0  dis=66 0f 48 c0
    cmovs  0x90909090,%ax               # gen=66 0f 48 05  dis=66 0f 48 05 90 90 90
    cmovns %ax,%ax                      # gen=66 0f 49 c0  dis=66 0f 49 c0
    cmovns 0x90909090,%ax               # gen=66 0f 49 05  dis=66 0f 49 05 90 90 90
    cmovp  %ax,%ax                      # gen=66 0f 4a c0  dis=66 0f 4a c0
    cmovp  0x90909090,%ax               # gen=66 0f 4a 05  dis=66 0f 4a 05 90 90 90
    cmovnp %ax,%ax                      # gen=66 0f 4b c0  dis=66 0f 4b c0
    cmovnp 0x90909090,%ax               # gen=66 0f 4b 05  dis=66 0f 4b 05 90 90 90
    cmovl  %ax,%ax                      # gen=66 0f 4c c0  dis=66 0f 4c c0
    cmovl  0x90909090,%ax               # gen=66 0f 4c 05  dis=66 0f 4c 05 90 90 90
    cmovge %ax,%ax                      # gen=66 0f 4d c0  dis=66 0f 4d c0
    cmovge 0x90909090,%ax               # gen=66 0f 4d 05  dis=66 0f 4d 05 90 90 90
    cmovle %ax,%ax                      # gen=66 0f 4e c0  dis=66 0f 4e c0
    cmovle 0x90909090,%ax               # gen=66 0f 4e 05  dis=66 0f 4e 05 90 90 90
    cmovg  %ax,%ax                      # gen=66 0f 4f c0  dis=66 0f 4f c0
    cmovg  0x90909090,%ax               # gen=66 0f 4f 05  dis=66 0f 4f 05 90 90 90
    movmskpd %xmm0,%eax                 # gen=66 0f 50 c0  dis=66 0f 50 c0
    movmskpd %xmm0,%ecx                 # gen=66 0f 50 c8  dis=66 0f 50 c8
    movmskpd %xmm0,%edx                 # gen=66 0f 50 d0  dis=66 0f 50 d0
    movmskpd %xmm0,%ebx                 # gen=66 0f 50 d8  dis=66 0f 50 d8
    movmskpd %xmm0,%esp                 # gen=66 0f 50 e0  dis=66 0f 50 e0
    movmskpd %xmm0,%ebp                 # gen=66 0f 50 e8  dis=66 0f 50 e8
    movmskpd %xmm0,%esi                 # gen=66 0f 50 f0  dis=66 0f 50 f0
    movmskpd %xmm0,%edi                 # gen=66 0f 50 f8  dis=66 0f 50 f8
    sqrtpd %xmm0,%xmm0                  # gen=66 0f 51 c0  dis=66 0f 51 c0
    sqrtpd 0x90909090,%xmm0             # gen=66 0f 51 05  dis=66 0f 51 05 90 90 90
    andpd  %xmm0,%xmm0                  # gen=66 0f 54 c0  dis=66 0f 54 c0
    andpd  0x90909090,%xmm0             # gen=66 0f 54 05  dis=66 0f 54 05 90 90 90
    andnpd %xmm0,%xmm0                  # gen=66 0f 55 c0  dis=66 0f 55 c0
    andnpd 0x90909090,%xmm0             # gen=66 0f 55 05  dis=66 0f 55 05 90 90 90
    orpd   %xmm0,%xmm0                  # gen=66 0f 56 c0  dis=66 0f 56 c0
    orpd   0x90909090,%xmm0             # gen=66 0f 56 05  dis=66 0f 56 05 90 90 90
    xorpd  %xmm0,%xmm0                  # gen=66 0f 57 c0  dis=66 0f 57 c0
    xorpd  0x90909090,%xmm0             # gen=66 0f 57 05  dis=66 0f 57 05 90 90 90
    addpd  %xmm0,%xmm0                  # gen=66 0f 58 c0  dis=66 0f 58 c0
    addpd  0x90909090,%xmm0             # gen=66 0f 58 05  dis=66 0f 58 05 90 90 90
    mulpd  %xmm0,%xmm0                  # gen=66 0f 59 c0  dis=66 0f 59 c0
    mulpd  0x90909090,%xmm0             # gen=66 0f 59 05  dis=66 0f 59 05 90 90 90
    cvtpd2ps %xmm0,%xmm0                # gen=66 0f 5a c0  dis=66 0f 5a c0
    cvtpd2ps 0x90909090,%xmm0           # gen=66 0f 5a 05  dis=66 0f 5a 05 90 90 90
    cvtps2dq %xmm0,%xmm0                # gen=66 0f 5b c0  dis=66 0f 5b c0
    cvtps2dq 0x90909090,%xmm0           # gen=66 0f 5b 05  dis=66 0f 5b 05 90 90 90
    subpd  %xmm0,%xmm0                  # gen=66 0f 5c c0  dis=66 0f 5c c0
    subpd  0x90909090,%xmm0             # gen=66 0f 5c 05  dis=66 0f 5c 05 90 90 90
    minpd  %xmm0,%xmm0                  # gen=66 0f 5d c0  dis=66 0f 5d c0
    minpd  0x90909090,%xmm0             # gen=66 0f 5d 05  dis=66 0f 5d 05 90 90 90
    divpd  %xmm0,%xmm0                  # gen=66 0f 5e c0  dis=66 0f 5e c0
    divpd  0x90909090,%xmm0             # gen=66 0f 5e 05  dis=66 0f 5e 05 90 90 90
    maxpd  %xmm0,%xmm0                  # gen=66 0f 5f c0  dis=66 0f 5f c0
    maxpd  0x90909090,%xmm0             # gen=66 0f 5f 05  dis=66 0f 5f 05 90 90 90
    punpcklbw %xmm0,%xmm0               # gen=66 0f 60 c0  dis=66 0f 60 c0
    punpcklbw 0x90909090,%xmm0          # gen=66 0f 60 05  dis=66 0f 60 05 90 90 90
    punpcklwd %xmm0,%xmm0               # gen=66 0f 61 c0  dis=66 0f 61 c0
    punpcklwd 0x90909090,%xmm0          # gen=66 0f 61 05  dis=66 0f 61 05 90 90 90
    punpckldq %xmm0,%xmm0               # gen=66 0f 62 c0  dis=66 0f 62 c0
    punpckldq 0x90909090,%xmm0          # gen=66 0f 62 05  dis=66 0f 62 05 90 90 90
    packsswb %xmm0,%xmm0                # gen=66 0f 63 c0  dis=66 0f 63 c0
    packsswb 0x90909090,%xmm0           # gen=66 0f 63 05  dis=66 0f 63 05 90 90 90
    pcmpgtb %xmm0,%xmm0                 # gen=66 0f 64 c0  dis=66 0f 64 c0
    pcmpgtb 0x90909090,%xmm0            # gen=66 0f 64 05  dis=66 0f 64 05 90 90 90
    pcmpgtw %xmm0,%xmm0                 # gen=66 0f 65 c0  dis=66 0f 65 c0
    pcmpgtw 0x90909090,%xmm0            # gen=66 0f 65 05  dis=66 0f 65 05 90 90 90
    pcmpgtd %xmm0,%xmm0                 # gen=66 0f 66 c0  dis=66 0f 66 c0
    pcmpgtd 0x90909090,%xmm0            # gen=66 0f 66 05  dis=66 0f 66 05 90 90 90
    packuswb %xmm0,%xmm0                # gen=66 0f 67 c0  dis=66 0f 67 c0
    packuswb 0x90909090,%xmm0           # gen=66 0f 67 05  dis=66 0f 67 05 90 90 90
    punpckhbw %xmm0,%xmm0               # gen=66 0f 68 c0  dis=66 0f 68 c0
    punpckhbw 0x90909090,%xmm0          # gen=66 0f 68 05  dis=66 0f 68 05 90 90 90
    punpckhwd %xmm0,%xmm0               # gen=66 0f 69 c0  dis=66 0f 69 c0
    punpckhwd 0x90909090,%xmm0          # gen=66 0f 69 05  dis=66 0f 69 05 90 90 90
    punpckhdq %xmm0,%xmm0               # gen=66 0f 6a c0  dis=66 0f 6a c0
    punpckhdq 0x90909090,%xmm0          # gen=66 0f 6a 05  dis=66 0f 6a 05 90 90 90
    packssdw %xmm0,%xmm0                # gen=66 0f 6b c0  dis=66 0f 6b c0
    packssdw 0x90909090,%xmm0           # gen=66 0f 6b 05  dis=66 0f 6b 05 90 90 90
    punpcklqdq %xmm0,%xmm0              # gen=66 0f 6c c0  dis=66 0f 6c c0
    punpcklqdq 0x90909090,%xmm0         # gen=66 0f 6c 05  dis=66 0f 6c 05 90 90 90
    punpckhqdq %xmm0,%xmm0              # gen=66 0f 6d c0  dis=66 0f 6d c0
    punpckhqdq 0x90909090,%xmm0         # gen=66 0f 6d 05  dis=66 0f 6d 05 90 90 90
    movd   %eax,%xmm0                   # gen=66 0f 6e c0  dis=66 0f 6e c0
    movd   0x90909090,%xmm0             # gen=66 0f 6e 05  dis=66 0f 6e 05 90 90 90
    movdqa %xmm0,%xmm0                  # gen=66 0f 6f c0  dis=66 0f 6f c0
    movdqa 0x90909090,%xmm0             # gen=66 0f 6f 05  dis=66 0f 6f 05 90 90 90
    pshufd $0x90,%xmm0,%xmm0            # gen=66 0f 70 c0  dis=66 0f 70 c0 90
    pshufd $0x90,0x90909090,%xmm0       # gen=66 0f 70 05  dis=66 0f 70 05 90 90 90
    psrlq  $0x90,%xmm0                  # gen=66 0f 73 d0  dis=66 0f 73 d0 90
    psrldq $0x90,%xmm0                  # gen=66 0f 73 d8  dis=66 0f 73 d8 90
    psllq  $0x90,%xmm0                  # gen=66 0f 73 f0  dis=66 0f 73 f0 90
    pslldq $0x90,%xmm0                  # gen=66 0f 73 f8  dis=66 0f 73 f8 90
    pcmpeqb %xmm0,%xmm0                 # gen=66 0f 74 c0  dis=66 0f 74 c0
    pcmpeqb 0x90909090,%xmm0            # gen=66 0f 74 05  dis=66 0f 74 05 90 90 90
    pcmpeqw %xmm0,%xmm0                 # gen=66 0f 75 c0  dis=66 0f 75 c0
    pcmpeqw 0x90909090,%xmm0            # gen=66 0f 75 05  dis=66 0f 75 05 90 90 90
    pcmpeqd %xmm0,%xmm0                 # gen=66 0f 76 c0  dis=66 0f 76 c0
    pcmpeqd 0x90909090,%xmm0            # gen=66 0f 76 05  dis=66 0f 76 05 90 90 90
    extrq  $0x90,$0x90,%xmm0            # gen=66 0f 78 c0  dis=66 0f 78 c0 90 90
    extrq  %xmm0,%xmm0                  # gen=66 0f 79 c0  dis=66 0f 79 c0
    haddpd %xmm0,%xmm0                  # gen=66 0f 7c c0  dis=66 0f 7c c0
    haddpd 0x90909090,%xmm0             # gen=66 0f 7c 05  dis=66 0f 7c 05 90 90 90
    hsubpd %xmm0,%xmm0                  # gen=66 0f 7d c0  dis=66 0f 7d c0
    hsubpd 0x90909090,%xmm0             # gen=66 0f 7d 05  dis=66 0f 7d 05 90 90 90
    movd   %xmm0,%eax                   # gen=66 0f 7e c0  dis=66 0f 7e c0
    movd   %xmm0,0x90909090             # gen=66 0f 7e 05  dis=66 0f 7e 05 90 90 90
    movdqa %xmm0,0x90909090             # gen=66 0f 7f 05  dis=66 0f 7f 05 90 90 90
    jo     0x8ce5                       # gen=66 0f 80 c0  dis=66 0f 80 c0 90
    jo     0x8c3a                       # gen=66 0f 80 05  dis=66 0f 80 05 90
    jno    0x8d05                       # gen=66 0f 81 c0  dis=66 0f 81 c0 90
    jno    0x8c5a                       # gen=66 0f 81 05  dis=66 0f 81 05 90
    jb     0x8d25                       # gen=66 0f 82 c0  dis=66 0f 82 c0 90
    jb     0x8c7a                       # gen=66 0f 82 05  dis=66 0f 82 05 90
    jae    0x8d45                       # gen=66 0f 83 c0  dis=66 0f 83 c0 90
    jae    0x8c9a                       # gen=66 0f 83 05  dis=66 0f 83 05 90
    je     0x8d65                       # gen=66 0f 84 c0  dis=66 0f 84 c0 90
    je     0x8cba                       # gen=66 0f 84 05  dis=66 0f 84 05 90
    jne    0x8d85                       # gen=66 0f 85 c0  dis=66 0f 85 c0 90
    jne    0x8cda                       # gen=66 0f 85 05  dis=66 0f 85 05 90
    jbe    0x8da5                       # gen=66 0f 86 c0  dis=66 0f 86 c0 90
    jbe    0x8cfa                       # gen=66 0f 86 05  dis=66 0f 86 05 90
    ja     0x8dc5                       # gen=66 0f 87 c0  dis=66 0f 87 c0 90
    ja     0x8d1a                       # gen=66 0f 87 05  dis=66 0f 87 05 90
    js     0x8de5                       # gen=66 0f 88 c0  dis=66 0f 88 c0 90
    js     0x8d3a                       # gen=66 0f 88 05  dis=66 0f 88 05 90
    jns    0x8e05                       # gen=66 0f 89 c0  dis=66 0f 89 c0 90
    jns    0x8d5a                       # gen=66 0f 89 05  dis=66 0f 89 05 90
    jp     0x8e25                       # gen=66 0f 8a c0  dis=66 0f 8a c0 90
    jp     0x8d7a                       # gen=66 0f 8a 05  dis=66 0f 8a 05 90
    jnp    0x8e45                       # gen=66 0f 8b c0  dis=66 0f 8b c0 90
    jnp    0x8d9a                       # gen=66 0f 8b 05  dis=66 0f 8b 05 90
    jl     0x8e65                       # gen=66 0f 8c c0  dis=66 0f 8c c0 90
    jl     0x8dba                       # gen=66 0f 8c 05  dis=66 0f 8c 05 90
    jge    0x8e85                       # gen=66 0f 8d c0  dis=66 0f 8d c0 90
    jge    0x8dda                       # gen=66 0f 8d 05  dis=66 0f 8d 05 90
    jle    0x8ea5                       # gen=66 0f 8e c0  dis=66 0f 8e c0 90
    jle    0x8dfa                       # gen=66 0f 8e 05  dis=66 0f 8e 05 90
    jg     0x8ec5                       # gen=66 0f 8f c0  dis=66 0f 8f c0 90
    jg     0x8e1a                       # gen=66 0f 8f 05  dis=66 0f 8f 05 90
    data16 seto %al                     # gen=66 0f 90 c0  dis=66 0f 90 c0
    data16 seto 0x90909090              # gen=66 0f 90 05  dis=66 0f 90 05 90 90 90
    data16 setno %al                    # gen=66 0f 91 c0  dis=66 0f 91 c0
    data16 setno 0x90909090             # gen=66 0f 91 05  dis=66 0f 91 05 90 90 90
    data16 setb %al                     # gen=66 0f 92 c0  dis=66 0f 92 c0
    data16 setb 0x90909090              # gen=66 0f 92 05  dis=66 0f 92 05 90 90 90
    data16 setae %al                    # gen=66 0f 93 c0  dis=66 0f 93 c0
    data16 setae 0x90909090             # gen=66 0f 93 05  dis=66 0f 93 05 90 90 90
    data16 sete %al                     # gen=66 0f 94 c0  dis=66 0f 94 c0
    data16 sete 0x90909090              # gen=66 0f 94 05  dis=66 0f 94 05 90 90 90
    data16 setne %al                    # gen=66 0f 95 c0  dis=66 0f 95 c0
    data16 setne 0x90909090             # gen=66 0f 95 05  dis=66 0f 95 05 90 90 90
    data16 setbe %al                    # gen=66 0f 96 c0  dis=66 0f 96 c0
    data16 setbe 0x90909090             # gen=66 0f 96 05  dis=66 0f 96 05 90 90 90
    data16 seta %al                     # gen=66 0f 97 c0  dis=66 0f 97 c0
    data16 seta 0x90909090              # gen=66 0f 97 05  dis=66 0f 97 05 90 90 90
    data16 sets %al                     # gen=66 0f 98 c0  dis=66 0f 98 c0
    data16 sets 0x90909090              # gen=66 0f 98 05  dis=66 0f 98 05 90 90 90
    data16 setns %al                    # gen=66 0f 99 c0  dis=66 0f 99 c0
    data16 setns 0x90909090             # gen=66 0f 99 05  dis=66 0f 99 05 90 90 90
    data16 setp %al                     # gen=66 0f 9a c0  dis=66 0f 9a c0
    data16 setp 0x90909090              # gen=66 0f 9a 05  dis=66 0f 9a 05 90 90 90
    data16 setnp %al                    # gen=66 0f 9b c0  dis=66 0f 9b c0
    data16 setnp 0x90909090             # gen=66 0f 9b 05  dis=66 0f 9b 05 90 90 90
    data16 setl %al                     # gen=66 0f 9c c0  dis=66 0f 9c c0
    data16 setl 0x90909090              # gen=66 0f 9c 05  dis=66 0f 9c 05 90 90 90
    data16 setge %al                    # gen=66 0f 9d c0  dis=66 0f 9d c0
    data16 setge 0x90909090             # gen=66 0f 9d 05  dis=66 0f 9d 05 90 90 90
    data16 setle %al                    # gen=66 0f 9e c0  dis=66 0f 9e c0
    data16 setle 0x90909090             # gen=66 0f 9e 05  dis=66 0f 9e 05 90 90 90
    data16 setg %al                     # gen=66 0f 9f c0  dis=66 0f 9f c0
    data16 setg 0x90909090              # gen=66 0f 9f 05  dis=66 0f 9f 05 90 90 90
    pushw  %fs                          # gen=66 0f a0 c0  dis=66 0f a0
    popw   %fs                          # gen=66 0f a1 c0  dis=66 0f a1
    data16 cpuid                        # gen=66 0f a2 c0  dis=66 0f a2
    bt     %ax,%ax                      # gen=66 0f a3 c0  dis=66 0f a3 c0
    bt     %ax,0x90909090               # gen=66 0f a3 05  dis=66 0f a3 05 90 90 90
    shld   $0x90,%ax,%ax                # gen=66 0f a4 c0  dis=66 0f a4 c0 90
    shld   $0x90,%ax,0x90909090         # gen=66 0f a4 05  dis=66 0f a4 05 90 90 90
    shld   %cl,%ax,%ax                  # gen=66 0f a5 c0  dis=66 0f a5 c0
    shld   %cl,%ax,0x90909090           # gen=66 0f a5 05  dis=66 0f a5 05 90 90 90
    data16 montmul                      # gen=66 0f a6 c0  dis=66 0f a6 c0
    data16 xstore-rng                   # gen=66 0f a7 c0  dis=66 0f a7 c0
    pushw  %gs                          # gen=66 0f a8 c0  dis=66 0f a8
    popw   %gs                          # gen=66 0f a9 c0  dis=66 0f a9
    data16 rsm                          # gen=66 0f aa c0  dis=66 0f aa
    bts    %ax,%ax                      # gen=66 0f ab c0  dis=66 0f ab c0
    bts    %ax,0x90909090               # gen=66 0f ab 05  dis=66 0f ab 05 90 90 90
    shrd   $0x90,%ax,%ax                # gen=66 0f ac c0  dis=66 0f ac c0 90
    shrd   $0x90,%ax,0x90909090         # gen=66 0f ac 05  dis=66 0f ac 05 90 90 90
    shrd   %cl,%ax,%ax                  # gen=66 0f ad c0  dis=66 0f ad c0
    shrd   %cl,%ax,0x90909090           # gen=66 0f ad 05  dis=66 0f ad 05 90 90 90
    data16 fxsave 0x90909090            # gen=66 0f ae 05  dis=66 0f ae 05 90 90 90
    tpause %eax                         # gen=66 0f ae f0  dis=66 0f ae f0
    data16 sfence                       # gen=66 0f ae f8  dis=66 0f ae f8
    data16 fxrstor 0x90909090           # gen=66 0f ae 0d  dis=66 0f ae 0d 90 90 90
    data16 ldmxcsr 0x90909090           # gen=66 0f ae 15  dis=66 0f ae 15 90 90 90
    data16 stmxcsr 0x90909090           # gen=66 0f ae 1d  dis=66 0f ae 1d 90 90 90
    clwb   0x90909090                   # gen=66 0f ae 35  dis=66 0f ae 35 90 90 90
    clflushopt 0x90909090               # gen=66 0f ae 3d  dis=66 0f ae 3d 90 90 90
    imul   %ax,%ax                      # gen=66 0f af c0  dis=66 0f af c0
    imul   0x90909090,%ax               # gen=66 0f af 05  dis=66 0f af 05 90 90 90
    data16 cmpxchg %al,%al              # gen=66 0f b0 c0  dis=66 0f b0 c0
    data16 cmpxchg %al,0x90909090       # gen=66 0f b0 05  dis=66 0f b0 05 90 90 90
    cmpxchg %ax,%ax                     # gen=66 0f b1 c0  dis=66 0f b1 c0
    cmpxchg %ax,0x90909090              # gen=66 0f b1 05  dis=66 0f b1 05 90 90 90
    lss    0x90909090,%ax               # gen=66 0f b2 05  dis=66 0f b2 05 90 90 90
    lss    0x90909090,%cx               # gen=66 0f b2 0d  dis=66 0f b2 0d 90 90 90
    lss    0x90909090,%dx               # gen=66 0f b2 15  dis=66 0f b2 15 90 90 90
    lss    0x90909090,%bx               # gen=66 0f b2 1d  dis=66 0f b2 1d 90 90 90
    lss    0x90909090,%sp               # gen=66 0f b2 25  dis=66 0f b2 25 90 90 90
    lss    0x90909090,%bp               # gen=66 0f b2 2d  dis=66 0f b2 2d 90 90 90
    lss    0x90909090,%si               # gen=66 0f b2 35  dis=66 0f b2 35 90 90 90
    lss    0x90909090,%di               # gen=66 0f b2 3d  dis=66 0f b2 3d 90 90 90
    btr    %ax,%ax                      # gen=66 0f b3 c0  dis=66 0f b3 c0
    btr    %ax,0x90909090               # gen=66 0f b3 05  dis=66 0f b3 05 90 90 90
    lfs    0x90909090,%ax               # gen=66 0f b4 05  dis=66 0f b4 05 90 90 90
    lfs    0x90909090,%cx               # gen=66 0f b4 0d  dis=66 0f b4 0d 90 90 90
    lfs    0x90909090,%dx               # gen=66 0f b4 15  dis=66 0f b4 15 90 90 90
    lfs    0x90909090,%bx               # gen=66 0f b4 1d  dis=66 0f b4 1d 90 90 90
    lfs    0x90909090,%sp               # gen=66 0f b4 25  dis=66 0f b4 25 90 90 90
    lfs    0x90909090,%bp               # gen=66 0f b4 2d  dis=66 0f b4 2d 90 90 90
    lfs    0x90909090,%si               # gen=66 0f b4 35  dis=66 0f b4 35 90 90 90
    lfs    0x90909090,%di               # gen=66 0f b4 3d  dis=66 0f b4 3d 90 90 90
    lgs    0x90909090,%ax               # gen=66 0f b5 05  dis=66 0f b5 05 90 90 90
    lgs    0x90909090,%cx               # gen=66 0f b5 0d  dis=66 0f b5 0d 90 90 90
    lgs    0x90909090,%dx               # gen=66 0f b5 15  dis=66 0f b5 15 90 90 90
    lgs    0x90909090,%bx               # gen=66 0f b5 1d  dis=66 0f b5 1d 90 90 90
    lgs    0x90909090,%sp               # gen=66 0f b5 25  dis=66 0f b5 25 90 90 90
    lgs    0x90909090,%bp               # gen=66 0f b5 2d  dis=66 0f b5 2d 90 90 90
    lgs    0x90909090,%si               # gen=66 0f b5 35  dis=66 0f b5 35 90 90 90
    lgs    0x90909090,%di               # gen=66 0f b5 3d  dis=66 0f b5 3d 90 90 90
    movzbw %al,%ax                      # gen=66 0f b6 c0  dis=66 0f b6 c0
    movzbw 0x90909090,%ax               # gen=66 0f b6 05  dis=66 0f b6 05 90 90 90
    ud1    %ax,%ax                      # gen=66 0f b9 c0  dis=66 0f b9 c0
    ud1    0x90909090,%ax               # gen=66 0f b9 05  dis=66 0f b9 05 90 90 90
    bt     $0x90,%ax                    # gen=66 0f ba e0  dis=66 0f ba e0 90
    bts    $0x90,%ax                    # gen=66 0f ba e8  dis=66 0f ba e8 90
    btr    $0x90,%ax                    # gen=66 0f ba f0  dis=66 0f ba f0 90
    btc    $0x90,%ax                    # gen=66 0f ba f8  dis=66 0f ba f8 90
    btw    $0x90,0x90909090             # gen=66 0f ba 25  dis=66 0f ba 25 90 90 90
    btsw   $0x90,0x90909090             # gen=66 0f ba 2d  dis=66 0f ba 2d 90 90 90
    btrw   $0x90,0x90909090             # gen=66 0f ba 35  dis=66 0f ba 35 90 90 90
    btcw   $0x90,0x90909090             # gen=66 0f ba 3d  dis=66 0f ba 3d 90 90 90
    btc    %ax,%ax                      # gen=66 0f bb c0  dis=66 0f bb c0
    btc    %ax,0x90909090               # gen=66 0f bb 05  dis=66 0f bb 05 90 90 90
    bsf    %ax,%ax                      # gen=66 0f bc c0  dis=66 0f bc c0
    bsf    0x90909090,%ax               # gen=66 0f bc 05  dis=66 0f bc 05 90 90 90
    bsr    %ax,%ax                      # gen=66 0f bd c0  dis=66 0f bd c0
    bsr    0x90909090,%ax               # gen=66 0f bd 05  dis=66 0f bd 05 90 90 90
    movsbw %al,%ax                      # gen=66 0f be c0  dis=66 0f be c0
    movsbw 0x90909090,%ax               # gen=66 0f be 05  dis=66 0f be 05 90 90 90
    data16 xadd %al,%al                 # gen=66 0f c0 c0  dis=66 0f c0 c0
    data16 xadd %al,0x90909090          # gen=66 0f c0 05  dis=66 0f c0 05 90 90 90
    xadd   %ax,%ax                      # gen=66 0f c1 c0  dis=66 0f c1 c0
    xadd   %ax,0x90909090               # gen=66 0f c1 05  dis=66 0f c1 05 90 90 90
    cmppd  $0x90,%xmm0,%xmm0            # gen=66 0f c2 c0  dis=66 0f c2 c0 90
    cmppd  $0x90,0x90909090,%xmm0       # gen=66 0f c2 05  dis=66 0f c2 05 90 90 90
    pinsrw $0x90,%eax,%xmm0             # gen=66 0f c4 c0  dis=66 0f c4 c0 90
    pinsrw $0x90,0x90909090,%xmm0       # gen=66 0f c4 05  dis=66 0f c4 05 90 90 90
    pextrw $0x90,%xmm0,%eax             # gen=66 0f c5 c0  dis=66 0f c5 c0 90
    shufpd $0x90,%xmm0,%xmm0            # gen=66 0f c6 c0  dis=66 0f c6 c0 90
    shufpd $0x90,0x90909090,%xmm0       # gen=66 0f c6 05  dis=66 0f c6 05 90 90 90
    data16 cmpxchg8b (bad)              # gen=66 0f c7 c8  dis=66 0f
    rdrand %ax                          # gen=66 0f c7 f0  dis=66 0f c7 f0
    rdseed %ax                          # gen=66 0f c7 f8  dis=66 0f c7 f8
    data16 cmpxchg8b 0x90909090         # gen=66 0f c7 0d  dis=66 0f c7 0d 90 90 90
    data16 xrstors 0x90909090           # gen=66 0f c7 1d  dis=66 0f c7 1d 90 90 90
    data16 xsavec 0x90909090            # gen=66 0f c7 25  dis=66 0f c7 25 90 90 90
    data16 xsaves 0x90909090            # gen=66 0f c7 2d  dis=66 0f c7 2d 90 90 90
    vmclear 0x90909090                  # gen=66 0f c7 35  dis=66 0f c7 35 90 90 90
    data16 vmptrst 0x90909090           # gen=66 0f c7 3d  dis=66 0f c7 3d 90 90 90
    addsubpd %xmm0,%xmm0                # gen=66 0f d0 c0  dis=66 0f d0 c0
    addsubpd 0x90909090,%xmm0           # gen=66 0f d0 05  dis=66 0f d0 05 90 90 90
    psrlw  %xmm0,%xmm0                  # gen=66 0f d1 c0  dis=66 0f d1 c0
    psrlw  0x90909090,%xmm0             # gen=66 0f d1 05  dis=66 0f d1 05 90 90 90
    psrld  %xmm0,%xmm0                  # gen=66 0f d2 c0  dis=66 0f d2 c0
    psrld  0x90909090,%xmm0             # gen=66 0f d2 05  dis=66 0f d2 05 90 90 90
    psrlq  %xmm0,%xmm0                  # gen=66 0f d3 c0  dis=66 0f d3 c0
    psrlq  0x90909090,%xmm0             # gen=66 0f d3 05  dis=66 0f d3 05 90 90 90
    paddq  %xmm0,%xmm0                  # gen=66 0f d4 c0  dis=66 0f d4 c0
    paddq  0x90909090,%xmm0             # gen=66 0f d4 05  dis=66 0f d4 05 90 90 90
    pmullw %xmm0,%xmm0                  # gen=66 0f d5 c0  dis=66 0f d5 c0
    pmullw 0x90909090,%xmm0             # gen=66 0f d5 05  dis=66 0f d5 05 90 90 90
    movq   %xmm0,%xmm0                  # gen=66 0f d6 c0  dis=66 0f d6 c0
    movq   %xmm0,0x90909090             # gen=66 0f d6 05  dis=66 0f d6 05 90 90 90
    pmovmskb %xmm0,%eax                 # gen=66 0f d7 c0  dis=66 0f d7 c0
    pmovmskb %xmm0,%ecx                 # gen=66 0f d7 c8  dis=66 0f d7 c8
    pmovmskb %xmm0,%edx                 # gen=66 0f d7 d0  dis=66 0f d7 d0
    pmovmskb %xmm0,%ebx                 # gen=66 0f d7 d8  dis=66 0f d7 d8
    pmovmskb %xmm0,%esp                 # gen=66 0f d7 e0  dis=66 0f d7 e0
    pmovmskb %xmm0,%ebp                 # gen=66 0f d7 e8  dis=66 0f d7 e8
    pmovmskb %xmm0,%esi                 # gen=66 0f d7 f0  dis=66 0f d7 f0
    pmovmskb %xmm0,%edi                 # gen=66 0f d7 f8  dis=66 0f d7 f8
    psubusb %xmm0,%xmm0                 # gen=66 0f d8 c0  dis=66 0f d8 c0
    psubusb 0x90909090,%xmm0            # gen=66 0f d8 05  dis=66 0f d8 05 90 90 90
    psubusw %xmm0,%xmm0                 # gen=66 0f d9 c0  dis=66 0f d9 c0
    psubusw 0x90909090,%xmm0            # gen=66 0f d9 05  dis=66 0f d9 05 90 90 90
    pminub %xmm0,%xmm0                  # gen=66 0f da c0  dis=66 0f da c0
    pminub 0x90909090,%xmm0             # gen=66 0f da 05  dis=66 0f da 05 90 90 90
    pand   %xmm0,%xmm0                  # gen=66 0f db c0  dis=66 0f db c0
    pand   0x90909090,%xmm0             # gen=66 0f db 05  dis=66 0f db 05 90 90 90
    paddusb %xmm0,%xmm0                 # gen=66 0f dc c0  dis=66 0f dc c0
    paddusb 0x90909090,%xmm0            # gen=66 0f dc 05  dis=66 0f dc 05 90 90 90
    paddusw %xmm0,%xmm0                 # gen=66 0f dd c0  dis=66 0f dd c0
    paddusw 0x90909090,%xmm0            # gen=66 0f dd 05  dis=66 0f dd 05 90 90 90
    pmaxub %xmm0,%xmm0                  # gen=66 0f de c0  dis=66 0f de c0
    pmaxub 0x90909090,%xmm0             # gen=66 0f de 05  dis=66 0f de 05 90 90 90
    pandn  %xmm0,%xmm0                  # gen=66 0f df c0  dis=66 0f df c0
    pandn  0x90909090,%xmm0             # gen=66 0f df 05  dis=66 0f df 05 90 90 90
    pavgb  %xmm0,%xmm0                  # gen=66 0f e0 c0  dis=66 0f e0 c0
    pavgb  0x90909090,%xmm0             # gen=66 0f e0 05  dis=66 0f e0 05 90 90 90
    psraw  %xmm0,%xmm0                  # gen=66 0f e1 c0  dis=66 0f e1 c0
    psraw  0x90909090,%xmm0             # gen=66 0f e1 05  dis=66 0f e1 05 90 90 90
    psrad  %xmm0,%xmm0                  # gen=66 0f e2 c0  dis=66 0f e2 c0
    psrad  0x90909090,%xmm0             # gen=66 0f e2 05  dis=66 0f e2 05 90 90 90
    pavgw  %xmm0,%xmm0                  # gen=66 0f e3 c0  dis=66 0f e3 c0
    pavgw  0x90909090,%xmm0             # gen=66 0f e3 05  dis=66 0f e3 05 90 90 90
    pmulhuw %xmm0,%xmm0                 # gen=66 0f e4 c0  dis=66 0f e4 c0
    pmulhuw 0x90909090,%xmm0            # gen=66 0f e4 05  dis=66 0f e4 05 90 90 90
    pmulhw %xmm0,%xmm0                  # gen=66 0f e5 c0  dis=66 0f e5 c0
    pmulhw 0x90909090,%xmm0             # gen=66 0f e5 05  dis=66 0f e5 05 90 90 90
    cvttpd2dq %xmm0,%xmm0               # gen=66 0f e6 c0  dis=66 0f e6 c0
    cvttpd2dq 0x90909090,%xmm0          # gen=66 0f e6 05  dis=66 0f e6 05 90 90 90
    movntdq %xmm0,0x90909090            # gen=66 0f e7 05  dis=66 0f e7 05 90 90 90
    psubsb %xmm0,%xmm0                  # gen=66 0f e8 c0  dis=66 0f e8 c0
    psubsb 0x90909090,%xmm0             # gen=66 0f e8 05  dis=66 0f e8 05 90 90 90
    psubsw %xmm0,%xmm0                  # gen=66 0f e9 c0  dis=66 0f e9 c0
    psubsw 0x90909090,%xmm0             # gen=66 0f e9 05  dis=66 0f e9 05 90 90 90
    pminsw %xmm0,%xmm0                  # gen=66 0f ea c0  dis=66 0f ea c0
    pminsw 0x90909090,%xmm0             # gen=66 0f ea 05  dis=66 0f ea 05 90 90 90
    por    %xmm0,%xmm0                  # gen=66 0f eb c0  dis=66 0f eb c0
    por    0x90909090,%xmm0             # gen=66 0f eb 05  dis=66 0f eb 05 90 90 90
    paddsb %xmm0,%xmm0                  # gen=66 0f ec c0  dis=66 0f ec c0
    paddsb 0x90909090,%xmm0             # gen=66 0f ec 05  dis=66 0f ec 05 90 90 90
    paddsw %xmm0,%xmm0                  # gen=66 0f ed c0  dis=66 0f ed c0
    paddsw 0x90909090,%xmm0             # gen=66 0f ed 05  dis=66 0f ed 05 90 90 90
    pmaxsw %xmm0,%xmm0                  # gen=66 0f ee c0  dis=66 0f ee c0
    pmaxsw 0x90909090,%xmm0             # gen=66 0f ee 05  dis=66 0f ee 05 90 90 90
    pxor   %xmm0,%xmm0                  # gen=66 0f ef c0  dis=66 0f ef c0
    pxor   0x90909090,%xmm0             # gen=66 0f ef 05  dis=66 0f ef 05 90 90 90
    psllw  %xmm0,%xmm0                  # gen=66 0f f1 c0  dis=66 0f f1 c0
    psllw  0x90909090,%xmm0             # gen=66 0f f1 05  dis=66 0f f1 05 90 90 90
    pslld  %xmm0,%xmm0                  # gen=66 0f f2 c0  dis=66 0f f2 c0
    pslld  0x90909090,%xmm0             # gen=66 0f f2 05  dis=66 0f f2 05 90 90 90
    psllq  %xmm0,%xmm0                  # gen=66 0f f3 c0  dis=66 0f f3 c0
    psllq  0x90909090,%xmm0             # gen=66 0f f3 05  dis=66 0f f3 05 90 90 90
    pmuludq %xmm0,%xmm0                 # gen=66 0f f4 c0  dis=66 0f f4 c0
    pmuludq 0x90909090,%xmm0            # gen=66 0f f4 05  dis=66 0f f4 05 90 90 90
    pmaddwd %xmm0,%xmm0                 # gen=66 0f f5 c0  dis=66 0f f5 c0
    pmaddwd 0x90909090,%xmm0            # gen=66 0f f5 05  dis=66 0f f5 05 90 90 90
    psadbw %xmm0,%xmm0                  # gen=66 0f f6 c0  dis=66 0f f6 c0
    psadbw 0x90909090,%xmm0             # gen=66 0f f6 05  dis=66 0f f6 05 90 90 90
    maskmovdqu %xmm0,%xmm0              # gen=66 0f f7 c0  dis=66 0f f7 c0
    psubb  %xmm0,%xmm0                  # gen=66 0f f8 c0  dis=66 0f f8 c0
    psubb  0x90909090,%xmm0             # gen=66 0f f8 05  dis=66 0f f8 05 90 90 90
    psubw  %xmm0,%xmm0                  # gen=66 0f f9 c0  dis=66 0f f9 c0
    psubw  0x90909090,%xmm0             # gen=66 0f f9 05  dis=66 0f f9 05 90 90 90
    psubd  %xmm0,%xmm0                  # gen=66 0f fa c0  dis=66 0f fa c0
    psubd  0x90909090,%xmm0             # gen=66 0f fa 05  dis=66 0f fa 05 90 90 90
    psubq  %xmm0,%xmm0                  # gen=66 0f fb c0  dis=66 0f fb c0
    psubq  0x90909090,%xmm0             # gen=66 0f fb 05  dis=66 0f fb 05 90 90 90
    paddb  %xmm0,%xmm0                  # gen=66 0f fc c0  dis=66 0f fc c0
    paddb  0x90909090,%xmm0             # gen=66 0f fc 05  dis=66 0f fc 05 90 90 90
    paddw  %xmm0,%xmm0                  # gen=66 0f fd c0  dis=66 0f fd c0
    paddw  0x90909090,%xmm0             # gen=66 0f fd 05  dis=66 0f fd 05 90 90 90
    paddd  %xmm0,%xmm0                  # gen=66 0f fe c0  dis=66 0f fe c0
    paddd  0x90909090,%xmm0             # gen=66 0f fe 05  dis=66 0f fe 05 90 90 90
    ud0    %ax,%ax                      # gen=66 0f ff c0  dis=66 0f ff c0
    ud0    0x90909090,%ax               # gen=66 0f ff 05  dis=66 0f ff 05 90 90 90
    pshufb %xmm0,%xmm0                  # gen=66 0f 38 00 c0  dis=66 0f 38 00 c0
    pshufb 0x90909090,%xmm0             # gen=66 0f 38 00 05  dis=66 0f 38 00 05 90 90
    phaddw %xmm0,%xmm0                  # gen=66 0f 38 01 c0  dis=66 0f 38 01 c0
    phaddw 0x90909090,%xmm0             # gen=66 0f 38 01 05  dis=66 0f 38 01 05 90 90
    phaddd %xmm0,%xmm0                  # gen=66 0f 38 02 c0  dis=66 0f 38 02 c0
    phaddd 0x90909090,%xmm0             # gen=66 0f 38 02 05  dis=66 0f 38 02 05 90 90
    phaddsw %xmm0,%xmm0                 # gen=66 0f 38 03 c0  dis=66 0f 38 03 c0
    phaddsw 0x90909090,%xmm0            # gen=66 0f 38 03 05  dis=66 0f 38 03 05 90 90
    pmaddubsw %xmm0,%xmm0               # gen=66 0f 38 04 c0  dis=66 0f 38 04 c0
    pmaddubsw 0x90909090,%xmm0          # gen=66 0f 38 04 05  dis=66 0f 38 04 05 90 90
    phsubw %xmm0,%xmm0                  # gen=66 0f 38 05 c0  dis=66 0f 38 05 c0
    phsubw 0x90909090,%xmm0             # gen=66 0f 38 05 05  dis=66 0f 38 05 05 90 90
    phsubd %xmm0,%xmm0                  # gen=66 0f 38 06 c0  dis=66 0f 38 06 c0
    phsubd 0x90909090,%xmm0             # gen=66 0f 38 06 05  dis=66 0f 38 06 05 90 90
    phsubsw %xmm0,%xmm0                 # gen=66 0f 38 07 c0  dis=66 0f 38 07 c0
    phsubsw 0x90909090,%xmm0            # gen=66 0f 38 07 05  dis=66 0f 38 07 05 90 90
    psignb %xmm0,%xmm0                  # gen=66 0f 38 08 c0  dis=66 0f 38 08 c0
    psignb 0x90909090,%xmm0             # gen=66 0f 38 08 05  dis=66 0f 38 08 05 90 90
    psignw %xmm0,%xmm0                  # gen=66 0f 38 09 c0  dis=66 0f 38 09 c0
    psignw 0x90909090,%xmm0             # gen=66 0f 38 09 05  dis=66 0f 38 09 05 90 90
    psignd %xmm0,%xmm0                  # gen=66 0f 38 0a c0  dis=66 0f 38 0a c0
    psignd 0x90909090,%xmm0             # gen=66 0f 38 0a 05  dis=66 0f 38 0a 05 90 90
    pmulhrsw %xmm0,%xmm0                # gen=66 0f 38 0b c0  dis=66 0f 38 0b c0
    pmulhrsw 0x90909090,%xmm0           # gen=66 0f 38 0b 05  dis=66 0f 38 0b 05 90 90
    pblendvb %xmm0,%xmm0,%xmm0          # gen=66 0f 38 10 c0  dis=66 0f 38 10 c0
    pblendvb %xmm0,0x90909090,%xmm0     # gen=66 0f 38 10 05  dis=66 0f 38 10 05 90 90
    blendvps %xmm0,%xmm0,%xmm0          # gen=66 0f 38 14 c0  dis=66 0f 38 14 c0
    blendvps %xmm0,0x90909090,%xmm0     # gen=66 0f 38 14 05  dis=66 0f 38 14 05 90 90
    blendvpd %xmm0,%xmm0,%xmm0          # gen=66 0f 38 15 c0  dis=66 0f 38 15 c0
    blendvpd %xmm0,0x90909090,%xmm0     # gen=66 0f 38 15 05  dis=66 0f 38 15 05 90 90
    ptest  %xmm0,%xmm0                  # gen=66 0f 38 17 c0  dis=66 0f 38 17 c0
    ptest  0x90909090,%xmm0             # gen=66 0f 38 17 05  dis=66 0f 38 17 05 90 90
    pabsb  %xmm0,%xmm0                  # gen=66 0f 38 1c c0  dis=66 0f 38 1c c0
    pabsb  0x90909090,%xmm0             # gen=66 0f 38 1c 05  dis=66 0f 38 1c 05 90 90
    pabsw  %xmm0,%xmm0                  # gen=66 0f 38 1d c0  dis=66 0f 38 1d c0
    pabsw  0x90909090,%xmm0             # gen=66 0f 38 1d 05  dis=66 0f 38 1d 05 90 90
    pabsd  %xmm0,%xmm0                  # gen=66 0f 38 1e c0  dis=66 0f 38 1e c0
    pabsd  0x90909090,%xmm0             # gen=66 0f 38 1e 05  dis=66 0f 38 1e 05 90 90
    pmovsxbw %xmm0,%xmm0                # gen=66 0f 38 20 c0  dis=66 0f 38 20 c0
    pmovsxbw 0x90909090,%xmm0           # gen=66 0f 38 20 05  dis=66 0f 38 20 05 90 90
    pmovsxbd %xmm0,%xmm0                # gen=66 0f 38 21 c0  dis=66 0f 38 21 c0
    pmovsxbd 0x90909090,%xmm0           # gen=66 0f 38 21 05  dis=66 0f 38 21 05 90 90
    pmovsxbq %xmm0,%xmm0                # gen=66 0f 38 22 c0  dis=66 0f 38 22 c0
    pmovsxbq 0x90909090,%xmm0           # gen=66 0f 38 22 05  dis=66 0f 38 22 05 90 90
    pmovsxwd %xmm0,%xmm0                # gen=66 0f 38 23 c0  dis=66 0f 38 23 c0
    pmovsxwd 0x90909090,%xmm0           # gen=66 0f 38 23 05  dis=66 0f 38 23 05 90 90
    pmovsxwq %xmm0,%xmm0                # gen=66 0f 38 24 c0  dis=66 0f 38 24 c0
    pmovsxwq 0x90909090,%xmm0           # gen=66 0f 38 24 05  dis=66 0f 38 24 05 90 90
    pmovsxdq %xmm0,%xmm0                # gen=66 0f 38 25 c0  dis=66 0f 38 25 c0
    pmovsxdq 0x90909090,%xmm0           # gen=66 0f 38 25 05  dis=66 0f 38 25 05 90 90
    pmuldq %xmm0,%xmm0                  # gen=66 0f 38 28 c0  dis=66 0f 38 28 c0
    pmuldq 0x90909090,%xmm0             # gen=66 0f 38 28 05  dis=66 0f 38 28 05 90 90
    pcmpeqq %xmm0,%xmm0                 # gen=66 0f 38 29 c0  dis=66 0f 38 29 c0
    pcmpeqq 0x90909090,%xmm0            # gen=66 0f 38 29 05  dis=66 0f 38 29 05 90 90
    movntdqa 0x90909090,%xmm0           # gen=66 0f 38 2a 05  dis=66 0f 38 2a 05 90 90
    movntdqa 0x90909090,%xmm1           # gen=66 0f 38 2a 0d  dis=66 0f 38 2a 0d 90 90
    movntdqa 0x90909090,%xmm2           # gen=66 0f 38 2a 15  dis=66 0f 38 2a 15 90 90
    movntdqa 0x90909090,%xmm3           # gen=66 0f 38 2a 1d  dis=66 0f 38 2a 1d 90 90
    movntdqa 0x90909090,%xmm4           # gen=66 0f 38 2a 25  dis=66 0f 38 2a 25 90 90
    movntdqa 0x90909090,%xmm5           # gen=66 0f 38 2a 2d  dis=66 0f 38 2a 2d 90 90
    movntdqa 0x90909090,%xmm6           # gen=66 0f 38 2a 35  dis=66 0f 38 2a 35 90 90
    movntdqa 0x90909090,%xmm7           # gen=66 0f 38 2a 3d  dis=66 0f 38 2a 3d 90 90
    packusdw %xmm0,%xmm0                # gen=66 0f 38 2b c0  dis=66 0f 38 2b c0
    packusdw 0x90909090,%xmm0           # gen=66 0f 38 2b 05  dis=66 0f 38 2b 05 90 90
    pmovzxbw %xmm0,%xmm0                # gen=66 0f 38 30 c0  dis=66 0f 38 30 c0
    pmovzxbw 0x90909090,%xmm0           # gen=66 0f 38 30 05  dis=66 0f 38 30 05 90 90
    pmovzxbd %xmm0,%xmm0                # gen=66 0f 38 31 c0  dis=66 0f 38 31 c0
    pmovzxbd 0x90909090,%xmm0           # gen=66 0f 38 31 05  dis=66 0f 38 31 05 90 90
    pmovzxbq %xmm0,%xmm0                # gen=66 0f 38 32 c0  dis=66 0f 38 32 c0
    pmovzxbq 0x90909090,%xmm0           # gen=66 0f 38 32 05  dis=66 0f 38 32 05 90 90
    pmovzxwd %xmm0,%xmm0                # gen=66 0f 38 33 c0  dis=66 0f 38 33 c0
    pmovzxwd 0x90909090,%xmm0           # gen=66 0f 38 33 05  dis=66 0f 38 33 05 90 90
    pmovzxwq %xmm0,%xmm0                # gen=66 0f 38 34 c0  dis=66 0f 38 34 c0
    pmovzxwq 0x90909090,%xmm0           # gen=66 0f 38 34 05  dis=66 0f 38 34 05 90 90
    pmovzxdq %xmm0,%xmm0                # gen=66 0f 38 35 c0  dis=66 0f 38 35 c0
    pmovzxdq 0x90909090,%xmm0           # gen=66 0f 38 35 05  dis=66 0f 38 35 05 90 90
    pcmpgtq %xmm0,%xmm0                 # gen=66 0f 38 37 c0  dis=66 0f 38 37 c0
    pcmpgtq 0x90909090,%xmm0            # gen=66 0f 38 37 05  dis=66 0f 38 37 05 90 90
    pminsb %xmm0,%xmm0                  # gen=66 0f 38 38 c0  dis=66 0f 38 38 c0
    pminsb 0x90909090,%xmm0             # gen=66 0f 38 38 05  dis=66 0f 38 38 05 90 90
    pminsd %xmm0,%xmm0                  # gen=66 0f 38 39 c0  dis=66 0f 38 39 c0
    pminsd 0x90909090,%xmm0             # gen=66 0f 38 39 05  dis=66 0f 38 39 05 90 90
    pminuw %xmm0,%xmm0                  # gen=66 0f 38 3a c0  dis=66 0f 38 3a c0
    pminuw 0x90909090,%xmm0             # gen=66 0f 38 3a 05  dis=66 0f 38 3a 05 90 90
    pminud %xmm0,%xmm0                  # gen=66 0f 38 3b c0  dis=66 0f 38 3b c0
    pminud 0x90909090,%xmm0             # gen=66 0f 38 3b 05  dis=66 0f 38 3b 05 90 90
    pmaxsb %xmm0,%xmm0                  # gen=66 0f 38 3c c0  dis=66 0f 38 3c c0
    pmaxsb 0x90909090,%xmm0             # gen=66 0f 38 3c 05  dis=66 0f 38 3c 05 90 90
    pmaxsd %xmm0,%xmm0                  # gen=66 0f 38 3d c0  dis=66 0f 38 3d c0
    pmaxsd 0x90909090,%xmm0             # gen=66 0f 38 3d 05  dis=66 0f 38 3d 05 90 90
    pmaxuw %xmm0,%xmm0                  # gen=66 0f 38 3e c0  dis=66 0f 38 3e c0
    pmaxuw 0x90909090,%xmm0             # gen=66 0f 38 3e 05  dis=66 0f 38 3e 05 90 90
    pmaxud %xmm0,%xmm0                  # gen=66 0f 38 3f c0  dis=66 0f 38 3f c0
    pmaxud 0x90909090,%xmm0             # gen=66 0f 38 3f 05  dis=66 0f 38 3f 05 90 90
    pmulld %xmm0,%xmm0                  # gen=66 0f 38 40 c0  dis=66 0f 38 40 c0
    pmulld 0x90909090,%xmm0             # gen=66 0f 38 40 05  dis=66 0f 38 40 05 90 90
    phminposuw %xmm0,%xmm0              # gen=66 0f 38 41 c0  dis=66 0f 38 41 c0
    phminposuw 0x90909090,%xmm0         # gen=66 0f 38 41 05  dis=66 0f 38 41 05 90 90
    invept (bad),%eax                   # gen=66 0f 38 80 c0  dis=66 0f
    invept 0x90909090,%eax              # gen=66 0f 38 80 05  dis=66 0f 38 80 05 90 90
    invvpid (bad),%eax                  # gen=66 0f 38 81 c0  dis=66 0f
    invvpid 0x90909090,%eax             # gen=66 0f 38 81 05  dis=66 0f 38 81 05 90 90
    invpcid (bad),%eax                  # gen=66 0f 38 82 c0  dis=66 0f
    invpcid 0x90909090,%eax             # gen=66 0f 38 82 05  dis=66 0f 38 82 05 90 90
    gf2p8mulb %xmm0,%xmm0               # gen=66 0f 38 cf c0  dis=66 0f 38 cf c0
    gf2p8mulb 0x90909090,%xmm0          # gen=66 0f 38 cf 05  dis=66 0f 38 cf 05 90 90
    aesimc %xmm0,%xmm0                  # gen=66 0f 38 db c0  dis=66 0f 38 db c0
    aesimc 0x90909090,%xmm0             # gen=66 0f 38 db 05  dis=66 0f 38 db 05 90 90
    aesenc %xmm0,%xmm0                  # gen=66 0f 38 dc c0  dis=66 0f 38 dc c0
    aesenc 0x90909090,%xmm0             # gen=66 0f 38 dc 05  dis=66 0f 38 dc 05 90 90
    aesenclast %xmm0,%xmm0              # gen=66 0f 38 dd c0  dis=66 0f 38 dd c0
    aesenclast 0x90909090,%xmm0         # gen=66 0f 38 dd 05  dis=66 0f 38 dd 05 90 90
    aesdec %xmm0,%xmm0                  # gen=66 0f 38 de c0  dis=66 0f 38 de c0
    aesdec 0x90909090,%xmm0             # gen=66 0f 38 de 05  dis=66 0f 38 de 05 90 90
    aesdeclast %xmm0,%xmm0              # gen=66 0f 38 df c0  dis=66 0f 38 df c0
    aesdeclast 0x90909090,%xmm0         # gen=66 0f 38 df 05  dis=66 0f 38 df 05 90 90
    movbe  (bad),%ax                    # gen=66 0f 38 f0 c0  dis=66 0f
    movbe  0x90909090,%ax               # gen=66 0f 38 f0 05  dis=66 0f 38 f0 05 90 90
    movbe  %ax,(bad)                    # gen=66 0f 38 f1 c0  dis=66 0f
    movbe  %ax,0x90909090               # gen=66 0f 38 f1 05  dis=66 0f 38 f1 05 90 90
    wrussd %eax,0x90909090              # gen=66 0f 38 f5 05  dis=66 0f 38 f5 05 90 90
    wrussd %ecx,0x90909090              # gen=66 0f 38 f5 0d  dis=66 0f 38 f5 0d 90 90
    wrussd %edx,0x90909090              # gen=66 0f 38 f5 15  dis=66 0f 38 f5 15 90 90
    wrussd %ebx,0x90909090              # gen=66 0f 38 f5 1d  dis=66 0f 38 f5 1d 90 90
    wrussd %esp,0x90909090              # gen=66 0f 38 f5 25  dis=66 0f 38 f5 25 90 90
    wrussd %ebp,0x90909090              # gen=66 0f 38 f5 2d  dis=66 0f 38 f5 2d 90 90
    wrussd %esi,0x90909090              # gen=66 0f 38 f5 35  dis=66 0f 38 f5 35 90 90
    wrussd %edi,0x90909090              # gen=66 0f 38 f5 3d  dis=66 0f 38 f5 3d 90 90
    adcx   %eax,%eax                    # gen=66 0f 38 f6 c0  dis=66 0f 38 f6 c0
    adcx   0x90909090,%eax              # gen=66 0f 38 f6 05  dis=66 0f 38 f6 05 90 90
    movdir64b 0x90909090,%eax           # gen=66 0f 38 f8 05  dis=66 0f 38 f8 05 90 90
    aand   %eax,(bad)                   # gen=66 0f 38 fc c0  dis=66 0f
    aand   %eax,0x90909090              # gen=66 0f 38 fc 05  dis=66 0f 38 fc 05 90 90
    roundps $0x90,%xmm0,%xmm0           # gen=66 0f 3a 08 c0  dis=66 0f 3a 08 c0 90
    roundps $0x90,0x90909090,%xmm0      # gen=66 0f 3a 08 05  dis=66 0f 3a 08 05 90 90
    roundpd $0x90,%xmm0,%xmm0           # gen=66 0f 3a 09 c0  dis=66 0f 3a 09 c0 90
    roundpd $0x90,0x90909090,%xmm0      # gen=66 0f 3a 09 05  dis=66 0f 3a 09 05 90 90
    roundss $0x90,%xmm0,%xmm0           # gen=66 0f 3a 0a c0  dis=66 0f 3a 0a c0 90
    roundss $0x90,0x90909090,%xmm0      # gen=66 0f 3a 0a 05  dis=66 0f 3a 0a 05 90 90
    roundsd $0x90,%xmm0,%xmm0           # gen=66 0f 3a 0b c0  dis=66 0f 3a 0b c0 90
    roundsd $0x90,0x90909090,%xmm0      # gen=66 0f 3a 0b 05  dis=66 0f 3a 0b 05 90 90
    blendps $0x90,%xmm0,%xmm0           # gen=66 0f 3a 0c c0  dis=66 0f 3a 0c c0 90
    blendps $0x90,0x90909090,%xmm0      # gen=66 0f 3a 0c 05  dis=66 0f 3a 0c 05 90 90
    blendpd $0x90,%xmm0,%xmm0           # gen=66 0f 3a 0d c0  dis=66 0f 3a 0d c0 90
    blendpd $0x90,0x90909090,%xmm0      # gen=66 0f 3a 0d 05  dis=66 0f 3a 0d 05 90 90
    pblendw $0x90,%xmm0,%xmm0           # gen=66 0f 3a 0e c0  dis=66 0f 3a 0e c0 90
    pblendw $0x90,0x90909090,%xmm0      # gen=66 0f 3a 0e 05  dis=66 0f 3a 0e 05 90 90
    palignr $0x90,%xmm0,%xmm0           # gen=66 0f 3a 0f c0  dis=66 0f 3a 0f c0 90
    palignr $0x90,0x90909090,%xmm0      # gen=66 0f 3a 0f 05  dis=66 0f 3a 0f 05 90 90
    pextrb $0x90,%xmm0,%eax             # gen=66 0f 3a 14 c0  dis=66 0f 3a 14 c0 90
    pextrb $0x90,%xmm0,0x90909090       # gen=66 0f 3a 14 05  dis=66 0f 3a 14 05 90 90
    pextrw $0x90,%xmm0,0x90909090       # gen=66 0f 3a 15 05  dis=66 0f 3a 15 05 90 90
    pextrd $0x90,%xmm0,%eax             # gen=66 0f 3a 16 c0  dis=66 0f 3a 16 c0 90
    pextrd $0x90,%xmm0,0x90909090       # gen=66 0f 3a 16 05  dis=66 0f 3a 16 05 90 90
    extractps $0x90,%xmm0,%eax          # gen=66 0f 3a 17 c0  dis=66 0f 3a 17 c0 90
    extractps $0x90,%xmm0,0x90909090    # gen=66 0f 3a 17 05  dis=66 0f 3a 17 05 90 90
    pinsrb $0x90,%eax,%xmm0             # gen=66 0f 3a 20 c0  dis=66 0f 3a 20 c0 90
    pinsrb $0x90,0x90909090,%xmm0       # gen=66 0f 3a 20 05  dis=66 0f 3a 20 05 90 90
    insertps $0x90,%xmm0,%xmm0          # gen=66 0f 3a 21 c0  dis=66 0f 3a 21 c0 90
    insertps $0x90,0x90909090,%xmm0     # gen=66 0f 3a 21 05  dis=66 0f 3a 21 05 90 90
    pinsrd $0x90,%eax,%xmm0             # gen=66 0f 3a 22 c0  dis=66 0f 3a 22 c0 90
    pinsrd $0x90,0x90909090,%xmm0       # gen=66 0f 3a 22 05  dis=66 0f 3a 22 05 90 90
    dpps   $0x90,%xmm0,%xmm0            # gen=66 0f 3a 40 c0  dis=66 0f 3a 40 c0 90
    dpps   $0x90,0x90909090,%xmm0       # gen=66 0f 3a 40 05  dis=66 0f 3a 40 05 90 90
    dppd   $0x90,%xmm0,%xmm0            # gen=66 0f 3a 41 c0  dis=66 0f 3a 41 c0 90
    dppd   $0x90,0x90909090,%xmm0       # gen=66 0f 3a 41 05  dis=66 0f 3a 41 05 90 90
    mpsadbw $0x90,%xmm0,%xmm0           # gen=66 0f 3a 42 c0  dis=66 0f 3a 42 c0 90
    mpsadbw $0x90,0x90909090,%xmm0      # gen=66 0f 3a 42 05  dis=66 0f 3a 42 05 90 90
    pclmulqdq $0x90,%xmm0,%xmm0         # gen=66 0f 3a 44 c0  dis=66 0f 3a 44 c0 90
    pclmulqdq $0x90,0x90909090,%xmm0    # gen=66 0f 3a 44 05  dis=66 0f 3a 44 05 90 90
    pcmpestrm $0x90,%xmm0,%xmm0         # gen=66 0f 3a 60 c0  dis=66 0f 3a 60 c0 90
    pcmpestrm $0x90,0x90909090,%xmm0    # gen=66 0f 3a 60 05  dis=66 0f 3a 60 05 90 90
    pcmpestri $0x90,%xmm0,%xmm0         # gen=66 0f 3a 61 c0  dis=66 0f 3a 61 c0 90
    pcmpestri $0x90,0x90909090,%xmm0    # gen=66 0f 3a 61 05  dis=66 0f 3a 61 05 90 90
    pcmpistrm $0x90,%xmm0,%xmm0         # gen=66 0f 3a 62 c0  dis=66 0f 3a 62 c0 90
    pcmpistrm $0x90,0x90909090,%xmm0    # gen=66 0f 3a 62 05  dis=66 0f 3a 62 05 90 90
    pcmpistri $0x90,%xmm0,%xmm0         # gen=66 0f 3a 63 c0  dis=66 0f 3a 63 c0 90
    pcmpistri $0x90,0x90909090,%xmm0    # gen=66 0f 3a 63 05  dis=66 0f 3a 63 05 90 90
    gf2p8affineqb $0x90,%xmm0,%xmm0     # gen=66 0f 3a ce c0  dis=66 0f 3a ce c0 90
    gf2p8affineqb $0x90,0x90909090,%xmm0 # gen=66 0f 3a ce 05  dis=66 0f 3a ce 05 90 90
    gf2p8affineinvqb $0x90,%xmm0,%xmm0  # gen=66 0f 3a cf c0  dis=66 0f 3a cf c0 90
    gf2p8affineinvqb $0x90,0x90909090,%xmm0 # gen=66 0f 3a cf 05  dis=66 0f 3a cf 05 90 90
    aeskeygenassist $0x90,%xmm0,%xmm0   # gen=66 0f 3a df c0  dis=66 0f 3a df c0 90
    aeskeygenassist $0x90,0x90909090,%xmm0 # gen=66 0f 3a df 05  dis=66 0f 3a df 05 90 90
    repnz insb (%dx),%es:(%edi)         # gen=f2 6c c0  dis=f2 6c
    repnz insl (%dx),%es:(%edi)         # gen=f2 6d c0  dis=f2 6d
    repnz outsb %ds:(%esi),(%dx)        # gen=f2 6e c0  dis=f2 6e
    repnz outsl %ds:(%esi),(%dx)        # gen=f2 6f c0  dis=f2 6f
    bnd jo 0x161c3                      # gen=f2 70 c0  dis=f2 70 c0
    bnd jo 0x16218                      # gen=f2 70 05  dis=f2 70 05
    bnd jno 0x161e3                     # gen=f2 71 c0  dis=f2 71 c0
    bnd jno 0x16238                     # gen=f2 71 05  dis=f2 71 05
    bnd jb 0x16203                      # gen=f2 72 c0  dis=f2 72 c0
    bnd jb 0x16258                      # gen=f2 72 05  dis=f2 72 05
    bnd jae 0x16223                     # gen=f2 73 c0  dis=f2 73 c0
    bnd jae 0x16278                     # gen=f2 73 05  dis=f2 73 05
    bnd je 0x16243                      # gen=f2 74 c0  dis=f2 74 c0
    bnd je 0x16298                      # gen=f2 74 05  dis=f2 74 05
    bnd jne 0x16263                     # gen=f2 75 c0  dis=f2 75 c0
    bnd jne 0x162b8                     # gen=f2 75 05  dis=f2 75 05
    bnd jbe 0x16283                     # gen=f2 76 c0  dis=f2 76 c0
    bnd jbe 0x162d8                     # gen=f2 76 05  dis=f2 76 05
    bnd ja 0x162a3                      # gen=f2 77 c0  dis=f2 77 c0
    bnd ja 0x162f8                      # gen=f2 77 05  dis=f2 77 05
    bnd js 0x162c3                      # gen=f2 78 c0  dis=f2 78 c0
    bnd js 0x16318                      # gen=f2 78 05  dis=f2 78 05
    bnd jns 0x162e3                     # gen=f2 79 c0  dis=f2 79 c0
    bnd jns 0x16338                     # gen=f2 79 05  dis=f2 79 05
    bnd jp 0x16303                      # gen=f2 7a c0  dis=f2 7a c0
    bnd jp 0x16358                      # gen=f2 7a 05  dis=f2 7a 05
    bnd jnp 0x16323                     # gen=f2 7b c0  dis=f2 7b c0
    bnd jnp 0x16378                     # gen=f2 7b 05  dis=f2 7b 05
    bnd jl 0x16343                      # gen=f2 7c c0  dis=f2 7c c0
    bnd jl 0x16398                      # gen=f2 7c 05  dis=f2 7c 05
    bnd jge 0x16363                     # gen=f2 7d c0  dis=f2 7d c0
    bnd jge 0x163b8                     # gen=f2 7d 05  dis=f2 7d 05
    bnd jle 0x16383                     # gen=f2 7e c0  dis=f2 7e c0
    bnd jle 0x163d8                     # gen=f2 7e 05  dis=f2 7e 05
    bnd jg 0x163a3                      # gen=f2 7f c0  dis=f2 7f c0
    bnd jg 0x163f8                      # gen=f2 7f 05  dis=f2 7f 05
    xacquire xchg %al,0x90909090        # gen=f2 86 05  dis=f2 86 05 90 90 90 90
    xacquire xchg %cl,0x90909090        # gen=f2 86 0d  dis=f2 86 0d 90 90 90 90
    xacquire xchg %dl,0x90909090        # gen=f2 86 15  dis=f2 86 15 90 90 90 90
    xacquire xchg %bl,0x90909090        # gen=f2 86 1d  dis=f2 86 1d 90 90 90 90
    xacquire xchg %ah,0x90909090        # gen=f2 86 25  dis=f2 86 25 90 90 90 90
    xacquire xchg %ch,0x90909090        # gen=f2 86 2d  dis=f2 86 2d 90 90 90 90
    xacquire xchg %dh,0x90909090        # gen=f2 86 35  dis=f2 86 35 90 90 90 90
    xacquire xchg %bh,0x90909090        # gen=f2 86 3d  dis=f2 86 3d 90 90 90 90
    xacquire xchg %eax,0x90909090       # gen=f2 87 05  dis=f2 87 05 90 90 90 90
    xacquire xchg %ecx,0x90909090       # gen=f2 87 0d  dis=f2 87 0d 90 90 90 90
    xacquire xchg %edx,0x90909090       # gen=f2 87 15  dis=f2 87 15 90 90 90 90
    xacquire xchg %ebx,0x90909090       # gen=f2 87 1d  dis=f2 87 1d 90 90 90 90
    xacquire xchg %esp,0x90909090       # gen=f2 87 25  dis=f2 87 25 90 90 90 90
    xacquire xchg %ebp,0x90909090       # gen=f2 87 2d  dis=f2 87 2d 90 90 90 90
    xacquire xchg %esi,0x90909090       # gen=f2 87 35  dis=f2 87 35 90 90 90 90
    xacquire xchg %edi,0x90909090       # gen=f2 87 3d  dis=f2 87 3d 90 90 90 90
    repnz nop                           # gen=f2 90 c0  dis=f2 90
    repnz movsb %ds:(%esi),%es:(%edi)   # gen=f2 a4 c0  dis=f2 a4
    repnz movsl %ds:(%esi),%es:(%edi)   # gen=f2 a5 c0  dis=f2 a5
    repnz cmpsb %es:(%edi),%ds:(%esi)   # gen=f2 a6 c0  dis=f2 a6
    repnz cmpsl %es:(%edi),%ds:(%esi)   # gen=f2 a7 c0  dis=f2 a7
    repnz stos %al,%es:(%edi)           # gen=f2 aa c0  dis=f2 aa
    repnz stos %eax,%es:(%edi)          # gen=f2 ab c0  dis=f2 ab
    repnz lods %ds:(%esi),%al           # gen=f2 ac c0  dis=f2 ac
    repnz lods %ds:(%esi),%eax          # gen=f2 ad c0  dis=f2 ad
    repnz scas %es:(%edi),%al           # gen=f2 ae c0  dis=f2 ae
    repnz scas %es:(%edi),%eax          # gen=f2 af c0  dis=f2 af
    bnd ret $0x90c0                     # gen=f2 c2 c0  dis=f2 c2 c0 90
    bnd ret $0x9005                     # gen=f2 c2 05  dis=f2 c2 05 90
    bnd ret                             # gen=f2 c3 c0  dis=f2 c3
    bnd call 0x90920386                 # gen=f2 e8 c0  dis=f2 e8 c0 90 90 90
    bnd call 0x909202db                 # gen=f2 e8 05  dis=f2 e8 05 90 90 90
    bnd jmp 0x909203a6                  # gen=f2 e9 c0  dis=f2 e9 c0 90 90 90
    bnd jmp 0x909202fb                  # gen=f2 e9 05  dis=f2 e9 05 90 90 90
    bnd jmp 0x172e3                     # gen=f2 eb c0  dis=f2 eb c0
    bnd jmp 0x17338                     # gen=f2 eb 05  dis=f2 eb 05
    movsd  %xmm0,%xmm0                  # gen=f2 0f 10 c0  dis=f2 0f 10 c0
    movsd  0x90909090,%xmm0             # gen=f2 0f 10 05  dis=f2 0f 10 05 90 90 90
    movsd  %xmm0,0x90909090             # gen=f2 0f 11 05  dis=f2 0f 11 05 90 90 90
    movddup %xmm0,%xmm0                 # gen=f2 0f 12 c0  dis=f2 0f 12 c0
    movddup 0x90909090,%xmm0            # gen=f2 0f 12 05  dis=f2 0f 12 05 90 90 90
    bndcu  %eax,%bnd0                   # gen=f2 0f 1a c0  dis=f2 0f 1a c0
    bndcu  0x90909090,%bnd0             # gen=f2 0f 1a 05  dis=f2 0f 1a 05 90 90 90
    bndcn  %eax,%bnd0                   # gen=f2 0f 1b c0  dis=f2 0f 1b c0
    bndcn  0x90909090,%bnd0             # gen=f2 0f 1b 05  dis=f2 0f 1b 05 90 90 90
    cvtsi2sd %eax,%xmm0                 # gen=f2 0f 2a c0  dis=f2 0f 2a c0
    cvtsi2sd 0x90909090,%xmm0           # gen=f2 0f 2a 05  dis=f2 0f 2a 05 90 90 90
    movntsd %xmm0,0x90909090            # gen=f2 0f 2b 05  dis=f2 0f 2b 05 90 90 90
    cvttsd2si %xmm0,%eax                # gen=f2 0f 2c c0  dis=f2 0f 2c c0
    cvttsd2si 0x90909090,%eax           # gen=f2 0f 2c 05  dis=f2 0f 2c 05 90 90 90
    cvtsd2si %xmm0,%eax                 # gen=f2 0f 2d c0  dis=f2 0f 2d c0
    cvtsd2si 0x90909090,%eax            # gen=f2 0f 2d 05  dis=f2 0f 2d 05 90 90 90
    crc32b -0x6f6f6f70(%eax),%edx       # gen=f2 0f 38 f0  dis=f2 0f 38 f0 90 90 90
    enqcmd -0x6f6f6f70(%eax),%edx       # gen=f2 0f 38 f8  dis=f2 0f 38 f8 90 90 90
    sqrtsd %xmm0,%xmm0                  # gen=f2 0f 51 c0  dis=f2 0f 51 c0
    sqrtsd 0x90909090,%xmm0             # gen=f2 0f 51 05  dis=f2 0f 51 05 90 90 90
    addsd  %xmm0,%xmm0                  # gen=f2 0f 58 c0  dis=f2 0f 58 c0
    addsd  0x90909090,%xmm0             # gen=f2 0f 58 05  dis=f2 0f 58 05 90 90 90
    mulsd  %xmm0,%xmm0                  # gen=f2 0f 59 c0  dis=f2 0f 59 c0
    mulsd  0x90909090,%xmm0             # gen=f2 0f 59 05  dis=f2 0f 59 05 90 90 90
    cvtsd2ss %xmm0,%xmm0                # gen=f2 0f 5a c0  dis=f2 0f 5a c0
    cvtsd2ss 0x90909090,%xmm0           # gen=f2 0f 5a 05  dis=f2 0f 5a 05 90 90 90
    subsd  %xmm0,%xmm0                  # gen=f2 0f 5c c0  dis=f2 0f 5c c0
    subsd  0x90909090,%xmm0             # gen=f2 0f 5c 05  dis=f2 0f 5c 05 90 90 90
    minsd  %xmm0,%xmm0                  # gen=f2 0f 5d c0  dis=f2 0f 5d c0
    minsd  0x90909090,%xmm0             # gen=f2 0f 5d 05  dis=f2 0f 5d 05 90 90 90
    divsd  %xmm0,%xmm0                  # gen=f2 0f 5e c0  dis=f2 0f 5e c0
    divsd  0x90909090,%xmm0             # gen=f2 0f 5e 05  dis=f2 0f 5e 05 90 90 90
    maxsd  %xmm0,%xmm0                  # gen=f2 0f 5f c0  dis=f2 0f 5f c0
    maxsd  0x90909090,%xmm0             # gen=f2 0f 5f 05  dis=f2 0f 5f 05 90 90 90
    pshuflw $0x90,%xmm0,%xmm0           # gen=f2 0f 70 c0  dis=f2 0f 70 c0 90
    pshuflw $0x90,0x90909090,%xmm0      # gen=f2 0f 70 05  dis=f2 0f 70 05 90 90 90
    insertq $0x90,$0x90,%xmm0,%xmm0     # gen=f2 0f 78 c0  dis=f2 0f 78 c0 90 90
    insertq %xmm0,%xmm0                 # gen=f2 0f 79 c0  dis=f2 0f 79 c0
    haddps %xmm0,%xmm0                  # gen=f2 0f 7c c0  dis=f2 0f 7c c0
    haddps 0x90909090,%xmm0             # gen=f2 0f 7c 05  dis=f2 0f 7c 05 90 90 90
    hsubps %xmm0,%xmm0                  # gen=f2 0f 7d c0  dis=f2 0f 7d c0
    hsubps 0x90909090,%xmm0             # gen=f2 0f 7d 05  dis=f2 0f 7d 05 90 90 90
    bnd jo 0x90921767                   # gen=f2 0f 80 c0  dis=f2 0f 80 c0 90 90 90
    bnd jo 0x909216bc                   # gen=f2 0f 80 05  dis=f2 0f 80 05 90 90 90
    bnd jno 0x90921787                  # gen=f2 0f 81 c0  dis=f2 0f 81 c0 90 90 90
    bnd jno 0x909216dc                  # gen=f2 0f 81 05  dis=f2 0f 81 05 90 90 90
    bnd jb 0x909217a7                   # gen=f2 0f 82 c0  dis=f2 0f 82 c0 90 90 90
    bnd jb 0x909216fc                   # gen=f2 0f 82 05  dis=f2 0f 82 05 90 90 90
    bnd jae 0x909217c7                  # gen=f2 0f 83 c0  dis=f2 0f 83 c0 90 90 90
    bnd jae 0x9092171c                  # gen=f2 0f 83 05  dis=f2 0f 83 05 90 90 90
    bnd je 0x909217e7                   # gen=f2 0f 84 c0  dis=f2 0f 84 c0 90 90 90
    bnd je 0x9092173c                   # gen=f2 0f 84 05  dis=f2 0f 84 05 90 90 90
    bnd jne 0x90921807                  # gen=f2 0f 85 c0  dis=f2 0f 85 c0 90 90 90
    bnd jne 0x9092175c                  # gen=f2 0f 85 05  dis=f2 0f 85 05 90 90 90
    bnd jbe 0x90921827                  # gen=f2 0f 86 c0  dis=f2 0f 86 c0 90 90 90
    bnd jbe 0x9092177c                  # gen=f2 0f 86 05  dis=f2 0f 86 05 90 90 90
    bnd ja 0x90921847                   # gen=f2 0f 87 c0  dis=f2 0f 87 c0 90 90 90
    bnd ja 0x9092179c                   # gen=f2 0f 87 05  dis=f2 0f 87 05 90 90 90
    bnd js 0x90921867                   # gen=f2 0f 88 c0  dis=f2 0f 88 c0 90 90 90
    bnd js 0x909217bc                   # gen=f2 0f 88 05  dis=f2 0f 88 05 90 90 90
    bnd jns 0x90921887                  # gen=f2 0f 89 c0  dis=f2 0f 89 c0 90 90 90
    bnd jns 0x909217dc                  # gen=f2 0f 89 05  dis=f2 0f 89 05 90 90 90
    bnd jp 0x909218a7                   # gen=f2 0f 8a c0  dis=f2 0f 8a c0 90 90 90
    bnd jp 0x909217fc                   # gen=f2 0f 8a 05  dis=f2 0f 8a 05 90 90 90
    bnd jnp 0x909218c7                  # gen=f2 0f 8b c0  dis=f2 0f 8b c0 90 90 90
    bnd jnp 0x9092181c                  # gen=f2 0f 8b 05  dis=f2 0f 8b 05 90 90 90
    bnd jl 0x909218e7                   # gen=f2 0f 8c c0  dis=f2 0f 8c c0 90 90 90
    bnd jl 0x9092183c                   # gen=f2 0f 8c 05  dis=f2 0f 8c 05 90 90 90
    bnd jge 0x90921907                  # gen=f2 0f 8d c0  dis=f2 0f 8d c0 90 90 90
    bnd jge 0x9092185c                  # gen=f2 0f 8d 05  dis=f2 0f 8d 05 90 90 90
    bnd jle 0x90921927                  # gen=f2 0f 8e c0  dis=f2 0f 8e c0 90 90 90
    bnd jle 0x9092187c                  # gen=f2 0f 8e 05  dis=f2 0f 8e 05 90 90 90
    bnd jg 0x90921947                   # gen=f2 0f 8f c0  dis=f2 0f 8f c0 90 90 90
    bnd jg 0x9092189c                   # gen=f2 0f 8f 05  dis=f2 0f 8f 05 90 90 90
    repnz xstore-rng                    # gen=f2 0f a7 c0  dis=f2 0f a7 c0
    cmpsd  $0x90,%xmm0,%xmm0            # gen=f2 0f c2 c0  dis=f2 0f c2 c0 90
    cmpsd  $0x90,0x90909090,%xmm0       # gen=f2 0f c2 05  dis=f2 0f c2 05 90 90 90
    addsubps %xmm0,%xmm0                # gen=f2 0f d0 c0  dis=f2 0f d0 c0
    addsubps 0x90909090,%xmm0           # gen=f2 0f d0 05  dis=f2 0f d0 05 90 90 90
    movdq2q %xmm0,%mm0                  # gen=f2 0f d6 c0  dis=f2 0f d6 c0
    cvtpd2dq %xmm0,%xmm0                # gen=f2 0f e6 c0  dis=f2 0f e6 c0
    cvtpd2dq 0x90909090,%xmm0           # gen=f2 0f e6 05  dis=f2 0f e6 05 90 90 90
    lddqu  0x90909090,%xmm0             # gen=f2 0f f0 05  dis=f2 0f f0 05 90 90 90
    crc32  %al,%eax                     # gen=f2 0f 38 f0 c0  dis=f2 0f 38 f0 c0
    crc32b 0x90909090,%eax              # gen=f2 0f 38 f0 05  dis=f2 0f 38 f0 05 90 90
    crc32  %al,%ecx                     # gen=f2 0f 38 f0 c8  dis=f2 0f 38 f0 c8
    crc32  %al,%edx                     # gen=f2 0f 38 f0 d0  dis=f2 0f 38 f0 d0
    crc32  %al,%ebx                     # gen=f2 0f 38 f0 d8  dis=f2 0f 38 f0 d8
    crc32  %al,%esp                     # gen=f2 0f 38 f0 e0  dis=f2 0f 38 f0 e0
    crc32  %al,%ebp                     # gen=f2 0f 38 f0 e8  dis=f2 0f 38 f0 e8
    crc32  %al,%esi                     # gen=f2 0f 38 f0 f0  dis=f2 0f 38 f0 f0
    crc32  %al,%edi                     # gen=f2 0f 38 f0 f8  dis=f2 0f 38 f0 f8
    crc32b 0x90909090,%ecx              # gen=f2 0f 38 f0 0d  dis=f2 0f 38 f0 0d 90 90
    crc32b 0x90909090,%edx              # gen=f2 0f 38 f0 15  dis=f2 0f 38 f0 15 90 90
    crc32b 0x90909090,%ebx              # gen=f2 0f 38 f0 1d  dis=f2 0f 38 f0 1d 90 90
    crc32b 0x90909090,%esp              # gen=f2 0f 38 f0 25  dis=f2 0f 38 f0 25 90 90
    crc32b 0x90909090,%ebp              # gen=f2 0f 38 f0 2d  dis=f2 0f 38 f0 2d 90 90
    crc32b 0x90909090,%esi              # gen=f2 0f 38 f0 35  dis=f2 0f 38 f0 35 90 90
    crc32b 0x90909090,%edi              # gen=f2 0f 38 f0 3d  dis=f2 0f 38 f0 3d 90 90
    crc32  %eax,%eax                    # gen=f2 0f 38 f1 c0  dis=f2 0f 38 f1 c0
    crc32l 0x90909090,%eax              # gen=f2 0f 38 f1 05  dis=f2 0f 38 f1 05 90 90
    crc32  %eax,%ecx                    # gen=f2 0f 38 f1 c8  dis=f2 0f 38 f1 c8
    crc32  %eax,%edx                    # gen=f2 0f 38 f1 d0  dis=f2 0f 38 f1 d0
    crc32  %eax,%ebx                    # gen=f2 0f 38 f1 d8  dis=f2 0f 38 f1 d8
    crc32  %eax,%esp                    # gen=f2 0f 38 f1 e0  dis=f2 0f 38 f1 e0
    crc32  %eax,%ebp                    # gen=f2 0f 38 f1 e8  dis=f2 0f 38 f1 e8
    crc32  %eax,%esi                    # gen=f2 0f 38 f1 f0  dis=f2 0f 38 f1 f0
    crc32  %eax,%edi                    # gen=f2 0f 38 f1 f8  dis=f2 0f 38 f1 f8
    crc32l 0x90909090,%ecx              # gen=f2 0f 38 f1 0d  dis=f2 0f 38 f1 0d 90 90
    crc32l 0x90909090,%edx              # gen=f2 0f 38 f1 15  dis=f2 0f 38 f1 15 90 90
    crc32l 0x90909090,%ebx              # gen=f2 0f 38 f1 1d  dis=f2 0f 38 f1 1d 90 90
    crc32l 0x90909090,%esp              # gen=f2 0f 38 f1 25  dis=f2 0f 38 f1 25 90 90
    crc32l 0x90909090,%ebp              # gen=f2 0f 38 f1 2d  dis=f2 0f 38 f1 2d 90 90
    crc32l 0x90909090,%esi              # gen=f2 0f 38 f1 35  dis=f2 0f 38 f1 35 90 90
    crc32l 0x90909090,%edi              # gen=f2 0f 38 f1 3d  dis=f2 0f 38 f1 3d 90 90
    enqcmd 0x90909090,%eax              # gen=f2 0f 38 f8 05  dis=f2 0f 38 f8 05 90 90
    aor    %eax,(bad)                   # gen=f2 0f 38 fc c0  dis=f2 0f
    aor    %eax,0x90909090              # gen=f2 0f 38 fc 05  dis=f2 0f 38 fc 05 90 90
    rep insb (%dx),%es:(%edi)           # gen=f3 6c c0  dis=f3 6c
    rep insl (%dx),%es:(%edi)           # gen=f3 6d c0  dis=f3 6d
    rep outsb %ds:(%esi),(%dx)          # gen=f3 6e c0  dis=f3 6e
    rep outsl %ds:(%esi),(%dx)          # gen=f3 6f c0  dis=f3 6f
    xrelease xchg %al,0x90909090        # gen=f3 86 05  dis=f3 86 05 90 90 90 90
    xrelease xchg %cl,0x90909090        # gen=f3 86 0d  dis=f3 86 0d 90 90 90 90
    xrelease xchg %dl,0x90909090        # gen=f3 86 15  dis=f3 86 15 90 90 90 90
    xrelease xchg %bl,0x90909090        # gen=f3 86 1d  dis=f3 86 1d 90 90 90 90
    xrelease xchg %ah,0x90909090        # gen=f3 86 25  dis=f3 86 25 90 90 90 90
    xrelease xchg %ch,0x90909090        # gen=f3 86 2d  dis=f3 86 2d 90 90 90 90
    xrelease xchg %dh,0x90909090        # gen=f3 86 35  dis=f3 86 35 90 90 90 90
    xrelease xchg %bh,0x90909090        # gen=f3 86 3d  dis=f3 86 3d 90 90 90 90
    xrelease xchg %eax,0x90909090       # gen=f3 87 05  dis=f3 87 05 90 90 90 90
    xrelease xchg %ecx,0x90909090       # gen=f3 87 0d  dis=f3 87 0d 90 90 90 90
    xrelease xchg %edx,0x90909090       # gen=f3 87 15  dis=f3 87 15 90 90 90 90
    xrelease xchg %ebx,0x90909090       # gen=f3 87 1d  dis=f3 87 1d 90 90 90 90
    xrelease xchg %esp,0x90909090       # gen=f3 87 25  dis=f3 87 25 90 90 90 90
    xrelease xchg %ebp,0x90909090       # gen=f3 87 2d  dis=f3 87 2d 90 90 90 90
    xrelease xchg %esi,0x90909090       # gen=f3 87 35  dis=f3 87 35 90 90 90 90
    xrelease xchg %edi,0x90909090       # gen=f3 87 3d  dis=f3 87 3d 90 90 90 90
    xrelease mov %al,0x90909090         # gen=f3 88 05  dis=f3 88 05 90 90 90 90
    xrelease mov %cl,0x90909090         # gen=f3 88 0d  dis=f3 88 0d 90 90 90 90
    xrelease mov %dl,0x90909090         # gen=f3 88 15  dis=f3 88 15 90 90 90 90
    xrelease mov %bl,0x90909090         # gen=f3 88 1d  dis=f3 88 1d 90 90 90 90
    xrelease mov %ah,0x90909090         # gen=f3 88 25  dis=f3 88 25 90 90 90 90
    xrelease mov %ch,0x90909090         # gen=f3 88 2d  dis=f3 88 2d 90 90 90 90
    xrelease mov %dh,0x90909090         # gen=f3 88 35  dis=f3 88 35 90 90 90 90
    xrelease mov %bh,0x90909090         # gen=f3 88 3d  dis=f3 88 3d 90 90 90 90
    xrelease mov %eax,0x90909090        # gen=f3 89 05  dis=f3 89 05 90 90 90 90
    xrelease mov %ecx,0x90909090        # gen=f3 89 0d  dis=f3 89 0d 90 90 90 90
    xrelease mov %edx,0x90909090        # gen=f3 89 15  dis=f3 89 15 90 90 90 90
    xrelease mov %ebx,0x90909090        # gen=f3 89 1d  dis=f3 89 1d 90 90 90 90
    xrelease mov %esp,0x90909090        # gen=f3 89 25  dis=f3 89 25 90 90 90 90
    xrelease mov %ebp,0x90909090        # gen=f3 89 2d  dis=f3 89 2d 90 90 90 90
    xrelease mov %esi,0x90909090        # gen=f3 89 35  dis=f3 89 35 90 90 90 90
    xrelease mov %edi,0x90909090        # gen=f3 89 3d  dis=f3 89 3d 90 90 90 90
    pause                               # gen=f3 90 c0  dis=f3 90
    rep movsb %ds:(%esi),%es:(%edi)     # gen=f3 a4 c0  dis=f3 a4
    rep movsl %ds:(%esi),%es:(%edi)     # gen=f3 a5 c0  dis=f3 a5
    repz cmpsb %es:(%edi),%ds:(%esi)    # gen=f3 a6 c0  dis=f3 a6
    repz cmpsl %es:(%edi),%ds:(%esi)    # gen=f3 a7 c0  dis=f3 a7
    rep stos %al,%es:(%edi)             # gen=f3 aa c0  dis=f3 aa
    rep stos %eax,%es:(%edi)            # gen=f3 ab c0  dis=f3 ab
    rep lods %ds:(%esi),%al             # gen=f3 ac c0  dis=f3 ac
    rep lods %ds:(%esi),%eax            # gen=f3 ad c0  dis=f3 ad
    repz scas %es:(%edi),%al            # gen=f3 ae c0  dis=f3 ae
    repz scas %es:(%edi),%eax           # gen=f3 af c0  dis=f3 af
    repz ret $0x90c0                    # gen=f3 c2 c0  dis=f3 c2 c0 90
    repz ret $0x9005                    # gen=f3 c2 05  dis=f3 c2 05 90
    repz ret                            # gen=f3 c3 c0  dis=f3 c3
    xrelease movb $0x90,0x90909090      # gen=f3 c6 05  dis=f3 c6 05 90 90 90 90
    xrelease movl $0x90909090,0x90909090 # gen=f3 c7 05  dis=f3 c7 05 90 90 90 90
    wbnoinvd                            # gen=f3 0f 09 c0  dis=f3 0f 09
    movss  %xmm0,%xmm0                  # gen=f3 0f 10 c0  dis=f3 0f 10 c0
    movss  0x90909090,%xmm0             # gen=f3 0f 10 05  dis=f3 0f 10 05 90 90 90
    movss  %xmm0,0x90909090             # gen=f3 0f 11 05  dis=f3 0f 11 05 90 90 90
    movsldup %xmm0,%xmm0                # gen=f3 0f 12 c0  dis=f3 0f 12 c0
    movsldup 0x90909090,%xmm0           # gen=f3 0f 12 05  dis=f3 0f 12 05 90 90 90
    movshdup %xmm0,%xmm0                # gen=f3 0f 16 c0  dis=f3 0f 16 c0
    movshdup 0x90909090,%xmm0           # gen=f3 0f 16 05  dis=f3 0f 16 05 90 90 90
    bndcl  %eax,%bnd0                   # gen=f3 0f 1a c0  dis=f3 0f 1a c0
    bndcl  0x90909090,%bnd0             # gen=f3 0f 1a 05  dis=f3 0f 1a 05 90 90 90
    bndmk  0x90909090,%bnd0             # gen=f3 0f 1b 05  dis=f3 0f 1b 05 90 90 90
    bndmk  0x90909090,%bnd1             # gen=f3 0f 1b 0d  dis=f3 0f 1b 0d 90 90 90
    bndmk  0x90909090,%bnd2             # gen=f3 0f 1b 15  dis=f3 0f 1b 15 90 90 90
    bndmk  0x90909090,%bnd3             # gen=f3 0f 1b 1d  dis=f3 0f 1b 1d 90 90 90
    cvtsi2ss %eax,%xmm0                 # gen=f3 0f 2a c0  dis=f3 0f 2a c0
    cvtsi2ss 0x90909090,%xmm0           # gen=f3 0f 2a 05  dis=f3 0f 2a 05 90 90 90
    movntss %xmm0,0x90909090            # gen=f3 0f 2b 05  dis=f3 0f 2b 05 90 90 90
    cvttss2si %xmm0,%eax                # gen=f3 0f 2c c0  dis=f3 0f 2c c0
    cvttss2si 0x90909090,%eax           # gen=f3 0f 2c 05  dis=f3 0f 2c 05 90 90 90
    cvtss2si %xmm0,%eax                 # gen=f3 0f 2d c0  dis=f3 0f 2d c0
    cvtss2si 0x90909090,%eax            # gen=f3 0f 2d 05  dis=f3 0f 2d 05 90 90 90
    aesencwide256kl -0x6f6f6f70(%eax)   # gen=f3 0f 38 d8  dis=f3 0f 38 d8 90 90 90
    enqcmds -0x6f6f6f70(%eax),%edx      # gen=f3 0f 38 f8  dis=f3 0f 38 f8 90 90 90
    sqrtss %xmm0,%xmm0                  # gen=f3 0f 51 c0  dis=f3 0f 51 c0
    sqrtss 0x90909090,%xmm0             # gen=f3 0f 51 05  dis=f3 0f 51 05 90 90 90
    rsqrtss %xmm0,%xmm0                 # gen=f3 0f 52 c0  dis=f3 0f 52 c0
    rsqrtss 0x90909090,%xmm0            # gen=f3 0f 52 05  dis=f3 0f 52 05 90 90 90
    rcpss  %xmm0,%xmm0                  # gen=f3 0f 53 c0  dis=f3 0f 53 c0
    rcpss  0x90909090,%xmm0             # gen=f3 0f 53 05  dis=f3 0f 53 05 90 90 90
    addss  %xmm0,%xmm0                  # gen=f3 0f 58 c0  dis=f3 0f 58 c0
    addss  0x90909090,%xmm0             # gen=f3 0f 58 05  dis=f3 0f 58 05 90 90 90
    mulss  %xmm0,%xmm0                  # gen=f3 0f 59 c0  dis=f3 0f 59 c0
    mulss  0x90909090,%xmm0             # gen=f3 0f 59 05  dis=f3 0f 59 05 90 90 90
    cvtss2sd %xmm0,%xmm0                # gen=f3 0f 5a c0  dis=f3 0f 5a c0
    cvtss2sd 0x90909090,%xmm0           # gen=f3 0f 5a 05  dis=f3 0f 5a 05 90 90 90
    cvttps2dq %xmm0,%xmm0               # gen=f3 0f 5b c0  dis=f3 0f 5b c0
    cvttps2dq 0x90909090,%xmm0          # gen=f3 0f 5b 05  dis=f3 0f 5b 05 90 90 90
    subss  %xmm0,%xmm0                  # gen=f3 0f 5c c0  dis=f3 0f 5c c0
    subss  0x90909090,%xmm0             # gen=f3 0f 5c 05  dis=f3 0f 5c 05 90 90 90
    minss  %xmm0,%xmm0                  # gen=f3 0f 5d c0  dis=f3 0f 5d c0
    minss  0x90909090,%xmm0             # gen=f3 0f 5d 05  dis=f3 0f 5d 05 90 90 90
    divss  %xmm0,%xmm0                  # gen=f3 0f 5e c0  dis=f3 0f 5e c0
    divss  0x90909090,%xmm0             # gen=f3 0f 5e 05  dis=f3 0f 5e 05 90 90 90
    maxss  %xmm0,%xmm0                  # gen=f3 0f 5f c0  dis=f3 0f 5f c0
    maxss  0x90909090,%xmm0             # gen=f3 0f 5f 05  dis=f3 0f 5f 05 90 90 90
    movdqu %xmm0,%xmm0                  # gen=f3 0f 6f c0  dis=f3 0f 6f c0
    movdqu 0x90909090,%xmm0             # gen=f3 0f 6f 05  dis=f3 0f 6f 05 90 90 90
    pshufhw $0x90,%xmm0,%xmm0           # gen=f3 0f 70 c0  dis=f3 0f 70 c0 90
    pshufhw $0x90,0x90909090,%xmm0      # gen=f3 0f 70 05  dis=f3 0f 70 05 90 90 90
    movq   0x90909090,%xmm0             # gen=f3 0f 7e 05  dis=f3 0f 7e 05 90 90 90
    movdqu %xmm0,0x90909090             # gen=f3 0f 7f 05  dis=f3 0f 7f 05 90 90 90
    repz montmul                        # gen=f3 0f a6 c0  dis=f3 0f a6 c0
    repz xstore-rng                     # gen=f3 0f a7 c0  dis=f3 0f a7 c0
    rdfsbase %eax                       # gen=f3 0f ae c0  dis=f3 0f ae c0
    rdgsbase %eax                       # gen=f3 0f ae c8  dis=f3 0f ae c8
    wrfsbase %eax                       # gen=f3 0f ae d0  dis=f3 0f ae d0
    wrgsbase %eax                       # gen=f3 0f ae d8  dis=f3 0f ae d8
    ptwrite %eax                        # gen=f3 0f ae e0  dis=f3 0f ae e0
    incsspd %eax                        # gen=f3 0f ae e8  dis=f3 0f ae e8
    umonitor %eax                       # gen=f3 0f ae f0  dis=f3 0f ae f0
    ptwrite 0x90909090                  # gen=f3 0f ae 25  dis=f3 0f ae 25 90 90 90
    clrssbsy 0x90909090                 # gen=f3 0f ae 35  dis=f3 0f ae 35 90 90 90
    popcnt %eax,%eax                    # gen=f3 0f b8 c0  dis=f3 0f b8 c0
    popcnt 0x90909090,%eax              # gen=f3 0f b8 05  dis=f3 0f b8 05 90 90 90
    tzcnt  %eax,%eax                    # gen=f3 0f bc c0  dis=f3 0f bc c0
    tzcnt  0x90909090,%eax              # gen=f3 0f bc 05  dis=f3 0f bc 05 90 90 90
    lzcnt  %eax,%eax                    # gen=f3 0f bd c0  dis=f3 0f bd c0
    lzcnt  0x90909090,%eax              # gen=f3 0f bd 05  dis=f3 0f bd 05 90 90 90
    cmpss  $0x90,%xmm0,%xmm0            # gen=f3 0f c2 c0  dis=f3 0f c2 c0 90
    cmpss  $0x90,0x90909090,%xmm0       # gen=f3 0f c2 05  dis=f3 0f c2 05 90 90 90
    rdpid  %eax                         # gen=f3 0f c7 f8  dis=f3 0f c7 f8
    vmxon  0x90909090                   # gen=f3 0f c7 35  dis=f3 0f c7 35 90 90 90
    movq2dq %mm0,%xmm0                  # gen=f3 0f d6 c0  dis=f3 0f d6 c0
    cvtdq2pd %xmm0,%xmm0                # gen=f3 0f e6 c0  dis=f3 0f e6 c0
    cvtdq2pd 0x90909090,%xmm0           # gen=f3 0f e6 05  dis=f3 0f e6 05 90 90 90
    aesencwide128kl (bad)               # gen=f3 0f 38 d8 c0  dis=f3 0f
    aesencwide128kl 0x90909090          # gen=f3 0f 38 d8 05  dis=f3 0f 38 d8 05 90 90
    loadiwkey %xmm0,%xmm0               # gen=f3 0f 38 dc c0  dis=f3 0f 38 dc c0
    aesenc128kl 0x90909090,%xmm0        # gen=f3 0f 38 dc 05  dis=f3 0f 38 dc 05 90 90
    loadiwkey %xmm0,%xmm1               # gen=f3 0f 38 dc c8  dis=f3 0f 38 dc c8
    loadiwkey %xmm0,%xmm2               # gen=f3 0f 38 dc d0  dis=f3 0f 38 dc d0
    loadiwkey %xmm0,%xmm3               # gen=f3 0f 38 dc d8  dis=f3 0f 38 dc d8
    loadiwkey %xmm0,%xmm4               # gen=f3 0f 38 dc e0  dis=f3 0f 38 dc e0
    loadiwkey %xmm0,%xmm5               # gen=f3 0f 38 dc e8  dis=f3 0f 38 dc e8
    loadiwkey %xmm0,%xmm6               # gen=f3 0f 38 dc f0  dis=f3 0f 38 dc f0
    loadiwkey %xmm0,%xmm7               # gen=f3 0f 38 dc f8  dis=f3 0f 38 dc f8
    aesenc128kl 0x90909090,%xmm1        # gen=f3 0f 38 dc 0d  dis=f3 0f 38 dc 0d 90 90
    aesenc128kl 0x90909090,%xmm2        # gen=f3 0f 38 dc 15  dis=f3 0f 38 dc 15 90 90
    aesenc128kl 0x90909090,%xmm3        # gen=f3 0f 38 dc 1d  dis=f3 0f 38 dc 1d 90 90
    aesenc128kl 0x90909090,%xmm4        # gen=f3 0f 38 dc 25  dis=f3 0f 38 dc 25 90 90
    aesenc128kl 0x90909090,%xmm5        # gen=f3 0f 38 dc 2d  dis=f3 0f 38 dc 2d 90 90
    aesenc128kl 0x90909090,%xmm6        # gen=f3 0f 38 dc 35  dis=f3 0f 38 dc 35 90 90
    aesenc128kl 0x90909090,%xmm7        # gen=f3 0f 38 dc 3d  dis=f3 0f 38 dc 3d 90 90
    aesdec128kl 0x90909090,%xmm0        # gen=f3 0f 38 dd 05  dis=f3 0f 38 dd 05 90 90
    aesenc256kl 0x90909090,%xmm0        # gen=f3 0f 38 de 05  dis=f3 0f 38 de 05 90 90
    aesdec256kl 0x90909090,%xmm0        # gen=f3 0f 38 df 05  dis=f3 0f 38 df 05 90 90
    adox   %eax,%eax                    # gen=f3 0f 38 f6 c0  dis=f3 0f 38 f6 c0
    adox   0x90909090,%eax              # gen=f3 0f 38 f6 05  dis=f3 0f 38 f6 05 90 90
    enqcmds 0x90909090,%eax             # gen=f3 0f 38 f8 05  dis=f3 0f 38 f8 05 90 90
    encodekey128 %eax,%eax              # gen=f3 0f 38 fa c0  dis=f3 0f 38 fa c0
    encodekey256 %eax,%eax              # gen=f3 0f 38 fb c0  dis=f3 0f 38 fb c0
    axor   %eax,(bad)                   # gen=f3 0f 38 fc c0  dis=f3 0f
    axor   %eax,0x90909090              # gen=f3 0f 38 fc 05  dis=f3 0f 38 fc 05 90 90
    hreset $0x90                        # gen=f3 0f 3a f0 c0  dis=f3 0f 3a f0 c0 90
    vmovhlps %xmm0,%xmm1,%xmm0          # gen=c5 f0 12 c0  dis=c5 f0 12 c0
    vmovlps 0x90909090,%xmm1,%xmm0      # gen=c5 f0 12 05  dis=c5 f0 12 05 90 90 90
    vmovhlps %xmm0,%xmm1,%xmm1          # gen=c5 f0 12 c8  dis=c5 f0 12 c8
    vmovhlps %xmm0,%xmm1,%xmm2          # gen=c5 f0 12 d0  dis=c5 f0 12 d0
    vmovhlps %xmm0,%xmm1,%xmm3          # gen=c5 f0 12 d8  dis=c5 f0 12 d8
    vmovhlps %xmm0,%xmm1,%xmm4          # gen=c5 f0 12 e0  dis=c5 f0 12 e0
    vmovhlps %xmm0,%xmm1,%xmm5          # gen=c5 f0 12 e8  dis=c5 f0 12 e8
    vmovhlps %xmm0,%xmm1,%xmm6          # gen=c5 f0 12 f0  dis=c5 f0 12 f0
    vmovhlps %xmm0,%xmm1,%xmm7          # gen=c5 f0 12 f8  dis=c5 f0 12 f8
    vmovlps 0x90909090,%xmm1,%xmm1      # gen=c5 f0 12 0d  dis=c5 f0 12 0d 90 90 90
    vmovlps 0x90909090,%xmm1,%xmm2      # gen=c5 f0 12 15  dis=c5 f0 12 15 90 90 90
    vmovlps 0x90909090,%xmm1,%xmm3      # gen=c5 f0 12 1d  dis=c5 f0 12 1d 90 90 90
    vmovlps 0x90909090,%xmm1,%xmm4      # gen=c5 f0 12 25  dis=c5 f0 12 25 90 90 90
    vmovlps 0x90909090,%xmm1,%xmm5      # gen=c5 f0 12 2d  dis=c5 f0 12 2d 90 90 90
    vmovlps 0x90909090,%xmm1,%xmm6      # gen=c5 f0 12 35  dis=c5 f0 12 35 90 90 90
    vmovlps 0x90909090,%xmm1,%xmm7      # gen=c5 f0 12 3d  dis=c5 f0 12 3d 90 90 90
    vunpcklps %xmm0,%xmm1,%xmm0         # gen=c5 f0 14 c0  dis=c5 f0 14 c0
    vunpcklps 0x90909090,%xmm1,%xmm0    # gen=c5 f0 14 05  dis=c5 f0 14 05 90 90 90
    vunpckhps %xmm0,%xmm1,%xmm0         # gen=c5 f0 15 c0  dis=c5 f0 15 c0
    vunpckhps 0x90909090,%xmm1,%xmm0    # gen=c5 f0 15 05  dis=c5 f0 15 05 90 90 90
    vmovlhps %xmm0,%xmm1,%xmm0          # gen=c5 f0 16 c0  dis=c5 f0 16 c0
    vmovhps 0x90909090,%xmm1,%xmm0      # gen=c5 f0 16 05  dis=c5 f0 16 05 90 90 90
    vmovlhps %xmm0,%xmm1,%xmm1          # gen=c5 f0 16 c8  dis=c5 f0 16 c8
    vmovlhps %xmm0,%xmm1,%xmm2          # gen=c5 f0 16 d0  dis=c5 f0 16 d0
    vmovlhps %xmm0,%xmm1,%xmm3          # gen=c5 f0 16 d8  dis=c5 f0 16 d8
    vmovlhps %xmm0,%xmm1,%xmm4          # gen=c5 f0 16 e0  dis=c5 f0 16 e0
    vmovlhps %xmm0,%xmm1,%xmm5          # gen=c5 f0 16 e8  dis=c5 f0 16 e8
    vmovlhps %xmm0,%xmm1,%xmm6          # gen=c5 f0 16 f0  dis=c5 f0 16 f0
    vmovlhps %xmm0,%xmm1,%xmm7          # gen=c5 f0 16 f8  dis=c5 f0 16 f8
    vmovhps 0x90909090,%xmm1,%xmm1      # gen=c5 f0 16 0d  dis=c5 f0 16 0d 90 90 90
    vmovhps 0x90909090,%xmm1,%xmm2      # gen=c5 f0 16 15  dis=c5 f0 16 15 90 90 90
    vmovhps 0x90909090,%xmm1,%xmm3      # gen=c5 f0 16 1d  dis=c5 f0 16 1d 90 90 90
    vmovhps 0x90909090,%xmm1,%xmm4      # gen=c5 f0 16 25  dis=c5 f0 16 25 90 90 90
    vmovhps 0x90909090,%xmm1,%xmm5      # gen=c5 f0 16 2d  dis=c5 f0 16 2d 90 90 90
    vmovhps 0x90909090,%xmm1,%xmm6      # gen=c5 f0 16 35  dis=c5 f0 16 35 90 90 90
    vmovhps 0x90909090,%xmm1,%xmm7      # gen=c5 f0 16 3d  dis=c5 f0 16 3d 90 90 90
    vandps %xmm0,%xmm1,%xmm0            # gen=c5 f0 54 c0  dis=c5 f0 54 c0
    vandps 0x90909090,%xmm1,%xmm0       # gen=c5 f0 54 05  dis=c5 f0 54 05 90 90 90
    vandnps %xmm0,%xmm1,%xmm0           # gen=c5 f0 55 c0  dis=c5 f0 55 c0
    vandnps 0x90909090,%xmm1,%xmm0      # gen=c5 f0 55 05  dis=c5 f0 55 05 90 90 90
    vorps  %xmm0,%xmm1,%xmm0            # gen=c5 f0 56 c0  dis=c5 f0 56 c0
    vorps  0x90909090,%xmm1,%xmm0       # gen=c5 f0 56 05  dis=c5 f0 56 05 90 90 90
    vxorps %xmm0,%xmm1,%xmm0            # gen=c5 f0 57 c0  dis=c5 f0 57 c0
    vxorps 0x90909090,%xmm1,%xmm0       # gen=c5 f0 57 05  dis=c5 f0 57 05 90 90 90
    vaddps %xmm0,%xmm1,%xmm0            # gen=c5 f0 58 c0  dis=c5 f0 58 c0
    vaddps 0x90909090,%xmm1,%xmm0       # gen=c5 f0 58 05  dis=c5 f0 58 05 90 90 90
    vmulps %xmm0,%xmm1,%xmm0            # gen=c5 f0 59 c0  dis=c5 f0 59 c0
    vmulps 0x90909090,%xmm1,%xmm0       # gen=c5 f0 59 05  dis=c5 f0 59 05 90 90 90
    vsubps %xmm0,%xmm1,%xmm0            # gen=c5 f0 5c c0  dis=c5 f0 5c c0
    vsubps 0x90909090,%xmm1,%xmm0       # gen=c5 f0 5c 05  dis=c5 f0 5c 05 90 90 90
    vminps %xmm0,%xmm1,%xmm0            # gen=c5 f0 5d c0  dis=c5 f0 5d c0
    vminps 0x90909090,%xmm1,%xmm0       # gen=c5 f0 5d 05  dis=c5 f0 5d 05 90 90 90
    vdivps %xmm0,%xmm1,%xmm0            # gen=c5 f0 5e c0  dis=c5 f0 5e c0
    vdivps 0x90909090,%xmm1,%xmm0       # gen=c5 f0 5e 05  dis=c5 f0 5e 05 90 90 90
    vmaxps %xmm0,%xmm1,%xmm0            # gen=c5 f0 5f c0  dis=c5 f0 5f c0
    vmaxps 0x90909090,%xmm1,%xmm0       # gen=c5 f0 5f 05  dis=c5 f0 5f 05 90 90 90
    vcmpps $0x90,%xmm0,%xmm1,%xmm0      # gen=c5 f0 c2 c0  dis=c5 f0 c2 c0 90
    vcmpps $0x90,0x90909090,%xmm1,%xmm0 # gen=c5 f0 c2 05  dis=c5 f0 c2 05 90 90 90
    vshufps $0x90,%xmm0,%xmm1,%xmm0     # gen=c5 f0 c6 c0  dis=c5 f0 c6 c0 90
    vshufps $0x90,0x90909090,%xmm1,%xmm0 # gen=c5 f0 c6 05  dis=c5 f0 c6 05 90 90 90
    vmovlpd 0x90909090,%xmm1,%xmm0      # gen=c5 f1 12 05  dis=c5 f1 12 05 90 90 90
    vunpcklpd %xmm0,%xmm1,%xmm0         # gen=c5 f1 14 c0  dis=c5 f1 14 c0
    vunpcklpd 0x90909090,%xmm1,%xmm0    # gen=c5 f1 14 05  dis=c5 f1 14 05 90 90 90
    vunpckhpd %xmm0,%xmm1,%xmm0         # gen=c5 f1 15 c0  dis=c5 f1 15 c0
    vunpckhpd 0x90909090,%xmm1,%xmm0    # gen=c5 f1 15 05  dis=c5 f1 15 05 90 90 90
    vmovhpd 0x90909090,%xmm1,%xmm0      # gen=c5 f1 16 05  dis=c5 f1 16 05 90 90 90
    vandpd %xmm0,%xmm1,%xmm0            # gen=c5 f1 54 c0  dis=c5 f1 54 c0
    vandpd 0x90909090,%xmm1,%xmm0       # gen=c5 f1 54 05  dis=c5 f1 54 05 90 90 90
    vandnpd %xmm0,%xmm1,%xmm0           # gen=c5 f1 55 c0  dis=c5 f1 55 c0
    vandnpd 0x90909090,%xmm1,%xmm0      # gen=c5 f1 55 05  dis=c5 f1 55 05 90 90 90
    vorpd  %xmm0,%xmm1,%xmm0            # gen=c5 f1 56 c0  dis=c5 f1 56 c0
    vorpd  0x90909090,%xmm1,%xmm0       # gen=c5 f1 56 05  dis=c5 f1 56 05 90 90 90
    vxorpd %xmm0,%xmm1,%xmm0            # gen=c5 f1 57 c0  dis=c5 f1 57 c0
    vxorpd 0x90909090,%xmm1,%xmm0       # gen=c5 f1 57 05  dis=c5 f1 57 05 90 90 90
    vaddpd %xmm0,%xmm1,%xmm0            # gen=c5 f1 58 c0  dis=c5 f1 58 c0
    vaddpd 0x90909090,%xmm1,%xmm0       # gen=c5 f1 58 05  dis=c5 f1 58 05 90 90 90
    vmulpd %xmm0,%xmm1,%xmm0            # gen=c5 f1 59 c0  dis=c5 f1 59 c0
    vmulpd 0x90909090,%xmm1,%xmm0       # gen=c5 f1 59 05  dis=c5 f1 59 05 90 90 90
    vsubpd %xmm0,%xmm1,%xmm0            # gen=c5 f1 5c c0  dis=c5 f1 5c c0
    vsubpd 0x90909090,%xmm1,%xmm0       # gen=c5 f1 5c 05  dis=c5 f1 5c 05 90 90 90
    vminpd %xmm0,%xmm1,%xmm0            # gen=c5 f1 5d c0  dis=c5 f1 5d c0
    vminpd 0x90909090,%xmm1,%xmm0       # gen=c5 f1 5d 05  dis=c5 f1 5d 05 90 90 90
    vdivpd %xmm0,%xmm1,%xmm0            # gen=c5 f1 5e c0  dis=c5 f1 5e c0
    vdivpd 0x90909090,%xmm1,%xmm0       # gen=c5 f1 5e 05  dis=c5 f1 5e 05 90 90 90
    vmaxpd %xmm0,%xmm1,%xmm0            # gen=c5 f1 5f c0  dis=c5 f1 5f c0
    vmaxpd 0x90909090,%xmm1,%xmm0       # gen=c5 f1 5f 05  dis=c5 f1 5f 05 90 90 90
    vpunpcklbw %xmm0,%xmm1,%xmm0        # gen=c5 f1 60 c0  dis=c5 f1 60 c0
    vpunpcklbw 0x90909090,%xmm1,%xmm0   # gen=c5 f1 60 05  dis=c5 f1 60 05 90 90 90
    vpunpcklwd %xmm0,%xmm1,%xmm0        # gen=c5 f1 61 c0  dis=c5 f1 61 c0
    vpunpcklwd 0x90909090,%xmm1,%xmm0   # gen=c5 f1 61 05  dis=c5 f1 61 05 90 90 90
    vpunpckldq %xmm0,%xmm1,%xmm0        # gen=c5 f1 62 c0  dis=c5 f1 62 c0
    vpunpckldq 0x90909090,%xmm1,%xmm0   # gen=c5 f1 62 05  dis=c5 f1 62 05 90 90 90
    vpacksswb %xmm0,%xmm1,%xmm0         # gen=c5 f1 63 c0  dis=c5 f1 63 c0
    vpacksswb 0x90909090,%xmm1,%xmm0    # gen=c5 f1 63 05  dis=c5 f1 63 05 90 90 90
    vpcmpgtb %xmm0,%xmm1,%xmm0          # gen=c5 f1 64 c0  dis=c5 f1 64 c0
    vpcmpgtb 0x90909090,%xmm1,%xmm0     # gen=c5 f1 64 05  dis=c5 f1 64 05 90 90 90
    vpcmpgtw %xmm0,%xmm1,%xmm0          # gen=c5 f1 65 c0  dis=c5 f1 65 c0
    vpcmpgtw 0x90909090,%xmm1,%xmm0     # gen=c5 f1 65 05  dis=c5 f1 65 05 90 90 90
    vpcmpgtd %xmm0,%xmm1,%xmm0          # gen=c5 f1 66 c0  dis=c5 f1 66 c0
    vpcmpgtd 0x90909090,%xmm1,%xmm0     # gen=c5 f1 66 05  dis=c5 f1 66 05 90 90 90
    vpackuswb %xmm0,%xmm1,%xmm0         # gen=c5 f1 67 c0  dis=c5 f1 67 c0
    vpackuswb 0x90909090,%xmm1,%xmm0    # gen=c5 f1 67 05  dis=c5 f1 67 05 90 90 90
    vpunpckhbw %xmm0,%xmm1,%xmm0        # gen=c5 f1 68 c0  dis=c5 f1 68 c0
    vpunpckhbw 0x90909090,%xmm1,%xmm0   # gen=c5 f1 68 05  dis=c5 f1 68 05 90 90 90
    vpunpckhwd %xmm0,%xmm1,%xmm0        # gen=c5 f1 69 c0  dis=c5 f1 69 c0
    vpunpckhwd 0x90909090,%xmm1,%xmm0   # gen=c5 f1 69 05  dis=c5 f1 69 05 90 90 90
    vpunpckhdq %xmm0,%xmm1,%xmm0        # gen=c5 f1 6a c0  dis=c5 f1 6a c0
    vpunpckhdq 0x90909090,%xmm1,%xmm0   # gen=c5 f1 6a 05  dis=c5 f1 6a 05 90 90 90
    vpackssdw %xmm0,%xmm1,%xmm0         # gen=c5 f1 6b c0  dis=c5 f1 6b c0
    vpackssdw 0x90909090,%xmm1,%xmm0    # gen=c5 f1 6b 05  dis=c5 f1 6b 05 90 90 90
    vpunpcklqdq %xmm0,%xmm1,%xmm0       # gen=c5 f1 6c c0  dis=c5 f1 6c c0
    vpunpcklqdq 0x90909090,%xmm1,%xmm0  # gen=c5 f1 6c 05  dis=c5 f1 6c 05 90 90 90
    vpunpckhqdq %xmm0,%xmm1,%xmm0       # gen=c5 f1 6d c0  dis=c5 f1 6d c0
    vpunpckhqdq 0x90909090,%xmm1,%xmm0  # gen=c5 f1 6d 05  dis=c5 f1 6d 05 90 90 90
    vpcmpeqb %xmm0,%xmm1,%xmm0          # gen=c5 f1 74 c0  dis=c5 f1 74 c0
    vpcmpeqb 0x90909090,%xmm1,%xmm0     # gen=c5 f1 74 05  dis=c5 f1 74 05 90 90 90
    vpcmpeqw %xmm0,%xmm1,%xmm0          # gen=c5 f1 75 c0  dis=c5 f1 75 c0
    vpcmpeqw 0x90909090,%xmm1,%xmm0     # gen=c5 f1 75 05  dis=c5 f1 75 05 90 90 90
    vpcmpeqd %xmm0,%xmm1,%xmm0          # gen=c5 f1 76 c0  dis=c5 f1 76 c0
    vpcmpeqd 0x90909090,%xmm1,%xmm0     # gen=c5 f1 76 05  dis=c5 f1 76 05 90 90 90
    vhaddpd %xmm0,%xmm1,%xmm0           # gen=c5 f1 7c c0  dis=c5 f1 7c c0
    vhaddpd 0x90909090,%xmm1,%xmm0      # gen=c5 f1 7c 05  dis=c5 f1 7c 05 90 90 90
    vhsubpd %xmm0,%xmm1,%xmm0           # gen=c5 f1 7d c0  dis=c5 f1 7d c0
    vhsubpd 0x90909090,%xmm1,%xmm0      # gen=c5 f1 7d 05  dis=c5 f1 7d 05 90 90 90
    vcmppd $0x90,%xmm0,%xmm1,%xmm0      # gen=c5 f1 c2 c0  dis=c5 f1 c2 c0 90
    vcmppd $0x90,0x90909090,%xmm1,%xmm0 # gen=c5 f1 c2 05  dis=c5 f1 c2 05 90 90 90
    vpinsrw $0x90,%eax,%xmm1,%xmm0      # gen=c5 f1 c4 c0  dis=c5 f1 c4 c0 90
    vpinsrw $0x90,0x90909090,%xmm1,%xmm0 # gen=c5 f1 c4 05  dis=c5 f1 c4 05 90 90 90
    vshufpd $0x90,%xmm0,%xmm1,%xmm0     # gen=c5 f1 c6 c0  dis=c5 f1 c6 c0 90
    vshufpd $0x90,0x90909090,%xmm1,%xmm0 # gen=c5 f1 c6 05  dis=c5 f1 c6 05 90 90 90
    vaddsubpd %xmm0,%xmm1,%xmm0         # gen=c5 f1 d0 c0  dis=c5 f1 d0 c0
    vaddsubpd 0x90909090,%xmm1,%xmm0    # gen=c5 f1 d0 05  dis=c5 f1 d0 05 90 90 90
    vpsrlw %xmm0,%xmm1,%xmm0            # gen=c5 f1 d1 c0  dis=c5 f1 d1 c0
    vpsrlw 0x90909090,%xmm1,%xmm0       # gen=c5 f1 d1 05  dis=c5 f1 d1 05 90 90 90
    vpsrld %xmm0,%xmm1,%xmm0            # gen=c5 f1 d2 c0  dis=c5 f1 d2 c0
    vpsrld 0x90909090,%xmm1,%xmm0       # gen=c5 f1 d2 05  dis=c5 f1 d2 05 90 90 90
    vpsrlq %xmm0,%xmm1,%xmm0            # gen=c5 f1 d3 c0  dis=c5 f1 d3 c0
    vpsrlq 0x90909090,%xmm1,%xmm0       # gen=c5 f1 d3 05  dis=c5 f1 d3 05 90 90 90
    vpaddq %xmm0,%xmm1,%xmm0            # gen=c5 f1 d4 c0  dis=c5 f1 d4 c0
    vpaddq 0x90909090,%xmm1,%xmm0       # gen=c5 f1 d4 05  dis=c5 f1 d4 05 90 90 90
    vpmullw %xmm0,%xmm1,%xmm0           # gen=c5 f1 d5 c0  dis=c5 f1 d5 c0
    vpmullw 0x90909090,%xmm1,%xmm0      # gen=c5 f1 d5 05  dis=c5 f1 d5 05 90 90 90
    vpsubusb %xmm0,%xmm1,%xmm0          # gen=c5 f1 d8 c0  dis=c5 f1 d8 c0
    vpsubusb 0x90909090,%xmm1,%xmm0     # gen=c5 f1 d8 05  dis=c5 f1 d8 05 90 90 90
    vpsubusw %xmm0,%xmm1,%xmm0          # gen=c5 f1 d9 c0  dis=c5 f1 d9 c0
    vpsubusw 0x90909090,%xmm1,%xmm0     # gen=c5 f1 d9 05  dis=c5 f1 d9 05 90 90 90
    vpminub %xmm0,%xmm1,%xmm0           # gen=c5 f1 da c0  dis=c5 f1 da c0
    vpminub 0x90909090,%xmm1,%xmm0      # gen=c5 f1 da 05  dis=c5 f1 da 05 90 90 90
    vpand  %xmm0,%xmm1,%xmm0            # gen=c5 f1 db c0  dis=c5 f1 db c0
    vpand  0x90909090,%xmm1,%xmm0       # gen=c5 f1 db 05  dis=c5 f1 db 05 90 90 90
    vpaddusb %xmm0,%xmm1,%xmm0          # gen=c5 f1 dc c0  dis=c5 f1 dc c0
    vpaddusb 0x90909090,%xmm1,%xmm0     # gen=c5 f1 dc 05  dis=c5 f1 dc 05 90 90 90
    vpaddusw %xmm0,%xmm1,%xmm0          # gen=c5 f1 dd c0  dis=c5 f1 dd c0
    vpaddusw 0x90909090,%xmm1,%xmm0     # gen=c5 f1 dd 05  dis=c5 f1 dd 05 90 90 90
    vpmaxub %xmm0,%xmm1,%xmm0           # gen=c5 f1 de c0  dis=c5 f1 de c0
    vpmaxub 0x90909090,%xmm1,%xmm0      # gen=c5 f1 de 05  dis=c5 f1 de 05 90 90 90
    vpandn %xmm0,%xmm1,%xmm0            # gen=c5 f1 df c0  dis=c5 f1 df c0
    vpandn 0x90909090,%xmm1,%xmm0       # gen=c5 f1 df 05  dis=c5 f1 df 05 90 90 90
    vpavgb %xmm0,%xmm1,%xmm0            # gen=c5 f1 e0 c0  dis=c5 f1 e0 c0
    vpavgb 0x90909090,%xmm1,%xmm0       # gen=c5 f1 e0 05  dis=c5 f1 e0 05 90 90 90
    vpsraw %xmm0,%xmm1,%xmm0            # gen=c5 f1 e1 c0  dis=c5 f1 e1 c0
    vpsraw 0x90909090,%xmm1,%xmm0       # gen=c5 f1 e1 05  dis=c5 f1 e1 05 90 90 90
    vpsrad %xmm0,%xmm1,%xmm0            # gen=c5 f1 e2 c0  dis=c5 f1 e2 c0
    vpsrad 0x90909090,%xmm1,%xmm0       # gen=c5 f1 e2 05  dis=c5 f1 e2 05 90 90 90
    vpavgw %xmm0,%xmm1,%xmm0            # gen=c5 f1 e3 c0  dis=c5 f1 e3 c0
    vpavgw 0x90909090,%xmm1,%xmm0       # gen=c5 f1 e3 05  dis=c5 f1 e3 05 90 90 90
    vpmulhuw %xmm0,%xmm1,%xmm0          # gen=c5 f1 e4 c0  dis=c5 f1 e4 c0
    vpmulhuw 0x90909090,%xmm1,%xmm0     # gen=c5 f1 e4 05  dis=c5 f1 e4 05 90 90 90
    vpmulhw %xmm0,%xmm1,%xmm0           # gen=c5 f1 e5 c0  dis=c5 f1 e5 c0
    vpmulhw 0x90909090,%xmm1,%xmm0      # gen=c5 f1 e5 05  dis=c5 f1 e5 05 90 90 90
    vpsubsb %xmm0,%xmm1,%xmm0           # gen=c5 f1 e8 c0  dis=c5 f1 e8 c0
    vpsubsb 0x90909090,%xmm1,%xmm0      # gen=c5 f1 e8 05  dis=c5 f1 e8 05 90 90 90
    vpsubsw %xmm0,%xmm1,%xmm0           # gen=c5 f1 e9 c0  dis=c5 f1 e9 c0
    vpsubsw 0x90909090,%xmm1,%xmm0      # gen=c5 f1 e9 05  dis=c5 f1 e9 05 90 90 90
    vpminsw %xmm0,%xmm1,%xmm0           # gen=c5 f1 ea c0  dis=c5 f1 ea c0
    vpminsw 0x90909090,%xmm1,%xmm0      # gen=c5 f1 ea 05  dis=c5 f1 ea 05 90 90 90
    vpor   %xmm0,%xmm1,%xmm0            # gen=c5 f1 eb c0  dis=c5 f1 eb c0
    vpor   0x90909090,%xmm1,%xmm0       # gen=c5 f1 eb 05  dis=c5 f1 eb 05 90 90 90
    vpaddsb %xmm0,%xmm1,%xmm0           # gen=c5 f1 ec c0  dis=c5 f1 ec c0
    vpaddsb 0x90909090,%xmm1,%xmm0      # gen=c5 f1 ec 05  dis=c5 f1 ec 05 90 90 90
    vpaddsw %xmm0,%xmm1,%xmm0           # gen=c5 f1 ed c0  dis=c5 f1 ed c0
    vpaddsw 0x90909090,%xmm1,%xmm0      # gen=c5 f1 ed 05  dis=c5 f1 ed 05 90 90 90
    vpmaxsw %xmm0,%xmm1,%xmm0           # gen=c5 f1 ee c0  dis=c5 f1 ee c0
    vpmaxsw 0x90909090,%xmm1,%xmm0      # gen=c5 f1 ee 05  dis=c5 f1 ee 05 90 90 90
    vpxor  %xmm0,%xmm1,%xmm0            # gen=c5 f1 ef c0  dis=c5 f1 ef c0
    vpxor  0x90909090,%xmm1,%xmm0       # gen=c5 f1 ef 05  dis=c5 f1 ef 05 90 90 90
    vpsllw %xmm0,%xmm1,%xmm0            # gen=c5 f1 f1 c0  dis=c5 f1 f1 c0
    vpsllw 0x90909090,%xmm1,%xmm0       # gen=c5 f1 f1 05  dis=c5 f1 f1 05 90 90 90
    vpslld %xmm0,%xmm1,%xmm0            # gen=c5 f1 f2 c0  dis=c5 f1 f2 c0
    vpslld 0x90909090,%xmm1,%xmm0       # gen=c5 f1 f2 05  dis=c5 f1 f2 05 90 90 90
    vpsllq %xmm0,%xmm1,%xmm0            # gen=c5 f1 f3 c0  dis=c5 f1 f3 c0
    vpsllq 0x90909090,%xmm1,%xmm0       # gen=c5 f1 f3 05  dis=c5 f1 f3 05 90 90 90
    vpmuludq %xmm0,%xmm1,%xmm0          # gen=c5 f1 f4 c0  dis=c5 f1 f4 c0
    vpmuludq 0x90909090,%xmm1,%xmm0     # gen=c5 f1 f4 05  dis=c5 f1 f4 05 90 90 90
    vpmaddwd %xmm0,%xmm1,%xmm0          # gen=c5 f1 f5 c0  dis=c5 f1 f5 c0
    vpmaddwd 0x90909090,%xmm1,%xmm0     # gen=c5 f1 f5 05  dis=c5 f1 f5 05 90 90 90
    vpsadbw %xmm0,%xmm1,%xmm0           # gen=c5 f1 f6 c0  dis=c5 f1 f6 c0
    vpsadbw 0x90909090,%xmm1,%xmm0      # gen=c5 f1 f6 05  dis=c5 f1 f6 05 90 90 90
    vpsubb %xmm0,%xmm1,%xmm0            # gen=c5 f1 f8 c0  dis=c5 f1 f8 c0
    vpsubb 0x90909090,%xmm1,%xmm0       # gen=c5 f1 f8 05  dis=c5 f1 f8 05 90 90 90
    vpsubw %xmm0,%xmm1,%xmm0            # gen=c5 f1 f9 c0  dis=c5 f1 f9 c0
    vpsubw 0x90909090,%xmm1,%xmm0       # gen=c5 f1 f9 05  dis=c5 f1 f9 05 90 90 90
    vpsubd %xmm0,%xmm1,%xmm0            # gen=c5 f1 fa c0  dis=c5 f1 fa c0
    vpsubd 0x90909090,%xmm1,%xmm0       # gen=c5 f1 fa 05  dis=c5 f1 fa 05 90 90 90
    vpsubq %xmm0,%xmm1,%xmm0            # gen=c5 f1 fb c0  dis=c5 f1 fb c0
    vpsubq 0x90909090,%xmm1,%xmm0       # gen=c5 f1 fb 05  dis=c5 f1 fb 05 90 90 90
    vpaddb %xmm0,%xmm1,%xmm0            # gen=c5 f1 fc c0  dis=c5 f1 fc c0
    vpaddb 0x90909090,%xmm1,%xmm0       # gen=c5 f1 fc 05  dis=c5 f1 fc 05 90 90 90
    vpaddw %xmm0,%xmm1,%xmm0            # gen=c5 f1 fd c0  dis=c5 f1 fd c0
    vpaddw 0x90909090,%xmm1,%xmm0       # gen=c5 f1 fd 05  dis=c5 f1 fd 05 90 90 90
    vpaddd %xmm0,%xmm1,%xmm0            # gen=c5 f1 fe c0  dis=c5 f1 fe c0
    vpaddd 0x90909090,%xmm1,%xmm0       # gen=c5 f1 fe 05  dis=c5 f1 fe 05 90 90 90
    vmovss %xmm0,%xmm1,%xmm0            # gen=c5 f2 10 c0  dis=c5 f2 10 c0
    vcvtsi2ss %eax,%xmm1,%xmm0          # gen=c5 f2 2a c0  dis=c5 f2 2a c0
    vcvtsi2ss 0x90909090,%xmm1,%xmm0    # gen=c5 f2 2a 05  dis=c5 f2 2a 05 90 90 90
    vsqrtss %xmm0,%xmm1,%xmm0           # gen=c5 f2 51 c0  dis=c5 f2 51 c0
    vsqrtss 0x90909090,%xmm1,%xmm0      # gen=c5 f2 51 05  dis=c5 f2 51 05 90 90 90
    vrsqrtss %xmm0,%xmm1,%xmm0          # gen=c5 f2 52 c0  dis=c5 f2 52 c0
    vrsqrtss 0x90909090,%xmm1,%xmm0     # gen=c5 f2 52 05  dis=c5 f2 52 05 90 90 90
    vrcpss %xmm0,%xmm1,%xmm0            # gen=c5 f2 53 c0  dis=c5 f2 53 c0
    vrcpss 0x90909090,%xmm1,%xmm0       # gen=c5 f2 53 05  dis=c5 f2 53 05 90 90 90
    vaddss %xmm0,%xmm1,%xmm0            # gen=c5 f2 58 c0  dis=c5 f2 58 c0
    vaddss 0x90909090,%xmm1,%xmm0       # gen=c5 f2 58 05  dis=c5 f2 58 05 90 90 90
    vmulss %xmm0,%xmm1,%xmm0            # gen=c5 f2 59 c0  dis=c5 f2 59 c0
    vmulss 0x90909090,%xmm1,%xmm0       # gen=c5 f2 59 05  dis=c5 f2 59 05 90 90 90
    vcvtss2sd %xmm0,%xmm1,%xmm0         # gen=c5 f2 5a c0  dis=c5 f2 5a c0
    vcvtss2sd 0x90909090,%xmm1,%xmm0    # gen=c5 f2 5a 05  dis=c5 f2 5a 05 90 90 90
    vsubss %xmm0,%xmm1,%xmm0            # gen=c5 f2 5c c0  dis=c5 f2 5c c0
    vsubss 0x90909090,%xmm1,%xmm0       # gen=c5 f2 5c 05  dis=c5 f2 5c 05 90 90 90
    vminss %xmm0,%xmm1,%xmm0            # gen=c5 f2 5d c0  dis=c5 f2 5d c0
    vminss 0x90909090,%xmm1,%xmm0       # gen=c5 f2 5d 05  dis=c5 f2 5d 05 90 90 90
    vdivss %xmm0,%xmm1,%xmm0            # gen=c5 f2 5e c0  dis=c5 f2 5e c0
    vdivss 0x90909090,%xmm1,%xmm0       # gen=c5 f2 5e 05  dis=c5 f2 5e 05 90 90 90
    vmaxss %xmm0,%xmm1,%xmm0            # gen=c5 f2 5f c0  dis=c5 f2 5f c0
    vmaxss 0x90909090,%xmm1,%xmm0       # gen=c5 f2 5f 05  dis=c5 f2 5f 05 90 90 90
    vcmpss $0x90,%xmm0,%xmm1,%xmm0      # gen=c5 f2 c2 c0  dis=c5 f2 c2 c0 90
    vcmpss $0x90,0x90909090,%xmm1,%xmm0 # gen=c5 f2 c2 05  dis=c5 f2 c2 05 90 90 90
    vmovsd %xmm0,%xmm1,%xmm0            # gen=c5 f3 10 c0  dis=c5 f3 10 c0
    vcvtsi2sd %eax,%xmm1,%xmm0          # gen=c5 f3 2a c0  dis=c5 f3 2a c0
    vcvtsi2sd 0x90909090,%xmm1,%xmm0    # gen=c5 f3 2a 05  dis=c5 f3 2a 05 90 90 90
    vsqrtsd %xmm0,%xmm1,%xmm0           # gen=c5 f3 51 c0  dis=c5 f3 51 c0
    vsqrtsd 0x90909090,%xmm1,%xmm0      # gen=c5 f3 51 05  dis=c5 f3 51 05 90 90 90
    vaddsd %xmm0,%xmm1,%xmm0            # gen=c5 f3 58 c0  dis=c5 f3 58 c0
    vaddsd 0x90909090,%xmm1,%xmm0       # gen=c5 f3 58 05  dis=c5 f3 58 05 90 90 90
    vmulsd %xmm0,%xmm1,%xmm0            # gen=c5 f3 59 c0  dis=c5 f3 59 c0
    vmulsd 0x90909090,%xmm1,%xmm0       # gen=c5 f3 59 05  dis=c5 f3 59 05 90 90 90
    vcvtsd2ss %xmm0,%xmm1,%xmm0         # gen=c5 f3 5a c0  dis=c5 f3 5a c0
    vcvtsd2ss 0x90909090,%xmm1,%xmm0    # gen=c5 f3 5a 05  dis=c5 f3 5a 05 90 90 90
    vsubsd %xmm0,%xmm1,%xmm0            # gen=c5 f3 5c c0  dis=c5 f3 5c c0
    vsubsd 0x90909090,%xmm1,%xmm0       # gen=c5 f3 5c 05  dis=c5 f3 5c 05 90 90 90
    vminsd %xmm0,%xmm1,%xmm0            # gen=c5 f3 5d c0  dis=c5 f3 5d c0
    vminsd 0x90909090,%xmm1,%xmm0       # gen=c5 f3 5d 05  dis=c5 f3 5d 05 90 90 90
    vdivsd %xmm0,%xmm1,%xmm0            # gen=c5 f3 5e c0  dis=c5 f3 5e c0
    vdivsd 0x90909090,%xmm1,%xmm0       # gen=c5 f3 5e 05  dis=c5 f3 5e 05 90 90 90
    vmaxsd %xmm0,%xmm1,%xmm0            # gen=c5 f3 5f c0  dis=c5 f3 5f c0
    vmaxsd 0x90909090,%xmm1,%xmm0       # gen=c5 f3 5f 05  dis=c5 f3 5f 05 90 90 90
    vhaddps %xmm0,%xmm1,%xmm0           # gen=c5 f3 7c c0  dis=c5 f3 7c c0
    vhaddps 0x90909090,%xmm1,%xmm0      # gen=c5 f3 7c 05  dis=c5 f3 7c 05 90 90 90
    vhsubps %xmm0,%xmm1,%xmm0           # gen=c5 f3 7d c0  dis=c5 f3 7d c0
    vhsubps 0x90909090,%xmm1,%xmm0      # gen=c5 f3 7d 05  dis=c5 f3 7d 05 90 90 90
    vcmpsd $0x90,%xmm0,%xmm1,%xmm0      # gen=c5 f3 c2 c0  dis=c5 f3 c2 c0 90
    vcmpsd $0x90,0x90909090,%xmm1,%xmm0 # gen=c5 f3 c2 05  dis=c5 f3 c2 05 90 90 90
    vaddsubps %xmm0,%xmm1,%xmm0         # gen=c5 f3 d0 c0  dis=c5 f3 d0 c0
    vaddsubps 0x90909090,%xmm1,%xmm0    # gen=c5 f3 d0 05  dis=c5 f3 d0 05 90 90 90
    vunpcklps %ymm0,%ymm1,%ymm0         # gen=c5 f4 14 c0  dis=c5 f4 14 c0
    vunpcklps 0x90909090,%ymm1,%ymm0    # gen=c5 f4 14 05  dis=c5 f4 14 05 90 90 90
    vunpckhps %ymm0,%ymm1,%ymm0         # gen=c5 f4 15 c0  dis=c5 f4 15 c0
    vunpckhps 0x90909090,%ymm1,%ymm0    # gen=c5 f4 15 05  dis=c5 f4 15 05 90 90 90
    kandw  %k0,%k1,%k0                  # gen=c5 f4 41 c0  dis=c5 f4 41 c0
    kandnw %k0,%k1,%k0                  # gen=c5 f4 42 c0  dis=c5 f4 42 c0
    korw   %k0,%k1,%k0                  # gen=c5 f4 45 c0  dis=c5 f4 45 c0
    kxnorw %k0,%k1,%k0                  # gen=c5 f4 46 c0  dis=c5 f4 46 c0
    kxorw  %k0,%k1,%k0                  # gen=c5 f4 47 c0  dis=c5 f4 47 c0
    kaddw  %k0,%k1,%k0                  # gen=c5 f4 4a c0  dis=c5 f4 4a c0
    kunpckwd %k0,%k1,%k0                # gen=c5 f4 4b c0  dis=c5 f4 4b c0
    vandps %ymm0,%ymm1,%ymm0            # gen=c5 f4 54 c0  dis=c5 f4 54 c0
    vandps 0x90909090,%ymm1,%ymm0       # gen=c5 f4 54 05  dis=c5 f4 54 05 90 90 90
    vandnps %ymm0,%ymm1,%ymm0           # gen=c5 f4 55 c0  dis=c5 f4 55 c0
    vandnps 0x90909090,%ymm1,%ymm0      # gen=c5 f4 55 05  dis=c5 f4 55 05 90 90 90
    vorps  %ymm0,%ymm1,%ymm0            # gen=c5 f4 56 c0  dis=c5 f4 56 c0
    vorps  0x90909090,%ymm1,%ymm0       # gen=c5 f4 56 05  dis=c5 f4 56 05 90 90 90
    vxorps %ymm0,%ymm1,%ymm0            # gen=c5 f4 57 c0  dis=c5 f4 57 c0
    vxorps 0x90909090,%ymm1,%ymm0       # gen=c5 f4 57 05  dis=c5 f4 57 05 90 90 90
    vaddps %ymm0,%ymm1,%ymm0            # gen=c5 f4 58 c0  dis=c5 f4 58 c0
    vaddps 0x90909090,%ymm1,%ymm0       # gen=c5 f4 58 05  dis=c5 f4 58 05 90 90 90
    vmulps %ymm0,%ymm1,%ymm0            # gen=c5 f4 59 c0  dis=c5 f4 59 c0
    vmulps 0x90909090,%ymm1,%ymm0       # gen=c5 f4 59 05  dis=c5 f4 59 05 90 90 90
    vsubps %ymm0,%ymm1,%ymm0            # gen=c5 f4 5c c0  dis=c5 f4 5c c0
    vsubps 0x90909090,%ymm1,%ymm0       # gen=c5 f4 5c 05  dis=c5 f4 5c 05 90 90 90
    vminps %ymm0,%ymm1,%ymm0            # gen=c5 f4 5d c0  dis=c5 f4 5d c0
    vminps 0x90909090,%ymm1,%ymm0       # gen=c5 f4 5d 05  dis=c5 f4 5d 05 90 90 90
    vdivps %ymm0,%ymm1,%ymm0            # gen=c5 f4 5e c0  dis=c5 f4 5e c0
    vdivps 0x90909090,%ymm1,%ymm0       # gen=c5 f4 5e 05  dis=c5 f4 5e 05 90 90 90
    vmaxps %ymm0,%ymm1,%ymm0            # gen=c5 f4 5f c0  dis=c5 f4 5f c0
    vmaxps 0x90909090,%ymm1,%ymm0       # gen=c5 f4 5f 05  dis=c5 f4 5f 05 90 90 90
    vcmpps $0x90,%ymm0,%ymm1,%ymm0      # gen=c5 f4 c2 c0  dis=c5 f4 c2 c0 90
    vcmpps $0x90,0x90909090,%ymm1,%ymm0 # gen=c5 f4 c2 05  dis=c5 f4 c2 05 90 90 90
    vshufps $0x90,%ymm0,%ymm1,%ymm0     # gen=c5 f4 c6 c0  dis=c5 f4 c6 c0 90
    vshufps $0x90,0x90909090,%ymm1,%ymm0 # gen=c5 f4 c6 05  dis=c5 f4 c6 05 90 90 90
    vunpcklpd %ymm0,%ymm1,%ymm0         # gen=c5 f5 14 c0  dis=c5 f5 14 c0
    vunpcklpd 0x90909090,%ymm1,%ymm0    # gen=c5 f5 14 05  dis=c5 f5 14 05 90 90 90
    vunpckhpd %ymm0,%ymm1,%ymm0         # gen=c5 f5 15 c0  dis=c5 f5 15 c0
    vunpckhpd 0x90909090,%ymm1,%ymm0    # gen=c5 f5 15 05  dis=c5 f5 15 05 90 90 90
    kandb  %k0,%k1,%k0                  # gen=c5 f5 41 c0  dis=c5 f5 41 c0
    kandnb %k0,%k1,%k0                  # gen=c5 f5 42 c0  dis=c5 f5 42 c0
    korb   %k0,%k1,%k0                  # gen=c5 f5 45 c0  dis=c5 f5 45 c0
    kxnorb %k0,%k1,%k0                  # gen=c5 f5 46 c0  dis=c5 f5 46 c0
    kxorb  %k0,%k1,%k0                  # gen=c5 f5 47 c0  dis=c5 f5 47 c0
    kaddb  %k0,%k1,%k0                  # gen=c5 f5 4a c0  dis=c5 f5 4a c0
    kunpckbw %k0,%k1,%k0                # gen=c5 f5 4b c0  dis=c5 f5 4b c0
    vandpd %ymm0,%ymm1,%ymm0            # gen=c5 f5 54 c0  dis=c5 f5 54 c0
    vandpd 0x90909090,%ymm1,%ymm0       # gen=c5 f5 54 05  dis=c5 f5 54 05 90 90 90
    vandnpd %ymm0,%ymm1,%ymm0           # gen=c5 f5 55 c0  dis=c5 f5 55 c0
    vandnpd 0x90909090,%ymm1,%ymm0      # gen=c5 f5 55 05  dis=c5 f5 55 05 90 90 90
    vorpd  %ymm0,%ymm1,%ymm0            # gen=c5 f5 56 c0  dis=c5 f5 56 c0
    vorpd  0x90909090,%ymm1,%ymm0       # gen=c5 f5 56 05  dis=c5 f5 56 05 90 90 90
    vxorpd %ymm0,%ymm1,%ymm0            # gen=c5 f5 57 c0  dis=c5 f5 57 c0
    vxorpd 0x90909090,%ymm1,%ymm0       # gen=c5 f5 57 05  dis=c5 f5 57 05 90 90 90
    vaddpd %ymm0,%ymm1,%ymm0            # gen=c5 f5 58 c0  dis=c5 f5 58 c0
    vaddpd 0x90909090,%ymm1,%ymm0       # gen=c5 f5 58 05  dis=c5 f5 58 05 90 90 90
    vmulpd %ymm0,%ymm1,%ymm0            # gen=c5 f5 59 c0  dis=c5 f5 59 c0
    vmulpd 0x90909090,%ymm1,%ymm0       # gen=c5 f5 59 05  dis=c5 f5 59 05 90 90 90
    vsubpd %ymm0,%ymm1,%ymm0            # gen=c5 f5 5c c0  dis=c5 f5 5c c0
    vsubpd 0x90909090,%ymm1,%ymm0       # gen=c5 f5 5c 05  dis=c5 f5 5c 05 90 90 90
    vminpd %ymm0,%ymm1,%ymm0            # gen=c5 f5 5d c0  dis=c5 f5 5d c0
    vminpd 0x90909090,%ymm1,%ymm0       # gen=c5 f5 5d 05  dis=c5 f5 5d 05 90 90 90
    vdivpd %ymm0,%ymm1,%ymm0            # gen=c5 f5 5e c0  dis=c5 f5 5e c0
    vdivpd 0x90909090,%ymm1,%ymm0       # gen=c5 f5 5e 05  dis=c5 f5 5e 05 90 90 90
    vmaxpd %ymm0,%ymm1,%ymm0            # gen=c5 f5 5f c0  dis=c5 f5 5f c0
    vmaxpd 0x90909090,%ymm1,%ymm0       # gen=c5 f5 5f 05  dis=c5 f5 5f 05 90 90 90
    vpunpcklbw %ymm0,%ymm1,%ymm0        # gen=c5 f5 60 c0  dis=c5 f5 60 c0
    vpunpcklbw 0x90909090,%ymm1,%ymm0   # gen=c5 f5 60 05  dis=c5 f5 60 05 90 90 90
    vpunpcklwd %ymm0,%ymm1,%ymm0        # gen=c5 f5 61 c0  dis=c5 f5 61 c0
    vpunpcklwd 0x90909090,%ymm1,%ymm0   # gen=c5 f5 61 05  dis=c5 f5 61 05 90 90 90
    vpunpckldq %ymm0,%ymm1,%ymm0        # gen=c5 f5 62 c0  dis=c5 f5 62 c0
    vpunpckldq 0x90909090,%ymm1,%ymm0   # gen=c5 f5 62 05  dis=c5 f5 62 05 90 90 90
    vpacksswb %ymm0,%ymm1,%ymm0         # gen=c5 f5 63 c0  dis=c5 f5 63 c0
    vpacksswb 0x90909090,%ymm1,%ymm0    # gen=c5 f5 63 05  dis=c5 f5 63 05 90 90 90
    vpcmpgtb %ymm0,%ymm1,%ymm0          # gen=c5 f5 64 c0  dis=c5 f5 64 c0
    vpcmpgtb 0x90909090,%ymm1,%ymm0     # gen=c5 f5 64 05  dis=c5 f5 64 05 90 90 90
    vpcmpgtw %ymm0,%ymm1,%ymm0          # gen=c5 f5 65 c0  dis=c5 f5 65 c0
    vpcmpgtw 0x90909090,%ymm1,%ymm0     # gen=c5 f5 65 05  dis=c5 f5 65 05 90 90 90
    vpcmpgtd %ymm0,%ymm1,%ymm0          # gen=c5 f5 66 c0  dis=c5 f5 66 c0
    vpcmpgtd 0x90909090,%ymm1,%ymm0     # gen=c5 f5 66 05  dis=c5 f5 66 05 90 90 90
    vpackuswb %ymm0,%ymm1,%ymm0         # gen=c5 f5 67 c0  dis=c5 f5 67 c0
    vpackuswb 0x90909090,%ymm1,%ymm0    # gen=c5 f5 67 05  dis=c5 f5 67 05 90 90 90
    vpunpckhbw %ymm0,%ymm1,%ymm0        # gen=c5 f5 68 c0  dis=c5 f5 68 c0
    vpunpckhbw 0x90909090,%ymm1,%ymm0   # gen=c5 f5 68 05  dis=c5 f5 68 05 90 90 90
    vpunpckhwd %ymm0,%ymm1,%ymm0        # gen=c5 f5 69 c0  dis=c5 f5 69 c0
    vpunpckhwd 0x90909090,%ymm1,%ymm0   # gen=c5 f5 69 05  dis=c5 f5 69 05 90 90 90
    vpunpckhdq %ymm0,%ymm1,%ymm0        # gen=c5 f5 6a c0  dis=c5 f5 6a c0
    vpunpckhdq 0x90909090,%ymm1,%ymm0   # gen=c5 f5 6a 05  dis=c5 f5 6a 05 90 90 90
    vpackssdw %ymm0,%ymm1,%ymm0         # gen=c5 f5 6b c0  dis=c5 f5 6b c0
    vpackssdw 0x90909090,%ymm1,%ymm0    # gen=c5 f5 6b 05  dis=c5 f5 6b 05 90 90 90
    vpunpcklqdq %ymm0,%ymm1,%ymm0       # gen=c5 f5 6c c0  dis=c5 f5 6c c0
    vpunpcklqdq 0x90909090,%ymm1,%ymm0  # gen=c5 f5 6c 05  dis=c5 f5 6c 05 90 90 90
    vpunpckhqdq %ymm0,%ymm1,%ymm0       # gen=c5 f5 6d c0  dis=c5 f5 6d c0
    vpunpckhqdq 0x90909090,%ymm1,%ymm0  # gen=c5 f5 6d 05  dis=c5 f5 6d 05 90 90 90
    vpcmpeqb %ymm0,%ymm1,%ymm0          # gen=c5 f5 74 c0  dis=c5 f5 74 c0
    vpcmpeqb 0x90909090,%ymm1,%ymm0     # gen=c5 f5 74 05  dis=c5 f5 74 05 90 90 90
    vpcmpeqw %ymm0,%ymm1,%ymm0          # gen=c5 f5 75 c0  dis=c5 f5 75 c0
    vpcmpeqw 0x90909090,%ymm1,%ymm0     # gen=c5 f5 75 05  dis=c5 f5 75 05 90 90 90
    vpcmpeqd %ymm0,%ymm1,%ymm0          # gen=c5 f5 76 c0  dis=c5 f5 76 c0
    vpcmpeqd 0x90909090,%ymm1,%ymm0     # gen=c5 f5 76 05  dis=c5 f5 76 05 90 90 90
    vhaddpd %ymm0,%ymm1,%ymm0           # gen=c5 f5 7c c0  dis=c5 f5 7c c0
    vhaddpd 0x90909090,%ymm1,%ymm0      # gen=c5 f5 7c 05  dis=c5 f5 7c 05 90 90 90
    vhsubpd %ymm0,%ymm1,%ymm0           # gen=c5 f5 7d c0  dis=c5 f5 7d c0
    vhsubpd 0x90909090,%ymm1,%ymm0      # gen=c5 f5 7d 05  dis=c5 f5 7d 05 90 90 90
    vcmppd $0x90,%ymm0,%ymm1,%ymm0      # gen=c5 f5 c2 c0  dis=c5 f5 c2 c0 90
    vcmppd $0x90,0x90909090,%ymm1,%ymm0 # gen=c5 f5 c2 05  dis=c5 f5 c2 05 90 90 90
    vshufpd $0x90,%ymm0,%ymm1,%ymm0     # gen=c5 f5 c6 c0  dis=c5 f5 c6 c0 90
    vshufpd $0x90,0x90909090,%ymm1,%ymm0 # gen=c5 f5 c6 05  dis=c5 f5 c6 05 90 90 90
    vaddsubpd %ymm0,%ymm1,%ymm0         # gen=c5 f5 d0 c0  dis=c5 f5 d0 c0
    vaddsubpd 0x90909090,%ymm1,%ymm0    # gen=c5 f5 d0 05  dis=c5 f5 d0 05 90 90 90
    vpsrlw %xmm0,%ymm1,%ymm0            # gen=c5 f5 d1 c0  dis=c5 f5 d1 c0
    vpsrlw 0x90909090,%ymm1,%ymm0       # gen=c5 f5 d1 05  dis=c5 f5 d1 05 90 90 90
    vpsrld %xmm0,%ymm1,%ymm0            # gen=c5 f5 d2 c0  dis=c5 f5 d2 c0
    vpsrld 0x90909090,%ymm1,%ymm0       # gen=c5 f5 d2 05  dis=c5 f5 d2 05 90 90 90
    vpsrlq %xmm0,%ymm1,%ymm0            # gen=c5 f5 d3 c0  dis=c5 f5 d3 c0
    vpsrlq 0x90909090,%ymm1,%ymm0       # gen=c5 f5 d3 05  dis=c5 f5 d3 05 90 90 90
    vpaddq %ymm0,%ymm1,%ymm0            # gen=c5 f5 d4 c0  dis=c5 f5 d4 c0
    vpaddq 0x90909090,%ymm1,%ymm0       # gen=c5 f5 d4 05  dis=c5 f5 d4 05 90 90 90
    vpmullw %ymm0,%ymm1,%ymm0           # gen=c5 f5 d5 c0  dis=c5 f5 d5 c0
    vpmullw 0x90909090,%ymm1,%ymm0      # gen=c5 f5 d5 05  dis=c5 f5 d5 05 90 90 90
    vpsubusb %ymm0,%ymm1,%ymm0          # gen=c5 f5 d8 c0  dis=c5 f5 d8 c0
    vpsubusb 0x90909090,%ymm1,%ymm0     # gen=c5 f5 d8 05  dis=c5 f5 d8 05 90 90 90
    vpsubusw %ymm0,%ymm1,%ymm0          # gen=c5 f5 d9 c0  dis=c5 f5 d9 c0
    vpsubusw 0x90909090,%ymm1,%ymm0     # gen=c5 f5 d9 05  dis=c5 f5 d9 05 90 90 90
    vpminub %ymm0,%ymm1,%ymm0           # gen=c5 f5 da c0  dis=c5 f5 da c0
    vpminub 0x90909090,%ymm1,%ymm0      # gen=c5 f5 da 05  dis=c5 f5 da 05 90 90 90
    vpand  %ymm0,%ymm1,%ymm0            # gen=c5 f5 db c0  dis=c5 f5 db c0
    vpand  0x90909090,%ymm1,%ymm0       # gen=c5 f5 db 05  dis=c5 f5 db 05 90 90 90
    vpaddusb %ymm0,%ymm1,%ymm0          # gen=c5 f5 dc c0  dis=c5 f5 dc c0
    vpaddusb 0x90909090,%ymm1,%ymm0     # gen=c5 f5 dc 05  dis=c5 f5 dc 05 90 90 90
    vpaddusw %ymm0,%ymm1,%ymm0          # gen=c5 f5 dd c0  dis=c5 f5 dd c0
    vpaddusw 0x90909090,%ymm1,%ymm0     # gen=c5 f5 dd 05  dis=c5 f5 dd 05 90 90 90
    vpmaxub %ymm0,%ymm1,%ymm0           # gen=c5 f5 de c0  dis=c5 f5 de c0
    vpmaxub 0x90909090,%ymm1,%ymm0      # gen=c5 f5 de 05  dis=c5 f5 de 05 90 90 90
    vpandn %ymm0,%ymm1,%ymm0            # gen=c5 f5 df c0  dis=c5 f5 df c0
    vpandn 0x90909090,%ymm1,%ymm0       # gen=c5 f5 df 05  dis=c5 f5 df 05 90 90 90
    vpavgb %ymm0,%ymm1,%ymm0            # gen=c5 f5 e0 c0  dis=c5 f5 e0 c0
    vpavgb 0x90909090,%ymm1,%ymm0       # gen=c5 f5 e0 05  dis=c5 f5 e0 05 90 90 90
    vpsraw %xmm0,%ymm1,%ymm0            # gen=c5 f5 e1 c0  dis=c5 f5 e1 c0
    vpsraw 0x90909090,%ymm1,%ymm0       # gen=c5 f5 e1 05  dis=c5 f5 e1 05 90 90 90
    vpsrad %xmm0,%ymm1,%ymm0            # gen=c5 f5 e2 c0  dis=c5 f5 e2 c0
    vpsrad 0x90909090,%ymm1,%ymm0       # gen=c5 f5 e2 05  dis=c5 f5 e2 05 90 90 90
    vpavgw %ymm0,%ymm1,%ymm0            # gen=c5 f5 e3 c0  dis=c5 f5 e3 c0
    vpavgw 0x90909090,%ymm1,%ymm0       # gen=c5 f5 e3 05  dis=c5 f5 e3 05 90 90 90
    vpmulhuw %ymm0,%ymm1,%ymm0          # gen=c5 f5 e4 c0  dis=c5 f5 e4 c0
    vpmulhuw 0x90909090,%ymm1,%ymm0     # gen=c5 f5 e4 05  dis=c5 f5 e4 05 90 90 90
    vpmulhw %ymm0,%ymm1,%ymm0           # gen=c5 f5 e5 c0  dis=c5 f5 e5 c0
    vpmulhw 0x90909090,%ymm1,%ymm0      # gen=c5 f5 e5 05  dis=c5 f5 e5 05 90 90 90
    vpsubsb %ymm0,%ymm1,%ymm0           # gen=c5 f5 e8 c0  dis=c5 f5 e8 c0
    vpsubsb 0x90909090,%ymm1,%ymm0      # gen=c5 f5 e8 05  dis=c5 f5 e8 05 90 90 90
    vpsubsw %ymm0,%ymm1,%ymm0           # gen=c5 f5 e9 c0  dis=c5 f5 e9 c0
    vpsubsw 0x90909090,%ymm1,%ymm0      # gen=c5 f5 e9 05  dis=c5 f5 e9 05 90 90 90
    vpminsw %ymm0,%ymm1,%ymm0           # gen=c5 f5 ea c0  dis=c5 f5 ea c0
    vpminsw 0x90909090,%ymm1,%ymm0      # gen=c5 f5 ea 05  dis=c5 f5 ea 05 90 90 90
    vpor   %ymm0,%ymm1,%ymm0            # gen=c5 f5 eb c0  dis=c5 f5 eb c0
    vpor   0x90909090,%ymm1,%ymm0       # gen=c5 f5 eb 05  dis=c5 f5 eb 05 90 90 90
    vpaddsb %ymm0,%ymm1,%ymm0           # gen=c5 f5 ec c0  dis=c5 f5 ec c0
    vpaddsb 0x90909090,%ymm1,%ymm0      # gen=c5 f5 ec 05  dis=c5 f5 ec 05 90 90 90
    vpaddsw %ymm0,%ymm1,%ymm0           # gen=c5 f5 ed c0  dis=c5 f5 ed c0
    vpaddsw 0x90909090,%ymm1,%ymm0      # gen=c5 f5 ed 05  dis=c5 f5 ed 05 90 90 90
    vpmaxsw %ymm0,%ymm1,%ymm0           # gen=c5 f5 ee c0  dis=c5 f5 ee c0
    vpmaxsw 0x90909090,%ymm1,%ymm0      # gen=c5 f5 ee 05  dis=c5 f5 ee 05 90 90 90
    vpxor  %ymm0,%ymm1,%ymm0            # gen=c5 f5 ef c0  dis=c5 f5 ef c0
    vpxor  0x90909090,%ymm1,%ymm0       # gen=c5 f5 ef 05  dis=c5 f5 ef 05 90 90 90
    vpsllw %xmm0,%ymm1,%ymm0            # gen=c5 f5 f1 c0  dis=c5 f5 f1 c0
    vpsllw 0x90909090,%ymm1,%ymm0       # gen=c5 f5 f1 05  dis=c5 f5 f1 05 90 90 90
    vpslld %xmm0,%ymm1,%ymm0            # gen=c5 f5 f2 c0  dis=c5 f5 f2 c0
    vpslld 0x90909090,%ymm1,%ymm0       # gen=c5 f5 f2 05  dis=c5 f5 f2 05 90 90 90
    vpsllq %xmm0,%ymm1,%ymm0            # gen=c5 f5 f3 c0  dis=c5 f5 f3 c0
    vpsllq 0x90909090,%ymm1,%ymm0       # gen=c5 f5 f3 05  dis=c5 f5 f3 05 90 90 90
    vpmuludq %ymm0,%ymm1,%ymm0          # gen=c5 f5 f4 c0  dis=c5 f5 f4 c0
    vpmuludq 0x90909090,%ymm1,%ymm0     # gen=c5 f5 f4 05  dis=c5 f5 f4 05 90 90 90
    vpmaddwd %ymm0,%ymm1,%ymm0          # gen=c5 f5 f5 c0  dis=c5 f5 f5 c0
    vpmaddwd 0x90909090,%ymm1,%ymm0     # gen=c5 f5 f5 05  dis=c5 f5 f5 05 90 90 90
    vpsadbw %ymm0,%ymm1,%ymm0           # gen=c5 f5 f6 c0  dis=c5 f5 f6 c0
    vpsadbw 0x90909090,%ymm1,%ymm0      # gen=c5 f5 f6 05  dis=c5 f5 f6 05 90 90 90
    vpsubb %ymm0,%ymm1,%ymm0            # gen=c5 f5 f8 c0  dis=c5 f5 f8 c0
    vpsubb 0x90909090,%ymm1,%ymm0       # gen=c5 f5 f8 05  dis=c5 f5 f8 05 90 90 90
    vpsubw %ymm0,%ymm1,%ymm0            # gen=c5 f5 f9 c0  dis=c5 f5 f9 c0
    vpsubw 0x90909090,%ymm1,%ymm0       # gen=c5 f5 f9 05  dis=c5 f5 f9 05 90 90 90
    vpsubd %ymm0,%ymm1,%ymm0            # gen=c5 f5 fa c0  dis=c5 f5 fa c0
    vpsubd 0x90909090,%ymm1,%ymm0       # gen=c5 f5 fa 05  dis=c5 f5 fa 05 90 90 90
    vpsubq %ymm0,%ymm1,%ymm0            # gen=c5 f5 fb c0  dis=c5 f5 fb c0
    vpsubq 0x90909090,%ymm1,%ymm0       # gen=c5 f5 fb 05  dis=c5 f5 fb 05 90 90 90
    vpaddb %ymm0,%ymm1,%ymm0            # gen=c5 f5 fc c0  dis=c5 f5 fc c0
    vpaddb 0x90909090,%ymm1,%ymm0       # gen=c5 f5 fc 05  dis=c5 f5 fc 05 90 90 90
    vpaddw %ymm0,%ymm1,%ymm0            # gen=c5 f5 fd c0  dis=c5 f5 fd c0
    vpaddw 0x90909090,%ymm1,%ymm0       # gen=c5 f5 fd 05  dis=c5 f5 fd 05 90 90 90
    vpaddd %ymm0,%ymm1,%ymm0            # gen=c5 f5 fe c0  dis=c5 f5 fe c0
    vpaddd 0x90909090,%ymm1,%ymm0       # gen=c5 f5 fe 05  dis=c5 f5 fe 05 90 90 90
    vhaddps %ymm0,%ymm1,%ymm0           # gen=c5 f7 7c c0  dis=c5 f7 7c c0
    vhaddps 0x90909090,%ymm1,%ymm0      # gen=c5 f7 7c 05  dis=c5 f7 7c 05 90 90 90
    vhsubps %ymm0,%ymm1,%ymm0           # gen=c5 f7 7d c0  dis=c5 f7 7d c0
    vhsubps 0x90909090,%ymm1,%ymm0      # gen=c5 f7 7d 05  dis=c5 f7 7d 05 90 90 90
    vaddsubps %ymm0,%ymm1,%ymm0         # gen=c5 f7 d0 c0  dis=c5 f7 d0 c0
    vaddsubps 0x90909090,%ymm1,%ymm0    # gen=c5 f7 d0 05  dis=c5 f7 d0 05 90 90 90
    kandq  %k0,%k1,%k0                  # gen=c4 e1 f4 41 c0  dis=c4 e1 f4 41 c0
    kandnq %k0,%k1,%k0                  # gen=c4 e1 f4 42 c0  dis=c4 e1 f4 42 c0
    korq   %k0,%k1,%k0                  # gen=c4 e1 f4 45 c0  dis=c4 e1 f4 45 c0
    kxnorq %k0,%k1,%k0                  # gen=c4 e1 f4 46 c0  dis=c4 e1 f4 46 c0
    kxorq  %k0,%k1,%k0                  # gen=c4 e1 f4 47 c0  dis=c4 e1 f4 47 c0
    kaddq  %k0,%k1,%k0                  # gen=c4 e1 f4 4a c0  dis=c4 e1 f4 4a c0
    kunpckdq %k0,%k1,%k0                # gen=c4 e1 f4 4b c0  dis=c4 e1 f4 4b c0
    kandd  %k0,%k1,%k0                  # gen=c4 e1 f5 41 c0  dis=c4 e1 f5 41 c0
    kandnd %k0,%k1,%k0                  # gen=c4 e1 f5 42 c0  dis=c4 e1 f5 42 c0
    kord   %k0,%k1,%k0                  # gen=c4 e1 f5 45 c0  dis=c4 e1 f5 45 c0
    kxnord %k0,%k1,%k0                  # gen=c4 e1 f5 46 c0  dis=c4 e1 f5 46 c0
    kxord  %k0,%k1,%k0                  # gen=c4 e1 f5 47 c0  dis=c4 e1 f5 47 c0
    kaddd  %k0,%k1,%k0                  # gen=c4 e1 f5 4a c0  dis=c4 e1 f5 4a c0
    vpdpbuud %xmm0,%xmm1,%xmm0          # gen=c4 e2 70 50 c0  dis=c4 e2 70 50 c0
    vpdpbuud 0x90909090,%xmm1,%xmm0     # gen=c4 e2 70 50 05  dis=c4 e2 70 50 05 90 90
    vpdpbuuds %xmm0,%xmm1,%xmm0         # gen=c4 e2 70 51 c0  dis=c4 e2 70 51 c0
    vpdpbuuds 0x90909090,%xmm1,%xmm0    # gen=c4 e2 70 51 05  dis=c4 e2 70 51 05 90 90
    andn   %eax,%ecx,%eax               # gen=c4 e2 70 f2 c0  dis=c4 e2 70 f2 c0
    andn   0x90909090,%ecx,%eax         # gen=c4 e2 70 f2 05  dis=c4 e2 70 f2 05 90 90
    bzhi   %ecx,%eax,%eax               # gen=c4 e2 70 f5 c0  dis=c4 e2 70 f5 c0
    bzhi   %ecx,0x90909090,%eax         # gen=c4 e2 70 f5 05  dis=c4 e2 70 f5 05 90 90
    bextr  %ecx,%eax,%eax               # gen=c4 e2 70 f7 c0  dis=c4 e2 70 f7 c0
    bextr  %ecx,0x90909090,%eax         # gen=c4 e2 70 f7 05  dis=c4 e2 70 f7 05 90 90
    vpshufb %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 00 c0  dis=c4 e2 71 00 c0
    vpshufb 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 00 05  dis=c4 e2 71 00 05 90 90
    vphaddw %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 01 c0  dis=c4 e2 71 01 c0
    vphaddw 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 01 05  dis=c4 e2 71 01 05 90 90
    vphaddd %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 02 c0  dis=c4 e2 71 02 c0
    vphaddd 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 02 05  dis=c4 e2 71 02 05 90 90
    vphaddsw %xmm0,%xmm1,%xmm0          # gen=c4 e2 71 03 c0  dis=c4 e2 71 03 c0
    vphaddsw 0x90909090,%xmm1,%xmm0     # gen=c4 e2 71 03 05  dis=c4 e2 71 03 05 90 90
    vpmaddubsw %xmm0,%xmm1,%xmm0        # gen=c4 e2 71 04 c0  dis=c4 e2 71 04 c0
    vpmaddubsw 0x90909090,%xmm1,%xmm0   # gen=c4 e2 71 04 05  dis=c4 e2 71 04 05 90 90
    vphsubw %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 05 c0  dis=c4 e2 71 05 c0
    vphsubw 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 05 05  dis=c4 e2 71 05 05 90 90
    vphsubd %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 06 c0  dis=c4 e2 71 06 c0
    vphsubd 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 06 05  dis=c4 e2 71 06 05 90 90
    vphsubsw %xmm0,%xmm1,%xmm0          # gen=c4 e2 71 07 c0  dis=c4 e2 71 07 c0
    vphsubsw 0x90909090,%xmm1,%xmm0     # gen=c4 e2 71 07 05  dis=c4 e2 71 07 05 90 90
    vpsignb %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 08 c0  dis=c4 e2 71 08 c0
    vpsignb 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 08 05  dis=c4 e2 71 08 05 90 90
    vpsignw %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 09 c0  dis=c4 e2 71 09 c0
    vpsignw 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 09 05  dis=c4 e2 71 09 05 90 90
    vpsignd %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 0a c0  dis=c4 e2 71 0a c0
    vpsignd 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 0a 05  dis=c4 e2 71 0a 05 90 90
    vpmulhrsw %xmm0,%xmm1,%xmm0         # gen=c4 e2 71 0b c0  dis=c4 e2 71 0b c0
    vpmulhrsw 0x90909090,%xmm1,%xmm0    # gen=c4 e2 71 0b 05  dis=c4 e2 71 0b 05 90 90
    vpermilps %xmm0,%xmm1,%xmm0         # gen=c4 e2 71 0c c0  dis=c4 e2 71 0c c0
    vpermilps 0x90909090,%xmm1,%xmm0    # gen=c4 e2 71 0c 05  dis=c4 e2 71 0c 05 90 90
    vpermilpd %xmm0,%xmm1,%xmm0         # gen=c4 e2 71 0d c0  dis=c4 e2 71 0d c0
    vpermilpd 0x90909090,%xmm1,%xmm0    # gen=c4 e2 71 0d 05  dis=c4 e2 71 0d 05 90 90
    vpmuldq %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 28 c0  dis=c4 e2 71 28 c0
    vpmuldq 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 28 05  dis=c4 e2 71 28 05 90 90
    vpcmpeqq %xmm0,%xmm1,%xmm0          # gen=c4 e2 71 29 c0  dis=c4 e2 71 29 c0
    vpcmpeqq 0x90909090,%xmm1,%xmm0     # gen=c4 e2 71 29 05  dis=c4 e2 71 29 05 90 90
    vpackusdw %xmm0,%xmm1,%xmm0         # gen=c4 e2 71 2b c0  dis=c4 e2 71 2b c0
    vpackusdw 0x90909090,%xmm1,%xmm0    # gen=c4 e2 71 2b 05  dis=c4 e2 71 2b 05 90 90
    vmaskmovps 0x90909090,%xmm1,%xmm0   # gen=c4 e2 71 2c 05  dis=c4 e2 71 2c 05 90 90
    vmaskmovpd 0x90909090,%xmm1,%xmm0   # gen=c4 e2 71 2d 05  dis=c4 e2 71 2d 05 90 90
    vmaskmovps %xmm0,%xmm1,0x90909090   # gen=c4 e2 71 2e 05  dis=c4 e2 71 2e 05 90 90
    vmaskmovpd %xmm0,%xmm1,0x90909090   # gen=c4 e2 71 2f 05  dis=c4 e2 71 2f 05 90 90
    vpcmpgtq %xmm0,%xmm1,%xmm0          # gen=c4 e2 71 37 c0  dis=c4 e2 71 37 c0
    vpcmpgtq 0x90909090,%xmm1,%xmm0     # gen=c4 e2 71 37 05  dis=c4 e2 71 37 05 90 90
    vpminsb %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 38 c0  dis=c4 e2 71 38 c0
    vpminsb 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 38 05  dis=c4 e2 71 38 05 90 90
    vpminsd %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 39 c0  dis=c4 e2 71 39 c0
    vpminsd 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 39 05  dis=c4 e2 71 39 05 90 90
    vpminuw %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 3a c0  dis=c4 e2 71 3a c0
    vpminuw 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 3a 05  dis=c4 e2 71 3a 05 90 90
    vpminud %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 3b c0  dis=c4 e2 71 3b c0
    vpminud 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 3b 05  dis=c4 e2 71 3b 05 90 90
    vpmaxsb %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 3c c0  dis=c4 e2 71 3c c0
    vpmaxsb 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 3c 05  dis=c4 e2 71 3c 05 90 90
    vpmaxsd %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 3d c0  dis=c4 e2 71 3d c0
    vpmaxsd 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 3d 05  dis=c4 e2 71 3d 05 90 90
    vpmaxuw %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 3e c0  dis=c4 e2 71 3e c0
    vpmaxuw 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 3e 05  dis=c4 e2 71 3e 05 90 90
    vpmaxud %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 3f c0  dis=c4 e2 71 3f c0
    vpmaxud 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 3f 05  dis=c4 e2 71 3f 05 90 90
    vpmulld %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 40 c0  dis=c4 e2 71 40 c0
    vpmulld 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 40 05  dis=c4 e2 71 40 05 90 90
    vpsrlvd %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 45 c0  dis=c4 e2 71 45 c0
    vpsrlvd 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 45 05  dis=c4 e2 71 45 05 90 90
    vpsravd %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 46 c0  dis=c4 e2 71 46 c0
    vpsravd 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 46 05  dis=c4 e2 71 46 05 90 90
    vpsllvd %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 47 c0  dis=c4 e2 71 47 c0
    vpsllvd 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 47 05  dis=c4 e2 71 47 05 90 90
    {vex} vpdpbusd %xmm0,%xmm1,%xmm0    # gen=c4 e2 71 50 c0  dis=c4 e2 71 50 c0
    {vex} vpdpbusd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 50 05  dis=c4 e2 71 50 05 90 90
    {vex} vpdpbusds %xmm0,%xmm1,%xmm0   # gen=c4 e2 71 51 c0  dis=c4 e2 71 51 c0
    {vex} vpdpbusds 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 51 05  dis=c4 e2 71 51 05 90 90
    {vex} vpdpwssd %xmm0,%xmm1,%xmm0    # gen=c4 e2 71 52 c0  dis=c4 e2 71 52 c0
    {vex} vpdpwssd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 52 05  dis=c4 e2 71 52 05 90 90
    {vex} vpdpwssds %xmm0,%xmm1,%xmm0   # gen=c4 e2 71 53 c0  dis=c4 e2 71 53 c0
    {vex} vpdpwssds 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 53 05  dis=c4 e2 71 53 05 90 90
    vpmaskmovd 0x90909090,%xmm1,%xmm0   # gen=c4 e2 71 8c 05  dis=c4 e2 71 8c 05 90 90
    vpmaskmovd %xmm0,%xmm1,0x90909090   # gen=c4 e2 71 8e 05  dis=c4 e2 71 8e 05 90 90
    vfmaddsub132ps %xmm0,%xmm1,%xmm0    # gen=c4 e2 71 96 c0  dis=c4 e2 71 96 c0
    vfmaddsub132ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 96 05  dis=c4 e2 71 96 05 90 90
    vfmsubadd132ps %xmm0,%xmm1,%xmm0    # gen=c4 e2 71 97 c0  dis=c4 e2 71 97 c0
    vfmsubadd132ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 97 05  dis=c4 e2 71 97 05 90 90
    vfmadd132ps %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 98 c0  dis=c4 e2 71 98 c0
    vfmadd132ps 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 98 05  dis=c4 e2 71 98 05 90 90
    vfmadd132ss %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 99 c0  dis=c4 e2 71 99 c0
    vfmadd132ss 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 99 05  dis=c4 e2 71 99 05 90 90
    vfmsub132ps %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 9a c0  dis=c4 e2 71 9a c0
    vfmsub132ps 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 9a 05  dis=c4 e2 71 9a 05 90 90
    vfmsub132ss %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 9b c0  dis=c4 e2 71 9b c0
    vfmsub132ss 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 9b 05  dis=c4 e2 71 9b 05 90 90
    vfnmadd132ps %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 9c c0  dis=c4 e2 71 9c c0
    vfnmadd132ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 9c 05  dis=c4 e2 71 9c 05 90 90
    vfnmadd132ss %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 9d c0  dis=c4 e2 71 9d c0
    vfnmadd132ss 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 9d 05  dis=c4 e2 71 9d 05 90 90
    vfnmsub132ps %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 9e c0  dis=c4 e2 71 9e c0
    vfnmsub132ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 9e 05  dis=c4 e2 71 9e 05 90 90
    vfnmsub132ss %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 9f c0  dis=c4 e2 71 9f c0
    vfnmsub132ss 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 9f 05  dis=c4 e2 71 9f 05 90 90
    vfmaddsub213ps %xmm0,%xmm1,%xmm0    # gen=c4 e2 71 a6 c0  dis=c4 e2 71 a6 c0
    vfmaddsub213ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 a6 05  dis=c4 e2 71 a6 05 90 90
    vfmsubadd213ps %xmm0,%xmm1,%xmm0    # gen=c4 e2 71 a7 c0  dis=c4 e2 71 a7 c0
    vfmsubadd213ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 a7 05  dis=c4 e2 71 a7 05 90 90
    vfmadd213ps %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 a8 c0  dis=c4 e2 71 a8 c0
    vfmadd213ps 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 a8 05  dis=c4 e2 71 a8 05 90 90
    vfmadd213ss %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 a9 c0  dis=c4 e2 71 a9 c0
    vfmadd213ss 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 a9 05  dis=c4 e2 71 a9 05 90 90
    vfmsub213ps %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 aa c0  dis=c4 e2 71 aa c0
    vfmsub213ps 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 aa 05  dis=c4 e2 71 aa 05 90 90
    vfmsub213ss %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 ab c0  dis=c4 e2 71 ab c0
    vfmsub213ss 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 ab 05  dis=c4 e2 71 ab 05 90 90
    vfnmadd213ps %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 ac c0  dis=c4 e2 71 ac c0
    vfnmadd213ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 ac 05  dis=c4 e2 71 ac 05 90 90
    vfnmadd213ss %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 ad c0  dis=c4 e2 71 ad c0
    vfnmadd213ss 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 ad 05  dis=c4 e2 71 ad 05 90 90
    vfnmsub213ps %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 ae c0  dis=c4 e2 71 ae c0
    vfnmsub213ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 ae 05  dis=c4 e2 71 ae 05 90 90
    vfnmsub213ss %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 af c0  dis=c4 e2 71 af c0
    vfnmsub213ss 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 af 05  dis=c4 e2 71 af 05 90 90
    vfmaddsub231ps %xmm0,%xmm1,%xmm0    # gen=c4 e2 71 b6 c0  dis=c4 e2 71 b6 c0
    vfmaddsub231ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 b6 05  dis=c4 e2 71 b6 05 90 90
    vfmsubadd231ps %xmm0,%xmm1,%xmm0    # gen=c4 e2 71 b7 c0  dis=c4 e2 71 b7 c0
    vfmsubadd231ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 b7 05  dis=c4 e2 71 b7 05 90 90
    vfmadd231ps %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 b8 c0  dis=c4 e2 71 b8 c0
    vfmadd231ps 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 b8 05  dis=c4 e2 71 b8 05 90 90
    vfmadd231ss %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 b9 c0  dis=c4 e2 71 b9 c0
    vfmadd231ss 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 b9 05  dis=c4 e2 71 b9 05 90 90
    vfmsub231ps %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 ba c0  dis=c4 e2 71 ba c0
    vfmsub231ps 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 ba 05  dis=c4 e2 71 ba 05 90 90
    vfmsub231ss %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 bb c0  dis=c4 e2 71 bb c0
    vfmsub231ss 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 bb 05  dis=c4 e2 71 bb 05 90 90
    vfnmadd231ps %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 bc c0  dis=c4 e2 71 bc c0
    vfnmadd231ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 bc 05  dis=c4 e2 71 bc 05 90 90
    vfnmadd231ss %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 bd c0  dis=c4 e2 71 bd c0
    vfnmadd231ss 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 bd 05  dis=c4 e2 71 bd 05 90 90
    vfnmsub231ps %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 be c0  dis=c4 e2 71 be c0
    vfnmsub231ps 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 be 05  dis=c4 e2 71 be 05 90 90
    vfnmsub231ss %xmm0,%xmm1,%xmm0      # gen=c4 e2 71 bf c0  dis=c4 e2 71 bf c0
    vfnmsub231ss 0x90909090,%xmm1,%xmm0 # gen=c4 e2 71 bf 05  dis=c4 e2 71 bf 05 90 90
    vgf2p8mulb %xmm0,%xmm1,%xmm0        # gen=c4 e2 71 cf c0  dis=c4 e2 71 cf c0
    vgf2p8mulb 0x90909090,%xmm1,%xmm0   # gen=c4 e2 71 cf 05  dis=c4 e2 71 cf 05 90 90
    vaesenc %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 dc c0  dis=c4 e2 71 dc c0
    vaesenc 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 dc 05  dis=c4 e2 71 dc 05 90 90
    vaesenclast %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 dd c0  dis=c4 e2 71 dd c0
    vaesenclast 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 dd 05  dis=c4 e2 71 dd 05 90 90
    vaesdec %xmm0,%xmm1,%xmm0           # gen=c4 e2 71 de c0  dis=c4 e2 71 de c0
    vaesdec 0x90909090,%xmm1,%xmm0      # gen=c4 e2 71 de 05  dis=c4 e2 71 de 05 90 90
    vaesdeclast %xmm0,%xmm1,%xmm0       # gen=c4 e2 71 df c0  dis=c4 e2 71 df c0
    vaesdeclast 0x90909090,%xmm1,%xmm0  # gen=c4 e2 71 df 05  dis=c4 e2 71 df 05 90 90
    shlx   %ecx,%eax,%eax               # gen=c4 e2 71 f7 c0  dis=c4 e2 71 f7 c0
    shlx   %ecx,0x90909090,%eax         # gen=c4 e2 71 f7 05  dis=c4 e2 71 f7 05 90 90
    vpdpbsud %xmm0,%xmm1,%xmm0          # gen=c4 e2 72 50 c0  dis=c4 e2 72 50 c0
    vpdpbsud 0x90909090,%xmm1,%xmm0     # gen=c4 e2 72 50 05  dis=c4 e2 72 50 05 90 90
    vpdpbsuds %xmm0,%xmm1,%xmm0         # gen=c4 e2 72 51 c0  dis=c4 e2 72 51 c0
    vpdpbsuds 0x90909090,%xmm1,%xmm0    # gen=c4 e2 72 51 05  dis=c4 e2 72 51 05 90 90
    pext   %eax,%ecx,%eax               # gen=c4 e2 72 f5 c0  dis=c4 e2 72 f5 c0
    pext   0x90909090,%ecx,%eax         # gen=c4 e2 72 f5 05  dis=c4 e2 72 f5 05 90 90
    sarx   %ecx,%eax,%eax               # gen=c4 e2 72 f7 c0  dis=c4 e2 72 f7 c0
    sarx   %ecx,0x90909090,%eax         # gen=c4 e2 72 f7 05  dis=c4 e2 72 f7 05 90 90
    vpdpbssd %xmm0,%xmm1,%xmm0          # gen=c4 e2 73 50 c0  dis=c4 e2 73 50 c0
    vpdpbssd 0x90909090,%xmm1,%xmm0     # gen=c4 e2 73 50 05  dis=c4 e2 73 50 05 90 90
    vpdpbssds %xmm0,%xmm1,%xmm0         # gen=c4 e2 73 51 c0  dis=c4 e2 73 51 c0
    vpdpbssds 0x90909090,%xmm1,%xmm0    # gen=c4 e2 73 51 05  dis=c4 e2 73 51 05 90 90
    pdep   %eax,%ecx,%eax               # gen=c4 e2 73 f5 c0  dis=c4 e2 73 f5 c0
    pdep   0x90909090,%ecx,%eax         # gen=c4 e2 73 f5 05  dis=c4 e2 73 f5 05 90 90
    mulx   %eax,%ecx,%eax               # gen=c4 e2 73 f6 c0  dis=c4 e2 73 f6 c0
    mulx   0x90909090,%ecx,%eax         # gen=c4 e2 73 f6 05  dis=c4 e2 73 f6 05 90 90
    shrx   %ecx,%eax,%eax               # gen=c4 e2 73 f7 c0  dis=c4 e2 73 f7 c0
    shrx   %ecx,0x90909090,%eax         # gen=c4 e2 73 f7 05  dis=c4 e2 73 f7 05 90 90
    vpdpbuud %ymm0,%ymm1,%ymm0          # gen=c4 e2 74 50 c0  dis=c4 e2 74 50 c0
    vpdpbuud 0x90909090,%ymm1,%ymm0     # gen=c4 e2 74 50 05  dis=c4 e2 74 50 05 90 90
    vpdpbuuds %ymm0,%ymm1,%ymm0         # gen=c4 e2 74 51 c0  dis=c4 e2 74 51 c0
    vpdpbuuds 0x90909090,%ymm1,%ymm0    # gen=c4 e2 74 51 05  dis=c4 e2 74 51 05 90 90
    vpshufb %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 00 c0  dis=c4 e2 75 00 c0
    vpshufb 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 00 05  dis=c4 e2 75 00 05 90 90
    vphaddw %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 01 c0  dis=c4 e2 75 01 c0
    vphaddw 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 01 05  dis=c4 e2 75 01 05 90 90
    vphaddd %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 02 c0  dis=c4 e2 75 02 c0
    vphaddd 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 02 05  dis=c4 e2 75 02 05 90 90
    vphaddsw %ymm0,%ymm1,%ymm0          # gen=c4 e2 75 03 c0  dis=c4 e2 75 03 c0
    vphaddsw 0x90909090,%ymm1,%ymm0     # gen=c4 e2 75 03 05  dis=c4 e2 75 03 05 90 90
    vpmaddubsw %ymm0,%ymm1,%ymm0        # gen=c4 e2 75 04 c0  dis=c4 e2 75 04 c0
    vpmaddubsw 0x90909090,%ymm1,%ymm0   # gen=c4 e2 75 04 05  dis=c4 e2 75 04 05 90 90
    vphsubw %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 05 c0  dis=c4 e2 75 05 c0
    vphsubw 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 05 05  dis=c4 e2 75 05 05 90 90
    vphsubd %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 06 c0  dis=c4 e2 75 06 c0
    vphsubd 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 06 05  dis=c4 e2 75 06 05 90 90
    vphsubsw %ymm0,%ymm1,%ymm0          # gen=c4 e2 75 07 c0  dis=c4 e2 75 07 c0
    vphsubsw 0x90909090,%ymm1,%ymm0     # gen=c4 e2 75 07 05  dis=c4 e2 75 07 05 90 90
    vpsignb %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 08 c0  dis=c4 e2 75 08 c0
    vpsignb 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 08 05  dis=c4 e2 75 08 05 90 90
    vpsignw %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 09 c0  dis=c4 e2 75 09 c0
    vpsignw 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 09 05  dis=c4 e2 75 09 05 90 90
    vpsignd %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 0a c0  dis=c4 e2 75 0a c0
    vpsignd 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 0a 05  dis=c4 e2 75 0a 05 90 90
    vpmulhrsw %ymm0,%ymm1,%ymm0         # gen=c4 e2 75 0b c0  dis=c4 e2 75 0b c0
    vpmulhrsw 0x90909090,%ymm1,%ymm0    # gen=c4 e2 75 0b 05  dis=c4 e2 75 0b 05 90 90
    vpermilps %ymm0,%ymm1,%ymm0         # gen=c4 e2 75 0c c0  dis=c4 e2 75 0c c0
    vpermilps 0x90909090,%ymm1,%ymm0    # gen=c4 e2 75 0c 05  dis=c4 e2 75 0c 05 90 90
    vpermilpd %ymm0,%ymm1,%ymm0         # gen=c4 e2 75 0d c0  dis=c4 e2 75 0d c0
    vpermilpd 0x90909090,%ymm1,%ymm0    # gen=c4 e2 75 0d 05  dis=c4 e2 75 0d 05 90 90
    vpermps %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 16 c0  dis=c4 e2 75 16 c0
    vpermps 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 16 05  dis=c4 e2 75 16 05 90 90
    vpmuldq %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 28 c0  dis=c4 e2 75 28 c0
    vpmuldq 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 28 05  dis=c4 e2 75 28 05 90 90
    vpcmpeqq %ymm0,%ymm1,%ymm0          # gen=c4 e2 75 29 c0  dis=c4 e2 75 29 c0
    vpcmpeqq 0x90909090,%ymm1,%ymm0     # gen=c4 e2 75 29 05  dis=c4 e2 75 29 05 90 90
    vpackusdw %ymm0,%ymm1,%ymm0         # gen=c4 e2 75 2b c0  dis=c4 e2 75 2b c0
    vpackusdw 0x90909090,%ymm1,%ymm0    # gen=c4 e2 75 2b 05  dis=c4 e2 75 2b 05 90 90
    vmaskmovps 0x90909090,%ymm1,%ymm0   # gen=c4 e2 75 2c 05  dis=c4 e2 75 2c 05 90 90
    vmaskmovpd 0x90909090,%ymm1,%ymm0   # gen=c4 e2 75 2d 05  dis=c4 e2 75 2d 05 90 90
    vmaskmovps %ymm0,%ymm1,0x90909090   # gen=c4 e2 75 2e 05  dis=c4 e2 75 2e 05 90 90
    vmaskmovpd %ymm0,%ymm1,0x90909090   # gen=c4 e2 75 2f 05  dis=c4 e2 75 2f 05 90 90
    vpermd %ymm0,%ymm1,%ymm0            # gen=c4 e2 75 36 c0  dis=c4 e2 75 36 c0
    vpermd 0x90909090,%ymm1,%ymm0       # gen=c4 e2 75 36 05  dis=c4 e2 75 36 05 90 90
    vpcmpgtq %ymm0,%ymm1,%ymm0          # gen=c4 e2 75 37 c0  dis=c4 e2 75 37 c0
    vpcmpgtq 0x90909090,%ymm1,%ymm0     # gen=c4 e2 75 37 05  dis=c4 e2 75 37 05 90 90
    vpminsb %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 38 c0  dis=c4 e2 75 38 c0
    vpminsb 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 38 05  dis=c4 e2 75 38 05 90 90
    vpminsd %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 39 c0  dis=c4 e2 75 39 c0
    vpminsd 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 39 05  dis=c4 e2 75 39 05 90 90
    vpminuw %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 3a c0  dis=c4 e2 75 3a c0
    vpminuw 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 3a 05  dis=c4 e2 75 3a 05 90 90
    vpminud %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 3b c0  dis=c4 e2 75 3b c0
    vpminud 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 3b 05  dis=c4 e2 75 3b 05 90 90
    vpmaxsb %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 3c c0  dis=c4 e2 75 3c c0
    vpmaxsb 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 3c 05  dis=c4 e2 75 3c 05 90 90
    vpmaxsd %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 3d c0  dis=c4 e2 75 3d c0
    vpmaxsd 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 3d 05  dis=c4 e2 75 3d 05 90 90
    vpmaxuw %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 3e c0  dis=c4 e2 75 3e c0
    vpmaxuw 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 3e 05  dis=c4 e2 75 3e 05 90 90
    vpmaxud %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 3f c0  dis=c4 e2 75 3f c0
    vpmaxud 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 3f 05  dis=c4 e2 75 3f 05 90 90
    vpmulld %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 40 c0  dis=c4 e2 75 40 c0
    vpmulld 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 40 05  dis=c4 e2 75 40 05 90 90
    vpsrlvd %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 45 c0  dis=c4 e2 75 45 c0
    vpsrlvd 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 45 05  dis=c4 e2 75 45 05 90 90
    vpsravd %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 46 c0  dis=c4 e2 75 46 c0
    vpsravd 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 46 05  dis=c4 e2 75 46 05 90 90
    vpsllvd %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 47 c0  dis=c4 e2 75 47 c0
    vpsllvd 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 47 05  dis=c4 e2 75 47 05 90 90
    {vex} vpdpbusd %ymm0,%ymm1,%ymm0    # gen=c4 e2 75 50 c0  dis=c4 e2 75 50 c0
    {vex} vpdpbusd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 50 05  dis=c4 e2 75 50 05 90 90
    {vex} vpdpbusds %ymm0,%ymm1,%ymm0   # gen=c4 e2 75 51 c0  dis=c4 e2 75 51 c0
    {vex} vpdpbusds 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 51 05  dis=c4 e2 75 51 05 90 90
    {vex} vpdpwssd %ymm0,%ymm1,%ymm0    # gen=c4 e2 75 52 c0  dis=c4 e2 75 52 c0
    {vex} vpdpwssd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 52 05  dis=c4 e2 75 52 05 90 90
    {vex} vpdpwssds %ymm0,%ymm1,%ymm0   # gen=c4 e2 75 53 c0  dis=c4 e2 75 53 c0
    {vex} vpdpwssds 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 53 05  dis=c4 e2 75 53 05 90 90
    vpmaskmovd 0x90909090,%ymm1,%ymm0   # gen=c4 e2 75 8c 05  dis=c4 e2 75 8c 05 90 90
    vpmaskmovd %ymm0,%ymm1,0x90909090   # gen=c4 e2 75 8e 05  dis=c4 e2 75 8e 05 90 90
    vfmaddsub132ps %ymm0,%ymm1,%ymm0    # gen=c4 e2 75 96 c0  dis=c4 e2 75 96 c0
    vfmaddsub132ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 96 05  dis=c4 e2 75 96 05 90 90
    vfmsubadd132ps %ymm0,%ymm1,%ymm0    # gen=c4 e2 75 97 c0  dis=c4 e2 75 97 c0
    vfmsubadd132ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 97 05  dis=c4 e2 75 97 05 90 90
    vfmadd132ps %ymm0,%ymm1,%ymm0       # gen=c4 e2 75 98 c0  dis=c4 e2 75 98 c0
    vfmadd132ps 0x90909090,%ymm1,%ymm0  # gen=c4 e2 75 98 05  dis=c4 e2 75 98 05 90 90
    vfmsub132ps %ymm0,%ymm1,%ymm0       # gen=c4 e2 75 9a c0  dis=c4 e2 75 9a c0
    vfmsub132ps 0x90909090,%ymm1,%ymm0  # gen=c4 e2 75 9a 05  dis=c4 e2 75 9a 05 90 90
    vfnmadd132ps %ymm0,%ymm1,%ymm0      # gen=c4 e2 75 9c c0  dis=c4 e2 75 9c c0
    vfnmadd132ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 9c 05  dis=c4 e2 75 9c 05 90 90
    vfnmsub132ps %ymm0,%ymm1,%ymm0      # gen=c4 e2 75 9e c0  dis=c4 e2 75 9e c0
    vfnmsub132ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 9e 05  dis=c4 e2 75 9e 05 90 90
    vfmaddsub213ps %ymm0,%ymm1,%ymm0    # gen=c4 e2 75 a6 c0  dis=c4 e2 75 a6 c0
    vfmaddsub213ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 a6 05  dis=c4 e2 75 a6 05 90 90
    vfmsubadd213ps %ymm0,%ymm1,%ymm0    # gen=c4 e2 75 a7 c0  dis=c4 e2 75 a7 c0
    vfmsubadd213ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 a7 05  dis=c4 e2 75 a7 05 90 90
    vfmadd213ps %ymm0,%ymm1,%ymm0       # gen=c4 e2 75 a8 c0  dis=c4 e2 75 a8 c0
    vfmadd213ps 0x90909090,%ymm1,%ymm0  # gen=c4 e2 75 a8 05  dis=c4 e2 75 a8 05 90 90
    vfmsub213ps %ymm0,%ymm1,%ymm0       # gen=c4 e2 75 aa c0  dis=c4 e2 75 aa c0
    vfmsub213ps 0x90909090,%ymm1,%ymm0  # gen=c4 e2 75 aa 05  dis=c4 e2 75 aa 05 90 90
    vfnmadd213ps %ymm0,%ymm1,%ymm0      # gen=c4 e2 75 ac c0  dis=c4 e2 75 ac c0
    vfnmadd213ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 ac 05  dis=c4 e2 75 ac 05 90 90
    vfnmsub213ps %ymm0,%ymm1,%ymm0      # gen=c4 e2 75 ae c0  dis=c4 e2 75 ae c0
    vfnmsub213ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 ae 05  dis=c4 e2 75 ae 05 90 90
    vfmaddsub231ps %ymm0,%ymm1,%ymm0    # gen=c4 e2 75 b6 c0  dis=c4 e2 75 b6 c0
    vfmaddsub231ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 b6 05  dis=c4 e2 75 b6 05 90 90
    vfmsubadd231ps %ymm0,%ymm1,%ymm0    # gen=c4 e2 75 b7 c0  dis=c4 e2 75 b7 c0
    vfmsubadd231ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 b7 05  dis=c4 e2 75 b7 05 90 90
    vfmadd231ps %ymm0,%ymm1,%ymm0       # gen=c4 e2 75 b8 c0  dis=c4 e2 75 b8 c0
    vfmadd231ps 0x90909090,%ymm1,%ymm0  # gen=c4 e2 75 b8 05  dis=c4 e2 75 b8 05 90 90
    vfmsub231ps %ymm0,%ymm1,%ymm0       # gen=c4 e2 75 ba c0  dis=c4 e2 75 ba c0
    vfmsub231ps 0x90909090,%ymm1,%ymm0  # gen=c4 e2 75 ba 05  dis=c4 e2 75 ba 05 90 90
    vfnmadd231ps %ymm0,%ymm1,%ymm0      # gen=c4 e2 75 bc c0  dis=c4 e2 75 bc c0
    vfnmadd231ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 bc 05  dis=c4 e2 75 bc 05 90 90
    vfnmsub231ps %ymm0,%ymm1,%ymm0      # gen=c4 e2 75 be c0  dis=c4 e2 75 be c0
    vfnmsub231ps 0x90909090,%ymm1,%ymm0 # gen=c4 e2 75 be 05  dis=c4 e2 75 be 05 90 90
    vgf2p8mulb %ymm0,%ymm1,%ymm0        # gen=c4 e2 75 cf c0  dis=c4 e2 75 cf c0
    vgf2p8mulb 0x90909090,%ymm1,%ymm0   # gen=c4 e2 75 cf 05  dis=c4 e2 75 cf 05 90 90
    vaesenc %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 dc c0  dis=c4 e2 75 dc c0
    vaesenc 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 dc 05  dis=c4 e2 75 dc 05 90 90
    vaesenclast %ymm0,%ymm1,%ymm0       # gen=c4 e2 75 dd c0  dis=c4 e2 75 dd c0
    vaesenclast 0x90909090,%ymm1,%ymm0  # gen=c4 e2 75 dd 05  dis=c4 e2 75 dd 05 90 90
    vaesdec %ymm0,%ymm1,%ymm0           # gen=c4 e2 75 de c0  dis=c4 e2 75 de c0
    vaesdec 0x90909090,%ymm1,%ymm0      # gen=c4 e2 75 de 05  dis=c4 e2 75 de 05 90 90
    vaesdeclast %ymm0,%ymm1,%ymm0       # gen=c4 e2 75 df c0  dis=c4 e2 75 df c0
    vaesdeclast 0x90909090,%ymm1,%ymm0  # gen=c4 e2 75 df 05  dis=c4 e2 75 df 05 90 90
    vpdpbsud %ymm0,%ymm1,%ymm0          # gen=c4 e2 76 50 c0  dis=c4 e2 76 50 c0
    vpdpbsud 0x90909090,%ymm1,%ymm0     # gen=c4 e2 76 50 05  dis=c4 e2 76 50 05 90 90
    vpdpbsuds %ymm0,%ymm1,%ymm0         # gen=c4 e2 76 51 c0  dis=c4 e2 76 51 c0
    vpdpbsuds 0x90909090,%ymm1,%ymm0    # gen=c4 e2 76 51 05  dis=c4 e2 76 51 05 90 90
    vpdpbssd %ymm0,%ymm1,%ymm0          # gen=c4 e2 77 50 c0  dis=c4 e2 77 50 c0
    vpdpbssd 0x90909090,%ymm1,%ymm0     # gen=c4 e2 77 50 05  dis=c4 e2 77 50 05 90 90
    vpdpbssds %ymm0,%ymm1,%ymm0         # gen=c4 e2 77 51 c0  dis=c4 e2 77 51 c0
    vpdpbssds 0x90909090,%ymm1,%ymm0    # gen=c4 e2 77 51 05  dis=c4 e2 77 51 05 90 90
    vpsrlvq %xmm0,%xmm1,%xmm0           # gen=c4 e2 f1 45 c0  dis=c4 e2 f1 45 c0
    vpsrlvq 0x90909090,%xmm1,%xmm0      # gen=c4 e2 f1 45 05  dis=c4 e2 f1 45 05 90 90
    vpsllvq %xmm0,%xmm1,%xmm0           # gen=c4 e2 f1 47 c0  dis=c4 e2 f1 47 c0
    vpsllvq 0x90909090,%xmm1,%xmm0      # gen=c4 e2 f1 47 05  dis=c4 e2 f1 47 05 90 90
    vpmaskmovq 0x90909090,%xmm1,%xmm0   # gen=c4 e2 f1 8c 05  dis=c4 e2 f1 8c 05 90 90
    vpmaskmovq %xmm0,%xmm1,0x90909090   # gen=c4 e2 f1 8e 05  dis=c4 e2 f1 8e 05 90 90
    vfmaddsub132pd %xmm0,%xmm1,%xmm0    # gen=c4 e2 f1 96 c0  dis=c4 e2 f1 96 c0
    vfmaddsub132pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 96 05  dis=c4 e2 f1 96 05 90 90
    vfmsubadd132pd %xmm0,%xmm1,%xmm0    # gen=c4 e2 f1 97 c0  dis=c4 e2 f1 97 c0
    vfmsubadd132pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 97 05  dis=c4 e2 f1 97 05 90 90
    vfmadd132pd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 98 c0  dis=c4 e2 f1 98 c0
    vfmadd132pd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 98 05  dis=c4 e2 f1 98 05 90 90
    vfmadd132sd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 99 c0  dis=c4 e2 f1 99 c0
    vfmadd132sd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 99 05  dis=c4 e2 f1 99 05 90 90
    vfmsub132pd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 9a c0  dis=c4 e2 f1 9a c0
    vfmsub132pd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 9a 05  dis=c4 e2 f1 9a 05 90 90
    vfmsub132sd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 9b c0  dis=c4 e2 f1 9b c0
    vfmsub132sd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 9b 05  dis=c4 e2 f1 9b 05 90 90
    vfnmadd132pd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 9c c0  dis=c4 e2 f1 9c c0
    vfnmadd132pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 9c 05  dis=c4 e2 f1 9c 05 90 90
    vfnmadd132sd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 9d c0  dis=c4 e2 f1 9d c0
    vfnmadd132sd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 9d 05  dis=c4 e2 f1 9d 05 90 90
    vfnmsub132pd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 9e c0  dis=c4 e2 f1 9e c0
    vfnmsub132pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 9e 05  dis=c4 e2 f1 9e 05 90 90
    vfnmsub132sd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 9f c0  dis=c4 e2 f1 9f c0
    vfnmsub132sd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 9f 05  dis=c4 e2 f1 9f 05 90 90
    vfmaddsub213pd %xmm0,%xmm1,%xmm0    # gen=c4 e2 f1 a6 c0  dis=c4 e2 f1 a6 c0
    vfmaddsub213pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 a6 05  dis=c4 e2 f1 a6 05 90 90
    vfmsubadd213pd %xmm0,%xmm1,%xmm0    # gen=c4 e2 f1 a7 c0  dis=c4 e2 f1 a7 c0
    vfmsubadd213pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 a7 05  dis=c4 e2 f1 a7 05 90 90
    vfmadd213pd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 a8 c0  dis=c4 e2 f1 a8 c0
    vfmadd213pd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 a8 05  dis=c4 e2 f1 a8 05 90 90
    vfmadd213sd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 a9 c0  dis=c4 e2 f1 a9 c0
    vfmadd213sd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 a9 05  dis=c4 e2 f1 a9 05 90 90
    vfmsub213pd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 aa c0  dis=c4 e2 f1 aa c0
    vfmsub213pd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 aa 05  dis=c4 e2 f1 aa 05 90 90
    vfmsub213sd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 ab c0  dis=c4 e2 f1 ab c0
    vfmsub213sd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 ab 05  dis=c4 e2 f1 ab 05 90 90
    vfnmadd213pd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 ac c0  dis=c4 e2 f1 ac c0
    vfnmadd213pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 ac 05  dis=c4 e2 f1 ac 05 90 90
    vfnmadd213sd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 ad c0  dis=c4 e2 f1 ad c0
    vfnmadd213sd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 ad 05  dis=c4 e2 f1 ad 05 90 90
    vfnmsub213pd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 ae c0  dis=c4 e2 f1 ae c0
    vfnmsub213pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 ae 05  dis=c4 e2 f1 ae 05 90 90
    vfnmsub213sd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 af c0  dis=c4 e2 f1 af c0
    vfnmsub213sd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 af 05  dis=c4 e2 f1 af 05 90 90
    {vex} vpmadd52luq %xmm0,%xmm1,%xmm0 # gen=c4 e2 f1 b4 c0  dis=c4 e2 f1 b4 c0
    {vex} vpmadd52luq 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 b4 05  dis=c4 e2 f1 b4 05 90 90
    {vex} vpmadd52huq %xmm0,%xmm1,%xmm0 # gen=c4 e2 f1 b5 c0  dis=c4 e2 f1 b5 c0
    {vex} vpmadd52huq 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 b5 05  dis=c4 e2 f1 b5 05 90 90
    vfmaddsub231pd %xmm0,%xmm1,%xmm0    # gen=c4 e2 f1 b6 c0  dis=c4 e2 f1 b6 c0
    vfmaddsub231pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 b6 05  dis=c4 e2 f1 b6 05 90 90
    vfmsubadd231pd %xmm0,%xmm1,%xmm0    # gen=c4 e2 f1 b7 c0  dis=c4 e2 f1 b7 c0
    vfmsubadd231pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 b7 05  dis=c4 e2 f1 b7 05 90 90
    vfmadd231pd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 b8 c0  dis=c4 e2 f1 b8 c0
    vfmadd231pd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 b8 05  dis=c4 e2 f1 b8 05 90 90
    vfmadd231sd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 b9 c0  dis=c4 e2 f1 b9 c0
    vfmadd231sd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 b9 05  dis=c4 e2 f1 b9 05 90 90
    vfmsub231pd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 ba c0  dis=c4 e2 f1 ba c0
    vfmsub231pd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 ba 05  dis=c4 e2 f1 ba 05 90 90
    vfmsub231sd %xmm0,%xmm1,%xmm0       # gen=c4 e2 f1 bb c0  dis=c4 e2 f1 bb c0
    vfmsub231sd 0x90909090,%xmm1,%xmm0  # gen=c4 e2 f1 bb 05  dis=c4 e2 f1 bb 05 90 90
    vfnmadd231pd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 bc c0  dis=c4 e2 f1 bc c0
    vfnmadd231pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 bc 05  dis=c4 e2 f1 bc 05 90 90
    vfnmadd231sd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 bd c0  dis=c4 e2 f1 bd c0
    vfnmadd231sd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 bd 05  dis=c4 e2 f1 bd 05 90 90
    vfnmsub231pd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 be c0  dis=c4 e2 f1 be c0
    vfnmsub231pd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 be 05  dis=c4 e2 f1 be 05 90 90
    vfnmsub231sd %xmm0,%xmm1,%xmm0      # gen=c4 e2 f1 bf c0  dis=c4 e2 f1 bf c0
    vfnmsub231sd 0x90909090,%xmm1,%xmm0 # gen=c4 e2 f1 bf 05  dis=c4 e2 f1 bf 05 90 90
    vpsrlvq %ymm0,%ymm1,%ymm0           # gen=c4 e2 f5 45 c0  dis=c4 e2 f5 45 c0
    vpsrlvq 0x90909090,%ymm1,%ymm0      # gen=c4 e2 f5 45 05  dis=c4 e2 f5 45 05 90 90
    vpsllvq %ymm0,%ymm1,%ymm0           # gen=c4 e2 f5 47 c0  dis=c4 e2 f5 47 c0
    vpsllvq 0x90909090,%ymm1,%ymm0      # gen=c4 e2 f5 47 05  dis=c4 e2 f5 47 05 90 90
    vpmaskmovq 0x90909090,%ymm1,%ymm0   # gen=c4 e2 f5 8c 05  dis=c4 e2 f5 8c 05 90 90
    vpmaskmovq %ymm0,%ymm1,0x90909090   # gen=c4 e2 f5 8e 05  dis=c4 e2 f5 8e 05 90 90
    vfmaddsub132pd %ymm0,%ymm1,%ymm0    # gen=c4 e2 f5 96 c0  dis=c4 e2 f5 96 c0
    vfmaddsub132pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 96 05  dis=c4 e2 f5 96 05 90 90
    vfmsubadd132pd %ymm0,%ymm1,%ymm0    # gen=c4 e2 f5 97 c0  dis=c4 e2 f5 97 c0
    vfmsubadd132pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 97 05  dis=c4 e2 f5 97 05 90 90
    vfmadd132pd %ymm0,%ymm1,%ymm0       # gen=c4 e2 f5 98 c0  dis=c4 e2 f5 98 c0
    vfmadd132pd 0x90909090,%ymm1,%ymm0  # gen=c4 e2 f5 98 05  dis=c4 e2 f5 98 05 90 90
    vfmsub132pd %ymm0,%ymm1,%ymm0       # gen=c4 e2 f5 9a c0  dis=c4 e2 f5 9a c0
    vfmsub132pd 0x90909090,%ymm1,%ymm0  # gen=c4 e2 f5 9a 05  dis=c4 e2 f5 9a 05 90 90
    vfnmadd132pd %ymm0,%ymm1,%ymm0      # gen=c4 e2 f5 9c c0  dis=c4 e2 f5 9c c0
    vfnmadd132pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 9c 05  dis=c4 e2 f5 9c 05 90 90
    vfnmsub132pd %ymm0,%ymm1,%ymm0      # gen=c4 e2 f5 9e c0  dis=c4 e2 f5 9e c0
    vfnmsub132pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 9e 05  dis=c4 e2 f5 9e 05 90 90
    vfmaddsub213pd %ymm0,%ymm1,%ymm0    # gen=c4 e2 f5 a6 c0  dis=c4 e2 f5 a6 c0
    vfmaddsub213pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 a6 05  dis=c4 e2 f5 a6 05 90 90
    vfmsubadd213pd %ymm0,%ymm1,%ymm0    # gen=c4 e2 f5 a7 c0  dis=c4 e2 f5 a7 c0
    vfmsubadd213pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 a7 05  dis=c4 e2 f5 a7 05 90 90
    vfmadd213pd %ymm0,%ymm1,%ymm0       # gen=c4 e2 f5 a8 c0  dis=c4 e2 f5 a8 c0
    vfmadd213pd 0x90909090,%ymm1,%ymm0  # gen=c4 e2 f5 a8 05  dis=c4 e2 f5 a8 05 90 90
    vfmsub213pd %ymm0,%ymm1,%ymm0       # gen=c4 e2 f5 aa c0  dis=c4 e2 f5 aa c0
    vfmsub213pd 0x90909090,%ymm1,%ymm0  # gen=c4 e2 f5 aa 05  dis=c4 e2 f5 aa 05 90 90
    vfnmadd213pd %ymm0,%ymm1,%ymm0      # gen=c4 e2 f5 ac c0  dis=c4 e2 f5 ac c0
    vfnmadd213pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 ac 05  dis=c4 e2 f5 ac 05 90 90
    vfnmsub213pd %ymm0,%ymm1,%ymm0      # gen=c4 e2 f5 ae c0  dis=c4 e2 f5 ae c0
    vfnmsub213pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 ae 05  dis=c4 e2 f5 ae 05 90 90
    {vex} vpmadd52luq %ymm0,%ymm1,%ymm0 # gen=c4 e2 f5 b4 c0  dis=c4 e2 f5 b4 c0
    {vex} vpmadd52luq 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 b4 05  dis=c4 e2 f5 b4 05 90 90
    {vex} vpmadd52huq %ymm0,%ymm1,%ymm0 # gen=c4 e2 f5 b5 c0  dis=c4 e2 f5 b5 c0
    {vex} vpmadd52huq 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 b5 05  dis=c4 e2 f5 b5 05 90 90
    vfmaddsub231pd %ymm0,%ymm1,%ymm0    # gen=c4 e2 f5 b6 c0  dis=c4 e2 f5 b6 c0
    vfmaddsub231pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 b6 05  dis=c4 e2 f5 b6 05 90 90
    vfmsubadd231pd %ymm0,%ymm1,%ymm0    # gen=c4 e2 f5 b7 c0  dis=c4 e2 f5 b7 c0
    vfmsubadd231pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 b7 05  dis=c4 e2 f5 b7 05 90 90
    vfmadd231pd %ymm0,%ymm1,%ymm0       # gen=c4 e2 f5 b8 c0  dis=c4 e2 f5 b8 c0
    vfmadd231pd 0x90909090,%ymm1,%ymm0  # gen=c4 e2 f5 b8 05  dis=c4 e2 f5 b8 05 90 90
    vfmsub231pd %ymm0,%ymm1,%ymm0       # gen=c4 e2 f5 ba c0  dis=c4 e2 f5 ba c0
    vfmsub231pd 0x90909090,%ymm1,%ymm0  # gen=c4 e2 f5 ba 05  dis=c4 e2 f5 ba 05 90 90
    vfnmadd231pd %ymm0,%ymm1,%ymm0      # gen=c4 e2 f5 bc c0  dis=c4 e2 f5 bc c0
    vfnmadd231pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 bc 05  dis=c4 e2 f5 bc 05 90 90
    vfnmsub231pd %ymm0,%ymm1,%ymm0      # gen=c4 e2 f5 be c0  dis=c4 e2 f5 be c0
    vfnmsub231pd 0x90909090,%ymm1,%ymm0 # gen=c4 e2 f5 be 05  dis=c4 e2 f5 be 05 90 90
    vpblendd $0x90,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 02 c0  dis=c4 e3 71 02 c0 90
    vpblendd $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 02 05  dis=c4 e3 71 02 05 90 90
    vroundss $0x90,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 0a c0  dis=c4 e3 71 0a c0 90
    vroundss $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 0a 05  dis=c4 e3 71 0a 05 90 90
    vroundsd $0x90,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 0b c0  dis=c4 e3 71 0b c0 90
    vroundsd $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 0b 05  dis=c4 e3 71 0b 05 90 90
    vblendps $0x90,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 0c c0  dis=c4 e3 71 0c c0 90
    vblendps $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 0c 05  dis=c4 e3 71 0c 05 90 90
    vblendpd $0x90,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 0d c0  dis=c4 e3 71 0d c0 90
    vblendpd $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 0d 05  dis=c4 e3 71 0d 05 90 90
    vpblendw $0x90,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 0e c0  dis=c4 e3 71 0e c0 90
    vpblendw $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 0e 05  dis=c4 e3 71 0e 05 90 90
    vpalignr $0x90,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 0f c0  dis=c4 e3 71 0f c0 90
    vpalignr $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 0f 05  dis=c4 e3 71 0f 05 90 90
    vpinsrb $0x90,%eax,%xmm1,%xmm0      # gen=c4 e3 71 20 c0  dis=c4 e3 71 20 c0 90
    vpinsrb $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 20 05  dis=c4 e3 71 20 05 90 90
    vinsertps $0x90,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 21 c0  dis=c4 e3 71 21 c0 90
    vinsertps $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 21 05  dis=c4 e3 71 21 05 90 90
    vpinsrd $0x90,%eax,%xmm1,%xmm0      # gen=c4 e3 71 22 c0  dis=c4 e3 71 22 c0 90
    vpinsrd $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 22 05  dis=c4 e3 71 22 05 90 90
    vdpps  $0x90,%xmm0,%xmm1,%xmm0      # gen=c4 e3 71 40 c0  dis=c4 e3 71 40 c0 90
    vdpps  $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 40 05  dis=c4 e3 71 40 05 90 90
    vdppd  $0x90,%xmm0,%xmm1,%xmm0      # gen=c4 e3 71 41 c0  dis=c4 e3 71 41 c0 90
    vdppd  $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 41 05  dis=c4 e3 71 41 05 90 90
    vmpsadbw $0x90,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 42 c0  dis=c4 e3 71 42 c0 90
    vmpsadbw $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 42 05  dis=c4 e3 71 42 05 90 90
    vpclmulqdq $0x90,%xmm0,%xmm1,%xmm0  # gen=c4 e3 71 44 c0  dis=c4 e3 71 44 c0 90
    vpclmulqdq $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 44 05  dis=c4 e3 71 44 05 90 90
    vpermil2ps $0x0,%xmm1,%xmm0,%xmm1,%xmm0 # gen=c4 e3 71 48 c0  dis=c4 e3 71 48 c0 90
    vpermil2ps $0x0,%xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 48 05  dis=c4 e3 71 48 05 90 90
    vpermil2pd $0x0,%xmm1,%xmm0,%xmm1,%xmm0 # gen=c4 e3 71 49 c0  dis=c4 e3 71 49 c0 90
    vpermil2pd $0x0,%xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 49 05  dis=c4 e3 71 49 05 90 90
    vblendvps %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 4a c0  dis=c4 e3 71 4a c0 90
    vblendvps %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 4a 05  dis=c4 e3 71 4a 05 90 90
    vblendvpd %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 4b c0  dis=c4 e3 71 4b c0 90
    vblendvpd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 4b 05  dis=c4 e3 71 4b 05 90 90
    vpblendvb %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 4c c0  dis=c4 e3 71 4c c0 90
    vpblendvb %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 4c 05  dis=c4 e3 71 4c 05 90 90
    vfmaddsubps %xmm1,%xmm0,%xmm1,%xmm0 # gen=c4 e3 71 5c c0  dis=c4 e3 71 5c c0 90
    vfmaddsubps %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 5c 05  dis=c4 e3 71 5c 05 90 90
    vfmaddsubpd %xmm1,%xmm0,%xmm1,%xmm0 # gen=c4 e3 71 5d c0  dis=c4 e3 71 5d c0 90
    vfmaddsubpd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 5d 05  dis=c4 e3 71 5d 05 90 90
    vfmsubaddps %xmm1,%xmm0,%xmm1,%xmm0 # gen=c4 e3 71 5e c0  dis=c4 e3 71 5e c0 90
    vfmsubaddps %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 5e 05  dis=c4 e3 71 5e 05 90 90
    vfmsubaddpd %xmm1,%xmm0,%xmm1,%xmm0 # gen=c4 e3 71 5f c0  dis=c4 e3 71 5f c0 90
    vfmsubaddpd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 5f 05  dis=c4 e3 71 5f 05 90 90
    vfmaddps %xmm1,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 68 c0  dis=c4 e3 71 68 c0 90
    vfmaddps %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 68 05  dis=c4 e3 71 68 05 90 90
    vfmaddpd %xmm1,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 69 c0  dis=c4 e3 71 69 c0 90
    vfmaddpd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 69 05  dis=c4 e3 71 69 05 90 90
    vfmaddss %xmm1,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 6a c0  dis=c4 e3 71 6a c0 90
    vfmaddss %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 6a 05  dis=c4 e3 71 6a 05 90 90
    vfmaddsd %xmm1,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 6b c0  dis=c4 e3 71 6b c0 90
    vfmaddsd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 6b 05  dis=c4 e3 71 6b 05 90 90
    vfmsubps %xmm1,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 6c c0  dis=c4 e3 71 6c c0 90
    vfmsubps %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 6c 05  dis=c4 e3 71 6c 05 90 90
    vfmsubpd %xmm1,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 6d c0  dis=c4 e3 71 6d c0 90
    vfmsubpd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 6d 05  dis=c4 e3 71 6d 05 90 90
    vfmsubss %xmm1,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 6e c0  dis=c4 e3 71 6e c0 90
    vfmsubss %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 6e 05  dis=c4 e3 71 6e 05 90 90
    vfmsubsd %xmm1,%xmm0,%xmm1,%xmm0    # gen=c4 e3 71 6f c0  dis=c4 e3 71 6f c0 90
    vfmsubsd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 6f 05  dis=c4 e3 71 6f 05 90 90
    vfnmaddps %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 78 c0  dis=c4 e3 71 78 c0 90
    vfnmaddps %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 78 05  dis=c4 e3 71 78 05 90 90
    vfnmaddpd %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 79 c0  dis=c4 e3 71 79 c0 90
    vfnmaddpd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 79 05  dis=c4 e3 71 79 05 90 90
    vfnmaddss %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 7a c0  dis=c4 e3 71 7a c0 90
    vfnmaddss %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 7a 05  dis=c4 e3 71 7a 05 90 90
    vfnmaddsd %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 7b c0  dis=c4 e3 71 7b c0 90
    vfnmaddsd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 7b 05  dis=c4 e3 71 7b 05 90 90
    vfnmsubps %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 7c c0  dis=c4 e3 71 7c c0 90
    vfnmsubps %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 7c 05  dis=c4 e3 71 7c 05 90 90
    vfnmsubpd %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 7d c0  dis=c4 e3 71 7d c0 90
    vfnmsubpd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 7d 05  dis=c4 e3 71 7d 05 90 90
    vfnmsubss %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 7e c0  dis=c4 e3 71 7e c0 90
    vfnmsubss %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 7e 05  dis=c4 e3 71 7e 05 90 90
    vfnmsubsd %xmm1,%xmm0,%xmm1,%xmm0   # gen=c4 e3 71 7f c0  dis=c4 e3 71 7f c0 90
    vfnmsubsd %xmm1,0x90909090,%xmm1,%xmm0 # gen=c4 e3 71 7f 05  dis=c4 e3 71 7f 05 90 90
    vpblendd $0x90,%ymm0,%ymm1,%ymm0    # gen=c4 e3 75 02 c0  dis=c4 e3 75 02 c0 90
    vpblendd $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 02 05  dis=c4 e3 75 02 05 90 90
    vperm2f128 $0x90,%ymm0,%ymm1,%ymm0  # gen=c4 e3 75 06 c0  dis=c4 e3 75 06 c0 90
    vperm2f128 $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 06 05  dis=c4 e3 75 06 05 90 90
    vblendps $0x90,%ymm0,%ymm1,%ymm0    # gen=c4 e3 75 0c c0  dis=c4 e3 75 0c c0 90
    vblendps $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 0c 05  dis=c4 e3 75 0c 05 90 90
    vblendpd $0x90,%ymm0,%ymm1,%ymm0    # gen=c4 e3 75 0d c0  dis=c4 e3 75 0d c0 90
    vblendpd $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 0d 05  dis=c4 e3 75 0d 05 90 90
    vpblendw $0x90,%ymm0,%ymm1,%ymm0    # gen=c4 e3 75 0e c0  dis=c4 e3 75 0e c0 90
    vpblendw $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 0e 05  dis=c4 e3 75 0e 05 90 90
    vpalignr $0x90,%ymm0,%ymm1,%ymm0    # gen=c4 e3 75 0f c0  dis=c4 e3 75 0f c0 90
    vpalignr $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 0f 05  dis=c4 e3 75 0f 05 90 90
    vinsertf128 $0x90,%xmm0,%ymm1,%ymm0 # gen=c4 e3 75 18 c0  dis=c4 e3 75 18 c0 90
    vinsertf128 $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 18 05  dis=c4 e3 75 18 05 90 90
    vinserti128 $0x90,%xmm0,%ymm1,%ymm0 # gen=c4 e3 75 38 c0  dis=c4 e3 75 38 c0 90
    vinserti128 $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 38 05  dis=c4 e3 75 38 05 90 90
    vdpps  $0x90,%ymm0,%ymm1,%ymm0      # gen=c4 e3 75 40 c0  dis=c4 e3 75 40 c0 90
    vdpps  $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 40 05  dis=c4 e3 75 40 05 90 90
    vmpsadbw $0x90,%ymm0,%ymm1,%ymm0    # gen=c4 e3 75 42 c0  dis=c4 e3 75 42 c0 90
    vmpsadbw $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 42 05  dis=c4 e3 75 42 05 90 90
    vpclmulqdq $0x90,%ymm0,%ymm1,%ymm0  # gen=c4 e3 75 44 c0  dis=c4 e3 75 44 c0 90
    vpclmulqdq $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 44 05  dis=c4 e3 75 44 05 90 90
    vperm2i128 $0x90,%ymm0,%ymm1,%ymm0  # gen=c4 e3 75 46 c0  dis=c4 e3 75 46 c0 90
    vperm2i128 $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 46 05  dis=c4 e3 75 46 05 90 90
    vpermil2ps $0x0,%ymm1,%ymm0,%ymm1,%ymm0 # gen=c4 e3 75 48 c0  dis=c4 e3 75 48 c0 90
    vpermil2ps $0x0,%ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 48 05  dis=c4 e3 75 48 05 90 90
    vpermil2pd $0x0,%ymm1,%ymm0,%ymm1,%ymm0 # gen=c4 e3 75 49 c0  dis=c4 e3 75 49 c0 90
    vpermil2pd $0x0,%ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 49 05  dis=c4 e3 75 49 05 90 90
    vblendvps %ymm1,%ymm0,%ymm1,%ymm0   # gen=c4 e3 75 4a c0  dis=c4 e3 75 4a c0 90
    vblendvps %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 4a 05  dis=c4 e3 75 4a 05 90 90
    vblendvpd %ymm1,%ymm0,%ymm1,%ymm0   # gen=c4 e3 75 4b c0  dis=c4 e3 75 4b c0 90
    vblendvpd %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 4b 05  dis=c4 e3 75 4b 05 90 90
    vpblendvb %ymm1,%ymm0,%ymm1,%ymm0   # gen=c4 e3 75 4c c0  dis=c4 e3 75 4c c0 90
    vpblendvb %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 4c 05  dis=c4 e3 75 4c 05 90 90
    vfmaddsubps %ymm1,%ymm0,%ymm1,%ymm0 # gen=c4 e3 75 5c c0  dis=c4 e3 75 5c c0 90
    vfmaddsubps %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 5c 05  dis=c4 e3 75 5c 05 90 90
    vfmaddsubpd %ymm1,%ymm0,%ymm1,%ymm0 # gen=c4 e3 75 5d c0  dis=c4 e3 75 5d c0 90
    vfmaddsubpd %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 5d 05  dis=c4 e3 75 5d 05 90 90
    vfmsubaddps %ymm1,%ymm0,%ymm1,%ymm0 # gen=c4 e3 75 5e c0  dis=c4 e3 75 5e c0 90
    vfmsubaddps %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 5e 05  dis=c4 e3 75 5e 05 90 90
    vfmsubaddpd %ymm1,%ymm0,%ymm1,%ymm0 # gen=c4 e3 75 5f c0  dis=c4 e3 75 5f c0 90
    vfmsubaddpd %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 5f 05  dis=c4 e3 75 5f 05 90 90
    vfmaddps %ymm1,%ymm0,%ymm1,%ymm0    # gen=c4 e3 75 68 c0  dis=c4 e3 75 68 c0 90
    vfmaddps %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 68 05  dis=c4 e3 75 68 05 90 90
    vfmaddpd %ymm1,%ymm0,%ymm1,%ymm0    # gen=c4 e3 75 69 c0  dis=c4 e3 75 69 c0 90
    vfmaddpd %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 69 05  dis=c4 e3 75 69 05 90 90
    vfmsubps %ymm1,%ymm0,%ymm1,%ymm0    # gen=c4 e3 75 6c c0  dis=c4 e3 75 6c c0 90
    vfmsubps %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 6c 05  dis=c4 e3 75 6c 05 90 90
    vfmsubpd %ymm1,%ymm0,%ymm1,%ymm0    # gen=c4 e3 75 6d c0  dis=c4 e3 75 6d c0 90
    vfmsubpd %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 6d 05  dis=c4 e3 75 6d 05 90 90
    vfnmaddps %ymm1,%ymm0,%ymm1,%ymm0   # gen=c4 e3 75 78 c0  dis=c4 e3 75 78 c0 90
    vfnmaddps %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 78 05  dis=c4 e3 75 78 05 90 90
    vfnmaddpd %ymm1,%ymm0,%ymm1,%ymm0   # gen=c4 e3 75 79 c0  dis=c4 e3 75 79 c0 90
    vfnmaddpd %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 79 05  dis=c4 e3 75 79 05 90 90
    vfnmsubps %ymm1,%ymm0,%ymm1,%ymm0   # gen=c4 e3 75 7c c0  dis=c4 e3 75 7c c0 90
    vfnmsubps %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 7c 05  dis=c4 e3 75 7c 05 90 90
    vfnmsubpd %ymm1,%ymm0,%ymm1,%ymm0   # gen=c4 e3 75 7d c0  dis=c4 e3 75 7d c0 90
    vfnmsubpd %ymm1,0x90909090,%ymm1,%ymm0 # gen=c4 e3 75 7d 05  dis=c4 e3 75 7d 05 90 90
    vpermil2ps $0x0,%xmm0,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 48 c0  dis=c4 e3 f1 48 c0 90
    vpermil2ps $0x0,0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 48 05  dis=c4 e3 f1 48 05 90 90
    vpermil2pd $0x0,%xmm0,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 49 c0  dis=c4 e3 f1 49 c0 90
    vpermil2pd $0x0,0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 49 05  dis=c4 e3 f1 49 05 90 90
    vfmaddsubps %xmm0,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 5c c0  dis=c4 e3 f1 5c c0 90
    vfmaddsubps 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 5c 05  dis=c4 e3 f1 5c 05 90 90
    vfmaddsubpd %xmm0,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 5d c0  dis=c4 e3 f1 5d c0 90
    vfmaddsubpd 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 5d 05  dis=c4 e3 f1 5d 05 90 90
    vfmsubaddps %xmm0,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 5e c0  dis=c4 e3 f1 5e c0 90
    vfmsubaddps 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 5e 05  dis=c4 e3 f1 5e 05 90 90
    vfmsubaddpd %xmm0,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 5f c0  dis=c4 e3 f1 5f c0 90
    vfmsubaddpd 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 5f 05  dis=c4 e3 f1 5f 05 90 90
    vfmaddps %xmm0,%xmm1,%xmm1,%xmm0    # gen=c4 e3 f1 68 c0  dis=c4 e3 f1 68 c0 90
    vfmaddps 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 68 05  dis=c4 e3 f1 68 05 90 90
    vfmaddpd %xmm0,%xmm1,%xmm1,%xmm0    # gen=c4 e3 f1 69 c0  dis=c4 e3 f1 69 c0 90
    vfmaddpd 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 69 05  dis=c4 e3 f1 69 05 90 90
    vfmaddss %xmm0,%xmm1,%xmm1,%xmm0    # gen=c4 e3 f1 6a c0  dis=c4 e3 f1 6a c0 90
    vfmaddss 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 6a 05  dis=c4 e3 f1 6a 05 90 90
    vfmaddsd %xmm0,%xmm1,%xmm1,%xmm0    # gen=c4 e3 f1 6b c0  dis=c4 e3 f1 6b c0 90
    vfmaddsd 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 6b 05  dis=c4 e3 f1 6b 05 90 90
    vfmsubps %xmm0,%xmm1,%xmm1,%xmm0    # gen=c4 e3 f1 6c c0  dis=c4 e3 f1 6c c0 90
    vfmsubps 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 6c 05  dis=c4 e3 f1 6c 05 90 90
    vfmsubpd %xmm0,%xmm1,%xmm1,%xmm0    # gen=c4 e3 f1 6d c0  dis=c4 e3 f1 6d c0 90
    vfmsubpd 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 6d 05  dis=c4 e3 f1 6d 05 90 90
    vfmsubss %xmm0,%xmm1,%xmm1,%xmm0    # gen=c4 e3 f1 6e c0  dis=c4 e3 f1 6e c0 90
    vfmsubss 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 6e 05  dis=c4 e3 f1 6e 05 90 90
    vfmsubsd %xmm0,%xmm1,%xmm1,%xmm0    # gen=c4 e3 f1 6f c0  dis=c4 e3 f1 6f c0 90
    vfmsubsd 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 6f 05  dis=c4 e3 f1 6f 05 90 90
    vfnmaddps %xmm0,%xmm1,%xmm1,%xmm0   # gen=c4 e3 f1 78 c0  dis=c4 e3 f1 78 c0 90
    vfnmaddps 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 78 05  dis=c4 e3 f1 78 05 90 90
    vfnmaddpd %xmm0,%xmm1,%xmm1,%xmm0   # gen=c4 e3 f1 79 c0  dis=c4 e3 f1 79 c0 90
    vfnmaddpd 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 79 05  dis=c4 e3 f1 79 05 90 90
    vfnmaddss %xmm0,%xmm1,%xmm1,%xmm0   # gen=c4 e3 f1 7a c0  dis=c4 e3 f1 7a c0 90
    vfnmaddss 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 7a 05  dis=c4 e3 f1 7a 05 90 90
    vfnmaddsd %xmm0,%xmm1,%xmm1,%xmm0   # gen=c4 e3 f1 7b c0  dis=c4 e3 f1 7b c0 90
    vfnmaddsd 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 7b 05  dis=c4 e3 f1 7b 05 90 90
    vfnmsubps %xmm0,%xmm1,%xmm1,%xmm0   # gen=c4 e3 f1 7c c0  dis=c4 e3 f1 7c c0 90
    vfnmsubps 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 7c 05  dis=c4 e3 f1 7c 05 90 90
    vfnmsubpd %xmm0,%xmm1,%xmm1,%xmm0   # gen=c4 e3 f1 7d c0  dis=c4 e3 f1 7d c0 90
    vfnmsubpd 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 7d 05  dis=c4 e3 f1 7d 05 90 90
    vfnmsubss %xmm0,%xmm1,%xmm1,%xmm0   # gen=c4 e3 f1 7e c0  dis=c4 e3 f1 7e c0 90
    vfnmsubss 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 7e 05  dis=c4 e3 f1 7e 05 90 90
    vfnmsubsd %xmm0,%xmm1,%xmm1,%xmm0   # gen=c4 e3 f1 7f c0  dis=c4 e3 f1 7f c0 90
    vfnmsubsd 0x90909090,%xmm1,%xmm1,%xmm0 # gen=c4 e3 f1 7f 05  dis=c4 e3 f1 7f 05 90 90
    vgf2p8affineqb $0x90,%xmm0,%xmm1,%xmm0 # gen=c4 e3 f1 ce c0  dis=c4 e3 f1 ce c0 90
    vgf2p8affineqb $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 f1 ce 05  dis=c4 e3 f1 ce 05 90 90
    vgf2p8affineinvqb $0x90,%xmm0,%xmm1,%xmm0 # gen=c4 e3 f1 cf c0  dis=c4 e3 f1 cf c0 90
    vgf2p8affineinvqb $0x90,0x90909090,%xmm1,%xmm0 # gen=c4 e3 f1 cf 05  dis=c4 e3 f1 cf 05 90 90
    vpermil2ps $0x0,%ymm0,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 48 c0  dis=c4 e3 f5 48 c0 90
    vpermil2ps $0x0,0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 48 05  dis=c4 e3 f5 48 05 90 90
    vpermil2pd $0x0,%ymm0,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 49 c0  dis=c4 e3 f5 49 c0 90
    vpermil2pd $0x0,0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 49 05  dis=c4 e3 f5 49 05 90 90
    vfmaddsubps %ymm0,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 5c c0  dis=c4 e3 f5 5c c0 90
    vfmaddsubps 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 5c 05  dis=c4 e3 f5 5c 05 90 90
    vfmaddsubpd %ymm0,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 5d c0  dis=c4 e3 f5 5d c0 90
    vfmaddsubpd 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 5d 05  dis=c4 e3 f5 5d 05 90 90
    vfmsubaddps %ymm0,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 5e c0  dis=c4 e3 f5 5e c0 90
    vfmsubaddps 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 5e 05  dis=c4 e3 f5 5e 05 90 90
    vfmsubaddpd %ymm0,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 5f c0  dis=c4 e3 f5 5f c0 90
    vfmsubaddpd 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 5f 05  dis=c4 e3 f5 5f 05 90 90
    vfmaddps %ymm0,%ymm1,%ymm1,%ymm0    # gen=c4 e3 f5 68 c0  dis=c4 e3 f5 68 c0 90
    vfmaddps 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 68 05  dis=c4 e3 f5 68 05 90 90
    vfmaddpd %ymm0,%ymm1,%ymm1,%ymm0    # gen=c4 e3 f5 69 c0  dis=c4 e3 f5 69 c0 90
    vfmaddpd 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 69 05  dis=c4 e3 f5 69 05 90 90
    vfmsubps %ymm0,%ymm1,%ymm1,%ymm0    # gen=c4 e3 f5 6c c0  dis=c4 e3 f5 6c c0 90
    vfmsubps 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 6c 05  dis=c4 e3 f5 6c 05 90 90
    vfmsubpd %ymm0,%ymm1,%ymm1,%ymm0    # gen=c4 e3 f5 6d c0  dis=c4 e3 f5 6d c0 90
    vfmsubpd 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 6d 05  dis=c4 e3 f5 6d 05 90 90
    vfnmaddps %ymm0,%ymm1,%ymm1,%ymm0   # gen=c4 e3 f5 78 c0  dis=c4 e3 f5 78 c0 90
    vfnmaddps 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 78 05  dis=c4 e3 f5 78 05 90 90
    vfnmaddpd %ymm0,%ymm1,%ymm1,%ymm0   # gen=c4 e3 f5 79 c0  dis=c4 e3 f5 79 c0 90
    vfnmaddpd 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 79 05  dis=c4 e3 f5 79 05 90 90
    vfnmsubps %ymm0,%ymm1,%ymm1,%ymm0   # gen=c4 e3 f5 7c c0  dis=c4 e3 f5 7c c0 90
    vfnmsubps 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 7c 05  dis=c4 e3 f5 7c 05 90 90
    vfnmsubpd %ymm0,%ymm1,%ymm1,%ymm0   # gen=c4 e3 f5 7d c0  dis=c4 e3 f5 7d c0 90
    vfnmsubpd 0x90909090,%ymm1,%ymm1,%ymm0 # gen=c4 e3 f5 7d 05  dis=c4 e3 f5 7d 05 90 90
    vgf2p8affineqb $0x90,%ymm0,%ymm1,%ymm0 # gen=c4 e3 f5 ce c0  dis=c4 e3 f5 ce c0 90
    vgf2p8affineqb $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 f5 ce 05  dis=c4 e3 f5 ce 05 90 90
    vgf2p8affineinvqb $0x90,%ymm0,%ymm1,%ymm0 # gen=c4 e3 f5 cf c0  dis=c4 e3 f5 cf c0 90
    vgf2p8affineinvqb $0x90,0x90909090,%ymm1,%ymm0 # gen=c4 e3 f5 cf 05  dis=c4 e3 f5 cf 05 90 90
    {evex} vmovhlps %xmm0,%xmm1,%xmm0   # gen=62 f1 74 08 12 c0  dis=62 f1 74 08 12 c0
    {evex} vmovlps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 12 05  dis=62 f1 74 08 12 05 90
    {evex} vunpcklps %xmm0,%xmm1,%xmm0  # gen=62 f1 74 08 14 c0  dis=62 f1 74 08 14 c0
    {evex} vunpcklps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 14 05  dis=62 f1 74 08 14 05 90
    {evex} vunpckhps %xmm0,%xmm1,%xmm0  # gen=62 f1 74 08 15 c0  dis=62 f1 74 08 15 c0
    {evex} vunpckhps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 15 05  dis=62 f1 74 08 15 05 90
    {evex} vmovlhps %xmm0,%xmm1,%xmm0   # gen=62 f1 74 08 16 c0  dis=62 f1 74 08 16 c0
    {evex} vmovhps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 16 05  dis=62 f1 74 08 16 05 90
    {evex} vandps %xmm0,%xmm1,%xmm0     # gen=62 f1 74 08 54 c0  dis=62 f1 74 08 54 c0
    {evex} vandps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 54 05  dis=62 f1 74 08 54 05 90
    {evex} vandnps %xmm0,%xmm1,%xmm0    # gen=62 f1 74 08 55 c0  dis=62 f1 74 08 55 c0
    {evex} vandnps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 55 05  dis=62 f1 74 08 55 05 90
    {evex} vorps %xmm0,%xmm1,%xmm0      # gen=62 f1 74 08 56 c0  dis=62 f1 74 08 56 c0
    {evex} vorps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 56 05  dis=62 f1 74 08 56 05 90
    {evex} vxorps %xmm0,%xmm1,%xmm0     # gen=62 f1 74 08 57 c0  dis=62 f1 74 08 57 c0
    {evex} vxorps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 57 05  dis=62 f1 74 08 57 05 90
    {evex} vaddps %xmm0,%xmm1,%xmm0     # gen=62 f1 74 08 58 c0  dis=62 f1 74 08 58 c0
    {evex} vaddps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 58 05  dis=62 f1 74 08 58 05 90
    {evex} vmulps %xmm0,%xmm1,%xmm0     # gen=62 f1 74 08 59 c0  dis=62 f1 74 08 59 c0
    {evex} vmulps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 59 05  dis=62 f1 74 08 59 05 90
    {evex} vsubps %xmm0,%xmm1,%xmm0     # gen=62 f1 74 08 5c c0  dis=62 f1 74 08 5c c0
    {evex} vsubps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 5c 05  dis=62 f1 74 08 5c 05 90
    {evex} vminps %xmm0,%xmm1,%xmm0     # gen=62 f1 74 08 5d c0  dis=62 f1 74 08 5d c0
    {evex} vminps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 5d 05  dis=62 f1 74 08 5d 05 90
    {evex} vdivps %xmm0,%xmm1,%xmm0     # gen=62 f1 74 08 5e c0  dis=62 f1 74 08 5e c0
    {evex} vdivps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 5e 05  dis=62 f1 74 08 5e 05 90
    {evex} vmaxps %xmm0,%xmm1,%xmm0     # gen=62 f1 74 08 5f c0  dis=62 f1 74 08 5f c0
    {evex} vmaxps 0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 5f 05  dis=62 f1 74 08 5f 05 90
    vcmpps $0x90,%xmm0,%xmm1,%k0        # gen=62 f1 74 08 c2 c0  dis=62 f1 74 08 c2 c0 90
    vcmpps $0x90,0x90909090,%xmm1,%k0   # gen=62 f1 74 08 c2 05  dis=62 f1 74 08 c2 05 90
    {evex} vshufps $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f1 74 08 c6 c0  dis=62 f1 74 08 c6 c0 90
    {evex} vshufps $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f1 74 08 c6 05  dis=62 f1 74 08 c6 05 90
    {evex} vunpcklps %ymm0,%ymm1,%ymm0  # gen=62 f1 74 28 14 c0  dis=62 f1 74 28 14 c0
    {evex} vunpcklps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 14 05  dis=62 f1 74 28 14 05 90
    {evex} vunpckhps %ymm0,%ymm1,%ymm0  # gen=62 f1 74 28 15 c0  dis=62 f1 74 28 15 c0
    {evex} vunpckhps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 15 05  dis=62 f1 74 28 15 05 90
    {evex} vandps %ymm0,%ymm1,%ymm0     # gen=62 f1 74 28 54 c0  dis=62 f1 74 28 54 c0
    {evex} vandps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 54 05  dis=62 f1 74 28 54 05 90
    {evex} vandnps %ymm0,%ymm1,%ymm0    # gen=62 f1 74 28 55 c0  dis=62 f1 74 28 55 c0
    {evex} vandnps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 55 05  dis=62 f1 74 28 55 05 90
    {evex} vorps %ymm0,%ymm1,%ymm0      # gen=62 f1 74 28 56 c0  dis=62 f1 74 28 56 c0
    {evex} vorps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 56 05  dis=62 f1 74 28 56 05 90
    {evex} vxorps %ymm0,%ymm1,%ymm0     # gen=62 f1 74 28 57 c0  dis=62 f1 74 28 57 c0
    {evex} vxorps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 57 05  dis=62 f1 74 28 57 05 90
    {evex} vaddps %ymm0,%ymm1,%ymm0     # gen=62 f1 74 28 58 c0  dis=62 f1 74 28 58 c0
    {evex} vaddps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 58 05  dis=62 f1 74 28 58 05 90
    {evex} vmulps %ymm0,%ymm1,%ymm0     # gen=62 f1 74 28 59 c0  dis=62 f1 74 28 59 c0
    {evex} vmulps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 59 05  dis=62 f1 74 28 59 05 90
    {evex} vsubps %ymm0,%ymm1,%ymm0     # gen=62 f1 74 28 5c c0  dis=62 f1 74 28 5c c0
    {evex} vsubps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 5c 05  dis=62 f1 74 28 5c 05 90
    {evex} vminps %ymm0,%ymm1,%ymm0     # gen=62 f1 74 28 5d c0  dis=62 f1 74 28 5d c0
    {evex} vminps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 5d 05  dis=62 f1 74 28 5d 05 90
    {evex} vdivps %ymm0,%ymm1,%ymm0     # gen=62 f1 74 28 5e c0  dis=62 f1 74 28 5e c0
    {evex} vdivps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 5e 05  dis=62 f1 74 28 5e 05 90
    {evex} vmaxps %ymm0,%ymm1,%ymm0     # gen=62 f1 74 28 5f c0  dis=62 f1 74 28 5f c0
    {evex} vmaxps 0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 5f 05  dis=62 f1 74 28 5f 05 90
    vcmpps $0x90,%ymm0,%ymm1,%k0        # gen=62 f1 74 28 c2 c0  dis=62 f1 74 28 c2 c0 90
    vcmpps $0x90,0x90909090,%ymm1,%k0   # gen=62 f1 74 28 c2 05  dis=62 f1 74 28 c2 05 90
    {evex} vshufps $0x90,%ymm0,%ymm1,%ymm0 # gen=62 f1 74 28 c6 c0  dis=62 f1 74 28 c6 c0 90
    {evex} vshufps $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f1 74 28 c6 05  dis=62 f1 74 28 c6 05 90
    vunpcklps %zmm0,%zmm1,%zmm0         # gen=62 f1 74 48 14 c0  dis=62 f1 74 48 14 c0
    vunpcklps 0x90909090,%zmm1,%zmm0    # gen=62 f1 74 48 14 05  dis=62 f1 74 48 14 05 90
    vunpckhps %zmm0,%zmm1,%zmm0         # gen=62 f1 74 48 15 c0  dis=62 f1 74 48 15 c0
    vunpckhps 0x90909090,%zmm1,%zmm0    # gen=62 f1 74 48 15 05  dis=62 f1 74 48 15 05 90
    vandps %zmm0,%zmm1,%zmm0            # gen=62 f1 74 48 54 c0  dis=62 f1 74 48 54 c0
    vandps 0x90909090,%zmm1,%zmm0       # gen=62 f1 74 48 54 05  dis=62 f1 74 48 54 05 90
    vandnps %zmm0,%zmm1,%zmm0           # gen=62 f1 74 48 55 c0  dis=62 f1 74 48 55 c0
    vandnps 0x90909090,%zmm1,%zmm0      # gen=62 f1 74 48 55 05  dis=62 f1 74 48 55 05 90
    vorps  %zmm0,%zmm1,%zmm0            # gen=62 f1 74 48 56 c0  dis=62 f1 74 48 56 c0
    vorps  0x90909090,%zmm1,%zmm0       # gen=62 f1 74 48 56 05  dis=62 f1 74 48 56 05 90
    vxorps %zmm0,%zmm1,%zmm0            # gen=62 f1 74 48 57 c0  dis=62 f1 74 48 57 c0
    vxorps 0x90909090,%zmm1,%zmm0       # gen=62 f1 74 48 57 05  dis=62 f1 74 48 57 05 90
    vaddps %zmm0,%zmm1,%zmm0            # gen=62 f1 74 48 58 c0  dis=62 f1 74 48 58 c0
    vaddps 0x90909090,%zmm1,%zmm0       # gen=62 f1 74 48 58 05  dis=62 f1 74 48 58 05 90
    vmulps %zmm0,%zmm1,%zmm0            # gen=62 f1 74 48 59 c0  dis=62 f1 74 48 59 c0
    vmulps 0x90909090,%zmm1,%zmm0       # gen=62 f1 74 48 59 05  dis=62 f1 74 48 59 05 90
    vsubps %zmm0,%zmm1,%zmm0            # gen=62 f1 74 48 5c c0  dis=62 f1 74 48 5c c0
    vsubps 0x90909090,%zmm1,%zmm0       # gen=62 f1 74 48 5c 05  dis=62 f1 74 48 5c 05 90
    vminps %zmm0,%zmm1,%zmm0            # gen=62 f1 74 48 5d c0  dis=62 f1 74 48 5d c0
    vminps 0x90909090,%zmm1,%zmm0       # gen=62 f1 74 48 5d 05  dis=62 f1 74 48 5d 05 90
    vdivps %zmm0,%zmm1,%zmm0            # gen=62 f1 74 48 5e c0  dis=62 f1 74 48 5e c0
    vdivps 0x90909090,%zmm1,%zmm0       # gen=62 f1 74 48 5e 05  dis=62 f1 74 48 5e 05 90
    vmaxps %zmm0,%zmm1,%zmm0            # gen=62 f1 74 48 5f c0  dis=62 f1 74 48 5f c0
    vmaxps 0x90909090,%zmm1,%zmm0       # gen=62 f1 74 48 5f 05  dis=62 f1 74 48 5f 05 90
    vcmpps $0x90,%zmm0,%zmm1,%k0        # gen=62 f1 74 48 c2 c0  dis=62 f1 74 48 c2 c0 90
    vcmpps $0x90,0x90909090,%zmm1,%k0   # gen=62 f1 74 48 c2 05  dis=62 f1 74 48 c2 05 90
    vshufps $0x90,%zmm0,%zmm1,%zmm0     # gen=62 f1 74 48 c6 c0  dis=62 f1 74 48 c6 c0 90
    vshufps $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f1 74 48 c6 05  dis=62 f1 74 48 c6 05 90
    {evex} vmovlpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 12 05  dis=62 f1 75 08 12 05 90
    {evex} vmovhpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 16 05  dis=62 f1 75 08 16 05 90
    {evex} vaddpd %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 58 c0  dis=62 f1 75 08 58 c0
    {evex} vaddpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 58 05  dis=62 f1 75 08 58 05 90
    {evex} vmulpd %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 59 c0  dis=62 f1 75 08 59 c0
    {evex} vmulpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 59 05  dis=62 f1 75 08 59 05 90
    {evex} vsubpd %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 5c c0  dis=62 f1 75 08 5c c0
    {evex} vsubpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 5c 05  dis=62 f1 75 08 5c 05 90
    {evex} vminpd %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 5d c0  dis=62 f1 75 08 5d c0
    {evex} vminpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 5d 05  dis=62 f1 75 08 5d 05 90
    {evex} vdivpd %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 5e c0  dis=62 f1 75 08 5e c0
    {evex} vdivpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 5e 05  dis=62 f1 75 08 5e 05 90
    {evex} vmaxpd %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 5f c0  dis=62 f1 75 08 5f c0
    {evex} vmaxpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 5f 05  dis=62 f1 75 08 5f 05 90
    {evex} vpunpcklbw %xmm0,%xmm1,%xmm0 # gen=62 f1 75 08 60 c0  dis=62 f1 75 08 60 c0
    {evex} vpunpcklbw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 60 05  dis=62 f1 75 08 60 05 90
    {evex} vpunpcklwd %xmm0,%xmm1,%xmm0 # gen=62 f1 75 08 61 c0  dis=62 f1 75 08 61 c0
    {evex} vpunpcklwd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 61 05  dis=62 f1 75 08 61 05 90
    {evex} vpunpckldq %xmm0,%xmm1,%xmm0 # gen=62 f1 75 08 62 c0  dis=62 f1 75 08 62 c0
    {evex} vpunpckldq 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 62 05  dis=62 f1 75 08 62 05 90
    {evex} vpacksswb %xmm0,%xmm1,%xmm0  # gen=62 f1 75 08 63 c0  dis=62 f1 75 08 63 c0
    {evex} vpacksswb 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 63 05  dis=62 f1 75 08 63 05 90
    vpcmpgtb %xmm0,%xmm1,%k0            # gen=62 f1 75 08 64 c0  dis=62 f1 75 08 64 c0
    vpcmpgtb 0x90909090,%xmm1,%k0       # gen=62 f1 75 08 64 05  dis=62 f1 75 08 64 05 90
    vpcmpgtw %xmm0,%xmm1,%k0            # gen=62 f1 75 08 65 c0  dis=62 f1 75 08 65 c0
    vpcmpgtw 0x90909090,%xmm1,%k0       # gen=62 f1 75 08 65 05  dis=62 f1 75 08 65 05 90
    vpcmpgtd %xmm0,%xmm1,%k0            # gen=62 f1 75 08 66 c0  dis=62 f1 75 08 66 c0
    vpcmpgtd 0x90909090,%xmm1,%k0       # gen=62 f1 75 08 66 05  dis=62 f1 75 08 66 05 90
    {evex} vpackuswb %xmm0,%xmm1,%xmm0  # gen=62 f1 75 08 67 c0  dis=62 f1 75 08 67 c0
    {evex} vpackuswb 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 67 05  dis=62 f1 75 08 67 05 90
    {evex} vpunpckhbw %xmm0,%xmm1,%xmm0 # gen=62 f1 75 08 68 c0  dis=62 f1 75 08 68 c0
    {evex} vpunpckhbw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 68 05  dis=62 f1 75 08 68 05 90
    {evex} vpunpckhwd %xmm0,%xmm1,%xmm0 # gen=62 f1 75 08 69 c0  dis=62 f1 75 08 69 c0
    {evex} vpunpckhwd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 69 05  dis=62 f1 75 08 69 05 90
    {evex} vpunpckhdq %xmm0,%xmm1,%xmm0 # gen=62 f1 75 08 6a c0  dis=62 f1 75 08 6a c0
    {evex} vpunpckhdq 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 6a 05  dis=62 f1 75 08 6a 05 90
    {evex} vpackssdw %xmm0,%xmm1,%xmm0  # gen=62 f1 75 08 6b c0  dis=62 f1 75 08 6b c0
    {evex} vpackssdw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 6b 05  dis=62 f1 75 08 6b 05 90
    vprord $0x90,%xmm0,%xmm1            # gen=62 f1 75 08 72 c0  dis=62 f1 75 08 72 c0 90
    vprord $0x90,0x90909090,%xmm1       # gen=62 f1 75 08 72 05  dis=62 f1 75 08 72 05 90
    vpcmpeqb %xmm0,%xmm1,%k0            # gen=62 f1 75 08 74 c0  dis=62 f1 75 08 74 c0
    vpcmpeqb 0x90909090,%xmm1,%k0       # gen=62 f1 75 08 74 05  dis=62 f1 75 08 74 05 90
    vpcmpeqw %xmm0,%xmm1,%k0            # gen=62 f1 75 08 75 c0  dis=62 f1 75 08 75 c0
    vpcmpeqw 0x90909090,%xmm1,%k0       # gen=62 f1 75 08 75 05  dis=62 f1 75 08 75 05 90
    vpcmpeqd %xmm0,%xmm1,%k0            # gen=62 f1 75 08 76 c0  dis=62 f1 75 08 76 c0
    vpcmpeqd 0x90909090,%xmm1,%k0       # gen=62 f1 75 08 76 05  dis=62 f1 75 08 76 05 90
    {evex} vpinsrw $0x90,%eax,%xmm1,%xmm0 # gen=62 f1 75 08 c4 c0  dis=62 f1 75 08 c4 c0 90
    {evex} vpinsrw $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 c4 05  dis=62 f1 75 08 c4 05 90
    {evex} vpsrlw %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 d1 c0  dis=62 f1 75 08 d1 c0
    {evex} vpsrlw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 d1 05  dis=62 f1 75 08 d1 05 90
    {evex} vpsrld %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 d2 c0  dis=62 f1 75 08 d2 c0
    {evex} vpsrld 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 d2 05  dis=62 f1 75 08 d2 05 90
    {evex} vpmullw %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 d5 c0  dis=62 f1 75 08 d5 c0
    {evex} vpmullw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 d5 05  dis=62 f1 75 08 d5 05 90
    {evex} vpsubusb %xmm0,%xmm1,%xmm0   # gen=62 f1 75 08 d8 c0  dis=62 f1 75 08 d8 c0
    {evex} vpsubusb 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 d8 05  dis=62 f1 75 08 d8 05 90
    {evex} vpsubusw %xmm0,%xmm1,%xmm0   # gen=62 f1 75 08 d9 c0  dis=62 f1 75 08 d9 c0
    {evex} vpsubusw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 d9 05  dis=62 f1 75 08 d9 05 90
    {evex} vpminub %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 da c0  dis=62 f1 75 08 da c0
    {evex} vpminub 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 da 05  dis=62 f1 75 08 da 05 90
    vpandd %xmm0,%xmm1,%xmm0            # gen=62 f1 75 08 db c0  dis=62 f1 75 08 db c0
    vpandd 0x90909090,%xmm1,%xmm0       # gen=62 f1 75 08 db 05  dis=62 f1 75 08 db 05 90
    {evex} vpaddusb %xmm0,%xmm1,%xmm0   # gen=62 f1 75 08 dc c0  dis=62 f1 75 08 dc c0
    {evex} vpaddusb 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 dc 05  dis=62 f1 75 08 dc 05 90
    {evex} vpaddusw %xmm0,%xmm1,%xmm0   # gen=62 f1 75 08 dd c0  dis=62 f1 75 08 dd c0
    {evex} vpaddusw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 dd 05  dis=62 f1 75 08 dd 05 90
    {evex} vpmaxub %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 de c0  dis=62 f1 75 08 de c0
    {evex} vpmaxub 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 de 05  dis=62 f1 75 08 de 05 90
    vpandnd %xmm0,%xmm1,%xmm0           # gen=62 f1 75 08 df c0  dis=62 f1 75 08 df c0
    vpandnd 0x90909090,%xmm1,%xmm0      # gen=62 f1 75 08 df 05  dis=62 f1 75 08 df 05 90
    {evex} vpavgb %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 e0 c0  dis=62 f1 75 08 e0 c0
    {evex} vpavgb 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 e0 05  dis=62 f1 75 08 e0 05 90
    {evex} vpsraw %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 e1 c0  dis=62 f1 75 08 e1 c0
    {evex} vpsraw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 e1 05  dis=62 f1 75 08 e1 05 90
    {evex} vpsrad %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 e2 c0  dis=62 f1 75 08 e2 c0
    {evex} vpsrad 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 e2 05  dis=62 f1 75 08 e2 05 90
    {evex} vpavgw %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 e3 c0  dis=62 f1 75 08 e3 c0
    {evex} vpavgw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 e3 05  dis=62 f1 75 08 e3 05 90
    {evex} vpmulhuw %xmm0,%xmm1,%xmm0   # gen=62 f1 75 08 e4 c0  dis=62 f1 75 08 e4 c0
    {evex} vpmulhuw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 e4 05  dis=62 f1 75 08 e4 05 90
    {evex} vpmulhw %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 e5 c0  dis=62 f1 75 08 e5 c0
    {evex} vpmulhw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 e5 05  dis=62 f1 75 08 e5 05 90
    {evex} vpsubsb %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 e8 c0  dis=62 f1 75 08 e8 c0
    {evex} vpsubsb 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 e8 05  dis=62 f1 75 08 e8 05 90
    {evex} vpsubsw %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 e9 c0  dis=62 f1 75 08 e9 c0
    {evex} vpsubsw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 e9 05  dis=62 f1 75 08 e9 05 90
    {evex} vpminsw %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 ea c0  dis=62 f1 75 08 ea c0
    {evex} vpminsw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 ea 05  dis=62 f1 75 08 ea 05 90
    vpord  %xmm0,%xmm1,%xmm0            # gen=62 f1 75 08 eb c0  dis=62 f1 75 08 eb c0
    vpord  0x90909090,%xmm1,%xmm0       # gen=62 f1 75 08 eb 05  dis=62 f1 75 08 eb 05 90
    {evex} vpaddsb %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 ec c0  dis=62 f1 75 08 ec c0
    {evex} vpaddsb 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 ec 05  dis=62 f1 75 08 ec 05 90
    {evex} vpaddsw %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 ed c0  dis=62 f1 75 08 ed c0
    {evex} vpaddsw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 ed 05  dis=62 f1 75 08 ed 05 90
    {evex} vpmaxsw %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 ee c0  dis=62 f1 75 08 ee c0
    {evex} vpmaxsw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 ee 05  dis=62 f1 75 08 ee 05 90
    vpxord %xmm0,%xmm1,%xmm0            # gen=62 f1 75 08 ef c0  dis=62 f1 75 08 ef c0
    vpxord 0x90909090,%xmm1,%xmm0       # gen=62 f1 75 08 ef 05  dis=62 f1 75 08 ef 05 90
    {evex} vpsllw %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 f1 c0  dis=62 f1 75 08 f1 c0
    {evex} vpsllw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 f1 05  dis=62 f1 75 08 f1 05 90
    {evex} vpslld %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 f2 c0  dis=62 f1 75 08 f2 c0
    {evex} vpslld 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 f2 05  dis=62 f1 75 08 f2 05 90
    {evex} vpmaddwd %xmm0,%xmm1,%xmm0   # gen=62 f1 75 08 f5 c0  dis=62 f1 75 08 f5 c0
    {evex} vpmaddwd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 f5 05  dis=62 f1 75 08 f5 05 90
    {evex} vpsadbw %xmm0,%xmm1,%xmm0    # gen=62 f1 75 08 f6 c0  dis=62 f1 75 08 f6 c0
    {evex} vpsadbw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 f6 05  dis=62 f1 75 08 f6 05 90
    {evex} vpsubb %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 f8 c0  dis=62 f1 75 08 f8 c0
    {evex} vpsubb 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 f8 05  dis=62 f1 75 08 f8 05 90
    {evex} vpsubw %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 f9 c0  dis=62 f1 75 08 f9 c0
    {evex} vpsubw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 f9 05  dis=62 f1 75 08 f9 05 90
    {evex} vpsubd %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 fa c0  dis=62 f1 75 08 fa c0
    {evex} vpsubd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 fa 05  dis=62 f1 75 08 fa 05 90
    {evex} vpaddb %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 fc c0  dis=62 f1 75 08 fc c0
    {evex} vpaddb 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 fc 05  dis=62 f1 75 08 fc 05 90
    {evex} vpaddw %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 fd c0  dis=62 f1 75 08 fd c0
    {evex} vpaddw 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 fd 05  dis=62 f1 75 08 fd 05 90
    {evex} vpaddd %xmm0,%xmm1,%xmm0     # gen=62 f1 75 08 fe c0  dis=62 f1 75 08 fe c0
    {evex} vpaddd 0x90909090,%xmm1,%xmm0 # gen=62 f1 75 08 fe 05  dis=62 f1 75 08 fe 05 90
    {evex} vaddpd %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 58 c0  dis=62 f1 75 28 58 c0
    {evex} vaddpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 58 05  dis=62 f1 75 28 58 05 90
    {evex} vmulpd %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 59 c0  dis=62 f1 75 28 59 c0
    {evex} vmulpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 59 05  dis=62 f1 75 28 59 05 90
    {evex} vsubpd %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 5c c0  dis=62 f1 75 28 5c c0
    {evex} vsubpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 5c 05  dis=62 f1 75 28 5c 05 90
    {evex} vminpd %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 5d c0  dis=62 f1 75 28 5d c0
    {evex} vminpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 5d 05  dis=62 f1 75 28 5d 05 90
    {evex} vdivpd %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 5e c0  dis=62 f1 75 28 5e c0
    {evex} vdivpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 5e 05  dis=62 f1 75 28 5e 05 90
    {evex} vmaxpd %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 5f c0  dis=62 f1 75 28 5f c0
    {evex} vmaxpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 5f 05  dis=62 f1 75 28 5f 05 90
    {evex} vpunpcklbw %ymm0,%ymm1,%ymm0 # gen=62 f1 75 28 60 c0  dis=62 f1 75 28 60 c0
    {evex} vpunpcklbw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 60 05  dis=62 f1 75 28 60 05 90
    {evex} vpunpcklwd %ymm0,%ymm1,%ymm0 # gen=62 f1 75 28 61 c0  dis=62 f1 75 28 61 c0
    {evex} vpunpcklwd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 61 05  dis=62 f1 75 28 61 05 90
    {evex} vpunpckldq %ymm0,%ymm1,%ymm0 # gen=62 f1 75 28 62 c0  dis=62 f1 75 28 62 c0
    {evex} vpunpckldq 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 62 05  dis=62 f1 75 28 62 05 90
    {evex} vpacksswb %ymm0,%ymm1,%ymm0  # gen=62 f1 75 28 63 c0  dis=62 f1 75 28 63 c0
    {evex} vpacksswb 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 63 05  dis=62 f1 75 28 63 05 90
    vpcmpgtb %ymm0,%ymm1,%k0            # gen=62 f1 75 28 64 c0  dis=62 f1 75 28 64 c0
    vpcmpgtb 0x90909090,%ymm1,%k0       # gen=62 f1 75 28 64 05  dis=62 f1 75 28 64 05 90
    vpcmpgtw %ymm0,%ymm1,%k0            # gen=62 f1 75 28 65 c0  dis=62 f1 75 28 65 c0
    vpcmpgtw 0x90909090,%ymm1,%k0       # gen=62 f1 75 28 65 05  dis=62 f1 75 28 65 05 90
    vpcmpgtd %ymm0,%ymm1,%k0            # gen=62 f1 75 28 66 c0  dis=62 f1 75 28 66 c0
    vpcmpgtd 0x90909090,%ymm1,%k0       # gen=62 f1 75 28 66 05  dis=62 f1 75 28 66 05 90
    {evex} vpackuswb %ymm0,%ymm1,%ymm0  # gen=62 f1 75 28 67 c0  dis=62 f1 75 28 67 c0
    {evex} vpackuswb 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 67 05  dis=62 f1 75 28 67 05 90
    {evex} vpunpckhbw %ymm0,%ymm1,%ymm0 # gen=62 f1 75 28 68 c0  dis=62 f1 75 28 68 c0
    {evex} vpunpckhbw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 68 05  dis=62 f1 75 28 68 05 90
    {evex} vpunpckhwd %ymm0,%ymm1,%ymm0 # gen=62 f1 75 28 69 c0  dis=62 f1 75 28 69 c0
    {evex} vpunpckhwd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 69 05  dis=62 f1 75 28 69 05 90
    {evex} vpunpckhdq %ymm0,%ymm1,%ymm0 # gen=62 f1 75 28 6a c0  dis=62 f1 75 28 6a c0
    {evex} vpunpckhdq 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 6a 05  dis=62 f1 75 28 6a 05 90
    {evex} vpackssdw %ymm0,%ymm1,%ymm0  # gen=62 f1 75 28 6b c0  dis=62 f1 75 28 6b c0
    {evex} vpackssdw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 6b 05  dis=62 f1 75 28 6b 05 90
    vprord $0x90,%ymm0,%ymm1            # gen=62 f1 75 28 72 c0  dis=62 f1 75 28 72 c0 90
    vprord $0x90,0x90909090,%ymm1       # gen=62 f1 75 28 72 05  dis=62 f1 75 28 72 05 90
    vpcmpeqb %ymm0,%ymm1,%k0            # gen=62 f1 75 28 74 c0  dis=62 f1 75 28 74 c0
    vpcmpeqb 0x90909090,%ymm1,%k0       # gen=62 f1 75 28 74 05  dis=62 f1 75 28 74 05 90
    vpcmpeqw %ymm0,%ymm1,%k0            # gen=62 f1 75 28 75 c0  dis=62 f1 75 28 75 c0
    vpcmpeqw 0x90909090,%ymm1,%k0       # gen=62 f1 75 28 75 05  dis=62 f1 75 28 75 05 90
    vpcmpeqd %ymm0,%ymm1,%k0            # gen=62 f1 75 28 76 c0  dis=62 f1 75 28 76 c0
    vpcmpeqd 0x90909090,%ymm1,%k0       # gen=62 f1 75 28 76 05  dis=62 f1 75 28 76 05 90
    {evex} vpsrlw %xmm0,%ymm1,%ymm0     # gen=62 f1 75 28 d1 c0  dis=62 f1 75 28 d1 c0
    {evex} vpsrlw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 d1 05  dis=62 f1 75 28 d1 05 90
    {evex} vpsrld %xmm0,%ymm1,%ymm0     # gen=62 f1 75 28 d2 c0  dis=62 f1 75 28 d2 c0
    {evex} vpsrld 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 d2 05  dis=62 f1 75 28 d2 05 90
    {evex} vpmullw %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 d5 c0  dis=62 f1 75 28 d5 c0
    {evex} vpmullw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 d5 05  dis=62 f1 75 28 d5 05 90
    {evex} vpsubusb %ymm0,%ymm1,%ymm0   # gen=62 f1 75 28 d8 c0  dis=62 f1 75 28 d8 c0
    {evex} vpsubusb 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 d8 05  dis=62 f1 75 28 d8 05 90
    {evex} vpsubusw %ymm0,%ymm1,%ymm0   # gen=62 f1 75 28 d9 c0  dis=62 f1 75 28 d9 c0
    {evex} vpsubusw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 d9 05  dis=62 f1 75 28 d9 05 90
    {evex} vpminub %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 da c0  dis=62 f1 75 28 da c0
    {evex} vpminub 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 da 05  dis=62 f1 75 28 da 05 90
    vpandd %ymm0,%ymm1,%ymm0            # gen=62 f1 75 28 db c0  dis=62 f1 75 28 db c0
    vpandd 0x90909090,%ymm1,%ymm0       # gen=62 f1 75 28 db 05  dis=62 f1 75 28 db 05 90
    {evex} vpaddusb %ymm0,%ymm1,%ymm0   # gen=62 f1 75 28 dc c0  dis=62 f1 75 28 dc c0
    {evex} vpaddusb 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 dc 05  dis=62 f1 75 28 dc 05 90
    {evex} vpaddusw %ymm0,%ymm1,%ymm0   # gen=62 f1 75 28 dd c0  dis=62 f1 75 28 dd c0
    {evex} vpaddusw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 dd 05  dis=62 f1 75 28 dd 05 90
    {evex} vpmaxub %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 de c0  dis=62 f1 75 28 de c0
    {evex} vpmaxub 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 de 05  dis=62 f1 75 28 de 05 90
    vpandnd %ymm0,%ymm1,%ymm0           # gen=62 f1 75 28 df c0  dis=62 f1 75 28 df c0
    vpandnd 0x90909090,%ymm1,%ymm0      # gen=62 f1 75 28 df 05  dis=62 f1 75 28 df 05 90
    {evex} vpavgb %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 e0 c0  dis=62 f1 75 28 e0 c0
    {evex} vpavgb 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 e0 05  dis=62 f1 75 28 e0 05 90
    {evex} vpsraw %xmm0,%ymm1,%ymm0     # gen=62 f1 75 28 e1 c0  dis=62 f1 75 28 e1 c0
    {evex} vpsraw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 e1 05  dis=62 f1 75 28 e1 05 90
    {evex} vpsrad %xmm0,%ymm1,%ymm0     # gen=62 f1 75 28 e2 c0  dis=62 f1 75 28 e2 c0
    {evex} vpsrad 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 e2 05  dis=62 f1 75 28 e2 05 90
    {evex} vpavgw %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 e3 c0  dis=62 f1 75 28 e3 c0
    {evex} vpavgw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 e3 05  dis=62 f1 75 28 e3 05 90
    {evex} vpmulhuw %ymm0,%ymm1,%ymm0   # gen=62 f1 75 28 e4 c0  dis=62 f1 75 28 e4 c0
    {evex} vpmulhuw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 e4 05  dis=62 f1 75 28 e4 05 90
    {evex} vpmulhw %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 e5 c0  dis=62 f1 75 28 e5 c0
    {evex} vpmulhw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 e5 05  dis=62 f1 75 28 e5 05 90
    {evex} vpsubsb %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 e8 c0  dis=62 f1 75 28 e8 c0
    {evex} vpsubsb 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 e8 05  dis=62 f1 75 28 e8 05 90
    {evex} vpsubsw %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 e9 c0  dis=62 f1 75 28 e9 c0
    {evex} vpsubsw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 e9 05  dis=62 f1 75 28 e9 05 90
    {evex} vpminsw %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 ea c0  dis=62 f1 75 28 ea c0
    {evex} vpminsw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 ea 05  dis=62 f1 75 28 ea 05 90
    vpord  %ymm0,%ymm1,%ymm0            # gen=62 f1 75 28 eb c0  dis=62 f1 75 28 eb c0
    vpord  0x90909090,%ymm1,%ymm0       # gen=62 f1 75 28 eb 05  dis=62 f1 75 28 eb 05 90
    {evex} vpaddsb %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 ec c0  dis=62 f1 75 28 ec c0
    {evex} vpaddsb 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 ec 05  dis=62 f1 75 28 ec 05 90
    {evex} vpaddsw %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 ed c0  dis=62 f1 75 28 ed c0
    {evex} vpaddsw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 ed 05  dis=62 f1 75 28 ed 05 90
    {evex} vpmaxsw %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 ee c0  dis=62 f1 75 28 ee c0
    {evex} vpmaxsw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 ee 05  dis=62 f1 75 28 ee 05 90
    vpxord %ymm0,%ymm1,%ymm0            # gen=62 f1 75 28 ef c0  dis=62 f1 75 28 ef c0
    vpxord 0x90909090,%ymm1,%ymm0       # gen=62 f1 75 28 ef 05  dis=62 f1 75 28 ef 05 90
    {evex} vpsllw %xmm0,%ymm1,%ymm0     # gen=62 f1 75 28 f1 c0  dis=62 f1 75 28 f1 c0
    {evex} vpsllw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 f1 05  dis=62 f1 75 28 f1 05 90
    {evex} vpslld %xmm0,%ymm1,%ymm0     # gen=62 f1 75 28 f2 c0  dis=62 f1 75 28 f2 c0
    {evex} vpslld 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 f2 05  dis=62 f1 75 28 f2 05 90
    {evex} vpmaddwd %ymm0,%ymm1,%ymm0   # gen=62 f1 75 28 f5 c0  dis=62 f1 75 28 f5 c0
    {evex} vpmaddwd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 f5 05  dis=62 f1 75 28 f5 05 90
    {evex} vpsadbw %ymm0,%ymm1,%ymm0    # gen=62 f1 75 28 f6 c0  dis=62 f1 75 28 f6 c0
    {evex} vpsadbw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 f6 05  dis=62 f1 75 28 f6 05 90
    {evex} vpsubb %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 f8 c0  dis=62 f1 75 28 f8 c0
    {evex} vpsubb 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 f8 05  dis=62 f1 75 28 f8 05 90
    {evex} vpsubw %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 f9 c0  dis=62 f1 75 28 f9 c0
    {evex} vpsubw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 f9 05  dis=62 f1 75 28 f9 05 90
    {evex} vpsubd %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 fa c0  dis=62 f1 75 28 fa c0
    {evex} vpsubd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 fa 05  dis=62 f1 75 28 fa 05 90
    {evex} vpaddb %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 fc c0  dis=62 f1 75 28 fc c0
    {evex} vpaddb 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 fc 05  dis=62 f1 75 28 fc 05 90
    {evex} vpaddw %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 fd c0  dis=62 f1 75 28 fd c0
    {evex} vpaddw 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 fd 05  dis=62 f1 75 28 fd 05 90
    {evex} vpaddd %ymm0,%ymm1,%ymm0     # gen=62 f1 75 28 fe c0  dis=62 f1 75 28 fe c0
    {evex} vpaddd 0x90909090,%ymm1,%ymm0 # gen=62 f1 75 28 fe 05  dis=62 f1 75 28 fe 05 90
    vaddpd %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 58 c0  dis=62 f1 75 48 58 c0
    vaddpd 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 58 05  dis=62 f1 75 48 58 05 90
    vmulpd %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 59 c0  dis=62 f1 75 48 59 c0
    vmulpd 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 59 05  dis=62 f1 75 48 59 05 90
    vsubpd %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 5c c0  dis=62 f1 75 48 5c c0
    vsubpd 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 5c 05  dis=62 f1 75 48 5c 05 90
    vminpd %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 5d c0  dis=62 f1 75 48 5d c0
    vminpd 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 5d 05  dis=62 f1 75 48 5d 05 90
    vdivpd %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 5e c0  dis=62 f1 75 48 5e c0
    vdivpd 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 5e 05  dis=62 f1 75 48 5e 05 90
    vmaxpd %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 5f c0  dis=62 f1 75 48 5f c0
    vmaxpd 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 5f 05  dis=62 f1 75 48 5f 05 90
    vpunpcklbw %zmm0,%zmm1,%zmm0        # gen=62 f1 75 48 60 c0  dis=62 f1 75 48 60 c0
    vpunpcklbw 0x90909090,%zmm1,%zmm0   # gen=62 f1 75 48 60 05  dis=62 f1 75 48 60 05 90
    vpunpcklwd %zmm0,%zmm1,%zmm0        # gen=62 f1 75 48 61 c0  dis=62 f1 75 48 61 c0
    vpunpcklwd 0x90909090,%zmm1,%zmm0   # gen=62 f1 75 48 61 05  dis=62 f1 75 48 61 05 90
    vpunpckldq %zmm0,%zmm1,%zmm0        # gen=62 f1 75 48 62 c0  dis=62 f1 75 48 62 c0
    vpunpckldq 0x90909090,%zmm1,%zmm0   # gen=62 f1 75 48 62 05  dis=62 f1 75 48 62 05 90
    vpacksswb %zmm0,%zmm1,%zmm0         # gen=62 f1 75 48 63 c0  dis=62 f1 75 48 63 c0
    vpacksswb 0x90909090,%zmm1,%zmm0    # gen=62 f1 75 48 63 05  dis=62 f1 75 48 63 05 90
    vpcmpgtb %zmm0,%zmm1,%k0            # gen=62 f1 75 48 64 c0  dis=62 f1 75 48 64 c0
    vpcmpgtb 0x90909090,%zmm1,%k0       # gen=62 f1 75 48 64 05  dis=62 f1 75 48 64 05 90
    vpcmpgtw %zmm0,%zmm1,%k0            # gen=62 f1 75 48 65 c0  dis=62 f1 75 48 65 c0
    vpcmpgtw 0x90909090,%zmm1,%k0       # gen=62 f1 75 48 65 05  dis=62 f1 75 48 65 05 90
    vpcmpgtd %zmm0,%zmm1,%k0            # gen=62 f1 75 48 66 c0  dis=62 f1 75 48 66 c0
    vpcmpgtd 0x90909090,%zmm1,%k0       # gen=62 f1 75 48 66 05  dis=62 f1 75 48 66 05 90
    vpackuswb %zmm0,%zmm1,%zmm0         # gen=62 f1 75 48 67 c0  dis=62 f1 75 48 67 c0
    vpackuswb 0x90909090,%zmm1,%zmm0    # gen=62 f1 75 48 67 05  dis=62 f1 75 48 67 05 90
    vpunpckhbw %zmm0,%zmm1,%zmm0        # gen=62 f1 75 48 68 c0  dis=62 f1 75 48 68 c0
    vpunpckhbw 0x90909090,%zmm1,%zmm0   # gen=62 f1 75 48 68 05  dis=62 f1 75 48 68 05 90
    vpunpckhwd %zmm0,%zmm1,%zmm0        # gen=62 f1 75 48 69 c0  dis=62 f1 75 48 69 c0
    vpunpckhwd 0x90909090,%zmm1,%zmm0   # gen=62 f1 75 48 69 05  dis=62 f1 75 48 69 05 90
    vpunpckhdq %zmm0,%zmm1,%zmm0        # gen=62 f1 75 48 6a c0  dis=62 f1 75 48 6a c0
    vpunpckhdq 0x90909090,%zmm1,%zmm0   # gen=62 f1 75 48 6a 05  dis=62 f1 75 48 6a 05 90
    vpackssdw %zmm0,%zmm1,%zmm0         # gen=62 f1 75 48 6b c0  dis=62 f1 75 48 6b c0
    vpackssdw 0x90909090,%zmm1,%zmm0    # gen=62 f1 75 48 6b 05  dis=62 f1 75 48 6b 05 90
    vprord $0x90,%zmm0,%zmm1            # gen=62 f1 75 48 72 c0  dis=62 f1 75 48 72 c0 90
    vprord $0x90,0x90909090,%zmm1       # gen=62 f1 75 48 72 05  dis=62 f1 75 48 72 05 90
    vpcmpeqb %zmm0,%zmm1,%k0            # gen=62 f1 75 48 74 c0  dis=62 f1 75 48 74 c0
    vpcmpeqb 0x90909090,%zmm1,%k0       # gen=62 f1 75 48 74 05  dis=62 f1 75 48 74 05 90
    vpcmpeqw %zmm0,%zmm1,%k0            # gen=62 f1 75 48 75 c0  dis=62 f1 75 48 75 c0
    vpcmpeqw 0x90909090,%zmm1,%k0       # gen=62 f1 75 48 75 05  dis=62 f1 75 48 75 05 90
    vpcmpeqd %zmm0,%zmm1,%k0            # gen=62 f1 75 48 76 c0  dis=62 f1 75 48 76 c0
    vpcmpeqd 0x90909090,%zmm1,%k0       # gen=62 f1 75 48 76 05  dis=62 f1 75 48 76 05 90
    vpsrlw %xmm0,%zmm1,%zmm0            # gen=62 f1 75 48 d1 c0  dis=62 f1 75 48 d1 c0
    vpsrlw 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 d1 05  dis=62 f1 75 48 d1 05 90
    vpsrld %xmm0,%zmm1,%zmm0            # gen=62 f1 75 48 d2 c0  dis=62 f1 75 48 d2 c0
    vpsrld 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 d2 05  dis=62 f1 75 48 d2 05 90
    vpmullw %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 d5 c0  dis=62 f1 75 48 d5 c0
    vpmullw 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 d5 05  dis=62 f1 75 48 d5 05 90
    vpsubusb %zmm0,%zmm1,%zmm0          # gen=62 f1 75 48 d8 c0  dis=62 f1 75 48 d8 c0
    vpsubusb 0x90909090,%zmm1,%zmm0     # gen=62 f1 75 48 d8 05  dis=62 f1 75 48 d8 05 90
    vpsubusw %zmm0,%zmm1,%zmm0          # gen=62 f1 75 48 d9 c0  dis=62 f1 75 48 d9 c0
    vpsubusw 0x90909090,%zmm1,%zmm0     # gen=62 f1 75 48 d9 05  dis=62 f1 75 48 d9 05 90
    vpminub %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 da c0  dis=62 f1 75 48 da c0
    vpminub 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 da 05  dis=62 f1 75 48 da 05 90
    vpandd %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 db c0  dis=62 f1 75 48 db c0
    vpandd 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 db 05  dis=62 f1 75 48 db 05 90
    vpaddusb %zmm0,%zmm1,%zmm0          # gen=62 f1 75 48 dc c0  dis=62 f1 75 48 dc c0
    vpaddusb 0x90909090,%zmm1,%zmm0     # gen=62 f1 75 48 dc 05  dis=62 f1 75 48 dc 05 90
    vpaddusw %zmm0,%zmm1,%zmm0          # gen=62 f1 75 48 dd c0  dis=62 f1 75 48 dd c0
    vpaddusw 0x90909090,%zmm1,%zmm0     # gen=62 f1 75 48 dd 05  dis=62 f1 75 48 dd 05 90
    vpmaxub %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 de c0  dis=62 f1 75 48 de c0
    vpmaxub 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 de 05  dis=62 f1 75 48 de 05 90
    vpandnd %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 df c0  dis=62 f1 75 48 df c0
    vpandnd 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 df 05  dis=62 f1 75 48 df 05 90
    vpavgb %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 e0 c0  dis=62 f1 75 48 e0 c0
    vpavgb 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 e0 05  dis=62 f1 75 48 e0 05 90
    vpsraw %xmm0,%zmm1,%zmm0            # gen=62 f1 75 48 e1 c0  dis=62 f1 75 48 e1 c0
    vpsraw 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 e1 05  dis=62 f1 75 48 e1 05 90
    vpsrad %xmm0,%zmm1,%zmm0            # gen=62 f1 75 48 e2 c0  dis=62 f1 75 48 e2 c0
    vpsrad 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 e2 05  dis=62 f1 75 48 e2 05 90
    vpavgw %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 e3 c0  dis=62 f1 75 48 e3 c0
    vpavgw 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 e3 05  dis=62 f1 75 48 e3 05 90
    vpmulhuw %zmm0,%zmm1,%zmm0          # gen=62 f1 75 48 e4 c0  dis=62 f1 75 48 e4 c0
    vpmulhuw 0x90909090,%zmm1,%zmm0     # gen=62 f1 75 48 e4 05  dis=62 f1 75 48 e4 05 90
    vpmulhw %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 e5 c0  dis=62 f1 75 48 e5 c0
    vpmulhw 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 e5 05  dis=62 f1 75 48 e5 05 90
    vpsubsb %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 e8 c0  dis=62 f1 75 48 e8 c0
    vpsubsb 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 e8 05  dis=62 f1 75 48 e8 05 90
    vpsubsw %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 e9 c0  dis=62 f1 75 48 e9 c0
    vpsubsw 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 e9 05  dis=62 f1 75 48 e9 05 90
    vpminsw %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 ea c0  dis=62 f1 75 48 ea c0
    vpminsw 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 ea 05  dis=62 f1 75 48 ea 05 90
    vpord  %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 eb c0  dis=62 f1 75 48 eb c0
    vpord  0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 eb 05  dis=62 f1 75 48 eb 05 90
    vpaddsb %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 ec c0  dis=62 f1 75 48 ec c0
    vpaddsb 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 ec 05  dis=62 f1 75 48 ec 05 90
    vpaddsw %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 ed c0  dis=62 f1 75 48 ed c0
    vpaddsw 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 ed 05  dis=62 f1 75 48 ed 05 90
    vpmaxsw %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 ee c0  dis=62 f1 75 48 ee c0
    vpmaxsw 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 ee 05  dis=62 f1 75 48 ee 05 90
    vpxord %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 ef c0  dis=62 f1 75 48 ef c0
    vpxord 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 ef 05  dis=62 f1 75 48 ef 05 90
    vpsllw %xmm0,%zmm1,%zmm0            # gen=62 f1 75 48 f1 c0  dis=62 f1 75 48 f1 c0
    vpsllw 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 f1 05  dis=62 f1 75 48 f1 05 90
    vpslld %xmm0,%zmm1,%zmm0            # gen=62 f1 75 48 f2 c0  dis=62 f1 75 48 f2 c0
    vpslld 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 f2 05  dis=62 f1 75 48 f2 05 90
    vpmaddwd %zmm0,%zmm1,%zmm0          # gen=62 f1 75 48 f5 c0  dis=62 f1 75 48 f5 c0
    vpmaddwd 0x90909090,%zmm1,%zmm0     # gen=62 f1 75 48 f5 05  dis=62 f1 75 48 f5 05 90
    vpsadbw %zmm0,%zmm1,%zmm0           # gen=62 f1 75 48 f6 c0  dis=62 f1 75 48 f6 c0
    vpsadbw 0x90909090,%zmm1,%zmm0      # gen=62 f1 75 48 f6 05  dis=62 f1 75 48 f6 05 90
    vpsubb %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 f8 c0  dis=62 f1 75 48 f8 c0
    vpsubb 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 f8 05  dis=62 f1 75 48 f8 05 90
    vpsubw %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 f9 c0  dis=62 f1 75 48 f9 c0
    vpsubw 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 f9 05  dis=62 f1 75 48 f9 05 90
    vpsubd %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 fa c0  dis=62 f1 75 48 fa c0
    vpsubd 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 fa 05  dis=62 f1 75 48 fa 05 90
    vpaddb %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 fc c0  dis=62 f1 75 48 fc c0
    vpaddb 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 fc 05  dis=62 f1 75 48 fc 05 90
    vpaddw %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 fd c0  dis=62 f1 75 48 fd c0
    vpaddw 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 fd 05  dis=62 f1 75 48 fd 05 90
    vpaddd %zmm0,%zmm1,%zmm0            # gen=62 f1 75 48 fe c0  dis=62 f1 75 48 fe c0
    vpaddd 0x90909090,%zmm1,%zmm0       # gen=62 f1 75 48 fe 05  dis=62 f1 75 48 fe 05 90
    {evex} vmovss %xmm0,%xmm1,%xmm0     # gen=62 f1 76 08 10 c0  dis=62 f1 76 08 10 c0
    {evex} vcvtsi2ss %eax,%xmm1,%xmm0   # gen=62 f1 76 08 2a c0  dis=62 f1 76 08 2a c0
    {evex} vcvtsi2ss 0x90909090,%xmm1,%xmm0 # gen=62 f1 76 08 2a 05  dis=62 f1 76 08 2a 05 90
    {evex} vsqrtss %xmm0,%xmm1,%xmm0    # gen=62 f1 76 08 51 c0  dis=62 f1 76 08 51 c0
    {evex} vsqrtss 0x90909090,%xmm1,%xmm0 # gen=62 f1 76 08 51 05  dis=62 f1 76 08 51 05 90
    {evex} vaddss %xmm0,%xmm1,%xmm0     # gen=62 f1 76 08 58 c0  dis=62 f1 76 08 58 c0
    {evex} vaddss 0x90909090,%xmm1,%xmm0 # gen=62 f1 76 08 58 05  dis=62 f1 76 08 58 05 90
    {evex} vmulss %xmm0,%xmm1,%xmm0     # gen=62 f1 76 08 59 c0  dis=62 f1 76 08 59 c0
    {evex} vmulss 0x90909090,%xmm1,%xmm0 # gen=62 f1 76 08 59 05  dis=62 f1 76 08 59 05 90
    {evex} vcvtss2sd %xmm0,%xmm1,%xmm0  # gen=62 f1 76 08 5a c0  dis=62 f1 76 08 5a c0
    {evex} vcvtss2sd 0x90909090,%xmm1,%xmm0 # gen=62 f1 76 08 5a 05  dis=62 f1 76 08 5a 05 90
    {evex} vsubss %xmm0,%xmm1,%xmm0     # gen=62 f1 76 08 5c c0  dis=62 f1 76 08 5c c0
    {evex} vsubss 0x90909090,%xmm1,%xmm0 # gen=62 f1 76 08 5c 05  dis=62 f1 76 08 5c 05 90
    {evex} vminss %xmm0,%xmm1,%xmm0     # gen=62 f1 76 08 5d c0  dis=62 f1 76 08 5d c0
    {evex} vminss 0x90909090,%xmm1,%xmm0 # gen=62 f1 76 08 5d 05  dis=62 f1 76 08 5d 05 90
    {evex} vdivss %xmm0,%xmm1,%xmm0     # gen=62 f1 76 08 5e c0  dis=62 f1 76 08 5e c0
    {evex} vdivss 0x90909090,%xmm1,%xmm0 # gen=62 f1 76 08 5e 05  dis=62 f1 76 08 5e 05 90
    {evex} vmaxss %xmm0,%xmm1,%xmm0     # gen=62 f1 76 08 5f c0  dis=62 f1 76 08 5f c0
    {evex} vmaxss 0x90909090,%xmm1,%xmm0 # gen=62 f1 76 08 5f 05  dis=62 f1 76 08 5f 05 90
    vcvtusi2ss %eax,%xmm1,%xmm0         # gen=62 f1 76 08 7b c0  dis=62 f1 76 08 7b c0
    vcvtusi2ss 0x90909090,%xmm1,%xmm0   # gen=62 f1 76 08 7b 05  dis=62 f1 76 08 7b 05 90
    vcmpss $0x90,%xmm0,%xmm1,%k0        # gen=62 f1 76 08 c2 c0  dis=62 f1 76 08 c2 c0 90
    vcmpss $0x90,0x90909090,%xmm1,%k0   # gen=62 f1 76 08 c2 05  dis=62 f1 76 08 c2 05 90
    {evex} vcvtsi2sd %eax,%xmm1,%xmm0   # gen=62 f1 77 08 2a c0  dis=62 f1 77 08 2a c0
    {evex} vcvtsi2sd 0x90909090,%xmm1,%xmm0 # gen=62 f1 77 08 2a 05  dis=62 f1 77 08 2a 05 90
    vcvtusi2sd %eax,%xmm1,%xmm0         # gen=62 f1 77 08 7b c0  dis=62 f1 77 08 7b c0
    vcvtusi2sd 0x90909090,%xmm1,%xmm0   # gen=62 f1 77 08 7b 05  dis=62 f1 77 08 7b 05 90
    {evex} vunpcklpd %xmm0,%xmm1,%xmm0  # gen=62 f1 f5 08 14 c0  dis=62 f1 f5 08 14 c0
    {evex} vunpcklpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 14 05  dis=62 f1 f5 08 14 05 90
    {evex} vunpckhpd %xmm0,%xmm1,%xmm0  # gen=62 f1 f5 08 15 c0  dis=62 f1 f5 08 15 c0
    {evex} vunpckhpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 15 05  dis=62 f1 f5 08 15 05 90
    {evex} vandpd %xmm0,%xmm1,%xmm0     # gen=62 f1 f5 08 54 c0  dis=62 f1 f5 08 54 c0
    {evex} vandpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 54 05  dis=62 f1 f5 08 54 05 90
    {evex} vandnpd %xmm0,%xmm1,%xmm0    # gen=62 f1 f5 08 55 c0  dis=62 f1 f5 08 55 c0
    {evex} vandnpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 55 05  dis=62 f1 f5 08 55 05 90
    {evex} vorpd %xmm0,%xmm1,%xmm0      # gen=62 f1 f5 08 56 c0  dis=62 f1 f5 08 56 c0
    {evex} vorpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 56 05  dis=62 f1 f5 08 56 05 90
    {evex} vxorpd %xmm0,%xmm1,%xmm0     # gen=62 f1 f5 08 57 c0  dis=62 f1 f5 08 57 c0
    {evex} vxorpd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 57 05  dis=62 f1 f5 08 57 05 90
    {evex} vpunpcklqdq %xmm0,%xmm1,%xmm0 # gen=62 f1 f5 08 6c c0  dis=62 f1 f5 08 6c c0
    {evex} vpunpcklqdq 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 6c 05  dis=62 f1 f5 08 6c 05 90
    {evex} vpunpckhqdq %xmm0,%xmm1,%xmm0 # gen=62 f1 f5 08 6d c0  dis=62 f1 f5 08 6d c0
    {evex} vpunpckhqdq 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 6d 05  dis=62 f1 f5 08 6d 05 90
    vprorq $0x90,%xmm0,%xmm1            # gen=62 f1 f5 08 72 c0  dis=62 f1 f5 08 72 c0 90
    vprorq $0x90,0x90909090,%xmm1       # gen=62 f1 f5 08 72 05  dis=62 f1 f5 08 72 05 90
    vcmppd $0x90,%xmm0,%xmm1,%k0        # gen=62 f1 f5 08 c2 c0  dis=62 f1 f5 08 c2 c0 90
    vcmppd $0x90,0x90909090,%xmm1,%k0   # gen=62 f1 f5 08 c2 05  dis=62 f1 f5 08 c2 05 90
    {evex} vshufpd $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f1 f5 08 c6 c0  dis=62 f1 f5 08 c6 c0 90
    {evex} vshufpd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 c6 05  dis=62 f1 f5 08 c6 05 90
    {evex} vpsrlq %xmm0,%xmm1,%xmm0     # gen=62 f1 f5 08 d3 c0  dis=62 f1 f5 08 d3 c0
    {evex} vpsrlq 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 d3 05  dis=62 f1 f5 08 d3 05 90
    {evex} vpaddq %xmm0,%xmm1,%xmm0     # gen=62 f1 f5 08 d4 c0  dis=62 f1 f5 08 d4 c0
    {evex} vpaddq 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 d4 05  dis=62 f1 f5 08 d4 05 90
    vpandq %xmm0,%xmm1,%xmm0            # gen=62 f1 f5 08 db c0  dis=62 f1 f5 08 db c0
    vpandq 0x90909090,%xmm1,%xmm0       # gen=62 f1 f5 08 db 05  dis=62 f1 f5 08 db 05 90
    vpandnq %xmm0,%xmm1,%xmm0           # gen=62 f1 f5 08 df c0  dis=62 f1 f5 08 df c0
    vpandnq 0x90909090,%xmm1,%xmm0      # gen=62 f1 f5 08 df 05  dis=62 f1 f5 08 df 05 90
    vpsraq %xmm0,%xmm1,%xmm0            # gen=62 f1 f5 08 e2 c0  dis=62 f1 f5 08 e2 c0
    vpsraq 0x90909090,%xmm1,%xmm0       # gen=62 f1 f5 08 e2 05  dis=62 f1 f5 08 e2 05 90
    vporq  %xmm0,%xmm1,%xmm0            # gen=62 f1 f5 08 eb c0  dis=62 f1 f5 08 eb c0
    vporq  0x90909090,%xmm1,%xmm0       # gen=62 f1 f5 08 eb 05  dis=62 f1 f5 08 eb 05 90
    vpxorq %xmm0,%xmm1,%xmm0            # gen=62 f1 f5 08 ef c0  dis=62 f1 f5 08 ef c0
    vpxorq 0x90909090,%xmm1,%xmm0       # gen=62 f1 f5 08 ef 05  dis=62 f1 f5 08 ef 05 90
    {evex} vpsllq %xmm0,%xmm1,%xmm0     # gen=62 f1 f5 08 f3 c0  dis=62 f1 f5 08 f3 c0
    {evex} vpsllq 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 f3 05  dis=62 f1 f5 08 f3 05 90
    {evex} vpmuludq %xmm0,%xmm1,%xmm0   # gen=62 f1 f5 08 f4 c0  dis=62 f1 f5 08 f4 c0
    {evex} vpmuludq 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 f4 05  dis=62 f1 f5 08 f4 05 90
    {evex} vpsubq %xmm0,%xmm1,%xmm0     # gen=62 f1 f5 08 fb c0  dis=62 f1 f5 08 fb c0
    {evex} vpsubq 0x90909090,%xmm1,%xmm0 # gen=62 f1 f5 08 fb 05  dis=62 f1 f5 08 fb 05 90
    {evex} vunpcklpd %ymm0,%ymm1,%ymm0  # gen=62 f1 f5 28 14 c0  dis=62 f1 f5 28 14 c0
    {evex} vunpcklpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 14 05  dis=62 f1 f5 28 14 05 90
    {evex} vunpckhpd %ymm0,%ymm1,%ymm0  # gen=62 f1 f5 28 15 c0  dis=62 f1 f5 28 15 c0
    {evex} vunpckhpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 15 05  dis=62 f1 f5 28 15 05 90
    {evex} vandpd %ymm0,%ymm1,%ymm0     # gen=62 f1 f5 28 54 c0  dis=62 f1 f5 28 54 c0
    {evex} vandpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 54 05  dis=62 f1 f5 28 54 05 90
    {evex} vandnpd %ymm0,%ymm1,%ymm0    # gen=62 f1 f5 28 55 c0  dis=62 f1 f5 28 55 c0
    {evex} vandnpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 55 05  dis=62 f1 f5 28 55 05 90
    {evex} vorpd %ymm0,%ymm1,%ymm0      # gen=62 f1 f5 28 56 c0  dis=62 f1 f5 28 56 c0
    {evex} vorpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 56 05  dis=62 f1 f5 28 56 05 90
    {evex} vxorpd %ymm0,%ymm1,%ymm0     # gen=62 f1 f5 28 57 c0  dis=62 f1 f5 28 57 c0
    {evex} vxorpd 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 57 05  dis=62 f1 f5 28 57 05 90
    {evex} vpunpcklqdq %ymm0,%ymm1,%ymm0 # gen=62 f1 f5 28 6c c0  dis=62 f1 f5 28 6c c0
    {evex} vpunpcklqdq 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 6c 05  dis=62 f1 f5 28 6c 05 90
    {evex} vpunpckhqdq %ymm0,%ymm1,%ymm0 # gen=62 f1 f5 28 6d c0  dis=62 f1 f5 28 6d c0
    {evex} vpunpckhqdq 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 6d 05  dis=62 f1 f5 28 6d 05 90
    vprorq $0x90,%ymm0,%ymm1            # gen=62 f1 f5 28 72 c0  dis=62 f1 f5 28 72 c0 90
    vprorq $0x90,0x90909090,%ymm1       # gen=62 f1 f5 28 72 05  dis=62 f1 f5 28 72 05 90
    vcmppd $0x90,%ymm0,%ymm1,%k0        # gen=62 f1 f5 28 c2 c0  dis=62 f1 f5 28 c2 c0 90
    vcmppd $0x90,0x90909090,%ymm1,%k0   # gen=62 f1 f5 28 c2 05  dis=62 f1 f5 28 c2 05 90
    {evex} vshufpd $0x90,%ymm0,%ymm1,%ymm0 # gen=62 f1 f5 28 c6 c0  dis=62 f1 f5 28 c6 c0 90
    {evex} vshufpd $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 c6 05  dis=62 f1 f5 28 c6 05 90
    {evex} vpsrlq %xmm0,%ymm1,%ymm0     # gen=62 f1 f5 28 d3 c0  dis=62 f1 f5 28 d3 c0
    {evex} vpsrlq 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 d3 05  dis=62 f1 f5 28 d3 05 90
    {evex} vpaddq %ymm0,%ymm1,%ymm0     # gen=62 f1 f5 28 d4 c0  dis=62 f1 f5 28 d4 c0
    {evex} vpaddq 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 d4 05  dis=62 f1 f5 28 d4 05 90
    vpandq %ymm0,%ymm1,%ymm0            # gen=62 f1 f5 28 db c0  dis=62 f1 f5 28 db c0
    vpandq 0x90909090,%ymm1,%ymm0       # gen=62 f1 f5 28 db 05  dis=62 f1 f5 28 db 05 90
    vpandnq %ymm0,%ymm1,%ymm0           # gen=62 f1 f5 28 df c0  dis=62 f1 f5 28 df c0
    vpandnq 0x90909090,%ymm1,%ymm0      # gen=62 f1 f5 28 df 05  dis=62 f1 f5 28 df 05 90
    vpsraq %xmm0,%ymm1,%ymm0            # gen=62 f1 f5 28 e2 c0  dis=62 f1 f5 28 e2 c0
    vpsraq 0x90909090,%ymm1,%ymm0       # gen=62 f1 f5 28 e2 05  dis=62 f1 f5 28 e2 05 90
    vporq  %ymm0,%ymm1,%ymm0            # gen=62 f1 f5 28 eb c0  dis=62 f1 f5 28 eb c0
    vporq  0x90909090,%ymm1,%ymm0       # gen=62 f1 f5 28 eb 05  dis=62 f1 f5 28 eb 05 90
    vpxorq %ymm0,%ymm1,%ymm0            # gen=62 f1 f5 28 ef c0  dis=62 f1 f5 28 ef c0
    vpxorq 0x90909090,%ymm1,%ymm0       # gen=62 f1 f5 28 ef 05  dis=62 f1 f5 28 ef 05 90
    {evex} vpsllq %xmm0,%ymm1,%ymm0     # gen=62 f1 f5 28 f3 c0  dis=62 f1 f5 28 f3 c0
    {evex} vpsllq 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 f3 05  dis=62 f1 f5 28 f3 05 90
    {evex} vpmuludq %ymm0,%ymm1,%ymm0   # gen=62 f1 f5 28 f4 c0  dis=62 f1 f5 28 f4 c0
    {evex} vpmuludq 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 f4 05  dis=62 f1 f5 28 f4 05 90
    {evex} vpsubq %ymm0,%ymm1,%ymm0     # gen=62 f1 f5 28 fb c0  dis=62 f1 f5 28 fb c0
    {evex} vpsubq 0x90909090,%ymm1,%ymm0 # gen=62 f1 f5 28 fb 05  dis=62 f1 f5 28 fb 05 90
    vunpcklpd %zmm0,%zmm1,%zmm0         # gen=62 f1 f5 48 14 c0  dis=62 f1 f5 48 14 c0
    vunpcklpd 0x90909090,%zmm1,%zmm0    # gen=62 f1 f5 48 14 05  dis=62 f1 f5 48 14 05 90
    vunpckhpd %zmm0,%zmm1,%zmm0         # gen=62 f1 f5 48 15 c0  dis=62 f1 f5 48 15 c0
    vunpckhpd 0x90909090,%zmm1,%zmm0    # gen=62 f1 f5 48 15 05  dis=62 f1 f5 48 15 05 90
    vandpd %zmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 54 c0  dis=62 f1 f5 48 54 c0
    vandpd 0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 54 05  dis=62 f1 f5 48 54 05 90
    vandnpd %zmm0,%zmm1,%zmm0           # gen=62 f1 f5 48 55 c0  dis=62 f1 f5 48 55 c0
    vandnpd 0x90909090,%zmm1,%zmm0      # gen=62 f1 f5 48 55 05  dis=62 f1 f5 48 55 05 90
    vorpd  %zmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 56 c0  dis=62 f1 f5 48 56 c0
    vorpd  0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 56 05  dis=62 f1 f5 48 56 05 90
    vxorpd %zmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 57 c0  dis=62 f1 f5 48 57 c0
    vxorpd 0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 57 05  dis=62 f1 f5 48 57 05 90
    vpunpcklqdq %zmm0,%zmm1,%zmm0       # gen=62 f1 f5 48 6c c0  dis=62 f1 f5 48 6c c0
    vpunpcklqdq 0x90909090,%zmm1,%zmm0  # gen=62 f1 f5 48 6c 05  dis=62 f1 f5 48 6c 05 90
    vpunpckhqdq %zmm0,%zmm1,%zmm0       # gen=62 f1 f5 48 6d c0  dis=62 f1 f5 48 6d c0
    vpunpckhqdq 0x90909090,%zmm1,%zmm0  # gen=62 f1 f5 48 6d 05  dis=62 f1 f5 48 6d 05 90
    vprorq $0x90,%zmm0,%zmm1            # gen=62 f1 f5 48 72 c0  dis=62 f1 f5 48 72 c0 90
    vprorq $0x90,0x90909090,%zmm1       # gen=62 f1 f5 48 72 05  dis=62 f1 f5 48 72 05 90
    vcmppd $0x90,%zmm0,%zmm1,%k0        # gen=62 f1 f5 48 c2 c0  dis=62 f1 f5 48 c2 c0 90
    vcmppd $0x90,0x90909090,%zmm1,%k0   # gen=62 f1 f5 48 c2 05  dis=62 f1 f5 48 c2 05 90
    vshufpd $0x90,%zmm0,%zmm1,%zmm0     # gen=62 f1 f5 48 c6 c0  dis=62 f1 f5 48 c6 c0 90
    vshufpd $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f1 f5 48 c6 05  dis=62 f1 f5 48 c6 05 90
    vpsrlq %xmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 d3 c0  dis=62 f1 f5 48 d3 c0
    vpsrlq 0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 d3 05  dis=62 f1 f5 48 d3 05 90
    vpaddq %zmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 d4 c0  dis=62 f1 f5 48 d4 c0
    vpaddq 0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 d4 05  dis=62 f1 f5 48 d4 05 90
    vpandq %zmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 db c0  dis=62 f1 f5 48 db c0
    vpandq 0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 db 05  dis=62 f1 f5 48 db 05 90
    vpandnq %zmm0,%zmm1,%zmm0           # gen=62 f1 f5 48 df c0  dis=62 f1 f5 48 df c0
    vpandnq 0x90909090,%zmm1,%zmm0      # gen=62 f1 f5 48 df 05  dis=62 f1 f5 48 df 05 90
    vpsraq %xmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 e2 c0  dis=62 f1 f5 48 e2 c0
    vpsraq 0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 e2 05  dis=62 f1 f5 48 e2 05 90
    vporq  %zmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 eb c0  dis=62 f1 f5 48 eb c0
    vporq  0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 eb 05  dis=62 f1 f5 48 eb 05 90
    vpxorq %zmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 ef c0  dis=62 f1 f5 48 ef c0
    vpxorq 0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 ef 05  dis=62 f1 f5 48 ef 05 90
    vpsllq %xmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 f3 c0  dis=62 f1 f5 48 f3 c0
    vpsllq 0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 f3 05  dis=62 f1 f5 48 f3 05 90
    vpmuludq %zmm0,%zmm1,%zmm0          # gen=62 f1 f5 48 f4 c0  dis=62 f1 f5 48 f4 c0
    vpmuludq 0x90909090,%zmm1,%zmm0     # gen=62 f1 f5 48 f4 05  dis=62 f1 f5 48 f4 05 90
    vpsubq %zmm0,%zmm1,%zmm0            # gen=62 f1 f5 48 fb c0  dis=62 f1 f5 48 fb c0
    vpsubq 0x90909090,%zmm1,%zmm0       # gen=62 f1 f5 48 fb 05  dis=62 f1 f5 48 fb 05 90
    {evex} vmovsd %xmm0,%xmm1,%xmm0     # gen=62 f1 f7 08 10 c0  dis=62 f1 f7 08 10 c0
    {evex} vsqrtsd %xmm0,%xmm1,%xmm0    # gen=62 f1 f7 08 51 c0  dis=62 f1 f7 08 51 c0
    {evex} vsqrtsd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f7 08 51 05  dis=62 f1 f7 08 51 05 90
    {evex} vaddsd %xmm0,%xmm1,%xmm0     # gen=62 f1 f7 08 58 c0  dis=62 f1 f7 08 58 c0
    {evex} vaddsd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f7 08 58 05  dis=62 f1 f7 08 58 05 90
    {evex} vmulsd %xmm0,%xmm1,%xmm0     # gen=62 f1 f7 08 59 c0  dis=62 f1 f7 08 59 c0
    {evex} vmulsd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f7 08 59 05  dis=62 f1 f7 08 59 05 90
    {evex} vcvtsd2ss %xmm0,%xmm1,%xmm0  # gen=62 f1 f7 08 5a c0  dis=62 f1 f7 08 5a c0
    {evex} vcvtsd2ss 0x90909090,%xmm1,%xmm0 # gen=62 f1 f7 08 5a 05  dis=62 f1 f7 08 5a 05 90
    {evex} vsubsd %xmm0,%xmm1,%xmm0     # gen=62 f1 f7 08 5c c0  dis=62 f1 f7 08 5c c0
    {evex} vsubsd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f7 08 5c 05  dis=62 f1 f7 08 5c 05 90
    {evex} vminsd %xmm0,%xmm1,%xmm0     # gen=62 f1 f7 08 5d c0  dis=62 f1 f7 08 5d c0
    {evex} vminsd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f7 08 5d 05  dis=62 f1 f7 08 5d 05 90
    {evex} vdivsd %xmm0,%xmm1,%xmm0     # gen=62 f1 f7 08 5e c0  dis=62 f1 f7 08 5e c0
    {evex} vdivsd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f7 08 5e 05  dis=62 f1 f7 08 5e 05 90
    {evex} vmaxsd %xmm0,%xmm1,%xmm0     # gen=62 f1 f7 08 5f c0  dis=62 f1 f7 08 5f c0
    {evex} vmaxsd 0x90909090,%xmm1,%xmm0 # gen=62 f1 f7 08 5f 05  dis=62 f1 f7 08 5f 05 90
    vcmpsd $0x90,%xmm0,%xmm1,%k0        # gen=62 f1 f7 08 c2 c0  dis=62 f1 f7 08 c2 c0 90
    vcmpsd $0x90,0x90909090,%xmm1,%k0   # gen=62 f1 f7 08 c2 05  dis=62 f1 f7 08 c2 05 90
    {evex} vpshufb %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 00 c0  dis=62 f2 75 08 00 c0
    {evex} vpshufb 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 00 05  dis=62 f2 75 08 00 05 90
    {evex} vpmaddubsw %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 04 c0  dis=62 f2 75 08 04 c0
    {evex} vpmaddubsw 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 04 05  dis=62 f2 75 08 04 05 90
    {evex} vpmulhrsw %xmm0,%xmm1,%xmm0  # gen=62 f2 75 08 0b c0  dis=62 f2 75 08 0b c0
    {evex} vpmulhrsw 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 0b 05  dis=62 f2 75 08 0b 05 90
    {evex} vpermilps %xmm0,%xmm1,%xmm0  # gen=62 f2 75 08 0c c0  dis=62 f2 75 08 0c c0
    {evex} vpermilps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 0c 05  dis=62 f2 75 08 0c 05 90
    vprorvd %xmm0,%xmm1,%xmm0           # gen=62 f2 75 08 14 c0  dis=62 f2 75 08 14 c0
    vprorvd 0x90909090,%xmm1,%xmm0      # gen=62 f2 75 08 14 05  dis=62 f2 75 08 14 05 90
    vprolvd %xmm0,%xmm1,%xmm0           # gen=62 f2 75 08 15 c0  dis=62 f2 75 08 15 c0
    vprolvd 0x90909090,%xmm1,%xmm0      # gen=62 f2 75 08 15 05  dis=62 f2 75 08 15 05 90
    vptestmb %xmm0,%xmm1,%k0            # gen=62 f2 75 08 26 c0  dis=62 f2 75 08 26 c0
    vptestmb 0x90909090,%xmm1,%k0       # gen=62 f2 75 08 26 05  dis=62 f2 75 08 26 05 90
    vptestmd %xmm0,%xmm1,%k0            # gen=62 f2 75 08 27 c0  dis=62 f2 75 08 27 c0
    vptestmd 0x90909090,%xmm1,%k0       # gen=62 f2 75 08 27 05  dis=62 f2 75 08 27 05 90
    {evex} vpackusdw %xmm0,%xmm1,%xmm0  # gen=62 f2 75 08 2b c0  dis=62 f2 75 08 2b c0
    {evex} vpackusdw 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 2b 05  dis=62 f2 75 08 2b 05 90
    vscalefps %xmm0,%xmm1,%xmm0         # gen=62 f2 75 08 2c c0  dis=62 f2 75 08 2c c0
    vscalefps 0x90909090,%xmm1,%xmm0    # gen=62 f2 75 08 2c 05  dis=62 f2 75 08 2c 05 90
    vscalefss %xmm0,%xmm1,%xmm0         # gen=62 f2 75 08 2d c0  dis=62 f2 75 08 2d c0
    vscalefss 0x90909090,%xmm1,%xmm0    # gen=62 f2 75 08 2d 05  dis=62 f2 75 08 2d 05 90
    {evex} vpminsb %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 38 c0  dis=62 f2 75 08 38 c0
    {evex} vpminsb 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 38 05  dis=62 f2 75 08 38 05 90
    {evex} vpminsd %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 39 c0  dis=62 f2 75 08 39 c0
    {evex} vpminsd 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 39 05  dis=62 f2 75 08 39 05 90
    {evex} vpminuw %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 3a c0  dis=62 f2 75 08 3a c0
    {evex} vpminuw 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 3a 05  dis=62 f2 75 08 3a 05 90
    {evex} vpminud %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 3b c0  dis=62 f2 75 08 3b c0
    {evex} vpminud 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 3b 05  dis=62 f2 75 08 3b 05 90
    {evex} vpmaxsb %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 3c c0  dis=62 f2 75 08 3c c0
    {evex} vpmaxsb 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 3c 05  dis=62 f2 75 08 3c 05 90
    {evex} vpmaxsd %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 3d c0  dis=62 f2 75 08 3d c0
    {evex} vpmaxsd 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 3d 05  dis=62 f2 75 08 3d 05 90
    {evex} vpmaxuw %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 3e c0  dis=62 f2 75 08 3e c0
    {evex} vpmaxuw 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 3e 05  dis=62 f2 75 08 3e 05 90
    {evex} vpmaxud %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 3f c0  dis=62 f2 75 08 3f c0
    {evex} vpmaxud 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 3f 05  dis=62 f2 75 08 3f 05 90
    {evex} vpmulld %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 40 c0  dis=62 f2 75 08 40 c0
    {evex} vpmulld 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 40 05  dis=62 f2 75 08 40 05 90
    vgetexpss %xmm0,%xmm1,%xmm0         # gen=62 f2 75 08 43 c0  dis=62 f2 75 08 43 c0
    vgetexpss 0x90909090,%xmm1,%xmm0    # gen=62 f2 75 08 43 05  dis=62 f2 75 08 43 05 90
    vrcp14ss %xmm0,%xmm1,%xmm0          # gen=62 f2 75 08 4d c0  dis=62 f2 75 08 4d c0
    vrcp14ss 0x90909090,%xmm1,%xmm0     # gen=62 f2 75 08 4d 05  dis=62 f2 75 08 4d 05 90
    vrsqrt14ss %xmm0,%xmm1,%xmm0        # gen=62 f2 75 08 4f c0  dis=62 f2 75 08 4f c0
    vrsqrt14ss 0x90909090,%xmm1,%xmm0   # gen=62 f2 75 08 4f 05  dis=62 f2 75 08 4f 05 90
    vpdpbusd %xmm0,%xmm1,%xmm0          # gen=62 f2 75 08 50 c0  dis=62 f2 75 08 50 c0
    vpdpbusd 0x90909090,%xmm1,%xmm0     # gen=62 f2 75 08 50 05  dis=62 f2 75 08 50 05 90
    vpdpbusds %xmm0,%xmm1,%xmm0         # gen=62 f2 75 08 51 c0  dis=62 f2 75 08 51 c0
    vpdpbusds 0x90909090,%xmm1,%xmm0    # gen=62 f2 75 08 51 05  dis=62 f2 75 08 51 05 90
    vpdpwssd %xmm0,%xmm1,%xmm0          # gen=62 f2 75 08 52 c0  dis=62 f2 75 08 52 c0
    vpdpwssd 0x90909090,%xmm1,%xmm0     # gen=62 f2 75 08 52 05  dis=62 f2 75 08 52 05 90
    vpdpwssds %xmm0,%xmm1,%xmm0         # gen=62 f2 75 08 53 c0  dis=62 f2 75 08 53 c0
    vpdpwssds 0x90909090,%xmm1,%xmm0    # gen=62 f2 75 08 53 05  dis=62 f2 75 08 53 05 90
    vpblendmd %xmm0,%xmm1,%xmm0         # gen=62 f2 75 08 64 c0  dis=62 f2 75 08 64 c0
    vpblendmd 0x90909090,%xmm1,%xmm0    # gen=62 f2 75 08 64 05  dis=62 f2 75 08 64 05 90
    vblendmps %xmm0,%xmm1,%xmm0         # gen=62 f2 75 08 65 c0  dis=62 f2 75 08 65 c0
    vblendmps 0x90909090,%xmm1,%xmm0    # gen=62 f2 75 08 65 05  dis=62 f2 75 08 65 05 90
    vpblendmb %xmm0,%xmm1,%xmm0         # gen=62 f2 75 08 66 c0  dis=62 f2 75 08 66 c0
    vpblendmb 0x90909090,%xmm1,%xmm0    # gen=62 f2 75 08 66 05  dis=62 f2 75 08 66 05 90
    vpshldvd %xmm0,%xmm1,%xmm0          # gen=62 f2 75 08 71 c0  dis=62 f2 75 08 71 c0
    vpshldvd 0x90909090,%xmm1,%xmm0     # gen=62 f2 75 08 71 05  dis=62 f2 75 08 71 05 90
    vpshrdvd %xmm0,%xmm1,%xmm0          # gen=62 f2 75 08 73 c0  dis=62 f2 75 08 73 c0
    vpshrdvd 0x90909090,%xmm1,%xmm0     # gen=62 f2 75 08 73 05  dis=62 f2 75 08 73 05 90
    vpermi2b %xmm0,%xmm1,%xmm0          # gen=62 f2 75 08 75 c0  dis=62 f2 75 08 75 c0
    vpermi2b 0x90909090,%xmm1,%xmm0     # gen=62 f2 75 08 75 05  dis=62 f2 75 08 75 05 90
    vpermi2d %xmm0,%xmm1,%xmm0          # gen=62 f2 75 08 76 c0  dis=62 f2 75 08 76 c0
    vpermi2d 0x90909090,%xmm1,%xmm0     # gen=62 f2 75 08 76 05  dis=62 f2 75 08 76 05 90
    vpermi2ps %xmm0,%xmm1,%xmm0         # gen=62 f2 75 08 77 c0  dis=62 f2 75 08 77 c0
    vpermi2ps 0x90909090,%xmm1,%xmm0    # gen=62 f2 75 08 77 05  dis=62 f2 75 08 77 05 90
    vpermt2b %xmm0,%xmm1,%xmm0          # gen=62 f2 75 08 7d c0  dis=62 f2 75 08 7d c0
    vpermt2b 0x90909090,%xmm1,%xmm0     # gen=62 f2 75 08 7d 05  dis=62 f2 75 08 7d 05 90
    vpermt2d %xmm0,%xmm1,%xmm0          # gen=62 f2 75 08 7e c0  dis=62 f2 75 08 7e c0
    vpermt2d 0x90909090,%xmm1,%xmm0     # gen=62 f2 75 08 7e 05  dis=62 f2 75 08 7e 05 90
    vpermt2ps %xmm0,%xmm1,%xmm0         # gen=62 f2 75 08 7f c0  dis=62 f2 75 08 7f c0
    vpermt2ps 0x90909090,%xmm1,%xmm0    # gen=62 f2 75 08 7f 05  dis=62 f2 75 08 7f 05 90
    vpermb %xmm0,%xmm1,%xmm0            # gen=62 f2 75 08 8d c0  dis=62 f2 75 08 8d c0
    vpermb 0x90909090,%xmm1,%xmm0       # gen=62 f2 75 08 8d 05  dis=62 f2 75 08 8d 05 90
    vpshufbitqmb %xmm0,%xmm1,%k0        # gen=62 f2 75 08 8f c0  dis=62 f2 75 08 8f c0
    vpshufbitqmb 0x90909090,%xmm1,%k0   # gen=62 f2 75 08 8f 05  dis=62 f2 75 08 8f 05 90
    {evex} vfmaddsub132ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 96 c0  dis=62 f2 75 08 96 c0
    {evex} vfmaddsub132ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 96 05  dis=62 f2 75 08 96 05 90
    {evex} vfmsubadd132ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 97 c0  dis=62 f2 75 08 97 c0
    {evex} vfmsubadd132ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 97 05  dis=62 f2 75 08 97 05 90
    {evex} vfmadd132ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 98 c0  dis=62 f2 75 08 98 c0
    {evex} vfmadd132ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 98 05  dis=62 f2 75 08 98 05 90
    {evex} vfmadd132ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 99 c0  dis=62 f2 75 08 99 c0
    {evex} vfmadd132ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 99 05  dis=62 f2 75 08 99 05 90
    {evex} vfmsub132ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 9a c0  dis=62 f2 75 08 9a c0
    {evex} vfmsub132ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 9a 05  dis=62 f2 75 08 9a 05 90
    {evex} vfmsub132ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 9b c0  dis=62 f2 75 08 9b c0
    {evex} vfmsub132ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 9b 05  dis=62 f2 75 08 9b 05 90
    {evex} vfnmadd132ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 9c c0  dis=62 f2 75 08 9c c0
    {evex} vfnmadd132ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 9c 05  dis=62 f2 75 08 9c 05 90
    {evex} vfnmadd132ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 9d c0  dis=62 f2 75 08 9d c0
    {evex} vfnmadd132ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 9d 05  dis=62 f2 75 08 9d 05 90
    {evex} vfnmsub132ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 9e c0  dis=62 f2 75 08 9e c0
    {evex} vfnmsub132ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 9e 05  dis=62 f2 75 08 9e 05 90
    {evex} vfnmsub132ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 9f c0  dis=62 f2 75 08 9f c0
    {evex} vfnmsub132ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 9f 05  dis=62 f2 75 08 9f 05 90
    {evex} vfmaddsub213ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 a6 c0  dis=62 f2 75 08 a6 c0
    {evex} vfmaddsub213ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 a6 05  dis=62 f2 75 08 a6 05 90
    {evex} vfmsubadd213ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 a7 c0  dis=62 f2 75 08 a7 c0
    {evex} vfmsubadd213ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 a7 05  dis=62 f2 75 08 a7 05 90
    {evex} vfmadd213ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 a8 c0  dis=62 f2 75 08 a8 c0
    {evex} vfmadd213ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 a8 05  dis=62 f2 75 08 a8 05 90
    {evex} vfmadd213ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 a9 c0  dis=62 f2 75 08 a9 c0
    {evex} vfmadd213ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 a9 05  dis=62 f2 75 08 a9 05 90
    {evex} vfmsub213ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 aa c0  dis=62 f2 75 08 aa c0
    {evex} vfmsub213ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 aa 05  dis=62 f2 75 08 aa 05 90
    {evex} vfmsub213ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 ab c0  dis=62 f2 75 08 ab c0
    {evex} vfmsub213ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 ab 05  dis=62 f2 75 08 ab 05 90
    {evex} vfnmadd213ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 ac c0  dis=62 f2 75 08 ac c0
    {evex} vfnmadd213ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 ac 05  dis=62 f2 75 08 ac 05 90
    {evex} vfnmadd213ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 ad c0  dis=62 f2 75 08 ad c0
    {evex} vfnmadd213ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 ad 05  dis=62 f2 75 08 ad 05 90
    {evex} vfnmsub213ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 ae c0  dis=62 f2 75 08 ae c0
    {evex} vfnmsub213ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 ae 05  dis=62 f2 75 08 ae 05 90
    {evex} vfnmsub213ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 af c0  dis=62 f2 75 08 af c0
    {evex} vfnmsub213ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 af 05  dis=62 f2 75 08 af 05 90
    {evex} vfmaddsub231ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 b6 c0  dis=62 f2 75 08 b6 c0
    {evex} vfmaddsub231ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 b6 05  dis=62 f2 75 08 b6 05 90
    {evex} vfmsubadd231ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 b7 c0  dis=62 f2 75 08 b7 c0
    {evex} vfmsubadd231ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 b7 05  dis=62 f2 75 08 b7 05 90
    {evex} vfmadd231ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 b8 c0  dis=62 f2 75 08 b8 c0
    {evex} vfmadd231ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 b8 05  dis=62 f2 75 08 b8 05 90
    {evex} vfmadd231ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 b9 c0  dis=62 f2 75 08 b9 c0
    {evex} vfmadd231ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 b9 05  dis=62 f2 75 08 b9 05 90
    {evex} vfmsub231ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 ba c0  dis=62 f2 75 08 ba c0
    {evex} vfmsub231ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 ba 05  dis=62 f2 75 08 ba 05 90
    {evex} vfmsub231ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 bb c0  dis=62 f2 75 08 bb c0
    {evex} vfmsub231ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 bb 05  dis=62 f2 75 08 bb 05 90
    {evex} vfnmadd231ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 bc c0  dis=62 f2 75 08 bc c0
    {evex} vfnmadd231ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 bc 05  dis=62 f2 75 08 bc 05 90
    {evex} vfnmadd231ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 bd c0  dis=62 f2 75 08 bd c0
    {evex} vfnmadd231ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 bd 05  dis=62 f2 75 08 bd 05 90
    {evex} vfnmsub231ps %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 be c0  dis=62 f2 75 08 be c0
    {evex} vfnmsub231ps 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 be 05  dis=62 f2 75 08 be 05 90
    {evex} vfnmsub231ss %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 bf c0  dis=62 f2 75 08 bf c0
    {evex} vfnmsub231ss 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 bf 05  dis=62 f2 75 08 bf 05 90
    vrcp28ss %xmm0,%xmm1,%xmm0          # gen=62 f2 75 08 cb c0  dis=62 f2 75 08 cb c0
    vrcp28ss 0x90909090,%xmm1,%xmm0     # gen=62 f2 75 08 cb 05  dis=62 f2 75 08 cb 05 90
    vrsqrt28ss %xmm0,%xmm1,%xmm0        # gen=62 f2 75 08 cd c0  dis=62 f2 75 08 cd c0
    vrsqrt28ss 0x90909090,%xmm1,%xmm0   # gen=62 f2 75 08 cd 05  dis=62 f2 75 08 cd 05 90
    {evex} vgf2p8mulb %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 cf c0  dis=62 f2 75 08 cf c0
    {evex} vgf2p8mulb 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 cf 05  dis=62 f2 75 08 cf 05 90
    {evex} vaesenc %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 dc c0  dis=62 f2 75 08 dc c0
    {evex} vaesenc 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 dc 05  dis=62 f2 75 08 dc 05 90
    {evex} vaesenclast %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 dd c0  dis=62 f2 75 08 dd c0
    {evex} vaesenclast 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 dd 05  dis=62 f2 75 08 dd 05 90
    {evex} vaesdec %xmm0,%xmm1,%xmm0    # gen=62 f2 75 08 de c0  dis=62 f2 75 08 de c0
    {evex} vaesdec 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 de 05  dis=62 f2 75 08 de 05 90
    {evex} vaesdeclast %xmm0,%xmm1,%xmm0 # gen=62 f2 75 08 df c0  dis=62 f2 75 08 df c0
    {evex} vaesdeclast 0x90909090,%xmm1,%xmm0 # gen=62 f2 75 08 df 05  dis=62 f2 75 08 df 05 90
    {evex} vpshufb %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 00 c0  dis=62 f2 75 28 00 c0
    {evex} vpshufb 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 00 05  dis=62 f2 75 28 00 05 90
    {evex} vpmaddubsw %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 04 c0  dis=62 f2 75 28 04 c0
    {evex} vpmaddubsw 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 04 05  dis=62 f2 75 28 04 05 90
    {evex} vpmulhrsw %ymm0,%ymm1,%ymm0  # gen=62 f2 75 28 0b c0  dis=62 f2 75 28 0b c0
    {evex} vpmulhrsw 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 0b 05  dis=62 f2 75 28 0b 05 90
    {evex} vpermilps %ymm0,%ymm1,%ymm0  # gen=62 f2 75 28 0c c0  dis=62 f2 75 28 0c c0
    {evex} vpermilps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 0c 05  dis=62 f2 75 28 0c 05 90
    vprorvd %ymm0,%ymm1,%ymm0           # gen=62 f2 75 28 14 c0  dis=62 f2 75 28 14 c0
    vprorvd 0x90909090,%ymm1,%ymm0      # gen=62 f2 75 28 14 05  dis=62 f2 75 28 14 05 90
    vprolvd %ymm0,%ymm1,%ymm0           # gen=62 f2 75 28 15 c0  dis=62 f2 75 28 15 c0
    vprolvd 0x90909090,%ymm1,%ymm0      # gen=62 f2 75 28 15 05  dis=62 f2 75 28 15 05 90
    {evex} vpermps %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 16 c0  dis=62 f2 75 28 16 c0
    {evex} vpermps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 16 05  dis=62 f2 75 28 16 05 90
    vptestmb %ymm0,%ymm1,%k0            # gen=62 f2 75 28 26 c0  dis=62 f2 75 28 26 c0
    vptestmb 0x90909090,%ymm1,%k0       # gen=62 f2 75 28 26 05  dis=62 f2 75 28 26 05 90
    vptestmd %ymm0,%ymm1,%k0            # gen=62 f2 75 28 27 c0  dis=62 f2 75 28 27 c0
    vptestmd 0x90909090,%ymm1,%k0       # gen=62 f2 75 28 27 05  dis=62 f2 75 28 27 05 90
    {evex} vpackusdw %ymm0,%ymm1,%ymm0  # gen=62 f2 75 28 2b c0  dis=62 f2 75 28 2b c0
    {evex} vpackusdw 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 2b 05  dis=62 f2 75 28 2b 05 90
    vscalefps %ymm0,%ymm1,%ymm0         # gen=62 f2 75 28 2c c0  dis=62 f2 75 28 2c c0
    vscalefps 0x90909090,%ymm1,%ymm0    # gen=62 f2 75 28 2c 05  dis=62 f2 75 28 2c 05 90
    {evex} vpermd %ymm0,%ymm1,%ymm0     # gen=62 f2 75 28 36 c0  dis=62 f2 75 28 36 c0
    {evex} vpermd 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 36 05  dis=62 f2 75 28 36 05 90
    {evex} vpminsb %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 38 c0  dis=62 f2 75 28 38 c0
    {evex} vpminsb 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 38 05  dis=62 f2 75 28 38 05 90
    {evex} vpminsd %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 39 c0  dis=62 f2 75 28 39 c0
    {evex} vpminsd 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 39 05  dis=62 f2 75 28 39 05 90
    {evex} vpminuw %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 3a c0  dis=62 f2 75 28 3a c0
    {evex} vpminuw 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 3a 05  dis=62 f2 75 28 3a 05 90
    {evex} vpminud %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 3b c0  dis=62 f2 75 28 3b c0
    {evex} vpminud 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 3b 05  dis=62 f2 75 28 3b 05 90
    {evex} vpmaxsb %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 3c c0  dis=62 f2 75 28 3c c0
    {evex} vpmaxsb 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 3c 05  dis=62 f2 75 28 3c 05 90
    {evex} vpmaxsd %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 3d c0  dis=62 f2 75 28 3d c0
    {evex} vpmaxsd 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 3d 05  dis=62 f2 75 28 3d 05 90
    {evex} vpmaxuw %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 3e c0  dis=62 f2 75 28 3e c0
    {evex} vpmaxuw 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 3e 05  dis=62 f2 75 28 3e 05 90
    {evex} vpmaxud %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 3f c0  dis=62 f2 75 28 3f c0
    {evex} vpmaxud 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 3f 05  dis=62 f2 75 28 3f 05 90
    {evex} vpmulld %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 40 c0  dis=62 f2 75 28 40 c0
    {evex} vpmulld 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 40 05  dis=62 f2 75 28 40 05 90
    vpdpbusd %ymm0,%ymm1,%ymm0          # gen=62 f2 75 28 50 c0  dis=62 f2 75 28 50 c0
    vpdpbusd 0x90909090,%ymm1,%ymm0     # gen=62 f2 75 28 50 05  dis=62 f2 75 28 50 05 90
    vpdpbusds %ymm0,%ymm1,%ymm0         # gen=62 f2 75 28 51 c0  dis=62 f2 75 28 51 c0
    vpdpbusds 0x90909090,%ymm1,%ymm0    # gen=62 f2 75 28 51 05  dis=62 f2 75 28 51 05 90
    vpdpwssd %ymm0,%ymm1,%ymm0          # gen=62 f2 75 28 52 c0  dis=62 f2 75 28 52 c0
    vpdpwssd 0x90909090,%ymm1,%ymm0     # gen=62 f2 75 28 52 05  dis=62 f2 75 28 52 05 90
    vpdpwssds %ymm0,%ymm1,%ymm0         # gen=62 f2 75 28 53 c0  dis=62 f2 75 28 53 c0
    vpdpwssds 0x90909090,%ymm1,%ymm0    # gen=62 f2 75 28 53 05  dis=62 f2 75 28 53 05 90
    vpblendmd %ymm0,%ymm1,%ymm0         # gen=62 f2 75 28 64 c0  dis=62 f2 75 28 64 c0
    vpblendmd 0x90909090,%ymm1,%ymm0    # gen=62 f2 75 28 64 05  dis=62 f2 75 28 64 05 90
    vblendmps %ymm0,%ymm1,%ymm0         # gen=62 f2 75 28 65 c0  dis=62 f2 75 28 65 c0
    vblendmps 0x90909090,%ymm1,%ymm0    # gen=62 f2 75 28 65 05  dis=62 f2 75 28 65 05 90
    vpblendmb %ymm0,%ymm1,%ymm0         # gen=62 f2 75 28 66 c0  dis=62 f2 75 28 66 c0
    vpblendmb 0x90909090,%ymm1,%ymm0    # gen=62 f2 75 28 66 05  dis=62 f2 75 28 66 05 90
    vpshldvd %ymm0,%ymm1,%ymm0          # gen=62 f2 75 28 71 c0  dis=62 f2 75 28 71 c0
    vpshldvd 0x90909090,%ymm1,%ymm0     # gen=62 f2 75 28 71 05  dis=62 f2 75 28 71 05 90
    vpshrdvd %ymm0,%ymm1,%ymm0          # gen=62 f2 75 28 73 c0  dis=62 f2 75 28 73 c0
    vpshrdvd 0x90909090,%ymm1,%ymm0     # gen=62 f2 75 28 73 05  dis=62 f2 75 28 73 05 90
    vpermi2b %ymm0,%ymm1,%ymm0          # gen=62 f2 75 28 75 c0  dis=62 f2 75 28 75 c0
    vpermi2b 0x90909090,%ymm1,%ymm0     # gen=62 f2 75 28 75 05  dis=62 f2 75 28 75 05 90
    vpermi2d %ymm0,%ymm1,%ymm0          # gen=62 f2 75 28 76 c0  dis=62 f2 75 28 76 c0
    vpermi2d 0x90909090,%ymm1,%ymm0     # gen=62 f2 75 28 76 05  dis=62 f2 75 28 76 05 90
    vpermi2ps %ymm0,%ymm1,%ymm0         # gen=62 f2 75 28 77 c0  dis=62 f2 75 28 77 c0
    vpermi2ps 0x90909090,%ymm1,%ymm0    # gen=62 f2 75 28 77 05  dis=62 f2 75 28 77 05 90
    vpermt2b %ymm0,%ymm1,%ymm0          # gen=62 f2 75 28 7d c0  dis=62 f2 75 28 7d c0
    vpermt2b 0x90909090,%ymm1,%ymm0     # gen=62 f2 75 28 7d 05  dis=62 f2 75 28 7d 05 90
    vpermt2d %ymm0,%ymm1,%ymm0          # gen=62 f2 75 28 7e c0  dis=62 f2 75 28 7e c0
    vpermt2d 0x90909090,%ymm1,%ymm0     # gen=62 f2 75 28 7e 05  dis=62 f2 75 28 7e 05 90
    vpermt2ps %ymm0,%ymm1,%ymm0         # gen=62 f2 75 28 7f c0  dis=62 f2 75 28 7f c0
    vpermt2ps 0x90909090,%ymm1,%ymm0    # gen=62 f2 75 28 7f 05  dis=62 f2 75 28 7f 05 90
    vpermb %ymm0,%ymm1,%ymm0            # gen=62 f2 75 28 8d c0  dis=62 f2 75 28 8d c0
    vpermb 0x90909090,%ymm1,%ymm0       # gen=62 f2 75 28 8d 05  dis=62 f2 75 28 8d 05 90
    vpshufbitqmb %ymm0,%ymm1,%k0        # gen=62 f2 75 28 8f c0  dis=62 f2 75 28 8f c0
    vpshufbitqmb 0x90909090,%ymm1,%k0   # gen=62 f2 75 28 8f 05  dis=62 f2 75 28 8f 05 90
    {evex} vfmaddsub132ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 96 c0  dis=62 f2 75 28 96 c0
    {evex} vfmaddsub132ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 96 05  dis=62 f2 75 28 96 05 90
    {evex} vfmsubadd132ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 97 c0  dis=62 f2 75 28 97 c0
    {evex} vfmsubadd132ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 97 05  dis=62 f2 75 28 97 05 90
    {evex} vfmadd132ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 98 c0  dis=62 f2 75 28 98 c0
    {evex} vfmadd132ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 98 05  dis=62 f2 75 28 98 05 90
    {evex} vfmsub132ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 9a c0  dis=62 f2 75 28 9a c0
    {evex} vfmsub132ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 9a 05  dis=62 f2 75 28 9a 05 90
    {evex} vfnmadd132ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 9c c0  dis=62 f2 75 28 9c c0
    {evex} vfnmadd132ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 9c 05  dis=62 f2 75 28 9c 05 90
    {evex} vfnmsub132ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 9e c0  dis=62 f2 75 28 9e c0
    {evex} vfnmsub132ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 9e 05  dis=62 f2 75 28 9e 05 90
    {evex} vfmaddsub213ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 a6 c0  dis=62 f2 75 28 a6 c0
    {evex} vfmaddsub213ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 a6 05  dis=62 f2 75 28 a6 05 90
    {evex} vfmsubadd213ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 a7 c0  dis=62 f2 75 28 a7 c0
    {evex} vfmsubadd213ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 a7 05  dis=62 f2 75 28 a7 05 90
    {evex} vfmadd213ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 a8 c0  dis=62 f2 75 28 a8 c0
    {evex} vfmadd213ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 a8 05  dis=62 f2 75 28 a8 05 90
    {evex} vfmsub213ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 aa c0  dis=62 f2 75 28 aa c0
    {evex} vfmsub213ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 aa 05  dis=62 f2 75 28 aa 05 90
    {evex} vfnmadd213ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 ac c0  dis=62 f2 75 28 ac c0
    {evex} vfnmadd213ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 ac 05  dis=62 f2 75 28 ac 05 90
    {evex} vfnmsub213ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 ae c0  dis=62 f2 75 28 ae c0
    {evex} vfnmsub213ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 ae 05  dis=62 f2 75 28 ae 05 90
    {evex} vfmaddsub231ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 b6 c0  dis=62 f2 75 28 b6 c0
    {evex} vfmaddsub231ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 b6 05  dis=62 f2 75 28 b6 05 90
    {evex} vfmsubadd231ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 b7 c0  dis=62 f2 75 28 b7 c0
    {evex} vfmsubadd231ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 b7 05  dis=62 f2 75 28 b7 05 90
    {evex} vfmadd231ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 b8 c0  dis=62 f2 75 28 b8 c0
    {evex} vfmadd231ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 b8 05  dis=62 f2 75 28 b8 05 90
    {evex} vfmsub231ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 ba c0  dis=62 f2 75 28 ba c0
    {evex} vfmsub231ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 ba 05  dis=62 f2 75 28 ba 05 90
    {evex} vfnmadd231ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 bc c0  dis=62 f2 75 28 bc c0
    {evex} vfnmadd231ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 bc 05  dis=62 f2 75 28 bc 05 90
    {evex} vfnmsub231ps %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 be c0  dis=62 f2 75 28 be c0
    {evex} vfnmsub231ps 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 be 05  dis=62 f2 75 28 be 05 90
    {evex} vgf2p8mulb %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 cf c0  dis=62 f2 75 28 cf c0
    {evex} vgf2p8mulb 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 cf 05  dis=62 f2 75 28 cf 05 90
    {evex} vaesenc %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 dc c0  dis=62 f2 75 28 dc c0
    {evex} vaesenc 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 dc 05  dis=62 f2 75 28 dc 05 90
    {evex} vaesenclast %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 dd c0  dis=62 f2 75 28 dd c0
    {evex} vaesenclast 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 dd 05  dis=62 f2 75 28 dd 05 90
    {evex} vaesdec %ymm0,%ymm1,%ymm0    # gen=62 f2 75 28 de c0  dis=62 f2 75 28 de c0
    {evex} vaesdec 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 de 05  dis=62 f2 75 28 de 05 90
    {evex} vaesdeclast %ymm0,%ymm1,%ymm0 # gen=62 f2 75 28 df c0  dis=62 f2 75 28 df c0
    {evex} vaesdeclast 0x90909090,%ymm1,%ymm0 # gen=62 f2 75 28 df 05  dis=62 f2 75 28 df 05 90
    vpshufb %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 00 c0  dis=62 f2 75 48 00 c0
    vpshufb 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 00 05  dis=62 f2 75 48 00 05 90
    vpmaddubsw %zmm0,%zmm1,%zmm0        # gen=62 f2 75 48 04 c0  dis=62 f2 75 48 04 c0
    vpmaddubsw 0x90909090,%zmm1,%zmm0   # gen=62 f2 75 48 04 05  dis=62 f2 75 48 04 05 90
    vpmulhrsw %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 0b c0  dis=62 f2 75 48 0b c0
    vpmulhrsw 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 0b 05  dis=62 f2 75 48 0b 05 90
    vpermilps %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 0c c0  dis=62 f2 75 48 0c c0
    vpermilps 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 0c 05  dis=62 f2 75 48 0c 05 90
    vprorvd %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 14 c0  dis=62 f2 75 48 14 c0
    vprorvd 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 14 05  dis=62 f2 75 48 14 05 90
    vprolvd %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 15 c0  dis=62 f2 75 48 15 c0
    vprolvd 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 15 05  dis=62 f2 75 48 15 05 90
    vpermps %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 16 c0  dis=62 f2 75 48 16 c0
    vpermps 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 16 05  dis=62 f2 75 48 16 05 90
    vptestmb %zmm0,%zmm1,%k0            # gen=62 f2 75 48 26 c0  dis=62 f2 75 48 26 c0
    vptestmb 0x90909090,%zmm1,%k0       # gen=62 f2 75 48 26 05  dis=62 f2 75 48 26 05 90
    vptestmd %zmm0,%zmm1,%k0            # gen=62 f2 75 48 27 c0  dis=62 f2 75 48 27 c0
    vptestmd 0x90909090,%zmm1,%k0       # gen=62 f2 75 48 27 05  dis=62 f2 75 48 27 05 90
    vpackusdw %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 2b c0  dis=62 f2 75 48 2b c0
    vpackusdw 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 2b 05  dis=62 f2 75 48 2b 05 90
    vscalefps %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 2c c0  dis=62 f2 75 48 2c c0
    vscalefps 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 2c 05  dis=62 f2 75 48 2c 05 90
    vpermd %zmm0,%zmm1,%zmm0            # gen=62 f2 75 48 36 c0  dis=62 f2 75 48 36 c0
    vpermd 0x90909090,%zmm1,%zmm0       # gen=62 f2 75 48 36 05  dis=62 f2 75 48 36 05 90
    vpminsb %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 38 c0  dis=62 f2 75 48 38 c0
    vpminsb 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 38 05  dis=62 f2 75 48 38 05 90
    vpminsd %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 39 c0  dis=62 f2 75 48 39 c0
    vpminsd 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 39 05  dis=62 f2 75 48 39 05 90
    vpminuw %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 3a c0  dis=62 f2 75 48 3a c0
    vpminuw 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 3a 05  dis=62 f2 75 48 3a 05 90
    vpminud %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 3b c0  dis=62 f2 75 48 3b c0
    vpminud 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 3b 05  dis=62 f2 75 48 3b 05 90
    vpmaxsb %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 3c c0  dis=62 f2 75 48 3c c0
    vpmaxsb 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 3c 05  dis=62 f2 75 48 3c 05 90
    vpmaxsd %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 3d c0  dis=62 f2 75 48 3d c0
    vpmaxsd 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 3d 05  dis=62 f2 75 48 3d 05 90
    vpmaxuw %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 3e c0  dis=62 f2 75 48 3e c0
    vpmaxuw 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 3e 05  dis=62 f2 75 48 3e 05 90
    vpmaxud %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 3f c0  dis=62 f2 75 48 3f c0
    vpmaxud 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 3f 05  dis=62 f2 75 48 3f 05 90
    vpmulld %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 40 c0  dis=62 f2 75 48 40 c0
    vpmulld 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 40 05  dis=62 f2 75 48 40 05 90
    vpsrlvd %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 45 c0  dis=62 f2 75 48 45 c0
    vpsrlvd 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 45 05  dis=62 f2 75 48 45 05 90
    vpsravd %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 46 c0  dis=62 f2 75 48 46 c0
    vpsravd 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 46 05  dis=62 f2 75 48 46 05 90
    vpsllvd %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 47 c0  dis=62 f2 75 48 47 c0
    vpsllvd 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 47 05  dis=62 f2 75 48 47 05 90
    vpdpbusd %zmm0,%zmm1,%zmm0          # gen=62 f2 75 48 50 c0  dis=62 f2 75 48 50 c0
    vpdpbusd 0x90909090,%zmm1,%zmm0     # gen=62 f2 75 48 50 05  dis=62 f2 75 48 50 05 90
    vpdpbusds %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 51 c0  dis=62 f2 75 48 51 c0
    vpdpbusds 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 51 05  dis=62 f2 75 48 51 05 90
    vpdpwssd %zmm0,%zmm1,%zmm0          # gen=62 f2 75 48 52 c0  dis=62 f2 75 48 52 c0
    vpdpwssd 0x90909090,%zmm1,%zmm0     # gen=62 f2 75 48 52 05  dis=62 f2 75 48 52 05 90
    vpdpwssds %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 53 c0  dis=62 f2 75 48 53 c0
    vpdpwssds 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 53 05  dis=62 f2 75 48 53 05 90
    vpblendmd %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 64 c0  dis=62 f2 75 48 64 c0
    vpblendmd 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 64 05  dis=62 f2 75 48 64 05 90
    vblendmps %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 65 c0  dis=62 f2 75 48 65 c0
    vblendmps 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 65 05  dis=62 f2 75 48 65 05 90
    vpblendmb %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 66 c0  dis=62 f2 75 48 66 c0
    vpblendmb 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 66 05  dis=62 f2 75 48 66 05 90
    vpshldvd %zmm0,%zmm1,%zmm0          # gen=62 f2 75 48 71 c0  dis=62 f2 75 48 71 c0
    vpshldvd 0x90909090,%zmm1,%zmm0     # gen=62 f2 75 48 71 05  dis=62 f2 75 48 71 05 90
    vpshrdvd %zmm0,%zmm1,%zmm0          # gen=62 f2 75 48 73 c0  dis=62 f2 75 48 73 c0
    vpshrdvd 0x90909090,%zmm1,%zmm0     # gen=62 f2 75 48 73 05  dis=62 f2 75 48 73 05 90
    vpermi2b %zmm0,%zmm1,%zmm0          # gen=62 f2 75 48 75 c0  dis=62 f2 75 48 75 c0
    vpermi2b 0x90909090,%zmm1,%zmm0     # gen=62 f2 75 48 75 05  dis=62 f2 75 48 75 05 90
    vpermi2d %zmm0,%zmm1,%zmm0          # gen=62 f2 75 48 76 c0  dis=62 f2 75 48 76 c0
    vpermi2d 0x90909090,%zmm1,%zmm0     # gen=62 f2 75 48 76 05  dis=62 f2 75 48 76 05 90
    vpermi2ps %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 77 c0  dis=62 f2 75 48 77 c0
    vpermi2ps 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 77 05  dis=62 f2 75 48 77 05 90
    vpermt2b %zmm0,%zmm1,%zmm0          # gen=62 f2 75 48 7d c0  dis=62 f2 75 48 7d c0
    vpermt2b 0x90909090,%zmm1,%zmm0     # gen=62 f2 75 48 7d 05  dis=62 f2 75 48 7d 05 90
    vpermt2d %zmm0,%zmm1,%zmm0          # gen=62 f2 75 48 7e c0  dis=62 f2 75 48 7e c0
    vpermt2d 0x90909090,%zmm1,%zmm0     # gen=62 f2 75 48 7e 05  dis=62 f2 75 48 7e 05 90
    vpermt2ps %zmm0,%zmm1,%zmm0         # gen=62 f2 75 48 7f c0  dis=62 f2 75 48 7f c0
    vpermt2ps 0x90909090,%zmm1,%zmm0    # gen=62 f2 75 48 7f 05  dis=62 f2 75 48 7f 05 90
    vpermb %zmm0,%zmm1,%zmm0            # gen=62 f2 75 48 8d c0  dis=62 f2 75 48 8d c0
    vpermb 0x90909090,%zmm1,%zmm0       # gen=62 f2 75 48 8d 05  dis=62 f2 75 48 8d 05 90
    vpshufbitqmb %zmm0,%zmm1,%k0        # gen=62 f2 75 48 8f c0  dis=62 f2 75 48 8f c0
    vpshufbitqmb 0x90909090,%zmm1,%k0   # gen=62 f2 75 48 8f 05  dis=62 f2 75 48 8f 05 90
    vfmaddsub132ps %zmm0,%zmm1,%zmm0    # gen=62 f2 75 48 96 c0  dis=62 f2 75 48 96 c0
    vfmaddsub132ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 96 05  dis=62 f2 75 48 96 05 90
    vfmsubadd132ps %zmm0,%zmm1,%zmm0    # gen=62 f2 75 48 97 c0  dis=62 f2 75 48 97 c0
    vfmsubadd132ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 97 05  dis=62 f2 75 48 97 05 90
    vfmadd132ps %zmm0,%zmm1,%zmm0       # gen=62 f2 75 48 98 c0  dis=62 f2 75 48 98 c0
    vfmadd132ps 0x90909090,%zmm1,%zmm0  # gen=62 f2 75 48 98 05  dis=62 f2 75 48 98 05 90
    vfmsub132ps %zmm0,%zmm1,%zmm0       # gen=62 f2 75 48 9a c0  dis=62 f2 75 48 9a c0
    vfmsub132ps 0x90909090,%zmm1,%zmm0  # gen=62 f2 75 48 9a 05  dis=62 f2 75 48 9a 05 90
    vfnmadd132ps %zmm0,%zmm1,%zmm0      # gen=62 f2 75 48 9c c0  dis=62 f2 75 48 9c c0
    vfnmadd132ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 9c 05  dis=62 f2 75 48 9c 05 90
    vfnmsub132ps %zmm0,%zmm1,%zmm0      # gen=62 f2 75 48 9e c0  dis=62 f2 75 48 9e c0
    vfnmsub132ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 9e 05  dis=62 f2 75 48 9e 05 90
    vfmaddsub213ps %zmm0,%zmm1,%zmm0    # gen=62 f2 75 48 a6 c0  dis=62 f2 75 48 a6 c0
    vfmaddsub213ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 a6 05  dis=62 f2 75 48 a6 05 90
    vfmsubadd213ps %zmm0,%zmm1,%zmm0    # gen=62 f2 75 48 a7 c0  dis=62 f2 75 48 a7 c0
    vfmsubadd213ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 a7 05  dis=62 f2 75 48 a7 05 90
    vfmadd213ps %zmm0,%zmm1,%zmm0       # gen=62 f2 75 48 a8 c0  dis=62 f2 75 48 a8 c0
    vfmadd213ps 0x90909090,%zmm1,%zmm0  # gen=62 f2 75 48 a8 05  dis=62 f2 75 48 a8 05 90
    vfmsub213ps %zmm0,%zmm1,%zmm0       # gen=62 f2 75 48 aa c0  dis=62 f2 75 48 aa c0
    vfmsub213ps 0x90909090,%zmm1,%zmm0  # gen=62 f2 75 48 aa 05  dis=62 f2 75 48 aa 05 90
    vfnmadd213ps %zmm0,%zmm1,%zmm0      # gen=62 f2 75 48 ac c0  dis=62 f2 75 48 ac c0
    vfnmadd213ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 ac 05  dis=62 f2 75 48 ac 05 90
    vfnmsub213ps %zmm0,%zmm1,%zmm0      # gen=62 f2 75 48 ae c0  dis=62 f2 75 48 ae c0
    vfnmsub213ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 ae 05  dis=62 f2 75 48 ae 05 90
    vfmaddsub231ps %zmm0,%zmm1,%zmm0    # gen=62 f2 75 48 b6 c0  dis=62 f2 75 48 b6 c0
    vfmaddsub231ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 b6 05  dis=62 f2 75 48 b6 05 90
    vfmsubadd231ps %zmm0,%zmm1,%zmm0    # gen=62 f2 75 48 b7 c0  dis=62 f2 75 48 b7 c0
    vfmsubadd231ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 b7 05  dis=62 f2 75 48 b7 05 90
    vfmadd231ps %zmm0,%zmm1,%zmm0       # gen=62 f2 75 48 b8 c0  dis=62 f2 75 48 b8 c0
    vfmadd231ps 0x90909090,%zmm1,%zmm0  # gen=62 f2 75 48 b8 05  dis=62 f2 75 48 b8 05 90
    vfmsub231ps %zmm0,%zmm1,%zmm0       # gen=62 f2 75 48 ba c0  dis=62 f2 75 48 ba c0
    vfmsub231ps 0x90909090,%zmm1,%zmm0  # gen=62 f2 75 48 ba 05  dis=62 f2 75 48 ba 05 90
    vfnmadd231ps %zmm0,%zmm1,%zmm0      # gen=62 f2 75 48 bc c0  dis=62 f2 75 48 bc c0
    vfnmadd231ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 bc 05  dis=62 f2 75 48 bc 05 90
    vfnmsub231ps %zmm0,%zmm1,%zmm0      # gen=62 f2 75 48 be c0  dis=62 f2 75 48 be c0
    vfnmsub231ps 0x90909090,%zmm1,%zmm0 # gen=62 f2 75 48 be 05  dis=62 f2 75 48 be 05 90
    vgf2p8mulb %zmm0,%zmm1,%zmm0        # gen=62 f2 75 48 cf c0  dis=62 f2 75 48 cf c0
    vgf2p8mulb 0x90909090,%zmm1,%zmm0   # gen=62 f2 75 48 cf 05  dis=62 f2 75 48 cf 05 90
    vaesenc %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 dc c0  dis=62 f2 75 48 dc c0
    vaesenc 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 dc 05  dis=62 f2 75 48 dc 05 90
    vaesenclast %zmm0,%zmm1,%zmm0       # gen=62 f2 75 48 dd c0  dis=62 f2 75 48 dd c0
    vaesenclast 0x90909090,%zmm1,%zmm0  # gen=62 f2 75 48 dd 05  dis=62 f2 75 48 dd 05 90
    vaesdec %zmm0,%zmm1,%zmm0           # gen=62 f2 75 48 de c0  dis=62 f2 75 48 de c0
    vaesdec 0x90909090,%zmm1,%zmm0      # gen=62 f2 75 48 de 05  dis=62 f2 75 48 de 05 90
    vaesdeclast %zmm0,%zmm1,%zmm0       # gen=62 f2 75 48 df c0  dis=62 f2 75 48 df c0
    vaesdeclast 0x90909090,%zmm1,%zmm0  # gen=62 f2 75 48 df 05  dis=62 f2 75 48 df 05 90
    vptestnmb %xmm0,%xmm1,%k0           # gen=62 f2 76 08 26 c0  dis=62 f2 76 08 26 c0
    vptestnmb 0x90909090,%xmm1,%k0      # gen=62 f2 76 08 26 05  dis=62 f2 76 08 26 05 90
    vptestnmd %xmm0,%xmm1,%k0           # gen=62 f2 76 08 27 c0  dis=62 f2 76 08 27 c0
    vptestnmd 0x90909090,%xmm1,%k0      # gen=62 f2 76 08 27 05  dis=62 f2 76 08 27 05 90
    vdpbf16ps %xmm0,%xmm1,%xmm0         # gen=62 f2 76 08 52 c0  dis=62 f2 76 08 52 c0
    vdpbf16ps 0x90909090,%xmm1,%xmm0    # gen=62 f2 76 08 52 05  dis=62 f2 76 08 52 05 90
    vptestnmb %ymm0,%ymm1,%k0           # gen=62 f2 76 28 26 c0  dis=62 f2 76 28 26 c0
    vptestnmb 0x90909090,%ymm1,%k0      # gen=62 f2 76 28 26 05  dis=62 f2 76 28 26 05 90
    vptestnmd %ymm0,%ymm1,%k0           # gen=62 f2 76 28 27 c0  dis=62 f2 76 28 27 c0
    vptestnmd 0x90909090,%ymm1,%k0      # gen=62 f2 76 28 27 05  dis=62 f2 76 28 27 05 90
    vdpbf16ps %ymm0,%ymm1,%ymm0         # gen=62 f2 76 28 52 c0  dis=62 f2 76 28 52 c0
    vdpbf16ps 0x90909090,%ymm1,%ymm0    # gen=62 f2 76 28 52 05  dis=62 f2 76 28 52 05 90
    vptestnmb %zmm0,%zmm1,%k0           # gen=62 f2 76 48 26 c0  dis=62 f2 76 48 26 c0
    vptestnmb 0x90909090,%zmm1,%k0      # gen=62 f2 76 48 26 05  dis=62 f2 76 48 26 05 90
    vptestnmd %zmm0,%zmm1,%k0           # gen=62 f2 76 48 27 c0  dis=62 f2 76 48 27 c0
    vptestnmd 0x90909090,%zmm1,%k0      # gen=62 f2 76 48 27 05  dis=62 f2 76 48 27 05 90
    vdpbf16ps %zmm0,%zmm1,%zmm0         # gen=62 f2 76 48 52 c0  dis=62 f2 76 48 52 c0
    vdpbf16ps 0x90909090,%zmm1,%zmm0    # gen=62 f2 76 48 52 05  dis=62 f2 76 48 52 05 90
    vp2intersectd %xmm0,%xmm1,%k0       # gen=62 f2 77 08 68 c0  dis=62 f2 77 08 68 c0
    vp2intersectd 0x90909090,%xmm1,%k0  # gen=62 f2 77 08 68 05  dis=62 f2 77 08 68 05 90
    vcvtne2ps2bf16 %xmm0,%xmm1,%xmm0    # gen=62 f2 77 08 72 c0  dis=62 f2 77 08 72 c0
    vcvtne2ps2bf16 0x90909090,%xmm1,%xmm0 # gen=62 f2 77 08 72 05  dis=62 f2 77 08 72 05 90
    vp2intersectd %ymm0,%ymm1,%k0       # gen=62 f2 77 28 68 c0  dis=62 f2 77 28 68 c0
    vp2intersectd 0x90909090,%ymm1,%k0  # gen=62 f2 77 28 68 05  dis=62 f2 77 28 68 05 90
    vcvtne2ps2bf16 %ymm0,%ymm1,%ymm0    # gen=62 f2 77 28 72 c0  dis=62 f2 77 28 72 c0
    vcvtne2ps2bf16 0x90909090,%ymm1,%ymm0 # gen=62 f2 77 28 72 05  dis=62 f2 77 28 72 05 90
    vp2intersectd %zmm0,%zmm1,%k0       # gen=62 f2 77 48 68 c0  dis=62 f2 77 48 68 c0
    vp2intersectd 0x90909090,%zmm1,%k0  # gen=62 f2 77 48 68 05  dis=62 f2 77 48 68 05 90
    vcvtne2ps2bf16 %zmm0,%zmm1,%zmm0    # gen=62 f2 77 48 72 c0  dis=62 f2 77 48 72 c0
    vcvtne2ps2bf16 0x90909090,%zmm1,%zmm0 # gen=62 f2 77 48 72 05  dis=62 f2 77 48 72 05 90
    {evex} vpermilpd %xmm0,%xmm1,%xmm0  # gen=62 f2 f5 08 0d c0  dis=62 f2 f5 08 0d c0
    {evex} vpermilpd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 0d 05  dis=62 f2 f5 08 0d 05 90
    vpsrlvw %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 10 c0  dis=62 f2 f5 08 10 c0
    vpsrlvw 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 10 05  dis=62 f2 f5 08 10 05 90
    vpsravw %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 11 c0  dis=62 f2 f5 08 11 c0
    vpsravw 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 11 05  dis=62 f2 f5 08 11 05 90
    vpsllvw %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 12 c0  dis=62 f2 f5 08 12 c0
    vpsllvw 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 12 05  dis=62 f2 f5 08 12 05 90
    vprorvq %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 14 c0  dis=62 f2 f5 08 14 c0
    vprorvq 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 14 05  dis=62 f2 f5 08 14 05 90
    vprolvq %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 15 c0  dis=62 f2 f5 08 15 c0
    vprolvq 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 15 05  dis=62 f2 f5 08 15 05 90
    vptestmw %xmm0,%xmm1,%k0            # gen=62 f2 f5 08 26 c0  dis=62 f2 f5 08 26 c0
    vptestmw 0x90909090,%xmm1,%k0       # gen=62 f2 f5 08 26 05  dis=62 f2 f5 08 26 05 90
    vptestmq %xmm0,%xmm1,%k0            # gen=62 f2 f5 08 27 c0  dis=62 f2 f5 08 27 c0
    vptestmq 0x90909090,%xmm1,%k0       # gen=62 f2 f5 08 27 05  dis=62 f2 f5 08 27 05 90
    {evex} vpmuldq %xmm0,%xmm1,%xmm0    # gen=62 f2 f5 08 28 c0  dis=62 f2 f5 08 28 c0
    {evex} vpmuldq 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 28 05  dis=62 f2 f5 08 28 05 90
    vpcmpeqq %xmm0,%xmm1,%k0            # gen=62 f2 f5 08 29 c0  dis=62 f2 f5 08 29 c0
    vpcmpeqq 0x90909090,%xmm1,%k0       # gen=62 f2 f5 08 29 05  dis=62 f2 f5 08 29 05 90
    vscalefpd %xmm0,%xmm1,%xmm0         # gen=62 f2 f5 08 2c c0  dis=62 f2 f5 08 2c c0
    vscalefpd 0x90909090,%xmm1,%xmm0    # gen=62 f2 f5 08 2c 05  dis=62 f2 f5 08 2c 05 90
    vscalefsd %xmm0,%xmm1,%xmm0         # gen=62 f2 f5 08 2d c0  dis=62 f2 f5 08 2d c0
    vscalefsd 0x90909090,%xmm1,%xmm0    # gen=62 f2 f5 08 2d 05  dis=62 f2 f5 08 2d 05 90
    vpcmpgtq %xmm0,%xmm1,%k0            # gen=62 f2 f5 08 37 c0  dis=62 f2 f5 08 37 c0
    vpcmpgtq 0x90909090,%xmm1,%k0       # gen=62 f2 f5 08 37 05  dis=62 f2 f5 08 37 05 90
    vpminsq %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 39 c0  dis=62 f2 f5 08 39 c0
    vpminsq 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 39 05  dis=62 f2 f5 08 39 05 90
    vpminuq %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 3b c0  dis=62 f2 f5 08 3b c0
    vpminuq 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 3b 05  dis=62 f2 f5 08 3b 05 90
    vpmaxsq %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 3d c0  dis=62 f2 f5 08 3d c0
    vpmaxsq 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 3d 05  dis=62 f2 f5 08 3d 05 90
    vpmaxuq %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 3f c0  dis=62 f2 f5 08 3f c0
    vpmaxuq 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 3f 05  dis=62 f2 f5 08 3f 05 90
    vpmullq %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 40 c0  dis=62 f2 f5 08 40 c0
    vpmullq 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 40 05  dis=62 f2 f5 08 40 05 90
    vgetexpsd %xmm0,%xmm1,%xmm0         # gen=62 f2 f5 08 43 c0  dis=62 f2 f5 08 43 c0
    vgetexpsd 0x90909090,%xmm1,%xmm0    # gen=62 f2 f5 08 43 05  dis=62 f2 f5 08 43 05 90
    vpsravq %xmm0,%xmm1,%xmm0           # gen=62 f2 f5 08 46 c0  dis=62 f2 f5 08 46 c0
    vpsravq 0x90909090,%xmm1,%xmm0      # gen=62 f2 f5 08 46 05  dis=62 f2 f5 08 46 05 90
    vrcp14sd %xmm0,%xmm1,%xmm0          # gen=62 f2 f5 08 4d c0  dis=62 f2 f5 08 4d c0
    vrcp14sd 0x90909090,%xmm1,%xmm0     # gen=62 f2 f5 08 4d 05  dis=62 f2 f5 08 4d 05 90
    vrsqrt14sd %xmm0,%xmm1,%xmm0        # gen=62 f2 f5 08 4f c0  dis=62 f2 f5 08 4f c0
    vrsqrt14sd 0x90909090,%xmm1,%xmm0   # gen=62 f2 f5 08 4f 05  dis=62 f2 f5 08 4f 05 90
    vpblendmq %xmm0,%xmm1,%xmm0         # gen=62 f2 f5 08 64 c0  dis=62 f2 f5 08 64 c0
    vpblendmq 0x90909090,%xmm1,%xmm0    # gen=62 f2 f5 08 64 05  dis=62 f2 f5 08 64 05 90
    vblendmpd %xmm0,%xmm1,%xmm0         # gen=62 f2 f5 08 65 c0  dis=62 f2 f5 08 65 c0
    vblendmpd 0x90909090,%xmm1,%xmm0    # gen=62 f2 f5 08 65 05  dis=62 f2 f5 08 65 05 90
    vpblendmw %xmm0,%xmm1,%xmm0         # gen=62 f2 f5 08 66 c0  dis=62 f2 f5 08 66 c0
    vpblendmw 0x90909090,%xmm1,%xmm0    # gen=62 f2 f5 08 66 05  dis=62 f2 f5 08 66 05 90
    vpshldvw %xmm0,%xmm1,%xmm0          # gen=62 f2 f5 08 70 c0  dis=62 f2 f5 08 70 c0
    vpshldvw 0x90909090,%xmm1,%xmm0     # gen=62 f2 f5 08 70 05  dis=62 f2 f5 08 70 05 90
    vpshldvq %xmm0,%xmm1,%xmm0          # gen=62 f2 f5 08 71 c0  dis=62 f2 f5 08 71 c0
    vpshldvq 0x90909090,%xmm1,%xmm0     # gen=62 f2 f5 08 71 05  dis=62 f2 f5 08 71 05 90
    vpshrdvw %xmm0,%xmm1,%xmm0          # gen=62 f2 f5 08 72 c0  dis=62 f2 f5 08 72 c0
    vpshrdvw 0x90909090,%xmm1,%xmm0     # gen=62 f2 f5 08 72 05  dis=62 f2 f5 08 72 05 90
    vpshrdvq %xmm0,%xmm1,%xmm0          # gen=62 f2 f5 08 73 c0  dis=62 f2 f5 08 73 c0
    vpshrdvq 0x90909090,%xmm1,%xmm0     # gen=62 f2 f5 08 73 05  dis=62 f2 f5 08 73 05 90
    vpermi2w %xmm0,%xmm1,%xmm0          # gen=62 f2 f5 08 75 c0  dis=62 f2 f5 08 75 c0
    vpermi2w 0x90909090,%xmm1,%xmm0     # gen=62 f2 f5 08 75 05  dis=62 f2 f5 08 75 05 90
    vpermi2q %xmm0,%xmm1,%xmm0          # gen=62 f2 f5 08 76 c0  dis=62 f2 f5 08 76 c0
    vpermi2q 0x90909090,%xmm1,%xmm0     # gen=62 f2 f5 08 76 05  dis=62 f2 f5 08 76 05 90
    vpermi2pd %xmm0,%xmm1,%xmm0         # gen=62 f2 f5 08 77 c0  dis=62 f2 f5 08 77 c0
    vpermi2pd 0x90909090,%xmm1,%xmm0    # gen=62 f2 f5 08 77 05  dis=62 f2 f5 08 77 05 90
    vpermt2w %xmm0,%xmm1,%xmm0          # gen=62 f2 f5 08 7d c0  dis=62 f2 f5 08 7d c0
    vpermt2w 0x90909090,%xmm1,%xmm0     # gen=62 f2 f5 08 7d 05  dis=62 f2 f5 08 7d 05 90
    vpermt2q %xmm0,%xmm1,%xmm0          # gen=62 f2 f5 08 7e c0  dis=62 f2 f5 08 7e c0
    vpermt2q 0x90909090,%xmm1,%xmm0     # gen=62 f2 f5 08 7e 05  dis=62 f2 f5 08 7e 05 90
    vpermt2pd %xmm0,%xmm1,%xmm0         # gen=62 f2 f5 08 7f c0  dis=62 f2 f5 08 7f c0
    vpermt2pd 0x90909090,%xmm1,%xmm0    # gen=62 f2 f5 08 7f 05  dis=62 f2 f5 08 7f 05 90
    vpmultishiftqb %xmm0,%xmm1,%xmm0    # gen=62 f2 f5 08 83 c0  dis=62 f2 f5 08 83 c0
    vpmultishiftqb 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 83 05  dis=62 f2 f5 08 83 05 90
    vpermw %xmm0,%xmm1,%xmm0            # gen=62 f2 f5 08 8d c0  dis=62 f2 f5 08 8d c0
    vpermw 0x90909090,%xmm1,%xmm0       # gen=62 f2 f5 08 8d 05  dis=62 f2 f5 08 8d 05 90
    {evex} vfmaddsub132pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 96 c0  dis=62 f2 f5 08 96 c0
    {evex} vfmaddsub132pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 96 05  dis=62 f2 f5 08 96 05 90
    {evex} vfmsubadd132pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 97 c0  dis=62 f2 f5 08 97 c0
    {evex} vfmsubadd132pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 97 05  dis=62 f2 f5 08 97 05 90
    {evex} vfmadd132pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 98 c0  dis=62 f2 f5 08 98 c0
    {evex} vfmadd132pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 98 05  dis=62 f2 f5 08 98 05 90
    {evex} vfmadd132sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 99 c0  dis=62 f2 f5 08 99 c0
    {evex} vfmadd132sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 99 05  dis=62 f2 f5 08 99 05 90
    {evex} vfmsub132pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 9a c0  dis=62 f2 f5 08 9a c0
    {evex} vfmsub132pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 9a 05  dis=62 f2 f5 08 9a 05 90
    {evex} vfmsub132sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 9b c0  dis=62 f2 f5 08 9b c0
    {evex} vfmsub132sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 9b 05  dis=62 f2 f5 08 9b 05 90
    {evex} vfnmadd132pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 9c c0  dis=62 f2 f5 08 9c c0
    {evex} vfnmadd132pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 9c 05  dis=62 f2 f5 08 9c 05 90
    {evex} vfnmadd132sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 9d c0  dis=62 f2 f5 08 9d c0
    {evex} vfnmadd132sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 9d 05  dis=62 f2 f5 08 9d 05 90
    {evex} vfnmsub132pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 9e c0  dis=62 f2 f5 08 9e c0
    {evex} vfnmsub132pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 9e 05  dis=62 f2 f5 08 9e 05 90
    {evex} vfnmsub132sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 9f c0  dis=62 f2 f5 08 9f c0
    {evex} vfnmsub132sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 9f 05  dis=62 f2 f5 08 9f 05 90
    {evex} vfmaddsub213pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 a6 c0  dis=62 f2 f5 08 a6 c0
    {evex} vfmaddsub213pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 a6 05  dis=62 f2 f5 08 a6 05 90
    {evex} vfmsubadd213pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 a7 c0  dis=62 f2 f5 08 a7 c0
    {evex} vfmsubadd213pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 a7 05  dis=62 f2 f5 08 a7 05 90
    {evex} vfmadd213pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 a8 c0  dis=62 f2 f5 08 a8 c0
    {evex} vfmadd213pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 a8 05  dis=62 f2 f5 08 a8 05 90
    {evex} vfmadd213sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 a9 c0  dis=62 f2 f5 08 a9 c0
    {evex} vfmadd213sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 a9 05  dis=62 f2 f5 08 a9 05 90
    {evex} vfmsub213pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 aa c0  dis=62 f2 f5 08 aa c0
    {evex} vfmsub213pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 aa 05  dis=62 f2 f5 08 aa 05 90
    {evex} vfmsub213sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 ab c0  dis=62 f2 f5 08 ab c0
    {evex} vfmsub213sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 ab 05  dis=62 f2 f5 08 ab 05 90
    {evex} vfnmadd213pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 ac c0  dis=62 f2 f5 08 ac c0
    {evex} vfnmadd213pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 ac 05  dis=62 f2 f5 08 ac 05 90
    {evex} vfnmadd213sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 ad c0  dis=62 f2 f5 08 ad c0
    {evex} vfnmadd213sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 ad 05  dis=62 f2 f5 08 ad 05 90
    {evex} vfnmsub213pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 ae c0  dis=62 f2 f5 08 ae c0
    {evex} vfnmsub213pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 ae 05  dis=62 f2 f5 08 ae 05 90
    {evex} vfnmsub213sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 af c0  dis=62 f2 f5 08 af c0
    {evex} vfnmsub213sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 af 05  dis=62 f2 f5 08 af 05 90
    vpmadd52luq %xmm0,%xmm1,%xmm0       # gen=62 f2 f5 08 b4 c0  dis=62 f2 f5 08 b4 c0
    vpmadd52luq 0x90909090,%xmm1,%xmm0  # gen=62 f2 f5 08 b4 05  dis=62 f2 f5 08 b4 05 90
    vpmadd52huq %xmm0,%xmm1,%xmm0       # gen=62 f2 f5 08 b5 c0  dis=62 f2 f5 08 b5 c0
    vpmadd52huq 0x90909090,%xmm1,%xmm0  # gen=62 f2 f5 08 b5 05  dis=62 f2 f5 08 b5 05 90
    {evex} vfmaddsub231pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 b6 c0  dis=62 f2 f5 08 b6 c0
    {evex} vfmaddsub231pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 b6 05  dis=62 f2 f5 08 b6 05 90
    {evex} vfmsubadd231pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 b7 c0  dis=62 f2 f5 08 b7 c0
    {evex} vfmsubadd231pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 b7 05  dis=62 f2 f5 08 b7 05 90
    {evex} vfmadd231pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 b8 c0  dis=62 f2 f5 08 b8 c0
    {evex} vfmadd231pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 b8 05  dis=62 f2 f5 08 b8 05 90
    {evex} vfmadd231sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 b9 c0  dis=62 f2 f5 08 b9 c0
    {evex} vfmadd231sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 b9 05  dis=62 f2 f5 08 b9 05 90
    {evex} vfmsub231pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 ba c0  dis=62 f2 f5 08 ba c0
    {evex} vfmsub231pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 ba 05  dis=62 f2 f5 08 ba 05 90
    {evex} vfmsub231sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 bb c0  dis=62 f2 f5 08 bb c0
    {evex} vfmsub231sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 bb 05  dis=62 f2 f5 08 bb 05 90
    {evex} vfnmadd231pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 bc c0  dis=62 f2 f5 08 bc c0
    {evex} vfnmadd231pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 bc 05  dis=62 f2 f5 08 bc 05 90
    {evex} vfnmadd231sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 bd c0  dis=62 f2 f5 08 bd c0
    {evex} vfnmadd231sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 bd 05  dis=62 f2 f5 08 bd 05 90
    {evex} vfnmsub231pd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 be c0  dis=62 f2 f5 08 be c0
    {evex} vfnmsub231pd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 be 05  dis=62 f2 f5 08 be 05 90
    {evex} vfnmsub231sd %xmm0,%xmm1,%xmm0 # gen=62 f2 f5 08 bf c0  dis=62 f2 f5 08 bf c0
    {evex} vfnmsub231sd 0x90909090,%xmm1,%xmm0 # gen=62 f2 f5 08 bf 05  dis=62 f2 f5 08 bf 05 90
    vrcp28sd %xmm0,%xmm1,%xmm0          # gen=62 f2 f5 08 cb c0  dis=62 f2 f5 08 cb c0
    vrcp28sd 0x90909090,%xmm1,%xmm0     # gen=62 f2 f5 08 cb 05  dis=62 f2 f5 08 cb 05 90
    vrsqrt28sd %xmm0,%xmm1,%xmm0        # gen=62 f2 f5 08 cd c0  dis=62 f2 f5 08 cd c0
    vrsqrt28sd 0x90909090,%xmm1,%xmm0   # gen=62 f2 f5 08 cd 05  dis=62 f2 f5 08 cd 05 90
    {evex} vpermilpd %ymm0,%ymm1,%ymm0  # gen=62 f2 f5 28 0d c0  dis=62 f2 f5 28 0d c0
    {evex} vpermilpd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 0d 05  dis=62 f2 f5 28 0d 05 90
    vpsrlvw %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 10 c0  dis=62 f2 f5 28 10 c0
    vpsrlvw 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 10 05  dis=62 f2 f5 28 10 05 90
    vpsravw %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 11 c0  dis=62 f2 f5 28 11 c0
    vpsravw 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 11 05  dis=62 f2 f5 28 11 05 90
    vpsllvw %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 12 c0  dis=62 f2 f5 28 12 c0
    vpsllvw 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 12 05  dis=62 f2 f5 28 12 05 90
    vprorvq %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 14 c0  dis=62 f2 f5 28 14 c0
    vprorvq 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 14 05  dis=62 f2 f5 28 14 05 90
    vprolvq %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 15 c0  dis=62 f2 f5 28 15 c0
    vprolvq 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 15 05  dis=62 f2 f5 28 15 05 90
    {evex} vpermpd %ymm0,%ymm1,%ymm0    # gen=62 f2 f5 28 16 c0  dis=62 f2 f5 28 16 c0
    {evex} vpermpd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 16 05  dis=62 f2 f5 28 16 05 90
    vptestmw %ymm0,%ymm1,%k0            # gen=62 f2 f5 28 26 c0  dis=62 f2 f5 28 26 c0
    vptestmw 0x90909090,%ymm1,%k0       # gen=62 f2 f5 28 26 05  dis=62 f2 f5 28 26 05 90
    vptestmq %ymm0,%ymm1,%k0            # gen=62 f2 f5 28 27 c0  dis=62 f2 f5 28 27 c0
    vptestmq 0x90909090,%ymm1,%k0       # gen=62 f2 f5 28 27 05  dis=62 f2 f5 28 27 05 90
    {evex} vpmuldq %ymm0,%ymm1,%ymm0    # gen=62 f2 f5 28 28 c0  dis=62 f2 f5 28 28 c0
    {evex} vpmuldq 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 28 05  dis=62 f2 f5 28 28 05 90
    vpcmpeqq %ymm0,%ymm1,%k0            # gen=62 f2 f5 28 29 c0  dis=62 f2 f5 28 29 c0
    vpcmpeqq 0x90909090,%ymm1,%k0       # gen=62 f2 f5 28 29 05  dis=62 f2 f5 28 29 05 90
    vscalefpd %ymm0,%ymm1,%ymm0         # gen=62 f2 f5 28 2c c0  dis=62 f2 f5 28 2c c0
    vscalefpd 0x90909090,%ymm1,%ymm0    # gen=62 f2 f5 28 2c 05  dis=62 f2 f5 28 2c 05 90
    vpermq %ymm0,%ymm1,%ymm0            # gen=62 f2 f5 28 36 c0  dis=62 f2 f5 28 36 c0
    vpermq 0x90909090,%ymm1,%ymm0       # gen=62 f2 f5 28 36 05  dis=62 f2 f5 28 36 05 90
    vpcmpgtq %ymm0,%ymm1,%k0            # gen=62 f2 f5 28 37 c0  dis=62 f2 f5 28 37 c0
    vpcmpgtq 0x90909090,%ymm1,%k0       # gen=62 f2 f5 28 37 05  dis=62 f2 f5 28 37 05 90
    vpminsq %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 39 c0  dis=62 f2 f5 28 39 c0
    vpminsq 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 39 05  dis=62 f2 f5 28 39 05 90
    vpminuq %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 3b c0  dis=62 f2 f5 28 3b c0
    vpminuq 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 3b 05  dis=62 f2 f5 28 3b 05 90
    vpmaxsq %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 3d c0  dis=62 f2 f5 28 3d c0
    vpmaxsq 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 3d 05  dis=62 f2 f5 28 3d 05 90
    vpmaxuq %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 3f c0  dis=62 f2 f5 28 3f c0
    vpmaxuq 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 3f 05  dis=62 f2 f5 28 3f 05 90
    vpmullq %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 40 c0  dis=62 f2 f5 28 40 c0
    vpmullq 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 40 05  dis=62 f2 f5 28 40 05 90
    vpsravq %ymm0,%ymm1,%ymm0           # gen=62 f2 f5 28 46 c0  dis=62 f2 f5 28 46 c0
    vpsravq 0x90909090,%ymm1,%ymm0      # gen=62 f2 f5 28 46 05  dis=62 f2 f5 28 46 05 90
    vpblendmq %ymm0,%ymm1,%ymm0         # gen=62 f2 f5 28 64 c0  dis=62 f2 f5 28 64 c0
    vpblendmq 0x90909090,%ymm1,%ymm0    # gen=62 f2 f5 28 64 05  dis=62 f2 f5 28 64 05 90
    vblendmpd %ymm0,%ymm1,%ymm0         # gen=62 f2 f5 28 65 c0  dis=62 f2 f5 28 65 c0
    vblendmpd 0x90909090,%ymm1,%ymm0    # gen=62 f2 f5 28 65 05  dis=62 f2 f5 28 65 05 90
    vpblendmw %ymm0,%ymm1,%ymm0         # gen=62 f2 f5 28 66 c0  dis=62 f2 f5 28 66 c0
    vpblendmw 0x90909090,%ymm1,%ymm0    # gen=62 f2 f5 28 66 05  dis=62 f2 f5 28 66 05 90
    vpshldvw %ymm0,%ymm1,%ymm0          # gen=62 f2 f5 28 70 c0  dis=62 f2 f5 28 70 c0
    vpshldvw 0x90909090,%ymm1,%ymm0     # gen=62 f2 f5 28 70 05  dis=62 f2 f5 28 70 05 90
    vpshldvq %ymm0,%ymm1,%ymm0          # gen=62 f2 f5 28 71 c0  dis=62 f2 f5 28 71 c0
    vpshldvq 0x90909090,%ymm1,%ymm0     # gen=62 f2 f5 28 71 05  dis=62 f2 f5 28 71 05 90
    vpshrdvw %ymm0,%ymm1,%ymm0          # gen=62 f2 f5 28 72 c0  dis=62 f2 f5 28 72 c0
    vpshrdvw 0x90909090,%ymm1,%ymm0     # gen=62 f2 f5 28 72 05  dis=62 f2 f5 28 72 05 90
    vpshrdvq %ymm0,%ymm1,%ymm0          # gen=62 f2 f5 28 73 c0  dis=62 f2 f5 28 73 c0
    vpshrdvq 0x90909090,%ymm1,%ymm0     # gen=62 f2 f5 28 73 05  dis=62 f2 f5 28 73 05 90
    vpermi2w %ymm0,%ymm1,%ymm0          # gen=62 f2 f5 28 75 c0  dis=62 f2 f5 28 75 c0
    vpermi2w 0x90909090,%ymm1,%ymm0     # gen=62 f2 f5 28 75 05  dis=62 f2 f5 28 75 05 90
    vpermi2q %ymm0,%ymm1,%ymm0          # gen=62 f2 f5 28 76 c0  dis=62 f2 f5 28 76 c0
    vpermi2q 0x90909090,%ymm1,%ymm0     # gen=62 f2 f5 28 76 05  dis=62 f2 f5 28 76 05 90
    vpermi2pd %ymm0,%ymm1,%ymm0         # gen=62 f2 f5 28 77 c0  dis=62 f2 f5 28 77 c0
    vpermi2pd 0x90909090,%ymm1,%ymm0    # gen=62 f2 f5 28 77 05  dis=62 f2 f5 28 77 05 90
    vpermt2w %ymm0,%ymm1,%ymm0          # gen=62 f2 f5 28 7d c0  dis=62 f2 f5 28 7d c0
    vpermt2w 0x90909090,%ymm1,%ymm0     # gen=62 f2 f5 28 7d 05  dis=62 f2 f5 28 7d 05 90
    vpermt2q %ymm0,%ymm1,%ymm0          # gen=62 f2 f5 28 7e c0  dis=62 f2 f5 28 7e c0
    vpermt2q 0x90909090,%ymm1,%ymm0     # gen=62 f2 f5 28 7e 05  dis=62 f2 f5 28 7e 05 90
    vpermt2pd %ymm0,%ymm1,%ymm0         # gen=62 f2 f5 28 7f c0  dis=62 f2 f5 28 7f c0
    vpermt2pd 0x90909090,%ymm1,%ymm0    # gen=62 f2 f5 28 7f 05  dis=62 f2 f5 28 7f 05 90
    vpmultishiftqb %ymm0,%ymm1,%ymm0    # gen=62 f2 f5 28 83 c0  dis=62 f2 f5 28 83 c0
    vpmultishiftqb 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 83 05  dis=62 f2 f5 28 83 05 90
    vpermw %ymm0,%ymm1,%ymm0            # gen=62 f2 f5 28 8d c0  dis=62 f2 f5 28 8d c0
    vpermw 0x90909090,%ymm1,%ymm0       # gen=62 f2 f5 28 8d 05  dis=62 f2 f5 28 8d 05 90
    {evex} vfmaddsub132pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 96 c0  dis=62 f2 f5 28 96 c0
    {evex} vfmaddsub132pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 96 05  dis=62 f2 f5 28 96 05 90
    {evex} vfmsubadd132pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 97 c0  dis=62 f2 f5 28 97 c0
    {evex} vfmsubadd132pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 97 05  dis=62 f2 f5 28 97 05 90
    {evex} vfmadd132pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 98 c0  dis=62 f2 f5 28 98 c0
    {evex} vfmadd132pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 98 05  dis=62 f2 f5 28 98 05 90
    {evex} vfmsub132pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 9a c0  dis=62 f2 f5 28 9a c0
    {evex} vfmsub132pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 9a 05  dis=62 f2 f5 28 9a 05 90
    {evex} vfnmadd132pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 9c c0  dis=62 f2 f5 28 9c c0
    {evex} vfnmadd132pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 9c 05  dis=62 f2 f5 28 9c 05 90
    {evex} vfnmsub132pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 9e c0  dis=62 f2 f5 28 9e c0
    {evex} vfnmsub132pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 9e 05  dis=62 f2 f5 28 9e 05 90
    {evex} vfmaddsub213pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 a6 c0  dis=62 f2 f5 28 a6 c0
    {evex} vfmaddsub213pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 a6 05  dis=62 f2 f5 28 a6 05 90
    {evex} vfmsubadd213pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 a7 c0  dis=62 f2 f5 28 a7 c0
    {evex} vfmsubadd213pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 a7 05  dis=62 f2 f5 28 a7 05 90
    {evex} vfmadd213pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 a8 c0  dis=62 f2 f5 28 a8 c0
    {evex} vfmadd213pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 a8 05  dis=62 f2 f5 28 a8 05 90
    {evex} vfmsub213pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 aa c0  dis=62 f2 f5 28 aa c0
    {evex} vfmsub213pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 aa 05  dis=62 f2 f5 28 aa 05 90
    {evex} vfnmadd213pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 ac c0  dis=62 f2 f5 28 ac c0
    {evex} vfnmadd213pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 ac 05  dis=62 f2 f5 28 ac 05 90
    {evex} vfnmsub213pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 ae c0  dis=62 f2 f5 28 ae c0
    {evex} vfnmsub213pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 ae 05  dis=62 f2 f5 28 ae 05 90
    vpmadd52luq %ymm0,%ymm1,%ymm0       # gen=62 f2 f5 28 b4 c0  dis=62 f2 f5 28 b4 c0
    vpmadd52luq 0x90909090,%ymm1,%ymm0  # gen=62 f2 f5 28 b4 05  dis=62 f2 f5 28 b4 05 90
    vpmadd52huq %ymm0,%ymm1,%ymm0       # gen=62 f2 f5 28 b5 c0  dis=62 f2 f5 28 b5 c0
    vpmadd52huq 0x90909090,%ymm1,%ymm0  # gen=62 f2 f5 28 b5 05  dis=62 f2 f5 28 b5 05 90
    {evex} vfmaddsub231pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 b6 c0  dis=62 f2 f5 28 b6 c0
    {evex} vfmaddsub231pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 b6 05  dis=62 f2 f5 28 b6 05 90
    {evex} vfmsubadd231pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 b7 c0  dis=62 f2 f5 28 b7 c0
    {evex} vfmsubadd231pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 b7 05  dis=62 f2 f5 28 b7 05 90
    {evex} vfmadd231pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 b8 c0  dis=62 f2 f5 28 b8 c0
    {evex} vfmadd231pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 b8 05  dis=62 f2 f5 28 b8 05 90
    {evex} vfmsub231pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 ba c0  dis=62 f2 f5 28 ba c0
    {evex} vfmsub231pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 ba 05  dis=62 f2 f5 28 ba 05 90
    {evex} vfnmadd231pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 bc c0  dis=62 f2 f5 28 bc c0
    {evex} vfnmadd231pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 bc 05  dis=62 f2 f5 28 bc 05 90
    {evex} vfnmsub231pd %ymm0,%ymm1,%ymm0 # gen=62 f2 f5 28 be c0  dis=62 f2 f5 28 be c0
    {evex} vfnmsub231pd 0x90909090,%ymm1,%ymm0 # gen=62 f2 f5 28 be 05  dis=62 f2 f5 28 be 05 90
    vpermilpd %zmm0,%zmm1,%zmm0         # gen=62 f2 f5 48 0d c0  dis=62 f2 f5 48 0d c0
    vpermilpd 0x90909090,%zmm1,%zmm0    # gen=62 f2 f5 48 0d 05  dis=62 f2 f5 48 0d 05 90
    vpsrlvw %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 10 c0  dis=62 f2 f5 48 10 c0
    vpsrlvw 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 10 05  dis=62 f2 f5 48 10 05 90
    vpsravw %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 11 c0  dis=62 f2 f5 48 11 c0
    vpsravw 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 11 05  dis=62 f2 f5 48 11 05 90
    vpsllvw %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 12 c0  dis=62 f2 f5 48 12 c0
    vpsllvw 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 12 05  dis=62 f2 f5 48 12 05 90
    vprorvq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 14 c0  dis=62 f2 f5 48 14 c0
    vprorvq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 14 05  dis=62 f2 f5 48 14 05 90
    vprolvq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 15 c0  dis=62 f2 f5 48 15 c0
    vprolvq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 15 05  dis=62 f2 f5 48 15 05 90
    vpermpd %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 16 c0  dis=62 f2 f5 48 16 c0
    vpermpd 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 16 05  dis=62 f2 f5 48 16 05 90
    vptestmw %zmm0,%zmm1,%k0            # gen=62 f2 f5 48 26 c0  dis=62 f2 f5 48 26 c0
    vptestmw 0x90909090,%zmm1,%k0       # gen=62 f2 f5 48 26 05  dis=62 f2 f5 48 26 05 90
    vptestmq %zmm0,%zmm1,%k0            # gen=62 f2 f5 48 27 c0  dis=62 f2 f5 48 27 c0
    vptestmq 0x90909090,%zmm1,%k0       # gen=62 f2 f5 48 27 05  dis=62 f2 f5 48 27 05 90
    vpmuldq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 28 c0  dis=62 f2 f5 48 28 c0
    vpmuldq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 28 05  dis=62 f2 f5 48 28 05 90
    vpcmpeqq %zmm0,%zmm1,%k0            # gen=62 f2 f5 48 29 c0  dis=62 f2 f5 48 29 c0
    vpcmpeqq 0x90909090,%zmm1,%k0       # gen=62 f2 f5 48 29 05  dis=62 f2 f5 48 29 05 90
    vscalefpd %zmm0,%zmm1,%zmm0         # gen=62 f2 f5 48 2c c0  dis=62 f2 f5 48 2c c0
    vscalefpd 0x90909090,%zmm1,%zmm0    # gen=62 f2 f5 48 2c 05  dis=62 f2 f5 48 2c 05 90
    vpermq %zmm0,%zmm1,%zmm0            # gen=62 f2 f5 48 36 c0  dis=62 f2 f5 48 36 c0
    vpermq 0x90909090,%zmm1,%zmm0       # gen=62 f2 f5 48 36 05  dis=62 f2 f5 48 36 05 90
    vpcmpgtq %zmm0,%zmm1,%k0            # gen=62 f2 f5 48 37 c0  dis=62 f2 f5 48 37 c0
    vpcmpgtq 0x90909090,%zmm1,%k0       # gen=62 f2 f5 48 37 05  dis=62 f2 f5 48 37 05 90
    vpminsq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 39 c0  dis=62 f2 f5 48 39 c0
    vpminsq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 39 05  dis=62 f2 f5 48 39 05 90
    vpminuq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 3b c0  dis=62 f2 f5 48 3b c0
    vpminuq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 3b 05  dis=62 f2 f5 48 3b 05 90
    vpmaxsq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 3d c0  dis=62 f2 f5 48 3d c0
    vpmaxsq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 3d 05  dis=62 f2 f5 48 3d 05 90
    vpmaxuq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 3f c0  dis=62 f2 f5 48 3f c0
    vpmaxuq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 3f 05  dis=62 f2 f5 48 3f 05 90
    vpmullq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 40 c0  dis=62 f2 f5 48 40 c0
    vpmullq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 40 05  dis=62 f2 f5 48 40 05 90
    vpsrlvq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 45 c0  dis=62 f2 f5 48 45 c0
    vpsrlvq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 45 05  dis=62 f2 f5 48 45 05 90
    vpsravq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 46 c0  dis=62 f2 f5 48 46 c0
    vpsravq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 46 05  dis=62 f2 f5 48 46 05 90
    vpsllvq %zmm0,%zmm1,%zmm0           # gen=62 f2 f5 48 47 c0  dis=62 f2 f5 48 47 c0
    vpsllvq 0x90909090,%zmm1,%zmm0      # gen=62 f2 f5 48 47 05  dis=62 f2 f5 48 47 05 90
    vpblendmq %zmm0,%zmm1,%zmm0         # gen=62 f2 f5 48 64 c0  dis=62 f2 f5 48 64 c0
    vpblendmq 0x90909090,%zmm1,%zmm0    # gen=62 f2 f5 48 64 05  dis=62 f2 f5 48 64 05 90
    vblendmpd %zmm0,%zmm1,%zmm0         # gen=62 f2 f5 48 65 c0  dis=62 f2 f5 48 65 c0
    vblendmpd 0x90909090,%zmm1,%zmm0    # gen=62 f2 f5 48 65 05  dis=62 f2 f5 48 65 05 90
    vpblendmw %zmm0,%zmm1,%zmm0         # gen=62 f2 f5 48 66 c0  dis=62 f2 f5 48 66 c0
    vpblendmw 0x90909090,%zmm1,%zmm0    # gen=62 f2 f5 48 66 05  dis=62 f2 f5 48 66 05 90
    vpshldvw %zmm0,%zmm1,%zmm0          # gen=62 f2 f5 48 70 c0  dis=62 f2 f5 48 70 c0
    vpshldvw 0x90909090,%zmm1,%zmm0     # gen=62 f2 f5 48 70 05  dis=62 f2 f5 48 70 05 90
    vpshldvq %zmm0,%zmm1,%zmm0          # gen=62 f2 f5 48 71 c0  dis=62 f2 f5 48 71 c0
    vpshldvq 0x90909090,%zmm1,%zmm0     # gen=62 f2 f5 48 71 05  dis=62 f2 f5 48 71 05 90
    vpshrdvw %zmm0,%zmm1,%zmm0          # gen=62 f2 f5 48 72 c0  dis=62 f2 f5 48 72 c0
    vpshrdvw 0x90909090,%zmm1,%zmm0     # gen=62 f2 f5 48 72 05  dis=62 f2 f5 48 72 05 90
    vpshrdvq %zmm0,%zmm1,%zmm0          # gen=62 f2 f5 48 73 c0  dis=62 f2 f5 48 73 c0
    vpshrdvq 0x90909090,%zmm1,%zmm0     # gen=62 f2 f5 48 73 05  dis=62 f2 f5 48 73 05 90
    vpermi2w %zmm0,%zmm1,%zmm0          # gen=62 f2 f5 48 75 c0  dis=62 f2 f5 48 75 c0
    vpermi2w 0x90909090,%zmm1,%zmm0     # gen=62 f2 f5 48 75 05  dis=62 f2 f5 48 75 05 90
    vpermi2q %zmm0,%zmm1,%zmm0          # gen=62 f2 f5 48 76 c0  dis=62 f2 f5 48 76 c0
    vpermi2q 0x90909090,%zmm1,%zmm0     # gen=62 f2 f5 48 76 05  dis=62 f2 f5 48 76 05 90
    vpermi2pd %zmm0,%zmm1,%zmm0         # gen=62 f2 f5 48 77 c0  dis=62 f2 f5 48 77 c0
    vpermi2pd 0x90909090,%zmm1,%zmm0    # gen=62 f2 f5 48 77 05  dis=62 f2 f5 48 77 05 90
    vpermt2w %zmm0,%zmm1,%zmm0          # gen=62 f2 f5 48 7d c0  dis=62 f2 f5 48 7d c0
    vpermt2w 0x90909090,%zmm1,%zmm0     # gen=62 f2 f5 48 7d 05  dis=62 f2 f5 48 7d 05 90
    vpermt2q %zmm0,%zmm1,%zmm0          # gen=62 f2 f5 48 7e c0  dis=62 f2 f5 48 7e c0
    vpermt2q 0x90909090,%zmm1,%zmm0     # gen=62 f2 f5 48 7e 05  dis=62 f2 f5 48 7e 05 90
    vpermt2pd %zmm0,%zmm1,%zmm0         # gen=62 f2 f5 48 7f c0  dis=62 f2 f5 48 7f c0
    vpermt2pd 0x90909090,%zmm1,%zmm0    # gen=62 f2 f5 48 7f 05  dis=62 f2 f5 48 7f 05 90
    vpmultishiftqb %zmm0,%zmm1,%zmm0    # gen=62 f2 f5 48 83 c0  dis=62 f2 f5 48 83 c0
    vpmultishiftqb 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 83 05  dis=62 f2 f5 48 83 05 90
    vpermw %zmm0,%zmm1,%zmm0            # gen=62 f2 f5 48 8d c0  dis=62 f2 f5 48 8d c0
    vpermw 0x90909090,%zmm1,%zmm0       # gen=62 f2 f5 48 8d 05  dis=62 f2 f5 48 8d 05 90
    vfmaddsub132pd %zmm0,%zmm1,%zmm0    # gen=62 f2 f5 48 96 c0  dis=62 f2 f5 48 96 c0
    vfmaddsub132pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 96 05  dis=62 f2 f5 48 96 05 90
    vfmsubadd132pd %zmm0,%zmm1,%zmm0    # gen=62 f2 f5 48 97 c0  dis=62 f2 f5 48 97 c0
    vfmsubadd132pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 97 05  dis=62 f2 f5 48 97 05 90
    vfmadd132pd %zmm0,%zmm1,%zmm0       # gen=62 f2 f5 48 98 c0  dis=62 f2 f5 48 98 c0
    vfmadd132pd 0x90909090,%zmm1,%zmm0  # gen=62 f2 f5 48 98 05  dis=62 f2 f5 48 98 05 90
    vfmsub132pd %zmm0,%zmm1,%zmm0       # gen=62 f2 f5 48 9a c0  dis=62 f2 f5 48 9a c0
    vfmsub132pd 0x90909090,%zmm1,%zmm0  # gen=62 f2 f5 48 9a 05  dis=62 f2 f5 48 9a 05 90
    vfnmadd132pd %zmm0,%zmm1,%zmm0      # gen=62 f2 f5 48 9c c0  dis=62 f2 f5 48 9c c0
    vfnmadd132pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 9c 05  dis=62 f2 f5 48 9c 05 90
    vfnmsub132pd %zmm0,%zmm1,%zmm0      # gen=62 f2 f5 48 9e c0  dis=62 f2 f5 48 9e c0
    vfnmsub132pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 9e 05  dis=62 f2 f5 48 9e 05 90
    vfmaddsub213pd %zmm0,%zmm1,%zmm0    # gen=62 f2 f5 48 a6 c0  dis=62 f2 f5 48 a6 c0
    vfmaddsub213pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 a6 05  dis=62 f2 f5 48 a6 05 90
    vfmsubadd213pd %zmm0,%zmm1,%zmm0    # gen=62 f2 f5 48 a7 c0  dis=62 f2 f5 48 a7 c0
    vfmsubadd213pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 a7 05  dis=62 f2 f5 48 a7 05 90
    vfmadd213pd %zmm0,%zmm1,%zmm0       # gen=62 f2 f5 48 a8 c0  dis=62 f2 f5 48 a8 c0
    vfmadd213pd 0x90909090,%zmm1,%zmm0  # gen=62 f2 f5 48 a8 05  dis=62 f2 f5 48 a8 05 90
    vfmsub213pd %zmm0,%zmm1,%zmm0       # gen=62 f2 f5 48 aa c0  dis=62 f2 f5 48 aa c0
    vfmsub213pd 0x90909090,%zmm1,%zmm0  # gen=62 f2 f5 48 aa 05  dis=62 f2 f5 48 aa 05 90
    vfnmadd213pd %zmm0,%zmm1,%zmm0      # gen=62 f2 f5 48 ac c0  dis=62 f2 f5 48 ac c0
    vfnmadd213pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 ac 05  dis=62 f2 f5 48 ac 05 90
    vfnmsub213pd %zmm0,%zmm1,%zmm0      # gen=62 f2 f5 48 ae c0  dis=62 f2 f5 48 ae c0
    vfnmsub213pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 ae 05  dis=62 f2 f5 48 ae 05 90
    vpmadd52luq %zmm0,%zmm1,%zmm0       # gen=62 f2 f5 48 b4 c0  dis=62 f2 f5 48 b4 c0
    vpmadd52luq 0x90909090,%zmm1,%zmm0  # gen=62 f2 f5 48 b4 05  dis=62 f2 f5 48 b4 05 90
    vpmadd52huq %zmm0,%zmm1,%zmm0       # gen=62 f2 f5 48 b5 c0  dis=62 f2 f5 48 b5 c0
    vpmadd52huq 0x90909090,%zmm1,%zmm0  # gen=62 f2 f5 48 b5 05  dis=62 f2 f5 48 b5 05 90
    vfmaddsub231pd %zmm0,%zmm1,%zmm0    # gen=62 f2 f5 48 b6 c0  dis=62 f2 f5 48 b6 c0
    vfmaddsub231pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 b6 05  dis=62 f2 f5 48 b6 05 90
    vfmsubadd231pd %zmm0,%zmm1,%zmm0    # gen=62 f2 f5 48 b7 c0  dis=62 f2 f5 48 b7 c0
    vfmsubadd231pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 b7 05  dis=62 f2 f5 48 b7 05 90
    vfmadd231pd %zmm0,%zmm1,%zmm0       # gen=62 f2 f5 48 b8 c0  dis=62 f2 f5 48 b8 c0
    vfmadd231pd 0x90909090,%zmm1,%zmm0  # gen=62 f2 f5 48 b8 05  dis=62 f2 f5 48 b8 05 90
    vfmsub231pd %zmm0,%zmm1,%zmm0       # gen=62 f2 f5 48 ba c0  dis=62 f2 f5 48 ba c0
    vfmsub231pd 0x90909090,%zmm1,%zmm0  # gen=62 f2 f5 48 ba 05  dis=62 f2 f5 48 ba 05 90
    vfnmadd231pd %zmm0,%zmm1,%zmm0      # gen=62 f2 f5 48 bc c0  dis=62 f2 f5 48 bc c0
    vfnmadd231pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 bc 05  dis=62 f2 f5 48 bc 05 90
    vfnmsub231pd %zmm0,%zmm1,%zmm0      # gen=62 f2 f5 48 be c0  dis=62 f2 f5 48 be c0
    vfnmsub231pd 0x90909090,%zmm1,%zmm0 # gen=62 f2 f5 48 be 05  dis=62 f2 f5 48 be 05 90
    vptestnmw %xmm0,%xmm1,%k0           # gen=62 f2 f6 08 26 c0  dis=62 f2 f6 08 26 c0
    vptestnmw 0x90909090,%xmm1,%k0      # gen=62 f2 f6 08 26 05  dis=62 f2 f6 08 26 05 90
    vptestnmq %xmm0,%xmm1,%k0           # gen=62 f2 f6 08 27 c0  dis=62 f2 f6 08 27 c0
    vptestnmq 0x90909090,%xmm1,%k0      # gen=62 f2 f6 08 27 05  dis=62 f2 f6 08 27 05 90
    vptestnmw %ymm0,%ymm1,%k0           # gen=62 f2 f6 28 26 c0  dis=62 f2 f6 28 26 c0
    vptestnmw 0x90909090,%ymm1,%k0      # gen=62 f2 f6 28 26 05  dis=62 f2 f6 28 26 05 90
    vptestnmq %ymm0,%ymm1,%k0           # gen=62 f2 f6 28 27 c0  dis=62 f2 f6 28 27 c0
    vptestnmq 0x90909090,%ymm1,%k0      # gen=62 f2 f6 28 27 05  dis=62 f2 f6 28 27 05 90
    vptestnmw %zmm0,%zmm1,%k0           # gen=62 f2 f6 48 26 c0  dis=62 f2 f6 48 26 c0
    vptestnmw 0x90909090,%zmm1,%k0      # gen=62 f2 f6 48 26 05  dis=62 f2 f6 48 26 05 90
    vptestnmq %zmm0,%zmm1,%k0           # gen=62 f2 f6 48 27 c0  dis=62 f2 f6 48 27 c0
    vptestnmq 0x90909090,%zmm1,%k0      # gen=62 f2 f6 48 27 05  dis=62 f2 f6 48 27 05 90
    vp2intersectq %xmm0,%xmm1,%k0       # gen=62 f2 f7 08 68 c0  dis=62 f2 f7 08 68 c0
    vp2intersectq 0x90909090,%xmm1,%k0  # gen=62 f2 f7 08 68 05  dis=62 f2 f7 08 68 05 90
    vp2intersectq %ymm0,%ymm1,%k0       # gen=62 f2 f7 28 68 c0  dis=62 f2 f7 28 68 c0
    vp2intersectq 0x90909090,%ymm1,%k0  # gen=62 f2 f7 28 68 05  dis=62 f2 f7 28 68 05 90
    vp2intersectq %zmm0,%zmm1,%k0       # gen=62 f2 f7 48 68 c0  dis=62 f2 f7 48 68 c0
    vp2intersectq 0x90909090,%zmm1,%k0  # gen=62 f2 f7 48 68 05  dis=62 f2 f7 48 68 05 90
    vrndscalesh $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 74 08 0a c0  dis=62 f3 74 08 0a c0 90
    vrndscalesh $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 74 08 0a 05  dis=62 f3 74 08 0a 05 90
    vgetmantsh $0x90,%xmm0,%xmm1,%xmm0  # gen=62 f3 74 08 27 c0  dis=62 f3 74 08 27 c0 90
    vgetmantsh $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 74 08 27 05  dis=62 f3 74 08 27 05 90
    vdbpsadbw $0x90,%xmm0,%xmm1,%xmm0   # gen=62 f3 74 08 42 c0  dis=62 f3 74 08 42 c0 90
    vdbpsadbw $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 74 08 42 05  dis=62 f3 74 08 42 05 90
    vreducesh $0x90,%xmm0,%xmm1,%xmm0   # gen=62 f3 74 08 57 c0  dis=62 f3 74 08 57 c0 90
    vreducesh $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 74 08 57 05  dis=62 f3 74 08 57 05 90
    vcmpph $0x90,%xmm0,%xmm1,%k0        # gen=62 f3 74 08 c2 c0  dis=62 f3 74 08 c2 c0 90
    vcmpph $0x90,0x90909090,%xmm1,%k0   # gen=62 f3 74 08 c2 05  dis=62 f3 74 08 c2 05 90
    vdbpsadbw $0x90,%ymm0,%ymm1,%ymm0   # gen=62 f3 74 28 42 c0  dis=62 f3 74 28 42 c0 90
    vdbpsadbw $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 74 28 42 05  dis=62 f3 74 28 42 05 90
    vcmpph $0x90,%ymm0,%ymm1,%k0        # gen=62 f3 74 28 c2 c0  dis=62 f3 74 28 c2 c0 90
    vcmpph $0x90,0x90909090,%ymm1,%k0   # gen=62 f3 74 28 c2 05  dis=62 f3 74 28 c2 05 90
    vdbpsadbw $0x90,%zmm0,%zmm1,%zmm0   # gen=62 f3 74 48 42 c0  dis=62 f3 74 48 42 c0 90
    vdbpsadbw $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 74 48 42 05  dis=62 f3 74 48 42 05 90
    vcmpph $0x90,%zmm0,%zmm1,%k0        # gen=62 f3 74 48 c2 c0  dis=62 f3 74 48 c2 c0 90
    vcmpph $0x90,0x90909090,%zmm1,%k0   # gen=62 f3 74 48 c2 05  dis=62 f3 74 48 c2 05 90
    valignd $0x90,%xmm0,%xmm1,%xmm0     # gen=62 f3 75 08 03 c0  dis=62 f3 75 08 03 c0 90
    valignd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 03 05  dis=62 f3 75 08 03 05 90
    vrndscaless $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 75 08 0a c0  dis=62 f3 75 08 0a c0 90
    vrndscaless $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 0a 05  dis=62 f3 75 08 0a 05 90
    {evex} vpalignr $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 75 08 0f c0  dis=62 f3 75 08 0f c0 90
    {evex} vpalignr $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 0f 05  dis=62 f3 75 08 0f 05 90
    vpcmpud $0x90,%xmm0,%xmm1,%k0       # gen=62 f3 75 08 1e c0  dis=62 f3 75 08 1e c0 90
    vpcmpud $0x90,0x90909090,%xmm1,%k0  # gen=62 f3 75 08 1e 05  dis=62 f3 75 08 1e 05 90
    vpcmpd $0x90,%xmm0,%xmm1,%k0        # gen=62 f3 75 08 1f c0  dis=62 f3 75 08 1f c0 90
    vpcmpd $0x90,0x90909090,%xmm1,%k0   # gen=62 f3 75 08 1f 05  dis=62 f3 75 08 1f 05 90
    {evex} vpinsrb $0x90,%eax,%xmm1,%xmm0 # gen=62 f3 75 08 20 c0  dis=62 f3 75 08 20 c0 90
    {evex} vpinsrb $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 20 05  dis=62 f3 75 08 20 05 90
    {evex} vinsertps $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 75 08 21 c0  dis=62 f3 75 08 21 c0 90
    {evex} vinsertps $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 21 05  dis=62 f3 75 08 21 05 90
    {evex} vpinsrd $0x90,%eax,%xmm1,%xmm0 # gen=62 f3 75 08 22 c0  dis=62 f3 75 08 22 c0 90
    {evex} vpinsrd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 22 05  dis=62 f3 75 08 22 05 90
    vpternlogd $0x90,%xmm0,%xmm1,%xmm0  # gen=62 f3 75 08 25 c0  dis=62 f3 75 08 25 c0 90
    vpternlogd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 25 05  dis=62 f3 75 08 25 05 90
    vgetmantss $0x90,%xmm0,%xmm1,%xmm0  # gen=62 f3 75 08 27 c0  dis=62 f3 75 08 27 c0 90
    vgetmantss $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 27 05  dis=62 f3 75 08 27 05 90
    vpcmpub $0x90,%xmm0,%xmm1,%k0       # gen=62 f3 75 08 3e c0  dis=62 f3 75 08 3e c0 90
    vpcmpub $0x90,0x90909090,%xmm1,%k0  # gen=62 f3 75 08 3e 05  dis=62 f3 75 08 3e 05 90
    vpcmpb $0x90,%xmm0,%xmm1,%k0        # gen=62 f3 75 08 3f c0  dis=62 f3 75 08 3f c0 90
    vpcmpb $0x90,0x90909090,%xmm1,%k0   # gen=62 f3 75 08 3f 05  dis=62 f3 75 08 3f 05 90
    {evex} vpclmulqdq $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 75 08 44 c0  dis=62 f3 75 08 44 c0 90
    {evex} vpclmulqdq $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 44 05  dis=62 f3 75 08 44 05 90
    vrangeps $0x90,%xmm0,%xmm1,%xmm0    # gen=62 f3 75 08 50 c0  dis=62 f3 75 08 50 c0 90
    vrangeps $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 50 05  dis=62 f3 75 08 50 05 90
    vrangess $0x90,%xmm0,%xmm1,%xmm0    # gen=62 f3 75 08 51 c0  dis=62 f3 75 08 51 c0 90
    vrangess $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 51 05  dis=62 f3 75 08 51 05 90
    vfixupimmps $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 75 08 54 c0  dis=62 f3 75 08 54 c0 90
    vfixupimmps $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 54 05  dis=62 f3 75 08 54 05 90
    vfixupimmss $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 75 08 55 c0  dis=62 f3 75 08 55 c0 90
    vfixupimmss $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 55 05  dis=62 f3 75 08 55 05 90
    vreducess $0x90,%xmm0,%xmm1,%xmm0   # gen=62 f3 75 08 57 c0  dis=62 f3 75 08 57 c0 90
    vreducess $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 57 05  dis=62 f3 75 08 57 05 90
    vpshldd $0x90,%xmm0,%xmm1,%xmm0     # gen=62 f3 75 08 71 c0  dis=62 f3 75 08 71 c0 90
    vpshldd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 71 05  dis=62 f3 75 08 71 05 90
    vpshrdd $0x90,%xmm0,%xmm1,%xmm0     # gen=62 f3 75 08 73 c0  dis=62 f3 75 08 73 c0 90
    vpshrdd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 75 08 73 05  dis=62 f3 75 08 73 05 90
    valignd $0x90,%ymm0,%ymm1,%ymm0     # gen=62 f3 75 28 03 c0  dis=62 f3 75 28 03 c0 90
    valignd $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 03 05  dis=62 f3 75 28 03 05 90
    {evex} vpalignr $0x90,%ymm0,%ymm1,%ymm0 # gen=62 f3 75 28 0f c0  dis=62 f3 75 28 0f c0 90
    {evex} vpalignr $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 0f 05  dis=62 f3 75 28 0f 05 90
    vinsertf32x4 $0x90,%xmm0,%ymm1,%ymm0 # gen=62 f3 75 28 18 c0  dis=62 f3 75 28 18 c0 90
    vinsertf32x4 $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 18 05  dis=62 f3 75 28 18 05 90
    vpcmpud $0x90,%ymm0,%ymm1,%k0       # gen=62 f3 75 28 1e c0  dis=62 f3 75 28 1e c0 90
    vpcmpud $0x90,0x90909090,%ymm1,%k0  # gen=62 f3 75 28 1e 05  dis=62 f3 75 28 1e 05 90
    vpcmpd $0x90,%ymm0,%ymm1,%k0        # gen=62 f3 75 28 1f c0  dis=62 f3 75 28 1f c0 90
    vpcmpd $0x90,0x90909090,%ymm1,%k0   # gen=62 f3 75 28 1f 05  dis=62 f3 75 28 1f 05 90
    vshuff32x4 $0x90,%ymm0,%ymm1,%ymm0  # gen=62 f3 75 28 23 c0  dis=62 f3 75 28 23 c0 90
    vshuff32x4 $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 23 05  dis=62 f3 75 28 23 05 90
    vpternlogd $0x90,%ymm0,%ymm1,%ymm0  # gen=62 f3 75 28 25 c0  dis=62 f3 75 28 25 c0 90
    vpternlogd $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 25 05  dis=62 f3 75 28 25 05 90
    vinserti32x4 $0x90,%xmm0,%ymm1,%ymm0 # gen=62 f3 75 28 38 c0  dis=62 f3 75 28 38 c0 90
    vinserti32x4 $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 38 05  dis=62 f3 75 28 38 05 90
    vpcmpub $0x90,%ymm0,%ymm1,%k0       # gen=62 f3 75 28 3e c0  dis=62 f3 75 28 3e c0 90
    vpcmpub $0x90,0x90909090,%ymm1,%k0  # gen=62 f3 75 28 3e 05  dis=62 f3 75 28 3e 05 90
    vpcmpb $0x90,%ymm0,%ymm1,%k0        # gen=62 f3 75 28 3f c0  dis=62 f3 75 28 3f c0 90
    vpcmpb $0x90,0x90909090,%ymm1,%k0   # gen=62 f3 75 28 3f 05  dis=62 f3 75 28 3f 05 90
    vshufi32x4 $0x90,%ymm0,%ymm1,%ymm0  # gen=62 f3 75 28 43 c0  dis=62 f3 75 28 43 c0 90
    vshufi32x4 $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 43 05  dis=62 f3 75 28 43 05 90
    {evex} vpclmulqdq $0x90,%ymm0,%ymm1,%ymm0 # gen=62 f3 75 28 44 c0  dis=62 f3 75 28 44 c0 90
    {evex} vpclmulqdq $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 44 05  dis=62 f3 75 28 44 05 90
    vrangeps $0x90,%ymm0,%ymm1,%ymm0    # gen=62 f3 75 28 50 c0  dis=62 f3 75 28 50 c0 90
    vrangeps $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 50 05  dis=62 f3 75 28 50 05 90
    vfixupimmps $0x90,%ymm0,%ymm1,%ymm0 # gen=62 f3 75 28 54 c0  dis=62 f3 75 28 54 c0 90
    vfixupimmps $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 54 05  dis=62 f3 75 28 54 05 90
    vpshldd $0x90,%ymm0,%ymm1,%ymm0     # gen=62 f3 75 28 71 c0  dis=62 f3 75 28 71 c0 90
    vpshldd $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 71 05  dis=62 f3 75 28 71 05 90
    vpshrdd $0x90,%ymm0,%ymm1,%ymm0     # gen=62 f3 75 28 73 c0  dis=62 f3 75 28 73 c0 90
    vpshrdd $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 75 28 73 05  dis=62 f3 75 28 73 05 90
    valignd $0x90,%zmm0,%zmm1,%zmm0     # gen=62 f3 75 48 03 c0  dis=62 f3 75 48 03 c0 90
    valignd $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 03 05  dis=62 f3 75 48 03 05 90
    vpalignr $0x90,%zmm0,%zmm1,%zmm0    # gen=62 f3 75 48 0f c0  dis=62 f3 75 48 0f c0 90
    vpalignr $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 0f 05  dis=62 f3 75 48 0f 05 90
    vinsertf32x4 $0x90,%xmm0,%zmm1,%zmm0 # gen=62 f3 75 48 18 c0  dis=62 f3 75 48 18 c0 90
    vinsertf32x4 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 18 05  dis=62 f3 75 48 18 05 90
    vinsertf32x8 $0x90,%ymm0,%zmm1,%zmm0 # gen=62 f3 75 48 1a c0  dis=62 f3 75 48 1a c0 90
    vinsertf32x8 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 1a 05  dis=62 f3 75 48 1a 05 90
    vpcmpud $0x90,%zmm0,%zmm1,%k0       # gen=62 f3 75 48 1e c0  dis=62 f3 75 48 1e c0 90
    vpcmpud $0x90,0x90909090,%zmm1,%k0  # gen=62 f3 75 48 1e 05  dis=62 f3 75 48 1e 05 90
    vpcmpd $0x90,%zmm0,%zmm1,%k0        # gen=62 f3 75 48 1f c0  dis=62 f3 75 48 1f c0 90
    vpcmpd $0x90,0x90909090,%zmm1,%k0   # gen=62 f3 75 48 1f 05  dis=62 f3 75 48 1f 05 90
    vshuff32x4 $0x90,%zmm0,%zmm1,%zmm0  # gen=62 f3 75 48 23 c0  dis=62 f3 75 48 23 c0 90
    vshuff32x4 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 23 05  dis=62 f3 75 48 23 05 90
    vpternlogd $0x90,%zmm0,%zmm1,%zmm0  # gen=62 f3 75 48 25 c0  dis=62 f3 75 48 25 c0 90
    vpternlogd $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 25 05  dis=62 f3 75 48 25 05 90
    vinserti32x4 $0x90,%xmm0,%zmm1,%zmm0 # gen=62 f3 75 48 38 c0  dis=62 f3 75 48 38 c0 90
    vinserti32x4 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 38 05  dis=62 f3 75 48 38 05 90
    vinserti32x8 $0x90,%ymm0,%zmm1,%zmm0 # gen=62 f3 75 48 3a c0  dis=62 f3 75 48 3a c0 90
    vinserti32x8 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 3a 05  dis=62 f3 75 48 3a 05 90
    vpcmpub $0x90,%zmm0,%zmm1,%k0       # gen=62 f3 75 48 3e c0  dis=62 f3 75 48 3e c0 90
    vpcmpub $0x90,0x90909090,%zmm1,%k0  # gen=62 f3 75 48 3e 05  dis=62 f3 75 48 3e 05 90
    vpcmpb $0x90,%zmm0,%zmm1,%k0        # gen=62 f3 75 48 3f c0  dis=62 f3 75 48 3f c0 90
    vpcmpb $0x90,0x90909090,%zmm1,%k0   # gen=62 f3 75 48 3f 05  dis=62 f3 75 48 3f 05 90
    vshufi32x4 $0x90,%zmm0,%zmm1,%zmm0  # gen=62 f3 75 48 43 c0  dis=62 f3 75 48 43 c0 90
    vshufi32x4 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 43 05  dis=62 f3 75 48 43 05 90
    vpclmulqdq $0x90,%zmm0,%zmm1,%zmm0  # gen=62 f3 75 48 44 c0  dis=62 f3 75 48 44 c0 90
    vpclmulqdq $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 44 05  dis=62 f3 75 48 44 05 90
    vrangeps $0x90,%zmm0,%zmm1,%zmm0    # gen=62 f3 75 48 50 c0  dis=62 f3 75 48 50 c0 90
    vrangeps $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 50 05  dis=62 f3 75 48 50 05 90
    vfixupimmps $0x90,%zmm0,%zmm1,%zmm0 # gen=62 f3 75 48 54 c0  dis=62 f3 75 48 54 c0 90
    vfixupimmps $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 54 05  dis=62 f3 75 48 54 05 90
    vpshldd $0x90,%zmm0,%zmm1,%zmm0     # gen=62 f3 75 48 71 c0  dis=62 f3 75 48 71 c0 90
    vpshldd $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 71 05  dis=62 f3 75 48 71 05 90
    vpshrdd $0x90,%zmm0,%zmm1,%zmm0     # gen=62 f3 75 48 73 c0  dis=62 f3 75 48 73 c0 90
    vpshrdd $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 75 48 73 05  dis=62 f3 75 48 73 05 90
    vcmpsh $0x90,%xmm0,%xmm1,%k0        # gen=62 f3 76 08 c2 c0  dis=62 f3 76 08 c2 c0 90
    vcmpsh $0x90,0x90909090,%xmm1,%k0   # gen=62 f3 76 08 c2 05  dis=62 f3 76 08 c2 05 90
    vpshldw $0x90,%xmm0,%xmm1,%xmm0     # gen=62 f3 f4 08 70 c0  dis=62 f3 f4 08 70 c0 90
    vpshldw $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f4 08 70 05  dis=62 f3 f4 08 70 05 90
    vpshrdw $0x90,%xmm0,%xmm1,%xmm0     # gen=62 f3 f4 08 72 c0  dis=62 f3 f4 08 72 c0 90
    vpshrdw $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f4 08 72 05  dis=62 f3 f4 08 72 05 90
    vpshldw $0x90,%ymm0,%ymm1,%ymm0     # gen=62 f3 f4 28 70 c0  dis=62 f3 f4 28 70 c0 90
    vpshldw $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f4 28 70 05  dis=62 f3 f4 28 70 05 90
    vpshrdw $0x90,%ymm0,%ymm1,%ymm0     # gen=62 f3 f4 28 72 c0  dis=62 f3 f4 28 72 c0 90
    vpshrdw $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f4 28 72 05  dis=62 f3 f4 28 72 05 90
    vpshldw $0x90,%zmm0,%zmm1,%zmm0     # gen=62 f3 f4 48 70 c0  dis=62 f3 f4 48 70 c0 90
    vpshldw $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f4 48 70 05  dis=62 f3 f4 48 70 05 90
    vpshrdw $0x90,%zmm0,%zmm1,%zmm0     # gen=62 f3 f4 48 72 c0  dis=62 f3 f4 48 72 c0 90
    vpshrdw $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f4 48 72 05  dis=62 f3 f4 48 72 05 90
    valignq $0x90,%xmm0,%xmm1,%xmm0     # gen=62 f3 f5 08 03 c0  dis=62 f3 f5 08 03 c0 90
    valignq $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 03 05  dis=62 f3 f5 08 03 05 90
    vrndscalesd $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 f5 08 0b c0  dis=62 f3 f5 08 0b c0 90
    vrndscalesd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 0b 05  dis=62 f3 f5 08 0b 05 90
    vpcmpuq $0x90,%xmm0,%xmm1,%k0       # gen=62 f3 f5 08 1e c0  dis=62 f3 f5 08 1e c0 90
    vpcmpuq $0x90,0x90909090,%xmm1,%k0  # gen=62 f3 f5 08 1e 05  dis=62 f3 f5 08 1e 05 90
    vpcmpq $0x90,%xmm0,%xmm1,%k0        # gen=62 f3 f5 08 1f c0  dis=62 f3 f5 08 1f c0 90
    vpcmpq $0x90,0x90909090,%xmm1,%k0   # gen=62 f3 f5 08 1f 05  dis=62 f3 f5 08 1f 05 90
    vpternlogq $0x90,%xmm0,%xmm1,%xmm0  # gen=62 f3 f5 08 25 c0  dis=62 f3 f5 08 25 c0 90
    vpternlogq $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 25 05  dis=62 f3 f5 08 25 05 90
    vgetmantsd $0x90,%xmm0,%xmm1,%xmm0  # gen=62 f3 f5 08 27 c0  dis=62 f3 f5 08 27 c0 90
    vgetmantsd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 27 05  dis=62 f3 f5 08 27 05 90
    vpcmpuw $0x90,%xmm0,%xmm1,%k0       # gen=62 f3 f5 08 3e c0  dis=62 f3 f5 08 3e c0 90
    vpcmpuw $0x90,0x90909090,%xmm1,%k0  # gen=62 f3 f5 08 3e 05  dis=62 f3 f5 08 3e 05 90
    vpcmpw $0x90,%xmm0,%xmm1,%k0        # gen=62 f3 f5 08 3f c0  dis=62 f3 f5 08 3f c0 90
    vpcmpw $0x90,0x90909090,%xmm1,%k0   # gen=62 f3 f5 08 3f 05  dis=62 f3 f5 08 3f 05 90
    vrangepd $0x90,%xmm0,%xmm1,%xmm0    # gen=62 f3 f5 08 50 c0  dis=62 f3 f5 08 50 c0 90
    vrangepd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 50 05  dis=62 f3 f5 08 50 05 90
    vrangesd $0x90,%xmm0,%xmm1,%xmm0    # gen=62 f3 f5 08 51 c0  dis=62 f3 f5 08 51 c0 90
    vrangesd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 51 05  dis=62 f3 f5 08 51 05 90
    vfixupimmpd $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 f5 08 54 c0  dis=62 f3 f5 08 54 c0 90
    vfixupimmpd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 54 05  dis=62 f3 f5 08 54 05 90
    vfixupimmsd $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 f5 08 55 c0  dis=62 f3 f5 08 55 c0 90
    vfixupimmsd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 55 05  dis=62 f3 f5 08 55 05 90
    vreducesd $0x90,%xmm0,%xmm1,%xmm0   # gen=62 f3 f5 08 57 c0  dis=62 f3 f5 08 57 c0 90
    vreducesd $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 57 05  dis=62 f3 f5 08 57 05 90
    vpshldq $0x90,%xmm0,%xmm1,%xmm0     # gen=62 f3 f5 08 71 c0  dis=62 f3 f5 08 71 c0 90
    vpshldq $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 71 05  dis=62 f3 f5 08 71 05 90
    vpshrdq $0x90,%xmm0,%xmm1,%xmm0     # gen=62 f3 f5 08 73 c0  dis=62 f3 f5 08 73 c0 90
    vpshrdq $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 73 05  dis=62 f3 f5 08 73 05 90
    {evex} vgf2p8affineqb $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 f5 08 ce c0  dis=62 f3 f5 08 ce c0 90
    {evex} vgf2p8affineqb $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 ce 05  dis=62 f3 f5 08 ce 05 90
    {evex} vgf2p8affineinvqb $0x90,%xmm0,%xmm1,%xmm0 # gen=62 f3 f5 08 cf c0  dis=62 f3 f5 08 cf c0 90
    {evex} vgf2p8affineinvqb $0x90,0x90909090,%xmm1,%xmm0 # gen=62 f3 f5 08 cf 05  dis=62 f3 f5 08 cf 05 90
    valignq $0x90,%ymm0,%ymm1,%ymm0     # gen=62 f3 f5 28 03 c0  dis=62 f3 f5 28 03 c0 90
    valignq $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 03 05  dis=62 f3 f5 28 03 05 90
    vinsertf64x2 $0x90,%xmm0,%ymm1,%ymm0 # gen=62 f3 f5 28 18 c0  dis=62 f3 f5 28 18 c0 90
    vinsertf64x2 $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 18 05  dis=62 f3 f5 28 18 05 90
    vpcmpuq $0x90,%ymm0,%ymm1,%k0       # gen=62 f3 f5 28 1e c0  dis=62 f3 f5 28 1e c0 90
    vpcmpuq $0x90,0x90909090,%ymm1,%k0  # gen=62 f3 f5 28 1e 05  dis=62 f3 f5 28 1e 05 90
    vpcmpq $0x90,%ymm0,%ymm1,%k0        # gen=62 f3 f5 28 1f c0  dis=62 f3 f5 28 1f c0 90
    vpcmpq $0x90,0x90909090,%ymm1,%k0   # gen=62 f3 f5 28 1f 05  dis=62 f3 f5 28 1f 05 90
    vshuff64x2 $0x90,%ymm0,%ymm1,%ymm0  # gen=62 f3 f5 28 23 c0  dis=62 f3 f5 28 23 c0 90
    vshuff64x2 $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 23 05  dis=62 f3 f5 28 23 05 90
    vpternlogq $0x90,%ymm0,%ymm1,%ymm0  # gen=62 f3 f5 28 25 c0  dis=62 f3 f5 28 25 c0 90
    vpternlogq $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 25 05  dis=62 f3 f5 28 25 05 90
    vinserti64x2 $0x90,%xmm0,%ymm1,%ymm0 # gen=62 f3 f5 28 38 c0  dis=62 f3 f5 28 38 c0 90
    vinserti64x2 $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 38 05  dis=62 f3 f5 28 38 05 90
    vpcmpuw $0x90,%ymm0,%ymm1,%k0       # gen=62 f3 f5 28 3e c0  dis=62 f3 f5 28 3e c0 90
    vpcmpuw $0x90,0x90909090,%ymm1,%k0  # gen=62 f3 f5 28 3e 05  dis=62 f3 f5 28 3e 05 90
    vpcmpw $0x90,%ymm0,%ymm1,%k0        # gen=62 f3 f5 28 3f c0  dis=62 f3 f5 28 3f c0 90
    vpcmpw $0x90,0x90909090,%ymm1,%k0   # gen=62 f3 f5 28 3f 05  dis=62 f3 f5 28 3f 05 90
    vshufi64x2 $0x90,%ymm0,%ymm1,%ymm0  # gen=62 f3 f5 28 43 c0  dis=62 f3 f5 28 43 c0 90
    vshufi64x2 $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 43 05  dis=62 f3 f5 28 43 05 90
    vrangepd $0x90,%ymm0,%ymm1,%ymm0    # gen=62 f3 f5 28 50 c0  dis=62 f3 f5 28 50 c0 90
    vrangepd $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 50 05  dis=62 f3 f5 28 50 05 90
    vfixupimmpd $0x90,%ymm0,%ymm1,%ymm0 # gen=62 f3 f5 28 54 c0  dis=62 f3 f5 28 54 c0 90
    vfixupimmpd $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 54 05  dis=62 f3 f5 28 54 05 90
    vpshldq $0x90,%ymm0,%ymm1,%ymm0     # gen=62 f3 f5 28 71 c0  dis=62 f3 f5 28 71 c0 90
    vpshldq $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 71 05  dis=62 f3 f5 28 71 05 90
    vpshrdq $0x90,%ymm0,%ymm1,%ymm0     # gen=62 f3 f5 28 73 c0  dis=62 f3 f5 28 73 c0 90
    vpshrdq $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 73 05  dis=62 f3 f5 28 73 05 90
    {evex} vgf2p8affineqb $0x90,%ymm0,%ymm1,%ymm0 # gen=62 f3 f5 28 ce c0  dis=62 f3 f5 28 ce c0 90
    {evex} vgf2p8affineqb $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 ce 05  dis=62 f3 f5 28 ce 05 90
    {evex} vgf2p8affineinvqb $0x90,%ymm0,%ymm1,%ymm0 # gen=62 f3 f5 28 cf c0  dis=62 f3 f5 28 cf c0 90
    {evex} vgf2p8affineinvqb $0x90,0x90909090,%ymm1,%ymm0 # gen=62 f3 f5 28 cf 05  dis=62 f3 f5 28 cf 05 90
    valignq $0x90,%zmm0,%zmm1,%zmm0     # gen=62 f3 f5 48 03 c0  dis=62 f3 f5 48 03 c0 90
    valignq $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 03 05  dis=62 f3 f5 48 03 05 90
    vinsertf64x2 $0x90,%xmm0,%zmm1,%zmm0 # gen=62 f3 f5 48 18 c0  dis=62 f3 f5 48 18 c0 90
    vinsertf64x2 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 18 05  dis=62 f3 f5 48 18 05 90
    vinsertf64x4 $0x90,%ymm0,%zmm1,%zmm0 # gen=62 f3 f5 48 1a c0  dis=62 f3 f5 48 1a c0 90
    vinsertf64x4 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 1a 05  dis=62 f3 f5 48 1a 05 90
    vpcmpuq $0x90,%zmm0,%zmm1,%k0       # gen=62 f3 f5 48 1e c0  dis=62 f3 f5 48 1e c0 90
    vpcmpuq $0x90,0x90909090,%zmm1,%k0  # gen=62 f3 f5 48 1e 05  dis=62 f3 f5 48 1e 05 90
    vpcmpq $0x90,%zmm0,%zmm1,%k0        # gen=62 f3 f5 48 1f c0  dis=62 f3 f5 48 1f c0 90
    vpcmpq $0x90,0x90909090,%zmm1,%k0   # gen=62 f3 f5 48 1f 05  dis=62 f3 f5 48 1f 05 90
    vshuff64x2 $0x90,%zmm0,%zmm1,%zmm0  # gen=62 f3 f5 48 23 c0  dis=62 f3 f5 48 23 c0 90
    vshuff64x2 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 23 05  dis=62 f3 f5 48 23 05 90
    vpternlogq $0x90,%zmm0,%zmm1,%zmm0  # gen=62 f3 f5 48 25 c0  dis=62 f3 f5 48 25 c0 90
    vpternlogq $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 25 05  dis=62 f3 f5 48 25 05 90
    vinserti64x2 $0x90,%xmm0,%zmm1,%zmm0 # gen=62 f3 f5 48 38 c0  dis=62 f3 f5 48 38 c0 90
    vinserti64x2 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 38 05  dis=62 f3 f5 48 38 05 90
    vinserti64x4 $0x90,%ymm0,%zmm1,%zmm0 # gen=62 f3 f5 48 3a c0  dis=62 f3 f5 48 3a c0 90
    vinserti64x4 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 3a 05  dis=62 f3 f5 48 3a 05 90
    vpcmpuw $0x90,%zmm0,%zmm1,%k0       # gen=62 f3 f5 48 3e c0  dis=62 f3 f5 48 3e c0 90
    vpcmpuw $0x90,0x90909090,%zmm1,%k0  # gen=62 f3 f5 48 3e 05  dis=62 f3 f5 48 3e 05 90
    vpcmpw $0x90,%zmm0,%zmm1,%k0        # gen=62 f3 f5 48 3f c0  dis=62 f3 f5 48 3f c0 90
    vpcmpw $0x90,0x90909090,%zmm1,%k0   # gen=62 f3 f5 48 3f 05  dis=62 f3 f5 48 3f 05 90
    vshufi64x2 $0x90,%zmm0,%zmm1,%zmm0  # gen=62 f3 f5 48 43 c0  dis=62 f3 f5 48 43 c0 90
    vshufi64x2 $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 43 05  dis=62 f3 f5 48 43 05 90
    vrangepd $0x90,%zmm0,%zmm1,%zmm0    # gen=62 f3 f5 48 50 c0  dis=62 f3 f5 48 50 c0 90
    vrangepd $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 50 05  dis=62 f3 f5 48 50 05 90
    vfixupimmpd $0x90,%zmm0,%zmm1,%zmm0 # gen=62 f3 f5 48 54 c0  dis=62 f3 f5 48 54 c0 90
    vfixupimmpd $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 54 05  dis=62 f3 f5 48 54 05 90
    vpshldq $0x90,%zmm0,%zmm1,%zmm0     # gen=62 f3 f5 48 71 c0  dis=62 f3 f5 48 71 c0 90
    vpshldq $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 71 05  dis=62 f3 f5 48 71 05 90
    vpshrdq $0x90,%zmm0,%zmm1,%zmm0     # gen=62 f3 f5 48 73 c0  dis=62 f3 f5 48 73 c0 90
    vpshrdq $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 73 05  dis=62 f3 f5 48 73 05 90
    vgf2p8affineqb $0x90,%zmm0,%zmm1,%zmm0 # gen=62 f3 f5 48 ce c0  dis=62 f3 f5 48 ce c0 90
    vgf2p8affineqb $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 ce 05  dis=62 f3 f5 48 ce 05 90
    vgf2p8affineinvqb $0x90,%zmm0,%zmm1,%zmm0 # gen=62 f3 f5 48 cf c0  dis=62 f3 f5 48 cf c0 90
    vgf2p8affineinvqb $0x90,0x90909090,%zmm1,%zmm0 # gen=62 f3 f5 48 cf 05  dis=62 f3 f5 48 cf 05 90
    pfcmpge 0x90909000,%mm0             # gen=0f 0f 05 00  dis=0f 0f 05 00 90 90 90
    pfcmpge 0x90909001,%mm0             # gen=0f 0f 05 01  dis=0f 0f 05 01 90 90 90
    pfcmpge 0x90909002,%mm0             # gen=0f 0f 05 02  dis=0f 0f 05 02 90 90 90
    pfcmpge 0x90909003,%mm0             # gen=0f 0f 05 03  dis=0f 0f 05 03 90 90 90
    pfcmpge 0x90909004,%mm0             # gen=0f 0f 05 04  dis=0f 0f 05 04 90 90 90
    pfcmpge 0x90909005,%mm0             # gen=0f 0f 05 05  dis=0f 0f 05 05 90 90 90
    pfcmpge 0x90909006,%mm0             # gen=0f 0f 05 06  dis=0f 0f 05 06 90 90 90
    pfcmpge 0x90909007,%mm0             # gen=0f 0f 05 07  dis=0f 0f 05 07 90 90 90
    pfcmpge 0x90909008,%mm0             # gen=0f 0f 05 08  dis=0f 0f 05 08 90 90 90
    pfcmpge 0x90909009,%mm0             # gen=0f 0f 05 09  dis=0f 0f 05 09 90 90 90
    pfcmpge 0x9090900a,%mm0             # gen=0f 0f 05 0a  dis=0f 0f 05 0a 90 90 90
    pfcmpge 0x9090900b,%mm0             # gen=0f 0f 05 0b  dis=0f 0f 05 0b 90 90 90
    pi2fw  %mm0,%mm0                    # gen=0f 0f c0 0c  dis=0f 0f c0 0c
    pfcmpge 0x9090900c,%mm0             # gen=0f 0f 05 0c  dis=0f 0f 05 0c 90 90 90
    pi2fw  %mm0,%mm1                    # gen=0f 0f c8 0c  dis=0f 0f c8 0c
    pi2fw  %mm0,%mm2                    # gen=0f 0f d0 0c  dis=0f 0f d0 0c
    pi2fw  %mm0,%mm3                    # gen=0f 0f d8 0c  dis=0f 0f d8 0c
    pi2fw  %mm0,%mm4                    # gen=0f 0f e0 0c  dis=0f 0f e0 0c
    pi2fw  %mm0,%mm5                    # gen=0f 0f e8 0c  dis=0f 0f e8 0c
    pi2fw  %mm0,%mm6                    # gen=0f 0f f0 0c  dis=0f 0f f0 0c
    pi2fw  %mm0,%mm7                    # gen=0f 0f f8 0c  dis=0f 0f f8 0c
    pfcmpge 0x9090900c,%mm1             # gen=0f 0f 0d 0c  dis=0f 0f 0d 0c 90 90 90
    pfcmpge 0x9090900c,%mm2             # gen=0f 0f 15 0c  dis=0f 0f 15 0c 90 90 90
    pfcmpge 0x9090900c,%mm3             # gen=0f 0f 1d 0c  dis=0f 0f 1d 0c 90 90 90
    pfcmpge 0x9090900c,%mm4             # gen=0f 0f 25 0c  dis=0f 0f 25 0c 90 90 90
    pfcmpge 0x9090900c,%mm5             # gen=0f 0f 2d 0c  dis=0f 0f 2d 0c 90 90 90
    pfcmpge 0x9090900c,%mm6             # gen=0f 0f 35 0c  dis=0f 0f 35 0c 90 90 90
    pfcmpge 0x9090900c,%mm7             # gen=0f 0f 3d 0c  dis=0f 0f 3d 0c 90 90 90
    pi2fd  %mm0,%mm0                    # gen=0f 0f c0 0d  dis=0f 0f c0 0d
    pfcmpge 0x9090900d,%mm0             # gen=0f 0f 05 0d  dis=0f 0f 05 0d 90 90 90
    pi2fd  %mm0,%mm1                    # gen=0f 0f c8 0d  dis=0f 0f c8 0d
    pi2fd  %mm0,%mm2                    # gen=0f 0f d0 0d  dis=0f 0f d0 0d
    pi2fd  %mm0,%mm3                    # gen=0f 0f d8 0d  dis=0f 0f d8 0d
    pi2fd  %mm0,%mm4                    # gen=0f 0f e0 0d  dis=0f 0f e0 0d
    pi2fd  %mm0,%mm5                    # gen=0f 0f e8 0d  dis=0f 0f e8 0d
    pi2fd  %mm0,%mm6                    # gen=0f 0f f0 0d  dis=0f 0f f0 0d
    pi2fd  %mm0,%mm7                    # gen=0f 0f f8 0d  dis=0f 0f f8 0d
    pfcmpge 0x9090900d,%mm1             # gen=0f 0f 0d 0d  dis=0f 0f 0d 0d 90 90 90
    pfcmpge 0x9090900d,%mm2             # gen=0f 0f 15 0d  dis=0f 0f 15 0d 90 90 90
    pfcmpge 0x9090900d,%mm3             # gen=0f 0f 1d 0d  dis=0f 0f 1d 0d 90 90 90
    pfcmpge 0x9090900d,%mm4             # gen=0f 0f 25 0d  dis=0f 0f 25 0d 90 90 90
    pfcmpge 0x9090900d,%mm5             # gen=0f 0f 2d 0d  dis=0f 0f 2d 0d 90 90 90
    pfcmpge 0x9090900d,%mm6             # gen=0f 0f 35 0d  dis=0f 0f 35 0d 90 90 90
    pfcmpge 0x9090900d,%mm7             # gen=0f 0f 3d 0d  dis=0f 0f 3d 0d 90 90 90
    pfcmpge 0x9090900e,%mm0             # gen=0f 0f 05 0e  dis=0f 0f 05 0e 90 90 90
    pfcmpge 0x9090900f,%mm0             # gen=0f 0f 05 0f  dis=0f 0f 05 0f 90 90 90
    pfcmpge 0x90909010,%mm0             # gen=0f 0f 05 10  dis=0f 0f 05 10 90 90 90
    pfcmpge 0x90909011,%mm0             # gen=0f 0f 05 11  dis=0f 0f 05 11 90 90 90
    pfcmpge 0x90909012,%mm0             # gen=0f 0f 05 12  dis=0f 0f 05 12 90 90 90
    pfcmpge 0x90909013,%mm0             # gen=0f 0f 05 13  dis=0f 0f 05 13 90 90 90
    pfcmpge 0x90909014,%mm0             # gen=0f 0f 05 14  dis=0f 0f 05 14 90 90 90
    pfcmpge 0x90909015,%mm0             # gen=0f 0f 05 15  dis=0f 0f 05 15 90 90 90
    pfcmpge 0x90909016,%mm0             # gen=0f 0f 05 16  dis=0f 0f 05 16 90 90 90
    pfcmpge 0x90909017,%mm0             # gen=0f 0f 05 17  dis=0f 0f 05 17 90 90 90
    pfcmpge 0x90909018,%mm0             # gen=0f 0f 05 18  dis=0f 0f 05 18 90 90 90
    pfcmpge 0x90909019,%mm0             # gen=0f 0f 05 19  dis=0f 0f 05 19 90 90 90
    pfcmpge 0x9090901a,%mm0             # gen=0f 0f 05 1a  dis=0f 0f 05 1a 90 90 90
    pfcmpge 0x9090901b,%mm0             # gen=0f 0f 05 1b  dis=0f 0f 05 1b 90 90 90
    pf2iw  %mm0,%mm0                    # gen=0f 0f c0 1c  dis=0f 0f c0 1c
    pfcmpge 0x9090901c,%mm0             # gen=0f 0f 05 1c  dis=0f 0f 05 1c 90 90 90
    pf2iw  %mm0,%mm1                    # gen=0f 0f c8 1c  dis=0f 0f c8 1c
    pf2iw  %mm0,%mm2                    # gen=0f 0f d0 1c  dis=0f 0f d0 1c
    pf2iw  %mm0,%mm3                    # gen=0f 0f d8 1c  dis=0f 0f d8 1c
    pf2iw  %mm0,%mm4                    # gen=0f 0f e0 1c  dis=0f 0f e0 1c
    pf2iw  %mm0,%mm5                    # gen=0f 0f e8 1c  dis=0f 0f e8 1c
    pf2iw  %mm0,%mm6                    # gen=0f 0f f0 1c  dis=0f 0f f0 1c
    pf2iw  %mm0,%mm7                    # gen=0f 0f f8 1c  dis=0f 0f f8 1c
    pfcmpge 0x9090901c,%mm1             # gen=0f 0f 0d 1c  dis=0f 0f 0d 1c 90 90 90
    pfcmpge 0x9090901c,%mm2             # gen=0f 0f 15 1c  dis=0f 0f 15 1c 90 90 90
    pfcmpge 0x9090901c,%mm3             # gen=0f 0f 1d 1c  dis=0f 0f 1d 1c 90 90 90
    pfcmpge 0x9090901c,%mm4             # gen=0f 0f 25 1c  dis=0f 0f 25 1c 90 90 90
    pfcmpge 0x9090901c,%mm5             # gen=0f 0f 2d 1c  dis=0f 0f 2d 1c 90 90 90
    pfcmpge 0x9090901c,%mm6             # gen=0f 0f 35 1c  dis=0f 0f 35 1c 90 90 90
    pfcmpge 0x9090901c,%mm7             # gen=0f 0f 3d 1c  dis=0f 0f 3d 1c 90 90 90
    pf2id  %mm0,%mm0                    # gen=0f 0f c0 1d  dis=0f 0f c0 1d
    pfcmpge 0x9090901d,%mm0             # gen=0f 0f 05 1d  dis=0f 0f 05 1d 90 90 90
    pf2id  %mm0,%mm1                    # gen=0f 0f c8 1d  dis=0f 0f c8 1d
    pf2id  %mm0,%mm2                    # gen=0f 0f d0 1d  dis=0f 0f d0 1d
    pf2id  %mm0,%mm3                    # gen=0f 0f d8 1d  dis=0f 0f d8 1d
    pf2id  %mm0,%mm4                    # gen=0f 0f e0 1d  dis=0f 0f e0 1d
    pf2id  %mm0,%mm5                    # gen=0f 0f e8 1d  dis=0f 0f e8 1d
    pf2id  %mm0,%mm6                    # gen=0f 0f f0 1d  dis=0f 0f f0 1d
    pf2id  %mm0,%mm7                    # gen=0f 0f f8 1d  dis=0f 0f f8 1d
    pfcmpge 0x9090901d,%mm1             # gen=0f 0f 0d 1d  dis=0f 0f 0d 1d 90 90 90
    pfcmpge 0x9090901d,%mm2             # gen=0f 0f 15 1d  dis=0f 0f 15 1d 90 90 90
    pfcmpge 0x9090901d,%mm3             # gen=0f 0f 1d 1d  dis=0f 0f 1d 1d 90 90 90
    pfcmpge 0x9090901d,%mm4             # gen=0f 0f 25 1d  dis=0f 0f 25 1d 90 90 90
    pfcmpge 0x9090901d,%mm5             # gen=0f 0f 2d 1d  dis=0f 0f 2d 1d 90 90 90
    pfcmpge 0x9090901d,%mm6             # gen=0f 0f 35 1d  dis=0f 0f 35 1d 90 90 90
    pfcmpge 0x9090901d,%mm7             # gen=0f 0f 3d 1d  dis=0f 0f 3d 1d 90 90 90
    pfcmpge 0x9090901e,%mm0             # gen=0f 0f 05 1e  dis=0f 0f 05 1e 90 90 90
    pfcmpge 0x9090901f,%mm0             # gen=0f 0f 05 1f  dis=0f 0f 05 1f 90 90 90
    pfcmpge 0x90909020,%mm0             # gen=0f 0f 05 20  dis=0f 0f 05 20 90 90 90
    pfcmpge 0x90909021,%mm0             # gen=0f 0f 05 21  dis=0f 0f 05 21 90 90 90
    pfcmpge 0x90909022,%mm0             # gen=0f 0f 05 22  dis=0f 0f 05 22 90 90 90
    pfcmpge 0x90909023,%mm0             # gen=0f 0f 05 23  dis=0f 0f 05 23 90 90 90
    pfcmpge 0x90909024,%mm0             # gen=0f 0f 05 24  dis=0f 0f 05 24 90 90 90
    pfcmpge 0x90909025,%mm0             # gen=0f 0f 05 25  dis=0f 0f 05 25 90 90 90
    pfcmpge 0x90909026,%mm0             # gen=0f 0f 05 26  dis=0f 0f 05 26 90 90 90
    pfcmpge 0x90909027,%mm0             # gen=0f 0f 05 27  dis=0f 0f 05 27 90 90 90
    pfcmpge 0x90909028,%mm0             # gen=0f 0f 05 28  dis=0f 0f 05 28 90 90 90
    pfcmpge 0x90909029,%mm0             # gen=0f 0f 05 29  dis=0f 0f 05 29 90 90 90
    pfcmpge 0x9090902a,%mm0             # gen=0f 0f 05 2a  dis=0f 0f 05 2a 90 90 90
    pfcmpge 0x9090902b,%mm0             # gen=0f 0f 05 2b  dis=0f 0f 05 2b 90 90 90
    pfcmpge 0x9090902c,%mm0             # gen=0f 0f 05 2c  dis=0f 0f 05 2c 90 90 90
    pfcmpge 0x9090902d,%mm0             # gen=0f 0f 05 2d  dis=0f 0f 05 2d 90 90 90
    pfcmpge 0x9090902e,%mm0             # gen=0f 0f 05 2e  dis=0f 0f 05 2e 90 90 90
    pfcmpge 0x9090902f,%mm0             # gen=0f 0f 05 2f  dis=0f 0f 05 2f 90 90 90
    pfcmpge 0x90909030,%mm0             # gen=0f 0f 05 30  dis=0f 0f 05 30 90 90 90
    pfcmpge 0x90909031,%mm0             # gen=0f 0f 05 31  dis=0f 0f 05 31 90 90 90
    pfcmpge 0x90909032,%mm0             # gen=0f 0f 05 32  dis=0f 0f 05 32 90 90 90
    pfcmpge 0x90909033,%mm0             # gen=0f 0f 05 33  dis=0f 0f 05 33 90 90 90
    pfcmpge 0x90909034,%mm0             # gen=0f 0f 05 34  dis=0f 0f 05 34 90 90 90
    pfcmpge 0x90909035,%mm0             # gen=0f 0f 05 35  dis=0f 0f 05 35 90 90 90
    pfcmpge 0x90909036,%mm0             # gen=0f 0f 05 36  dis=0f 0f 05 36 90 90 90
    pfcmpge 0x90909037,%mm0             # gen=0f 0f 05 37  dis=0f 0f 05 37 90 90 90
    pfcmpge 0x90909038,%mm0             # gen=0f 0f 05 38  dis=0f 0f 05 38 90 90 90
    pfcmpge 0x90909039,%mm0             # gen=0f 0f 05 39  dis=0f 0f 05 39 90 90 90
    pfcmpge 0x9090903a,%mm0             # gen=0f 0f 05 3a  dis=0f 0f 05 3a 90 90 90
    pfcmpge 0x9090903b,%mm0             # gen=0f 0f 05 3b  dis=0f 0f 05 3b 90 90 90
    pfcmpge 0x9090903c,%mm0             # gen=0f 0f 05 3c  dis=0f 0f 05 3c 90 90 90
    pfcmpge 0x9090903d,%mm0             # gen=0f 0f 05 3d  dis=0f 0f 05 3d 90 90 90
    pfcmpge 0x9090903e,%mm0             # gen=0f 0f 05 3e  dis=0f 0f 05 3e 90 90 90
    pfcmpge 0x9090903f,%mm0             # gen=0f 0f 05 3f  dis=0f 0f 05 3f 90 90 90
    pfcmpge 0x90909040,%mm0             # gen=0f 0f 05 40  dis=0f 0f 05 40 90 90 90
    pfcmpge 0x90909041,%mm0             # gen=0f 0f 05 41  dis=0f 0f 05 41 90 90 90
    pfcmpge 0x90909042,%mm0             # gen=0f 0f 05 42  dis=0f 0f 05 42 90 90 90
    pfcmpge 0x90909043,%mm0             # gen=0f 0f 05 43  dis=0f 0f 05 43 90 90 90
    pfcmpge 0x90909044,%mm0             # gen=0f 0f 05 44  dis=0f 0f 05 44 90 90 90
    pfcmpge 0x90909045,%mm0             # gen=0f 0f 05 45  dis=0f 0f 05 45 90 90 90
    pfcmpge 0x90909046,%mm0             # gen=0f 0f 05 46  dis=0f 0f 05 46 90 90 90
    pfcmpge 0x90909047,%mm0             # gen=0f 0f 05 47  dis=0f 0f 05 47 90 90 90
    pfcmpge 0x90909048,%mm0             # gen=0f 0f 05 48  dis=0f 0f 05 48 90 90 90
    pfcmpge 0x90909049,%mm0             # gen=0f 0f 05 49  dis=0f 0f 05 49 90 90 90
    pfcmpge 0x9090904a,%mm0             # gen=0f 0f 05 4a  dis=0f 0f 05 4a 90 90 90
    pfcmpge 0x9090904b,%mm0             # gen=0f 0f 05 4b  dis=0f 0f 05 4b 90 90 90
    pfcmpge 0x9090904c,%mm0             # gen=0f 0f 05 4c  dis=0f 0f 05 4c 90 90 90
    pfcmpge 0x9090904d,%mm0             # gen=0f 0f 05 4d  dis=0f 0f 05 4d 90 90 90
    pfcmpge 0x9090904e,%mm0             # gen=0f 0f 05 4e  dis=0f 0f 05 4e 90 90 90
    pfcmpge 0x9090904f,%mm0             # gen=0f 0f 05 4f  dis=0f 0f 05 4f 90 90 90
    pfcmpge 0x90909050,%mm0             # gen=0f 0f 05 50  dis=0f 0f 05 50 90 90 90
    pfcmpge 0x90909051,%mm0             # gen=0f 0f 05 51  dis=0f 0f 05 51 90 90 90
    pfcmpge 0x90909052,%mm0             # gen=0f 0f 05 52  dis=0f 0f 05 52 90 90 90
    pfcmpge 0x90909053,%mm0             # gen=0f 0f 05 53  dis=0f 0f 05 53 90 90 90
    pfcmpge 0x90909054,%mm0             # gen=0f 0f 05 54  dis=0f 0f 05 54 90 90 90
    pfcmpge 0x90909055,%mm0             # gen=0f 0f 05 55  dis=0f 0f 05 55 90 90 90
    pfcmpge 0x90909056,%mm0             # gen=0f 0f 05 56  dis=0f 0f 05 56 90 90 90
    pfcmpge 0x90909057,%mm0             # gen=0f 0f 05 57  dis=0f 0f 05 57 90 90 90
    pfcmpge 0x90909058,%mm0             # gen=0f 0f 05 58  dis=0f 0f 05 58 90 90 90
    pfcmpge 0x90909059,%mm0             # gen=0f 0f 05 59  dis=0f 0f 05 59 90 90 90
    pfcmpge 0x9090905a,%mm0             # gen=0f 0f 05 5a  dis=0f 0f 05 5a 90 90 90
    pfcmpge 0x9090905b,%mm0             # gen=0f 0f 05 5b  dis=0f 0f 05 5b 90 90 90
    pfcmpge 0x9090905c,%mm0             # gen=0f 0f 05 5c  dis=0f 0f 05 5c 90 90 90
    pfcmpge 0x9090905d,%mm0             # gen=0f 0f 05 5d  dis=0f 0f 05 5d 90 90 90
    pfcmpge 0x9090905e,%mm0             # gen=0f 0f 05 5e  dis=0f 0f 05 5e 90 90 90
    pfcmpge 0x9090905f,%mm0             # gen=0f 0f 05 5f  dis=0f 0f 05 5f 90 90 90
    pfcmpge 0x90909060,%mm0             # gen=0f 0f 05 60  dis=0f 0f 05 60 90 90 90
    pfcmpge 0x90909061,%mm0             # gen=0f 0f 05 61  dis=0f 0f 05 61 90 90 90
    pfcmpge 0x90909062,%mm0             # gen=0f 0f 05 62  dis=0f 0f 05 62 90 90 90
    pfcmpge 0x90909063,%mm0             # gen=0f 0f 05 63  dis=0f 0f 05 63 90 90 90
    pfcmpge 0x90909064,%mm0             # gen=0f 0f 05 64  dis=0f 0f 05 64 90 90 90
    pfcmpge 0x90909065,%mm0             # gen=0f 0f 05 65  dis=0f 0f 05 65 90 90 90
    pfcmpge 0x90909066,%mm0             # gen=0f 0f 05 66  dis=0f 0f 05 66 90 90 90
    pfcmpge 0x90909067,%mm0             # gen=0f 0f 05 67  dis=0f 0f 05 67 90 90 90
    pfcmpge 0x90909068,%mm0             # gen=0f 0f 05 68  dis=0f 0f 05 68 90 90 90
    pfcmpge 0x90909069,%mm0             # gen=0f 0f 05 69  dis=0f 0f 05 69 90 90 90
    pfcmpge 0x9090906a,%mm0             # gen=0f 0f 05 6a  dis=0f 0f 05 6a 90 90 90
    pfcmpge 0x9090906b,%mm0             # gen=0f 0f 05 6b  dis=0f 0f 05 6b 90 90 90
    pfcmpge 0x9090906c,%mm0             # gen=0f 0f 05 6c  dis=0f 0f 05 6c 90 90 90
    pfcmpge 0x9090906d,%mm0             # gen=0f 0f 05 6d  dis=0f 0f 05 6d 90 90 90
    pfcmpge 0x9090906e,%mm0             # gen=0f 0f 05 6e  dis=0f 0f 05 6e 90 90 90
    pfcmpge 0x9090906f,%mm0             # gen=0f 0f 05 6f  dis=0f 0f 05 6f 90 90 90
    pfcmpge 0x90909070,%mm0             # gen=0f 0f 05 70  dis=0f 0f 05 70 90 90 90
    pfcmpge 0x90909071,%mm0             # gen=0f 0f 05 71  dis=0f 0f 05 71 90 90 90
    pfcmpge 0x90909072,%mm0             # gen=0f 0f 05 72  dis=0f 0f 05 72 90 90 90
    pfcmpge 0x90909073,%mm0             # gen=0f 0f 05 73  dis=0f 0f 05 73 90 90 90
    pfcmpge 0x90909074,%mm0             # gen=0f 0f 05 74  dis=0f 0f 05 74 90 90 90
    pfcmpge 0x90909075,%mm0             # gen=0f 0f 05 75  dis=0f 0f 05 75 90 90 90
    pfcmpge 0x90909076,%mm0             # gen=0f 0f 05 76  dis=0f 0f 05 76 90 90 90
    pfcmpge 0x90909077,%mm0             # gen=0f 0f 05 77  dis=0f 0f 05 77 90 90 90
    pfcmpge 0x90909078,%mm0             # gen=0f 0f 05 78  dis=0f 0f 05 78 90 90 90
    pfcmpge 0x90909079,%mm0             # gen=0f 0f 05 79  dis=0f 0f 05 79 90 90 90
    pfcmpge 0x9090907a,%mm0             # gen=0f 0f 05 7a  dis=0f 0f 05 7a 90 90 90
    pfcmpge 0x9090907b,%mm0             # gen=0f 0f 05 7b  dis=0f 0f 05 7b 90 90 90
    pfcmpge 0x9090907c,%mm0             # gen=0f 0f 05 7c  dis=0f 0f 05 7c 90 90 90
    pfcmpge 0x9090907d,%mm0             # gen=0f 0f 05 7d  dis=0f 0f 05 7d 90 90 90
    pfcmpge 0x9090907e,%mm0             # gen=0f 0f 05 7e  dis=0f 0f 05 7e 90 90 90
    pfcmpge 0x9090907f,%mm0             # gen=0f 0f 05 7f  dis=0f 0f 05 7f 90 90 90
    pfcmpge 0x90909080,%mm0             # gen=0f 0f 05 80  dis=0f 0f 05 80 90 90 90
    pfcmpge 0x90909081,%mm0             # gen=0f 0f 05 81  dis=0f 0f 05 81 90 90 90
    pfcmpge 0x90909082,%mm0             # gen=0f 0f 05 82  dis=0f 0f 05 82 90 90 90
    pfcmpge 0x90909083,%mm0             # gen=0f 0f 05 83  dis=0f 0f 05 83 90 90 90
    pfcmpge 0x90909084,%mm0             # gen=0f 0f 05 84  dis=0f 0f 05 84 90 90 90
    pfcmpge 0x90909085,%mm0             # gen=0f 0f 05 85  dis=0f 0f 05 85 90 90 90
    pfcmpge 0x90909086,%mm0             # gen=0f 0f 05 86  dis=0f 0f 05 86 90 90 90
    pfcmpge 0x90909087,%mm0             # gen=0f 0f 05 87  dis=0f 0f 05 87 90 90 90
    pfcmpge 0x90909088,%mm0             # gen=0f 0f 05 88  dis=0f 0f 05 88 90 90 90
    pfcmpge 0x90909089,%mm0             # gen=0f 0f 05 89  dis=0f 0f 05 89 90 90 90
    pfnacc %mm0,%mm0                    # gen=0f 0f c0 8a  dis=0f 0f c0 8a
    pfcmpge 0x9090908a,%mm0             # gen=0f 0f 05 8a  dis=0f 0f 05 8a 90 90 90
    pfnacc %mm0,%mm1                    # gen=0f 0f c8 8a  dis=0f 0f c8 8a
    pfnacc %mm0,%mm2                    # gen=0f 0f d0 8a  dis=0f 0f d0 8a
    pfnacc %mm0,%mm3                    # gen=0f 0f d8 8a  dis=0f 0f d8 8a
    pfnacc %mm0,%mm4                    # gen=0f 0f e0 8a  dis=0f 0f e0 8a
    pfnacc %mm0,%mm5                    # gen=0f 0f e8 8a  dis=0f 0f e8 8a
    pfnacc %mm0,%mm6                    # gen=0f 0f f0 8a  dis=0f 0f f0 8a
    pfnacc %mm0,%mm7                    # gen=0f 0f f8 8a  dis=0f 0f f8 8a
    pfcmpge 0x9090908a,%mm1             # gen=0f 0f 0d 8a  dis=0f 0f 0d 8a 90 90 90
    pfcmpge 0x9090908a,%mm2             # gen=0f 0f 15 8a  dis=0f 0f 15 8a 90 90 90
    pfcmpge 0x9090908a,%mm3             # gen=0f 0f 1d 8a  dis=0f 0f 1d 8a 90 90 90
    pfcmpge 0x9090908a,%mm4             # gen=0f 0f 25 8a  dis=0f 0f 25 8a 90 90 90
    pfcmpge 0x9090908a,%mm5             # gen=0f 0f 2d 8a  dis=0f 0f 2d 8a 90 90 90
    pfcmpge 0x9090908a,%mm6             # gen=0f 0f 35 8a  dis=0f 0f 35 8a 90 90 90
    pfcmpge 0x9090908a,%mm7             # gen=0f 0f 3d 8a  dis=0f 0f 3d 8a 90 90 90
    pfcmpge 0x9090908b,%mm0             # gen=0f 0f 05 8b  dis=0f 0f 05 8b 90 90 90
    pfcmpge 0x9090908c,%mm0             # gen=0f 0f 05 8c  dis=0f 0f 05 8c 90 90 90
    pfcmpge 0x9090908d,%mm0             # gen=0f 0f 05 8d  dis=0f 0f 05 8d 90 90 90
    pfpnacc %mm0,%mm0                   # gen=0f 0f c0 8e  dis=0f 0f c0 8e
    pfcmpge 0x9090908e,%mm0             # gen=0f 0f 05 8e  dis=0f 0f 05 8e 90 90 90
    pfpnacc %mm0,%mm1                   # gen=0f 0f c8 8e  dis=0f 0f c8 8e
    pfpnacc %mm0,%mm2                   # gen=0f 0f d0 8e  dis=0f 0f d0 8e
    pfpnacc %mm0,%mm3                   # gen=0f 0f d8 8e  dis=0f 0f d8 8e
    pfpnacc %mm0,%mm4                   # gen=0f 0f e0 8e  dis=0f 0f e0 8e
    pfpnacc %mm0,%mm5                   # gen=0f 0f e8 8e  dis=0f 0f e8 8e
    pfpnacc %mm0,%mm6                   # gen=0f 0f f0 8e  dis=0f 0f f0 8e
    pfpnacc %mm0,%mm7                   # gen=0f 0f f8 8e  dis=0f 0f f8 8e
    pfcmpge 0x9090908e,%mm1             # gen=0f 0f 0d 8e  dis=0f 0f 0d 8e 90 90 90
    pfcmpge 0x9090908e,%mm2             # gen=0f 0f 15 8e  dis=0f 0f 15 8e 90 90 90
    pfcmpge 0x9090908e,%mm3             # gen=0f 0f 1d 8e  dis=0f 0f 1d 8e 90 90 90
    pfcmpge 0x9090908e,%mm4             # gen=0f 0f 25 8e  dis=0f 0f 25 8e 90 90 90
    pfcmpge 0x9090908e,%mm5             # gen=0f 0f 2d 8e  dis=0f 0f 2d 8e 90 90 90
    pfcmpge 0x9090908e,%mm6             # gen=0f 0f 35 8e  dis=0f 0f 35 8e 90 90 90
    pfcmpge 0x9090908e,%mm7             # gen=0f 0f 3d 8e  dis=0f 0f 3d 8e 90 90 90
    pfcmpge 0x9090908f,%mm0             # gen=0f 0f 05 8f  dis=0f 0f 05 8f 90 90 90
    pfcmpge 0x90909091,%mm0             # gen=0f 0f 05 91  dis=0f 0f 05 91 90 90 90
    pfcmpge 0x90909092,%mm0             # gen=0f 0f 05 92  dis=0f 0f 05 92 90 90 90
    pfcmpge 0x90909093,%mm0             # gen=0f 0f 05 93  dis=0f 0f 05 93 90 90 90
    pfmin  %mm0,%mm0                    # gen=0f 0f c0 94  dis=0f 0f c0 94
    pfcmpge 0x90909094,%mm0             # gen=0f 0f 05 94  dis=0f 0f 05 94 90 90 90
    pfmin  %mm0,%mm1                    # gen=0f 0f c8 94  dis=0f 0f c8 94
    pfmin  %mm0,%mm2                    # gen=0f 0f d0 94  dis=0f 0f d0 94
    pfmin  %mm0,%mm3                    # gen=0f 0f d8 94  dis=0f 0f d8 94
    pfmin  %mm0,%mm4                    # gen=0f 0f e0 94  dis=0f 0f e0 94
    pfmin  %mm0,%mm5                    # gen=0f 0f e8 94  dis=0f 0f e8 94
    pfmin  %mm0,%mm6                    # gen=0f 0f f0 94  dis=0f 0f f0 94
    pfmin  %mm0,%mm7                    # gen=0f 0f f8 94  dis=0f 0f f8 94
    pfcmpge 0x90909094,%mm1             # gen=0f 0f 0d 94  dis=0f 0f 0d 94 90 90 90
    pfcmpge 0x90909094,%mm2             # gen=0f 0f 15 94  dis=0f 0f 15 94 90 90 90
    pfcmpge 0x90909094,%mm3             # gen=0f 0f 1d 94  dis=0f 0f 1d 94 90 90 90
    pfcmpge 0x90909094,%mm4             # gen=0f 0f 25 94  dis=0f 0f 25 94 90 90 90
    pfcmpge 0x90909094,%mm5             # gen=0f 0f 2d 94  dis=0f 0f 2d 94 90 90 90
    pfcmpge 0x90909094,%mm6             # gen=0f 0f 35 94  dis=0f 0f 35 94 90 90 90
    pfcmpge 0x90909094,%mm7             # gen=0f 0f 3d 94  dis=0f 0f 3d 94 90 90 90
    pfcmpge 0x90909095,%mm0             # gen=0f 0f 05 95  dis=0f 0f 05 95 90 90 90
    pfrcp  %mm0,%mm0                    # gen=0f 0f c0 96  dis=0f 0f c0 96
    pfcmpge 0x90909096,%mm0             # gen=0f 0f 05 96  dis=0f 0f 05 96 90 90 90
    pfrcp  %mm0,%mm1                    # gen=0f 0f c8 96  dis=0f 0f c8 96
    pfrcp  %mm0,%mm2                    # gen=0f 0f d0 96  dis=0f 0f d0 96
    pfrcp  %mm0,%mm3                    # gen=0f 0f d8 96  dis=0f 0f d8 96
    pfrcp  %mm0,%mm4                    # gen=0f 0f e0 96  dis=0f 0f e0 96
    pfrcp  %mm0,%mm5                    # gen=0f 0f e8 96  dis=0f 0f e8 96
    pfrcp  %mm0,%mm6                    # gen=0f 0f f0 96  dis=0f 0f f0 96
    pfrcp  %mm0,%mm7                    # gen=0f 0f f8 96  dis=0f 0f f8 96
    pfcmpge 0x90909096,%mm1             # gen=0f 0f 0d 96  dis=0f 0f 0d 96 90 90 90
    pfcmpge 0x90909096,%mm2             # gen=0f 0f 15 96  dis=0f 0f 15 96 90 90 90
    pfcmpge 0x90909096,%mm3             # gen=0f 0f 1d 96  dis=0f 0f 1d 96 90 90 90
    pfcmpge 0x90909096,%mm4             # gen=0f 0f 25 96  dis=0f 0f 25 96 90 90 90
    pfcmpge 0x90909096,%mm5             # gen=0f 0f 2d 96  dis=0f 0f 2d 96 90 90 90
    pfcmpge 0x90909096,%mm6             # gen=0f 0f 35 96  dis=0f 0f 35 96 90 90 90
    pfcmpge 0x90909096,%mm7             # gen=0f 0f 3d 96  dis=0f 0f 3d 96 90 90 90
    pfrsqrt %mm0,%mm0                   # gen=0f 0f c0 97  dis=0f 0f c0 97
    pfcmpge 0x90909097,%mm0             # gen=0f 0f 05 97  dis=0f 0f 05 97 90 90 90
    pfrsqrt %mm0,%mm1                   # gen=0f 0f c8 97  dis=0f 0f c8 97
    pfrsqrt %mm0,%mm2                   # gen=0f 0f d0 97  dis=0f 0f d0 97
    pfrsqrt %mm0,%mm3                   # gen=0f 0f d8 97  dis=0f 0f d8 97
    pfrsqrt %mm0,%mm4                   # gen=0f 0f e0 97  dis=0f 0f e0 97
    pfrsqrt %mm0,%mm5                   # gen=0f 0f e8 97  dis=0f 0f e8 97
    pfrsqrt %mm0,%mm6                   # gen=0f 0f f0 97  dis=0f 0f f0 97
    pfrsqrt %mm0,%mm7                   # gen=0f 0f f8 97  dis=0f 0f f8 97
    pfcmpge 0x90909097,%mm1             # gen=0f 0f 0d 97  dis=0f 0f 0d 97 90 90 90
    pfcmpge 0x90909097,%mm2             # gen=0f 0f 15 97  dis=0f 0f 15 97 90 90 90
    pfcmpge 0x90909097,%mm3             # gen=0f 0f 1d 97  dis=0f 0f 1d 97 90 90 90
    pfcmpge 0x90909097,%mm4             # gen=0f 0f 25 97  dis=0f 0f 25 97 90 90 90
    pfcmpge 0x90909097,%mm5             # gen=0f 0f 2d 97  dis=0f 0f 2d 97 90 90 90
    pfcmpge 0x90909097,%mm6             # gen=0f 0f 35 97  dis=0f 0f 35 97 90 90 90
    pfcmpge 0x90909097,%mm7             # gen=0f 0f 3d 97  dis=0f 0f 3d 97 90 90 90
    pfcmpge 0x90909098,%mm0             # gen=0f 0f 05 98  dis=0f 0f 05 98 90 90 90
    pfcmpge 0x90909099,%mm0             # gen=0f 0f 05 99  dis=0f 0f 05 99 90 90 90
    pfsub  %mm0,%mm0                    # gen=0f 0f c0 9a  dis=0f 0f c0 9a
    pfcmpge 0x9090909a,%mm0             # gen=0f 0f 05 9a  dis=0f 0f 05 9a 90 90 90
    pfsub  %mm0,%mm1                    # gen=0f 0f c8 9a  dis=0f 0f c8 9a
    pfsub  %mm0,%mm2                    # gen=0f 0f d0 9a  dis=0f 0f d0 9a
    pfsub  %mm0,%mm3                    # gen=0f 0f d8 9a  dis=0f 0f d8 9a
    pfsub  %mm0,%mm4                    # gen=0f 0f e0 9a  dis=0f 0f e0 9a
    pfsub  %mm0,%mm5                    # gen=0f 0f e8 9a  dis=0f 0f e8 9a
    pfsub  %mm0,%mm6                    # gen=0f 0f f0 9a  dis=0f 0f f0 9a
    pfsub  %mm0,%mm7                    # gen=0f 0f f8 9a  dis=0f 0f f8 9a
    pfcmpge 0x9090909a,%mm1             # gen=0f 0f 0d 9a  dis=0f 0f 0d 9a 90 90 90
    pfcmpge 0x9090909a,%mm2             # gen=0f 0f 15 9a  dis=0f 0f 15 9a 90 90 90
    pfcmpge 0x9090909a,%mm3             # gen=0f 0f 1d 9a  dis=0f 0f 1d 9a 90 90 90
    pfcmpge 0x9090909a,%mm4             # gen=0f 0f 25 9a  dis=0f 0f 25 9a 90 90 90
    pfcmpge 0x9090909a,%mm5             # gen=0f 0f 2d 9a  dis=0f 0f 2d 9a 90 90 90
    pfcmpge 0x9090909a,%mm6             # gen=0f 0f 35 9a  dis=0f 0f 35 9a 90 90 90
    pfcmpge 0x9090909a,%mm7             # gen=0f 0f 3d 9a  dis=0f 0f 3d 9a 90 90 90
    pfcmpge 0x9090909b,%mm0             # gen=0f 0f 05 9b  dis=0f 0f 05 9b 90 90 90
    pfcmpge 0x9090909c,%mm0             # gen=0f 0f 05 9c  dis=0f 0f 05 9c 90 90 90
    pfcmpge 0x9090909d,%mm0             # gen=0f 0f 05 9d  dis=0f 0f 05 9d 90 90 90
    pfadd  %mm0,%mm0                    # gen=0f 0f c0 9e  dis=0f 0f c0 9e
    pfcmpge 0x9090909e,%mm0             # gen=0f 0f 05 9e  dis=0f 0f 05 9e 90 90 90
    pfadd  %mm0,%mm1                    # gen=0f 0f c8 9e  dis=0f 0f c8 9e
    pfadd  %mm0,%mm2                    # gen=0f 0f d0 9e  dis=0f 0f d0 9e
    pfadd  %mm0,%mm3                    # gen=0f 0f d8 9e  dis=0f 0f d8 9e
    pfadd  %mm0,%mm4                    # gen=0f 0f e0 9e  dis=0f 0f e0 9e
    pfadd  %mm0,%mm5                    # gen=0f 0f e8 9e  dis=0f 0f e8 9e
    pfadd  %mm0,%mm6                    # gen=0f 0f f0 9e  dis=0f 0f f0 9e
    pfadd  %mm0,%mm7                    # gen=0f 0f f8 9e  dis=0f 0f f8 9e
    pfcmpge 0x9090909e,%mm1             # gen=0f 0f 0d 9e  dis=0f 0f 0d 9e 90 90 90
    pfcmpge 0x9090909e,%mm2             # gen=0f 0f 15 9e  dis=0f 0f 15 9e 90 90 90
    pfcmpge 0x9090909e,%mm3             # gen=0f 0f 1d 9e  dis=0f 0f 1d 9e 90 90 90
    pfcmpge 0x9090909e,%mm4             # gen=0f 0f 25 9e  dis=0f 0f 25 9e 90 90 90
    pfcmpge 0x9090909e,%mm5             # gen=0f 0f 2d 9e  dis=0f 0f 2d 9e 90 90 90
    pfcmpge 0x9090909e,%mm6             # gen=0f 0f 35 9e  dis=0f 0f 35 9e 90 90 90
    pfcmpge 0x9090909e,%mm7             # gen=0f 0f 3d 9e  dis=0f 0f 3d 9e 90 90 90
    pfcmpge 0x9090909f,%mm0             # gen=0f 0f 05 9f  dis=0f 0f 05 9f 90 90 90
    pfcmpgt %mm0,%mm0                   # gen=0f 0f c0 a0  dis=0f 0f c0 a0
    pfcmpge 0x909090a0,%mm0             # gen=0f 0f 05 a0  dis=0f 0f 05 a0 90 90 90
    pfcmpgt %mm0,%mm1                   # gen=0f 0f c8 a0  dis=0f 0f c8 a0
    pfcmpgt %mm0,%mm2                   # gen=0f 0f d0 a0  dis=0f 0f d0 a0
    pfcmpgt %mm0,%mm3                   # gen=0f 0f d8 a0  dis=0f 0f d8 a0
    pfcmpgt %mm0,%mm4                   # gen=0f 0f e0 a0  dis=0f 0f e0 a0
    pfcmpgt %mm0,%mm5                   # gen=0f 0f e8 a0  dis=0f 0f e8 a0
    pfcmpgt %mm0,%mm6                   # gen=0f 0f f0 a0  dis=0f 0f f0 a0
    pfcmpgt %mm0,%mm7                   # gen=0f 0f f8 a0  dis=0f 0f f8 a0
    pfcmpge 0x909090a0,%mm1             # gen=0f 0f 0d a0  dis=0f 0f 0d a0 90 90 90
    pfcmpge 0x909090a0,%mm2             # gen=0f 0f 15 a0  dis=0f 0f 15 a0 90 90 90
    pfcmpge 0x909090a0,%mm3             # gen=0f 0f 1d a0  dis=0f 0f 1d a0 90 90 90
    pfcmpge 0x909090a0,%mm4             # gen=0f 0f 25 a0  dis=0f 0f 25 a0 90 90 90
    pfcmpge 0x909090a0,%mm5             # gen=0f 0f 2d a0  dis=0f 0f 2d a0 90 90 90
    pfcmpge 0x909090a0,%mm6             # gen=0f 0f 35 a0  dis=0f 0f 35 a0 90 90 90
    pfcmpge 0x909090a0,%mm7             # gen=0f 0f 3d a0  dis=0f 0f 3d a0 90 90 90
    pfcmpge 0x909090a1,%mm0             # gen=0f 0f 05 a1  dis=0f 0f 05 a1 90 90 90
    pfcmpge 0x909090a2,%mm0             # gen=0f 0f 05 a2  dis=0f 0f 05 a2 90 90 90
    pfcmpge 0x909090a3,%mm0             # gen=0f 0f 05 a3  dis=0f 0f 05 a3 90 90 90
    pfmax  %mm0,%mm0                    # gen=0f 0f c0 a4  dis=0f 0f c0 a4
    pfcmpge 0x909090a4,%mm0             # gen=0f 0f 05 a4  dis=0f 0f 05 a4 90 90 90
    pfmax  %mm0,%mm1                    # gen=0f 0f c8 a4  dis=0f 0f c8 a4
    pfmax  %mm0,%mm2                    # gen=0f 0f d0 a4  dis=0f 0f d0 a4
    pfmax  %mm0,%mm3                    # gen=0f 0f d8 a4  dis=0f 0f d8 a4
    pfmax  %mm0,%mm4                    # gen=0f 0f e0 a4  dis=0f 0f e0 a4
    pfmax  %mm0,%mm5                    # gen=0f 0f e8 a4  dis=0f 0f e8 a4
    pfmax  %mm0,%mm6                    # gen=0f 0f f0 a4  dis=0f 0f f0 a4
    pfmax  %mm0,%mm7                    # gen=0f 0f f8 a4  dis=0f 0f f8 a4
    pfcmpge 0x909090a4,%mm1             # gen=0f 0f 0d a4  dis=0f 0f 0d a4 90 90 90
    pfcmpge 0x909090a4,%mm2             # gen=0f 0f 15 a4  dis=0f 0f 15 a4 90 90 90
    pfcmpge 0x909090a4,%mm3             # gen=0f 0f 1d a4  dis=0f 0f 1d a4 90 90 90
    pfcmpge 0x909090a4,%mm4             # gen=0f 0f 25 a4  dis=0f 0f 25 a4 90 90 90
    pfcmpge 0x909090a4,%mm5             # gen=0f 0f 2d a4  dis=0f 0f 2d a4 90 90 90
    pfcmpge 0x909090a4,%mm6             # gen=0f 0f 35 a4  dis=0f 0f 35 a4 90 90 90
    pfcmpge 0x909090a4,%mm7             # gen=0f 0f 3d a4  dis=0f 0f 3d a4 90 90 90
    pfcmpge 0x909090a5,%mm0             # gen=0f 0f 05 a5  dis=0f 0f 05 a5 90 90 90
    pfrcpit1 %mm0,%mm0                  # gen=0f 0f c0 a6  dis=0f 0f c0 a6
    pfcmpge 0x909090a6,%mm0             # gen=0f 0f 05 a6  dis=0f 0f 05 a6 90 90 90
    pfrcpit1 %mm0,%mm1                  # gen=0f 0f c8 a6  dis=0f 0f c8 a6
    pfrcpit1 %mm0,%mm2                  # gen=0f 0f d0 a6  dis=0f 0f d0 a6
    pfrcpit1 %mm0,%mm3                  # gen=0f 0f d8 a6  dis=0f 0f d8 a6
    pfrcpit1 %mm0,%mm4                  # gen=0f 0f e0 a6  dis=0f 0f e0 a6
    pfrcpit1 %mm0,%mm5                  # gen=0f 0f e8 a6  dis=0f 0f e8 a6
    pfrcpit1 %mm0,%mm6                  # gen=0f 0f f0 a6  dis=0f 0f f0 a6
    pfrcpit1 %mm0,%mm7                  # gen=0f 0f f8 a6  dis=0f 0f f8 a6
    pfcmpge 0x909090a6,%mm1             # gen=0f 0f 0d a6  dis=0f 0f 0d a6 90 90 90
    pfcmpge 0x909090a6,%mm2             # gen=0f 0f 15 a6  dis=0f 0f 15 a6 90 90 90
    pfcmpge 0x909090a6,%mm3             # gen=0f 0f 1d a6  dis=0f 0f 1d a6 90 90 90
    pfcmpge 0x909090a6,%mm4             # gen=0f 0f 25 a6  dis=0f 0f 25 a6 90 90 90
    pfcmpge 0x909090a6,%mm5             # gen=0f 0f 2d a6  dis=0f 0f 2d a6 90 90 90
    pfcmpge 0x909090a6,%mm6             # gen=0f 0f 35 a6  dis=0f 0f 35 a6 90 90 90
    pfcmpge 0x909090a6,%mm7             # gen=0f 0f 3d a6  dis=0f 0f 3d a6 90 90 90
    pfrsqit1 %mm0,%mm0                  # gen=0f 0f c0 a7  dis=0f 0f c0 a7
    pfcmpge 0x909090a7,%mm0             # gen=0f 0f 05 a7  dis=0f 0f 05 a7 90 90 90
    pfrsqit1 %mm0,%mm1                  # gen=0f 0f c8 a7  dis=0f 0f c8 a7
    pfrsqit1 %mm0,%mm2                  # gen=0f 0f d0 a7  dis=0f 0f d0 a7
    pfrsqit1 %mm0,%mm3                  # gen=0f 0f d8 a7  dis=0f 0f d8 a7
    pfrsqit1 %mm0,%mm4                  # gen=0f 0f e0 a7  dis=0f 0f e0 a7
    pfrsqit1 %mm0,%mm5                  # gen=0f 0f e8 a7  dis=0f 0f e8 a7
    pfrsqit1 %mm0,%mm6                  # gen=0f 0f f0 a7  dis=0f 0f f0 a7
    pfrsqit1 %mm0,%mm7                  # gen=0f 0f f8 a7  dis=0f 0f f8 a7
    pfcmpge 0x909090a7,%mm1             # gen=0f 0f 0d a7  dis=0f 0f 0d a7 90 90 90
    pfcmpge 0x909090a7,%mm2             # gen=0f 0f 15 a7  dis=0f 0f 15 a7 90 90 90
    pfcmpge 0x909090a7,%mm3             # gen=0f 0f 1d a7  dis=0f 0f 1d a7 90 90 90
    pfcmpge 0x909090a7,%mm4             # gen=0f 0f 25 a7  dis=0f 0f 25 a7 90 90 90
    pfcmpge 0x909090a7,%mm5             # gen=0f 0f 2d a7  dis=0f 0f 2d a7 90 90 90
    pfcmpge 0x909090a7,%mm6             # gen=0f 0f 35 a7  dis=0f 0f 35 a7 90 90 90
    pfcmpge 0x909090a7,%mm7             # gen=0f 0f 3d a7  dis=0f 0f 3d a7 90 90 90
    pfcmpge 0x909090a8,%mm0             # gen=0f 0f 05 a8  dis=0f 0f 05 a8 90 90 90
    pfcmpge 0x909090a9,%mm0             # gen=0f 0f 05 a9  dis=0f 0f 05 a9 90 90 90
    pfsubr %mm0,%mm0                    # gen=0f 0f c0 aa  dis=0f 0f c0 aa
    pfcmpge 0x909090aa,%mm0             # gen=0f 0f 05 aa  dis=0f 0f 05 aa 90 90 90
    pfsubr %mm0,%mm1                    # gen=0f 0f c8 aa  dis=0f 0f c8 aa
    pfsubr %mm0,%mm2                    # gen=0f 0f d0 aa  dis=0f 0f d0 aa
    pfsubr %mm0,%mm3                    # gen=0f 0f d8 aa  dis=0f 0f d8 aa
    pfsubr %mm0,%mm4                    # gen=0f 0f e0 aa  dis=0f 0f e0 aa
    pfsubr %mm0,%mm5                    # gen=0f 0f e8 aa  dis=0f 0f e8 aa
    pfsubr %mm0,%mm6                    # gen=0f 0f f0 aa  dis=0f 0f f0 aa
    pfsubr %mm0,%mm7                    # gen=0f 0f f8 aa  dis=0f 0f f8 aa
    pfcmpge 0x909090aa,%mm1             # gen=0f 0f 0d aa  dis=0f 0f 0d aa 90 90 90
    pfcmpge 0x909090aa,%mm2             # gen=0f 0f 15 aa  dis=0f 0f 15 aa 90 90 90
    pfcmpge 0x909090aa,%mm3             # gen=0f 0f 1d aa  dis=0f 0f 1d aa 90 90 90
    pfcmpge 0x909090aa,%mm4             # gen=0f 0f 25 aa  dis=0f 0f 25 aa 90 90 90
    pfcmpge 0x909090aa,%mm5             # gen=0f 0f 2d aa  dis=0f 0f 2d aa 90 90 90
    pfcmpge 0x909090aa,%mm6             # gen=0f 0f 35 aa  dis=0f 0f 35 aa 90 90 90
    pfcmpge 0x909090aa,%mm7             # gen=0f 0f 3d aa  dis=0f 0f 3d aa 90 90 90
    pfcmpge 0x909090ab,%mm0             # gen=0f 0f 05 ab  dis=0f 0f 05 ab 90 90 90
    pfcmpge 0x909090ac,%mm0             # gen=0f 0f 05 ac  dis=0f 0f 05 ac 90 90 90
    pfcmpge 0x909090ad,%mm0             # gen=0f 0f 05 ad  dis=0f 0f 05 ad 90 90 90
    pfacc  %mm0,%mm0                    # gen=0f 0f c0 ae  dis=0f 0f c0 ae
    pfcmpge 0x909090ae,%mm0             # gen=0f 0f 05 ae  dis=0f 0f 05 ae 90 90 90
    pfacc  %mm0,%mm1                    # gen=0f 0f c8 ae  dis=0f 0f c8 ae
    pfacc  %mm0,%mm2                    # gen=0f 0f d0 ae  dis=0f 0f d0 ae
    pfacc  %mm0,%mm3                    # gen=0f 0f d8 ae  dis=0f 0f d8 ae
    pfacc  %mm0,%mm4                    # gen=0f 0f e0 ae  dis=0f 0f e0 ae
    pfacc  %mm0,%mm5                    # gen=0f 0f e8 ae  dis=0f 0f e8 ae
    pfacc  %mm0,%mm6                    # gen=0f 0f f0 ae  dis=0f 0f f0 ae
    pfacc  %mm0,%mm7                    # gen=0f 0f f8 ae  dis=0f 0f f8 ae
    pfcmpge 0x909090ae,%mm1             # gen=0f 0f 0d ae  dis=0f 0f 0d ae 90 90 90
    pfcmpge 0x909090ae,%mm2             # gen=0f 0f 15 ae  dis=0f 0f 15 ae 90 90 90
    pfcmpge 0x909090ae,%mm3             # gen=0f 0f 1d ae  dis=0f 0f 1d ae 90 90 90
    pfcmpge 0x909090ae,%mm4             # gen=0f 0f 25 ae  dis=0f 0f 25 ae 90 90 90
    pfcmpge 0x909090ae,%mm5             # gen=0f 0f 2d ae  dis=0f 0f 2d ae 90 90 90
    pfcmpge 0x909090ae,%mm6             # gen=0f 0f 35 ae  dis=0f 0f 35 ae 90 90 90
    pfcmpge 0x909090ae,%mm7             # gen=0f 0f 3d ae  dis=0f 0f 3d ae 90 90 90
    pfcmpge 0x909090af,%mm0             # gen=0f 0f 05 af  dis=0f 0f 05 af 90 90 90
    pfcmpeq %mm0,%mm0                   # gen=0f 0f c0 b0  dis=0f 0f c0 b0
    pfcmpge 0x909090b0,%mm0             # gen=0f 0f 05 b0  dis=0f 0f 05 b0 90 90 90
    pfcmpeq %mm0,%mm1                   # gen=0f 0f c8 b0  dis=0f 0f c8 b0
    pfcmpeq %mm0,%mm2                   # gen=0f 0f d0 b0  dis=0f 0f d0 b0
    pfcmpeq %mm0,%mm3                   # gen=0f 0f d8 b0  dis=0f 0f d8 b0
    pfcmpeq %mm0,%mm4                   # gen=0f 0f e0 b0  dis=0f 0f e0 b0
    pfcmpeq %mm0,%mm5                   # gen=0f 0f e8 b0  dis=0f 0f e8 b0
    pfcmpeq %mm0,%mm6                   # gen=0f 0f f0 b0  dis=0f 0f f0 b0
    pfcmpeq %mm0,%mm7                   # gen=0f 0f f8 b0  dis=0f 0f f8 b0
    pfcmpge 0x909090b0,%mm1             # gen=0f 0f 0d b0  dis=0f 0f 0d b0 90 90 90
    pfcmpge 0x909090b0,%mm2             # gen=0f 0f 15 b0  dis=0f 0f 15 b0 90 90 90
    pfcmpge 0x909090b0,%mm3             # gen=0f 0f 1d b0  dis=0f 0f 1d b0 90 90 90
    pfcmpge 0x909090b0,%mm4             # gen=0f 0f 25 b0  dis=0f 0f 25 b0 90 90 90
    pfcmpge 0x909090b0,%mm5             # gen=0f 0f 2d b0  dis=0f 0f 2d b0 90 90 90
    pfcmpge 0x909090b0,%mm6             # gen=0f 0f 35 b0  dis=0f 0f 35 b0 90 90 90
    pfcmpge 0x909090b0,%mm7             # gen=0f 0f 3d b0  dis=0f 0f 3d b0 90 90 90
    pfcmpge 0x909090b1,%mm0             # gen=0f 0f 05 b1  dis=0f 0f 05 b1 90 90 90
    pfcmpge 0x909090b2,%mm0             # gen=0f 0f 05 b2  dis=0f 0f 05 b2 90 90 90
    pfcmpge 0x909090b3,%mm0             # gen=0f 0f 05 b3  dis=0f 0f 05 b3 90 90 90
    pfmul  %mm0,%mm0                    # gen=0f 0f c0 b4  dis=0f 0f c0 b4
    pfcmpge 0x909090b4,%mm0             # gen=0f 0f 05 b4  dis=0f 0f 05 b4 90 90 90
    pfmul  %mm0,%mm1                    # gen=0f 0f c8 b4  dis=0f 0f c8 b4
    pfmul  %mm0,%mm2                    # gen=0f 0f d0 b4  dis=0f 0f d0 b4
    pfmul  %mm0,%mm3                    # gen=0f 0f d8 b4  dis=0f 0f d8 b4
    pfmul  %mm0,%mm4                    # gen=0f 0f e0 b4  dis=0f 0f e0 b4
    pfmul  %mm0,%mm5                    # gen=0f 0f e8 b4  dis=0f 0f e8 b4
    pfmul  %mm0,%mm6                    # gen=0f 0f f0 b4  dis=0f 0f f0 b4
    pfmul  %mm0,%mm7                    # gen=0f 0f f8 b4  dis=0f 0f f8 b4
    pfcmpge 0x909090b4,%mm1             # gen=0f 0f 0d b4  dis=0f 0f 0d b4 90 90 90
    pfcmpge 0x909090b4,%mm2             # gen=0f 0f 15 b4  dis=0f 0f 15 b4 90 90 90
    pfcmpge 0x909090b4,%mm3             # gen=0f 0f 1d b4  dis=0f 0f 1d b4 90 90 90
    pfcmpge 0x909090b4,%mm4             # gen=0f 0f 25 b4  dis=0f 0f 25 b4 90 90 90
    pfcmpge 0x909090b4,%mm5             # gen=0f 0f 2d b4  dis=0f 0f 2d b4 90 90 90
    pfcmpge 0x909090b4,%mm6             # gen=0f 0f 35 b4  dis=0f 0f 35 b4 90 90 90
    pfcmpge 0x909090b4,%mm7             # gen=0f 0f 3d b4  dis=0f 0f 3d b4 90 90 90
    pfcmpge 0x909090b5,%mm0             # gen=0f 0f 05 b5  dis=0f 0f 05 b5 90 90 90
    pfrcpit2 %mm0,%mm0                  # gen=0f 0f c0 b6  dis=0f 0f c0 b6
    pfcmpge 0x909090b6,%mm0             # gen=0f 0f 05 b6  dis=0f 0f 05 b6 90 90 90
    pfrcpit2 %mm0,%mm1                  # gen=0f 0f c8 b6  dis=0f 0f c8 b6
    pfrcpit2 %mm0,%mm2                  # gen=0f 0f d0 b6  dis=0f 0f d0 b6
    pfrcpit2 %mm0,%mm3                  # gen=0f 0f d8 b6  dis=0f 0f d8 b6
    pfrcpit2 %mm0,%mm4                  # gen=0f 0f e0 b6  dis=0f 0f e0 b6
    pfrcpit2 %mm0,%mm5                  # gen=0f 0f e8 b6  dis=0f 0f e8 b6
    pfrcpit2 %mm0,%mm6                  # gen=0f 0f f0 b6  dis=0f 0f f0 b6
    pfrcpit2 %mm0,%mm7                  # gen=0f 0f f8 b6  dis=0f 0f f8 b6
    pfcmpge 0x909090b6,%mm1             # gen=0f 0f 0d b6  dis=0f 0f 0d b6 90 90 90
    pfcmpge 0x909090b6,%mm2             # gen=0f 0f 15 b6  dis=0f 0f 15 b6 90 90 90
    pfcmpge 0x909090b6,%mm3             # gen=0f 0f 1d b6  dis=0f 0f 1d b6 90 90 90
    pfcmpge 0x909090b6,%mm4             # gen=0f 0f 25 b6  dis=0f 0f 25 b6 90 90 90
    pfcmpge 0x909090b6,%mm5             # gen=0f 0f 2d b6  dis=0f 0f 2d b6 90 90 90
    pfcmpge 0x909090b6,%mm6             # gen=0f 0f 35 b6  dis=0f 0f 35 b6 90 90 90
    pfcmpge 0x909090b6,%mm7             # gen=0f 0f 3d b6  dis=0f 0f 3d b6 90 90 90
    pmulhrw %mm0,%mm0                   # gen=0f 0f c0 b7  dis=0f 0f c0 b7
    pfcmpge 0x909090b7,%mm0             # gen=0f 0f 05 b7  dis=0f 0f 05 b7 90 90 90
    pmulhrw %mm0,%mm1                   # gen=0f 0f c8 b7  dis=0f 0f c8 b7
    pmulhrw %mm0,%mm2                   # gen=0f 0f d0 b7  dis=0f 0f d0 b7
    pmulhrw %mm0,%mm3                   # gen=0f 0f d8 b7  dis=0f 0f d8 b7
    pmulhrw %mm0,%mm4                   # gen=0f 0f e0 b7  dis=0f 0f e0 b7
    pmulhrw %mm0,%mm5                   # gen=0f 0f e8 b7  dis=0f 0f e8 b7
    pmulhrw %mm0,%mm6                   # gen=0f 0f f0 b7  dis=0f 0f f0 b7
    pmulhrw %mm0,%mm7                   # gen=0f 0f f8 b7  dis=0f 0f f8 b7
    pfcmpge 0x909090b7,%mm1             # gen=0f 0f 0d b7  dis=0f 0f 0d b7 90 90 90
    pfcmpge 0x909090b7,%mm2             # gen=0f 0f 15 b7  dis=0f 0f 15 b7 90 90 90
    pfcmpge 0x909090b7,%mm3             # gen=0f 0f 1d b7  dis=0f 0f 1d b7 90 90 90
    pfcmpge 0x909090b7,%mm4             # gen=0f 0f 25 b7  dis=0f 0f 25 b7 90 90 90
    pfcmpge 0x909090b7,%mm5             # gen=0f 0f 2d b7  dis=0f 0f 2d b7 90 90 90
    pfcmpge 0x909090b7,%mm6             # gen=0f 0f 35 b7  dis=0f 0f 35 b7 90 90 90
    pfcmpge 0x909090b7,%mm7             # gen=0f 0f 3d b7  dis=0f 0f 3d b7 90 90 90
    pfcmpge 0x909090b8,%mm0             # gen=0f 0f 05 b8  dis=0f 0f 05 b8 90 90 90
    pfcmpge 0x909090b9,%mm0             # gen=0f 0f 05 b9  dis=0f 0f 05 b9 90 90 90
    pfcmpge 0x909090ba,%mm0             # gen=0f 0f 05 ba  dis=0f 0f 05 ba 90 90 90
    pswapd %mm0,%mm0                    # gen=0f 0f c0 bb  dis=0f 0f c0 bb
    pfcmpge 0x909090bb,%mm0             # gen=0f 0f 05 bb  dis=0f 0f 05 bb 90 90 90
    pswapd %mm0,%mm1                    # gen=0f 0f c8 bb  dis=0f 0f c8 bb
    pswapd %mm0,%mm2                    # gen=0f 0f d0 bb  dis=0f 0f d0 bb
    pswapd %mm0,%mm3                    # gen=0f 0f d8 bb  dis=0f 0f d8 bb
    pswapd %mm0,%mm4                    # gen=0f 0f e0 bb  dis=0f 0f e0 bb
    pswapd %mm0,%mm5                    # gen=0f 0f e8 bb  dis=0f 0f e8 bb
    pswapd %mm0,%mm6                    # gen=0f 0f f0 bb  dis=0f 0f f0 bb
    pswapd %mm0,%mm7                    # gen=0f 0f f8 bb  dis=0f 0f f8 bb
    pfcmpge 0x909090bb,%mm1             # gen=0f 0f 0d bb  dis=0f 0f 0d bb 90 90 90
    pfcmpge 0x909090bb,%mm2             # gen=0f 0f 15 bb  dis=0f 0f 15 bb 90 90 90
    pfcmpge 0x909090bb,%mm3             # gen=0f 0f 1d bb  dis=0f 0f 1d bb 90 90 90
    pfcmpge 0x909090bb,%mm4             # gen=0f 0f 25 bb  dis=0f 0f 25 bb 90 90 90
    pfcmpge 0x909090bb,%mm5             # gen=0f 0f 2d bb  dis=0f 0f 2d bb 90 90 90
    pfcmpge 0x909090bb,%mm6             # gen=0f 0f 35 bb  dis=0f 0f 35 bb 90 90 90
    pfcmpge 0x909090bb,%mm7             # gen=0f 0f 3d bb  dis=0f 0f 3d bb 90 90 90
    pfcmpge 0x909090bc,%mm0             # gen=0f 0f 05 bc  dis=0f 0f 05 bc 90 90 90
    pfcmpge 0x909090bd,%mm0             # gen=0f 0f 05 bd  dis=0f 0f 05 bd 90 90 90
    pfcmpge 0x909090be,%mm0             # gen=0f 0f 05 be  dis=0f 0f 05 be 90 90 90
    pavgusb %mm0,%mm0                   # gen=0f 0f c0 bf  dis=0f 0f c0 bf
    pfcmpge 0x909090bf,%mm0             # gen=0f 0f 05 bf  dis=0f 0f 05 bf 90 90 90
    pavgusb %mm0,%mm1                   # gen=0f 0f c8 bf  dis=0f 0f c8 bf
    pavgusb %mm0,%mm2                   # gen=0f 0f d0 bf  dis=0f 0f d0 bf
    pavgusb %mm0,%mm3                   # gen=0f 0f d8 bf  dis=0f 0f d8 bf
    pavgusb %mm0,%mm4                   # gen=0f 0f e0 bf  dis=0f 0f e0 bf
    pavgusb %mm0,%mm5                   # gen=0f 0f e8 bf  dis=0f 0f e8 bf
    pavgusb %mm0,%mm6                   # gen=0f 0f f0 bf  dis=0f 0f f0 bf
    pavgusb %mm0,%mm7                   # gen=0f 0f f8 bf  dis=0f 0f f8 bf
    pfcmpge 0x909090bf,%mm1             # gen=0f 0f 0d bf  dis=0f 0f 0d bf 90 90 90
    pfcmpge 0x909090bf,%mm2             # gen=0f 0f 15 bf  dis=0f 0f 15 bf 90 90 90
    pfcmpge 0x909090bf,%mm3             # gen=0f 0f 1d bf  dis=0f 0f 1d bf 90 90 90
    pfcmpge 0x909090bf,%mm4             # gen=0f 0f 25 bf  dis=0f 0f 25 bf 90 90 90
    pfcmpge 0x909090bf,%mm5             # gen=0f 0f 2d bf  dis=0f 0f 2d bf 90 90 90
    pfcmpge 0x909090bf,%mm6             # gen=0f 0f 35 bf  dis=0f 0f 35 bf 90 90 90
    pfcmpge 0x909090bf,%mm7             # gen=0f 0f 3d bf  dis=0f 0f 3d bf 90 90 90
    pfcmpge 0x909090c0,%mm0             # gen=0f 0f 05 c0  dis=0f 0f 05 c0 90 90 90
    pfcmpge 0x909090c1,%mm0             # gen=0f 0f 05 c1  dis=0f 0f 05 c1 90 90 90
    pfcmpge 0x909090c2,%mm0             # gen=0f 0f 05 c2  dis=0f 0f 05 c2 90 90 90
    pfcmpge 0x909090c3,%mm0             # gen=0f 0f 05 c3  dis=0f 0f 05 c3 90 90 90
    pfcmpge 0x909090c4,%mm0             # gen=0f 0f 05 c4  dis=0f 0f 05 c4 90 90 90
    pfcmpge 0x909090c5,%mm0             # gen=0f 0f 05 c5  dis=0f 0f 05 c5 90 90 90
    pfcmpge 0x909090c6,%mm0             # gen=0f 0f 05 c6  dis=0f 0f 05 c6 90 90 90
    pfcmpge 0x909090c7,%mm0             # gen=0f 0f 05 c7  dis=0f 0f 05 c7 90 90 90
    pfcmpge 0x909090c8,%mm0             # gen=0f 0f 05 c8  dis=0f 0f 05 c8 90 90 90
    pfcmpge 0x909090c9,%mm0             # gen=0f 0f 05 c9  dis=0f 0f 05 c9 90 90 90
    pfcmpge 0x909090ca,%mm0             # gen=0f 0f 05 ca  dis=0f 0f 05 ca 90 90 90
    pfcmpge 0x909090cb,%mm0             # gen=0f 0f 05 cb  dis=0f 0f 05 cb 90 90 90
    pfcmpge 0x909090cc,%mm0             # gen=0f 0f 05 cc  dis=0f 0f 05 cc 90 90 90
    pfcmpge 0x909090cd,%mm0             # gen=0f 0f 05 cd  dis=0f 0f 05 cd 90 90 90
    pfcmpge 0x909090ce,%mm0             # gen=0f 0f 05 ce  dis=0f 0f 05 ce 90 90 90
    pfcmpge 0x909090cf,%mm0             # gen=0f 0f 05 cf  dis=0f 0f 05 cf 90 90 90
    pfcmpge 0x909090d0,%mm0             # gen=0f 0f 05 d0  dis=0f 0f 05 d0 90 90 90
    pfcmpge 0x909090d1,%mm0             # gen=0f 0f 05 d1  dis=0f 0f 05 d1 90 90 90
    pfcmpge 0x909090d2,%mm0             # gen=0f 0f 05 d2  dis=0f 0f 05 d2 90 90 90
    pfcmpge 0x909090d3,%mm0             # gen=0f 0f 05 d3  dis=0f 0f 05 d3 90 90 90
    pfcmpge 0x909090d4,%mm0             # gen=0f 0f 05 d4  dis=0f 0f 05 d4 90 90 90
    pfcmpge 0x909090d5,%mm0             # gen=0f 0f 05 d5  dis=0f 0f 05 d5 90 90 90
    pfcmpge 0x909090d6,%mm0             # gen=0f 0f 05 d6  dis=0f 0f 05 d6 90 90 90
    pfcmpge 0x909090d7,%mm0             # gen=0f 0f 05 d7  dis=0f 0f 05 d7 90 90 90
    pfcmpge 0x909090d8,%mm0             # gen=0f 0f 05 d8  dis=0f 0f 05 d8 90 90 90
    pfcmpge 0x909090d9,%mm0             # gen=0f 0f 05 d9  dis=0f 0f 05 d9 90 90 90
    pfcmpge 0x909090da,%mm0             # gen=0f 0f 05 da  dis=0f 0f 05 da 90 90 90
    pfcmpge 0x909090db,%mm0             # gen=0f 0f 05 db  dis=0f 0f 05 db 90 90 90
    pfcmpge 0x909090dc,%mm0             # gen=0f 0f 05 dc  dis=0f 0f 05 dc 90 90 90
    pfcmpge 0x909090dd,%mm0             # gen=0f 0f 05 dd  dis=0f 0f 05 dd 90 90 90
    pfcmpge 0x909090de,%mm0             # gen=0f 0f 05 de  dis=0f 0f 05 de 90 90 90
    pfcmpge 0x909090df,%mm0             # gen=0f 0f 05 df  dis=0f 0f 05 df 90 90 90
    pfcmpge 0x909090e0,%mm0             # gen=0f 0f 05 e0  dis=0f 0f 05 e0 90 90 90
    pfcmpge 0x909090e1,%mm0             # gen=0f 0f 05 e1  dis=0f 0f 05 e1 90 90 90
    pfcmpge 0x909090e2,%mm0             # gen=0f 0f 05 e2  dis=0f 0f 05 e2 90 90 90
    pfcmpge 0x909090e3,%mm0             # gen=0f 0f 05 e3  dis=0f 0f 05 e3 90 90 90
    pfcmpge 0x909090e4,%mm0             # gen=0f 0f 05 e4  dis=0f 0f 05 e4 90 90 90
    pfcmpge 0x909090e5,%mm0             # gen=0f 0f 05 e5  dis=0f 0f 05 e5 90 90 90
    pfcmpge 0x909090e6,%mm0             # gen=0f 0f 05 e6  dis=0f 0f 05 e6 90 90 90
    pfcmpge 0x909090e7,%mm0             # gen=0f 0f 05 e7  dis=0f 0f 05 e7 90 90 90
    pfcmpge 0x909090e8,%mm0             # gen=0f 0f 05 e8  dis=0f 0f 05 e8 90 90 90
    pfcmpge 0x909090e9,%mm0             # gen=0f 0f 05 e9  dis=0f 0f 05 e9 90 90 90
    pfcmpge 0x909090ea,%mm0             # gen=0f 0f 05 ea  dis=0f 0f 05 ea 90 90 90
    pfcmpge 0x909090eb,%mm0             # gen=0f 0f 05 eb  dis=0f 0f 05 eb 90 90 90
    pfcmpge 0x909090ec,%mm0             # gen=0f 0f 05 ec  dis=0f 0f 05 ec 90 90 90
    pfcmpge 0x909090ed,%mm0             # gen=0f 0f 05 ed  dis=0f 0f 05 ed 90 90 90
    pfcmpge 0x909090ee,%mm0             # gen=0f 0f 05 ee  dis=0f 0f 05 ee 90 90 90
    pfcmpge 0x909090ef,%mm0             # gen=0f 0f 05 ef  dis=0f 0f 05 ef 90 90 90
    pfcmpge 0x909090f0,%mm0             # gen=0f 0f 05 f0  dis=0f 0f 05 f0 90 90 90
    pfcmpge 0x909090f1,%mm0             # gen=0f 0f 05 f1  dis=0f 0f 05 f1 90 90 90
    pfcmpge 0x909090f2,%mm0             # gen=0f 0f 05 f2  dis=0f 0f 05 f2 90 90 90
    pfcmpge 0x909090f3,%mm0             # gen=0f 0f 05 f3  dis=0f 0f 05 f3 90 90 90
    pfcmpge 0x909090f4,%mm0             # gen=0f 0f 05 f4  dis=0f 0f 05 f4 90 90 90
    pfcmpge 0x909090f5,%mm0             # gen=0f 0f 05 f5  dis=0f 0f 05 f5 90 90 90
    pfcmpge 0x909090f6,%mm0             # gen=0f 0f 05 f6  dis=0f 0f 05 f6 90 90 90
    pfcmpge 0x909090f7,%mm0             # gen=0f 0f 05 f7  dis=0f 0f 05 f7 90 90 90
    pfcmpge 0x909090f8,%mm0             # gen=0f 0f 05 f8  dis=0f 0f 05 f8 90 90 90
    pfcmpge 0x909090f9,%mm0             # gen=0f 0f 05 f9  dis=0f 0f 05 f9 90 90 90
    pfcmpge 0x909090fa,%mm0             # gen=0f 0f 05 fa  dis=0f 0f 05 fa 90 90 90
    pfcmpge 0x909090fb,%mm0             # gen=0f 0f 05 fb  dis=0f 0f 05 fb 90 90 90
    pfcmpge 0x909090fc,%mm0             # gen=0f 0f 05 fc  dis=0f 0f 05 fc 90 90 90
    pfcmpge 0x909090fd,%mm0             # gen=0f 0f 05 fd  dis=0f 0f 05 fd 90 90 90
    pfcmpge 0x909090fe,%mm0             # gen=0f 0f 05 fe  dis=0f 0f 05 fe 90 90 90
    pfcmpge 0x909090ff,%mm0             # gen=0f 0f 05 ff  dis=0f 0f 05 ff 90 90 90
# ---- Curated control-flow/prefix forms (labels + range forcing) ----
    # unconditional jumps (short then near)
    jmp 1f
1:
    jmp 1f
    .space 200
1:

    # near call
    call 1f
1:

    # loop-family (rel8-only)
    loop 1f
1:
    loope 1f
1:
    loopne 1f
1:
    jecxz 1f
1:

    # conditional jumps (short then near)
    jo 1f
1:
    jo 1f
    .space 200
1:

    jno 1f
1:
    jno 1f
    .space 200
1:

    jb 1f
1:
    jb 1f
    .space 200
1:

    jae 1f
1:
    jae 1f
    .space 200
1:

    je 1f
1:
    je 1f
    .space 200
1:

    jne 1f
1:
    jne 1f
    .space 200
1:

    jbe 1f
1:
    jbe 1f
    .space 200
1:

    ja 1f
1:
    ja 1f
    .space 200
1:

    js 1f
1:
    js 1f
    .space 200
1:

    jns 1f
1:
    jns 1f
    .space 200
1:

    jp 1f
1:
    jp 1f
    .space 200
1:

    jnp 1f
1:
    jnp 1f
    .space 200
1:

    jl 1f
1:
    jl 1f
    .space 200
1:

    jge 1f
1:
    jge 1f
    .space 200
1:

    jle 1f
1:
    jle 1f
    .space 200
1:

    jg 1f
1:
    jg 1f
    .space 200
1:

    # prefixes with string ops / pause
    rep movsb
    rep movsl
    rep stosb
    rep stosl
    repne scasb
    repe cmpsb
    pause

    # lock prefix on lockable ops
    lock addl $1,(%eax)
    lock xaddl %eax,(%ebx)

    # segment override examples
    movl %gs:(%eax),%eax
    movl %eax,%fs:(%ebx)

