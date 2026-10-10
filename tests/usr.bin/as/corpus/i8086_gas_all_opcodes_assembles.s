.text
.code16
.arch i8086
.globl _start
_start:
# Auto-generated i8086 opcode/mnemonic corpus (AT&T syntax) for GNU as (gas).

    add    %al,%al                      # gen=00 c0  dis=00 c0
    add    %al,-0x6f70                  # gen=00 06  dis=00 06 90 90
    add    %ax,%ax                      # gen=01 c0  dis=01 c0
    add    %ax,-0x6f70                  # gen=01 06  dis=01 06 90 90
    add    %al,%al                      # gen=02 c0  dis=02 c0
    add    -0x6f70,%al                  # gen=02 06  dis=02 06 90 90
    add    %ax,%ax                      # gen=03 c0  dis=03 c0
    add    -0x6f70,%ax                  # gen=03 06  dis=03 06 90 90
    add    $0xc0,%al                    # gen=04 c0  dis=04 c0
    add    $0x6,%al                     # gen=04 06  dis=04 06
    add    $0x90c0,%ax                  # gen=05 c0  dis=05 c0 90
    add    $0x9006,%ax                  # gen=05 06  dis=05 06 90
    push   %es                          # gen=06 c0  dis=06
    push   %es                          # gen=06 06  dis=06
    pop    %es                          # gen=07 c0  dis=07
    pop    %es                          # gen=07 06  dis=07
    or     %al,%al                      # gen=08 c0  dis=08 c0
    or     %al,-0x6f70                  # gen=08 06  dis=08 06 90 90
    or     %ax,%ax                      # gen=09 c0  dis=09 c0
    or     %ax,-0x6f70                  # gen=09 06  dis=09 06 90 90
    or     %al,%al                      # gen=0a c0  dis=0a c0
    or     -0x6f70,%al                  # gen=0a 06  dis=0a 06 90 90
    or     %ax,%ax                      # gen=0b c0  dis=0b c0
    or     -0x6f70,%ax                  # gen=0b 06  dis=0b 06 90 90
    or     $0xc0,%al                    # gen=0c c0  dis=0c c0
    or     $0x6,%al                     # gen=0c 06  dis=0c 06
    or     $0x90c0,%ax                  # gen=0d c0  dis=0d c0 90
    or     $0x9006,%ax                  # gen=0d 06  dis=0d 06 90
    push   %cs                          # gen=0e c0  dis=0e
    push   %cs                          # gen=0e 06  dis=0e
    .byte 0x0f,0xc0                 # fallback; gen=0f c0
    .byte 0x0f,0x06                 # fallback; gen=0f 06
    .byte 0x0f,0xc8                 # fallback; gen=0f c8
    .byte 0x0f,0xd0                 # fallback; gen=0f d0
    .byte 0x0f,0xd8                 # fallback; gen=0f d8
    .byte 0x0f,0xe0                 # fallback; gen=0f e0
    .byte 0x0f,0xe8                 # fallback; gen=0f e8
    .byte 0x0f,0xf0                 # fallback; gen=0f f0
    .byte 0x0f,0xf8                 # fallback; gen=0f f8
    .byte 0x0f,0x0e                 # fallback; gen=0f 0e
    .byte 0x0f,0x16                 # fallback; gen=0f 16
    .byte 0x0f,0x1e                 # fallback; gen=0f 1e
    .byte 0x0f,0x26                 # fallback; gen=0f 26
    .byte 0x0f,0x2e                 # fallback; gen=0f 2e
    .byte 0x0f,0x36                 # fallback; gen=0f 36
    .byte 0x0f,0x3e                 # fallback; gen=0f 3e
    adc    %al,%al                      # gen=10 c0  dis=10 c0
    adc    %al,-0x6f70                  # gen=10 06  dis=10 06 90 90
    adc    %ax,%ax                      # gen=11 c0  dis=11 c0
    adc    %ax,-0x6f70                  # gen=11 06  dis=11 06 90 90
    adc    %al,%al                      # gen=12 c0  dis=12 c0
    adc    -0x6f70,%al                  # gen=12 06  dis=12 06 90 90
    adc    %ax,%ax                      # gen=13 c0  dis=13 c0
    adc    -0x6f70,%ax                  # gen=13 06  dis=13 06 90 90
    adc    $0xc0,%al                    # gen=14 c0  dis=14 c0
    adc    $0x6,%al                     # gen=14 06  dis=14 06
    adc    $0x90c0,%ax                  # gen=15 c0  dis=15 c0 90
    adc    $0x9006,%ax                  # gen=15 06  dis=15 06 90
    push   %ss                          # gen=16 c0  dis=16
    push   %ss                          # gen=16 06  dis=16
    pop    %ss                          # gen=17 c0  dis=17
    pop    %ss                          # gen=17 06  dis=17
    sbb    %al,%al                      # gen=18 c0  dis=18 c0
    sbb    %al,-0x6f70                  # gen=18 06  dis=18 06 90 90
    sbb    %ax,%ax                      # gen=19 c0  dis=19 c0
    sbb    %ax,-0x6f70                  # gen=19 06  dis=19 06 90 90
    sbb    %al,%al                      # gen=1a c0  dis=1a c0
    sbb    -0x6f70,%al                  # gen=1a 06  dis=1a 06 90 90
    sbb    %ax,%ax                      # gen=1b c0  dis=1b c0
    sbb    -0x6f70,%ax                  # gen=1b 06  dis=1b 06 90 90
    sbb    $0xc0,%al                    # gen=1c c0  dis=1c c0
    sbb    $0x6,%al                     # gen=1c 06  dis=1c 06
    sbb    $0x90c0,%ax                  # gen=1d c0  dis=1d c0 90
    sbb    $0x9006,%ax                  # gen=1d 06  dis=1d 06 90
    push   %ds                          # gen=1e c0  dis=1e
    push   %ds                          # gen=1e 06  dis=1e
    pop    %ds                          # gen=1f c0  dis=1f
    pop    %ds                          # gen=1f 06  dis=1f
    and    %al,%al                      # gen=20 c0  dis=20 c0
    and    %al,-0x6f70                  # gen=20 06  dis=20 06 90 90
    and    %ax,%ax                      # gen=21 c0  dis=21 c0
    and    %ax,-0x6f70                  # gen=21 06  dis=21 06 90 90
    and    %al,%al                      # gen=22 c0  dis=22 c0
    and    -0x6f70,%al                  # gen=22 06  dis=22 06 90 90
    and    %ax,%ax                      # gen=23 c0  dis=23 c0
    and    -0x6f70,%ax                  # gen=23 06  dis=23 06 90 90
    and    $0xc0,%al                    # gen=24 c0  dis=24 c0
    and    $0x6,%al                     # gen=24 06  dis=24 06
    and    $0x90c0,%ax                  # gen=25 c0  dis=25 c0 90
    and    $0x9006,%ax                  # gen=25 06  dis=25 06 90
    .byte 0x26,0xc0                 # fallback; gen=26 c0
    es push %es                         # gen=26 06  dis=26 06
    .byte 0x26,0xc8                 # fallback; gen=26 c8
    rclb   $1,%es:-0x6f70(%bx,%si)      # gen=26 d0  dis=26 d0 90 90 90
    .byte 0x26,0xd8                 # fallback; gen=26 d8
    .byte 0x26,0xe0                 # fallback; gen=26 e0
    .byte 0x26,0xe8                 # fallback; gen=26 e8
    .byte 0x26,0xf0                 # fallback; gen=26 f0
    es clc                              # gen=26 f8  dis=26 f8
    es push %cs                         # gen=26 0e  dis=26 0e
    es push %ss                         # gen=26 16  dis=26 16
    es push %ds                         # gen=26 1e  dis=26 1e
    .byte 0x26,0x26                 # fallback; gen=26 26
    .byte 0x26,0x2e                 # fallback; gen=26 2e
    .byte 0x26,0x36                 # fallback; gen=26 36
    .byte 0x26,0x3e                 # fallback; gen=26 3e
    daa                                 # gen=27 c0  dis=27
    daa                                 # gen=27 06  dis=27
    sub    %al,%al                      # gen=28 c0  dis=28 c0
    sub    %al,-0x6f70                  # gen=28 06  dis=28 06 90 90
    sub    %ax,%ax                      # gen=29 c0  dis=29 c0
    sub    %ax,-0x6f70                  # gen=29 06  dis=29 06 90 90
    sub    %al,%al                      # gen=2a c0  dis=2a c0
    sub    -0x6f70,%al                  # gen=2a 06  dis=2a 06 90 90
    sub    %ax,%ax                      # gen=2b c0  dis=2b c0
    sub    -0x6f70,%ax                  # gen=2b 06  dis=2b 06 90 90
    sub    $0xc0,%al                    # gen=2c c0  dis=2c c0
    sub    $0x6,%al                     # gen=2c 06  dis=2c 06
    sub    $0x90c0,%ax                  # gen=2d c0  dis=2d c0 90
    sub    $0x9006,%ax                  # gen=2d 06  dis=2d 06 90
    .byte 0x2e,0xc0                 # fallback; gen=2e c0
    cs push %es                         # gen=2e 06  dis=2e 06
    .byte 0x2e,0xc8                 # fallback; gen=2e c8
    rclb   $1,%cs:-0x6f70(%bx,%si)      # gen=2e d0  dis=2e d0 90 90 90
    .byte 0x2e,0xd8                 # fallback; gen=2e d8
    .byte 0x2e,0xe0                 # fallback; gen=2e e0
    .byte 0x2e,0xe8                 # fallback; gen=2e e8
    .byte 0x2e,0xf0                 # fallback; gen=2e f0
    cs clc                              # gen=2e f8  dis=2e f8
    cs push %cs                         # gen=2e 0e  dis=2e 0e
    cs push %ss                         # gen=2e 16  dis=2e 16
    cs push %ds                         # gen=2e 1e  dis=2e 1e
    .byte 0x2e,0x26                 # fallback; gen=2e 26
    .byte 0x2e,0x2e                 # fallback; gen=2e 2e
    .byte 0x2e,0x36                 # fallback; gen=2e 36
    .byte 0x2e,0x3e                 # fallback; gen=2e 3e
    das                                 # gen=2f c0  dis=2f
    das                                 # gen=2f 06  dis=2f
    xor    %al,%al                      # gen=30 c0  dis=30 c0
    xor    %al,-0x6f70                  # gen=30 06  dis=30 06 90 90
    xor    %ax,%ax                      # gen=31 c0  dis=31 c0
    xor    %ax,-0x6f70                  # gen=31 06  dis=31 06 90 90
    xor    %al,%al                      # gen=32 c0  dis=32 c0
    xor    -0x6f70,%al                  # gen=32 06  dis=32 06 90 90
    xor    %ax,%ax                      # gen=33 c0  dis=33 c0
    xor    -0x6f70,%ax                  # gen=33 06  dis=33 06 90 90
    xor    $0xc0,%al                    # gen=34 c0  dis=34 c0
    xor    $0x6,%al                     # gen=34 06  dis=34 06
    xor    $0x90c0,%ax                  # gen=35 c0  dis=35 c0 90
    xor    $0x9006,%ax                  # gen=35 06  dis=35 06 90
    .byte 0x36,0xc0                 # fallback; gen=36 c0
    ss push %es                         # gen=36 06  dis=36 06
    .byte 0x36,0xc8                 # fallback; gen=36 c8
    rclb   $1,%ss:-0x6f70(%bx,%si)      # gen=36 d0  dis=36 d0 90 90 90
    .byte 0x36,0xd8                 # fallback; gen=36 d8
    .byte 0x36,0xe0                 # fallback; gen=36 e0
    .byte 0x36,0xe8                 # fallback; gen=36 e8
    .byte 0x36,0xf0                 # fallback; gen=36 f0
    ss clc                              # gen=36 f8  dis=36 f8
    ss push %cs                         # gen=36 0e  dis=36 0e
    ss push %ss                         # gen=36 16  dis=36 16
    ss push %ds                         # gen=36 1e  dis=36 1e
    .byte 0x36,0x26                 # fallback; gen=36 26
    .byte 0x36,0x2e                 # fallback; gen=36 2e
    .byte 0x36,0x36                 # fallback; gen=36 36
    .byte 0x36,0x3e                 # fallback; gen=36 3e
    aaa                                 # gen=37 c0  dis=37
    aaa                                 # gen=37 06  dis=37
    cmp    %al,%al                      # gen=38 c0  dis=38 c0
    cmp    %al,-0x6f70                  # gen=38 06  dis=38 06 90 90
    cmp    %ax,%ax                      # gen=39 c0  dis=39 c0
    cmp    %ax,-0x6f70                  # gen=39 06  dis=39 06 90 90
    cmp    %al,%al                      # gen=3a c0  dis=3a c0
    cmp    -0x6f70,%al                  # gen=3a 06  dis=3a 06 90 90
    cmp    %ax,%ax                      # gen=3b c0  dis=3b c0
    cmp    -0x6f70,%ax                  # gen=3b 06  dis=3b 06 90 90
    cmp    $0xc0,%al                    # gen=3c c0  dis=3c c0
    cmp    $0x6,%al                     # gen=3c 06  dis=3c 06
    cmp    $0x90c0,%ax                  # gen=3d c0  dis=3d c0 90
    cmp    $0x9006,%ax                  # gen=3d 06  dis=3d 06 90
    .byte 0x3e,0xc0                 # fallback; gen=3e c0
    ds push %es                         # gen=3e 06  dis=3e 06
    .byte 0x3e,0xc8                 # fallback; gen=3e c8
    rclb   $1,%ds:-0x6f70(%bx,%si)      # gen=3e d0  dis=3e d0 90 90 90
    .byte 0x3e,0xd8                 # fallback; gen=3e d8
    .byte 0x3e,0xe0                 # fallback; gen=3e e0
    .byte 0x3e,0xe8                 # fallback; gen=3e e8
    .byte 0x3e,0xf0                 # fallback; gen=3e f0
    ds clc                              # gen=3e f8  dis=3e f8
    ds push %cs                         # gen=3e 0e  dis=3e 0e
    ds push %ss                         # gen=3e 16  dis=3e 16
    ds push %ds                         # gen=3e 1e  dis=3e 1e
    .byte 0x3e,0x26                 # fallback; gen=3e 26
    .byte 0x3e,0x2e                 # fallback; gen=3e 2e
    .byte 0x3e,0x36                 # fallback; gen=3e 36
    .byte 0x3e,0x3e                 # fallback; gen=3e 3e
    aas                                 # gen=3f c0  dis=3f
    aas                                 # gen=3f 06  dis=3f
    inc    %ax                          # gen=40 c0  dis=40
    inc    %ax                          # gen=40 06  dis=40
    inc    %cx                          # gen=41 c0  dis=41
    inc    %cx                          # gen=41 06  dis=41
    inc    %dx                          # gen=42 c0  dis=42
    inc    %dx                          # gen=42 06  dis=42
    inc    %bx                          # gen=43 c0  dis=43
    inc    %bx                          # gen=43 06  dis=43
    inc    %sp                          # gen=44 c0  dis=44
    inc    %sp                          # gen=44 06  dis=44
    inc    %bp                          # gen=45 c0  dis=45
    inc    %bp                          # gen=45 06  dis=45
    inc    %si                          # gen=46 c0  dis=46
    inc    %si                          # gen=46 06  dis=46
    inc    %di                          # gen=47 c0  dis=47
    inc    %di                          # gen=47 06  dis=47
    dec    %ax                          # gen=48 c0  dis=48
    dec    %ax                          # gen=48 06  dis=48
    dec    %cx                          # gen=49 c0  dis=49
    dec    %cx                          # gen=49 06  dis=49
    dec    %dx                          # gen=4a c0  dis=4a
    dec    %dx                          # gen=4a 06  dis=4a
    dec    %bx                          # gen=4b c0  dis=4b
    dec    %bx                          # gen=4b 06  dis=4b
    dec    %sp                          # gen=4c c0  dis=4c
    dec    %sp                          # gen=4c 06  dis=4c
    dec    %bp                          # gen=4d c0  dis=4d
    dec    %bp                          # gen=4d 06  dis=4d
    dec    %si                          # gen=4e c0  dis=4e
    dec    %si                          # gen=4e 06  dis=4e
    dec    %di                          # gen=4f c0  dis=4f
    dec    %di                          # gen=4f 06  dis=4f
    push   %ax                          # gen=50 c0  dis=50
    push   %ax                          # gen=50 06  dis=50
    push   %cx                          # gen=51 c0  dis=51
    push   %cx                          # gen=51 06  dis=51
    push   %dx                          # gen=52 c0  dis=52
    push   %dx                          # gen=52 06  dis=52
    push   %bx                          # gen=53 c0  dis=53
    push   %bx                          # gen=53 06  dis=53
    push   %sp                          # gen=54 c0  dis=54
    push   %sp                          # gen=54 06  dis=54
    push   %bp                          # gen=55 c0  dis=55
    push   %bp                          # gen=55 06  dis=55
    push   %si                          # gen=56 c0  dis=56
    push   %si                          # gen=56 06  dis=56
    push   %di                          # gen=57 c0  dis=57
    push   %di                          # gen=57 06  dis=57
    pop    %ax                          # gen=58 c0  dis=58
    pop    %ax                          # gen=58 06  dis=58
    pop    %cx                          # gen=59 c0  dis=59
    pop    %cx                          # gen=59 06  dis=59
    pop    %dx                          # gen=5a c0  dis=5a
    pop    %dx                          # gen=5a 06  dis=5a
    pop    %bx                          # gen=5b c0  dis=5b
    pop    %bx                          # gen=5b 06  dis=5b
    pop    %sp                          # gen=5c c0  dis=5c
    pop    %sp                          # gen=5c 06  dis=5c
    pop    %bp                          # gen=5d c0  dis=5d
    pop    %bp                          # gen=5d 06  dis=5d
    pop    %si                          # gen=5e c0  dis=5e
    pop    %si                          # gen=5e 06  dis=5e
    pop    %di                          # gen=5f c0  dis=5f
    pop    %di                          # gen=5f 06  dis=5f
    .byte 0x60,0xc0                 # fallback; gen=60 c0
    .byte 0x60,0x06                 # fallback; gen=60 06
    .byte 0x61,0xc0                 # fallback; gen=61 c0
    .byte 0x61,0x06                 # fallback; gen=61 06
    .byte 0x62,0xc0                 # fallback; gen=62 c0
    .byte 0x62,0x06                 # fallback; gen=62 06
    .byte 0x63,0xc0                 # fallback; gen=63 c0
    .byte 0x63,0x06                 # fallback; gen=63 06
    .byte 0x64,0xc0                 # fallback; gen=64 c0
    .byte 0x64,0x06                 # fallback; gen=64 06
    .byte 0x64,0xc8                 # fallback; gen=64 c8
    .byte 0x64,0xd0                 # fallback; gen=64 d0
    .byte 0x64,0xd8                 # fallback; gen=64 d8
    .byte 0x64,0xe0                 # fallback; gen=64 e0
    .byte 0x64,0xe8                 # fallback; gen=64 e8
    .byte 0x64,0xf0                 # fallback; gen=64 f0
    .byte 0x64,0xf8                 # fallback; gen=64 f8
    .byte 0x64,0x0e                 # fallback; gen=64 0e
    .byte 0x64,0x16                 # fallback; gen=64 16
    .byte 0x64,0x1e                 # fallback; gen=64 1e
    .byte 0x64,0x26                 # fallback; gen=64 26
    .byte 0x64,0x2e                 # fallback; gen=64 2e
    .byte 0x64,0x36                 # fallback; gen=64 36
    .byte 0x64,0x3e                 # fallback; gen=64 3e
    .byte 0x65,0xc0                 # fallback; gen=65 c0
    .byte 0x65,0x06                 # fallback; gen=65 06
    .byte 0x65,0xc8                 # fallback; gen=65 c8
    .byte 0x65,0xd0                 # fallback; gen=65 d0
    .byte 0x65,0xd8                 # fallback; gen=65 d8
    .byte 0x65,0xe0                 # fallback; gen=65 e0
    .byte 0x65,0xe8                 # fallback; gen=65 e8
    .byte 0x65,0xf0                 # fallback; gen=65 f0
    .byte 0x65,0xf8                 # fallback; gen=65 f8
    .byte 0x65,0x0e                 # fallback; gen=65 0e
    .byte 0x65,0x16                 # fallback; gen=65 16
    .byte 0x65,0x1e                 # fallback; gen=65 1e
    .byte 0x65,0x26                 # fallback; gen=65 26
    .byte 0x65,0x2e                 # fallback; gen=65 2e
    .byte 0x65,0x36                 # fallback; gen=65 36
    .byte 0x65,0x3e                 # fallback; gen=65 3e
    .byte 0x66,0xc0                 # fallback; gen=66 c0
    .byte 0x66,0x06                 # fallback; gen=66 06
    .byte 0x66,0xc8                 # fallback; gen=66 c8
    .byte 0x66,0xd0                 # fallback; gen=66 d0
    .byte 0x66,0xd8                 # fallback; gen=66 d8
    .byte 0x66,0xe0                 # fallback; gen=66 e0
    .byte 0x66,0xe8                 # fallback; gen=66 e8
    .byte 0x66,0xf0                 # fallback; gen=66 f0
    .byte 0x66,0xf8                 # fallback; gen=66 f8
    .byte 0x66,0x0e                 # fallback; gen=66 0e
    .byte 0x66,0x16                 # fallback; gen=66 16
    .byte 0x66,0x1e                 # fallback; gen=66 1e
    .byte 0x66,0x26                 # fallback; gen=66 26
    .byte 0x66,0x2e                 # fallback; gen=66 2e
    .byte 0x66,0x36                 # fallback; gen=66 36
    .byte 0x66,0x3e                 # fallback; gen=66 3e
    .byte 0x67,0xc0                 # fallback; gen=67 c0
    .byte 0x67,0x06                 # fallback; gen=67 06
    .byte 0x67,0xc8                 # fallback; gen=67 c8
    .byte 0x67,0xd0                 # fallback; gen=67 d0
    .byte 0x67,0xd8                 # fallback; gen=67 d8
    .byte 0x67,0xe0                 # fallback; gen=67 e0
    .byte 0x67,0xe8                 # fallback; gen=67 e8
    .byte 0x67,0xf0                 # fallback; gen=67 f0
    .byte 0x67,0xf8                 # fallback; gen=67 f8
    .byte 0x67,0x0e                 # fallback; gen=67 0e
    .byte 0x67,0x16                 # fallback; gen=67 16
    .byte 0x67,0x1e                 # fallback; gen=67 1e
    .byte 0x67,0x26                 # fallback; gen=67 26
    .byte 0x67,0x2e                 # fallback; gen=67 2e
    .byte 0x67,0x36                 # fallback; gen=67 36
    .byte 0x67,0x3e                 # fallback; gen=67 3e
    .byte 0x68,0xc0                 # fallback; gen=68 c0
    .byte 0x68,0x06                 # fallback; gen=68 06
    .byte 0x69,0xc0                 # fallback; gen=69 c0
    .byte 0x69,0x06                 # fallback; gen=69 06
    .byte 0x6a,0xc0                 # fallback; gen=6a c0
    .byte 0x6a,0x06                 # fallback; gen=6a 06
    .byte 0x6b,0xc0                 # fallback; gen=6b c0
    .byte 0x6b,0x06                 # fallback; gen=6b 06
    .byte 0x6c,0xc0                 # fallback; gen=6c c0
    .byte 0x6c,0x06                 # fallback; gen=6c 06
    .byte 0x6d,0xc0                 # fallback; gen=6d c0
    .byte 0x6d,0x06                 # fallback; gen=6d 06
    .byte 0x6e,0xc0                 # fallback; gen=6e c0
    .byte 0x6e,0x06                 # fallback; gen=6e 06
    .byte 0x6f,0xc0                 # fallback; gen=6f c0
    .byte 0x6f,0x06                 # fallback; gen=6f 06
    jo     0x15a2                       # gen=70 c0  dis=70 c0
    jo     0x15f8                       # gen=70 06  dis=70 06
    jno    0x15c2                       # gen=71 c0  dis=71 c0
    jno    0x1618                       # gen=71 06  dis=71 06
    jb     0x15e2                       # gen=72 c0  dis=72 c0
    jb     0x1638                       # gen=72 06  dis=72 06
    jae    0x1602                       # gen=73 c0  dis=73 c0
    jae    0x1658                       # gen=73 06  dis=73 06
    je     0x1622                       # gen=74 c0  dis=74 c0
    je     0x1678                       # gen=74 06  dis=74 06
    jne    0x1642                       # gen=75 c0  dis=75 c0
    jne    0x1698                       # gen=75 06  dis=75 06
    jbe    0x1662                       # gen=76 c0  dis=76 c0
    jbe    0x16b8                       # gen=76 06  dis=76 06
    ja     0x1682                       # gen=77 c0  dis=77 c0
    ja     0x16d8                       # gen=77 06  dis=77 06
    js     0x16a2                       # gen=78 c0  dis=78 c0
    js     0x16f8                       # gen=78 06  dis=78 06
    jns    0x16c2                       # gen=79 c0  dis=79 c0
    jns    0x1718                       # gen=79 06  dis=79 06
    jp     0x16e2                       # gen=7a c0  dis=7a c0
    jp     0x1738                       # gen=7a 06  dis=7a 06
    jnp    0x1702                       # gen=7b c0  dis=7b c0
    jnp    0x1758                       # gen=7b 06  dis=7b 06
    jl     0x1722                       # gen=7c c0  dis=7c c0
    jl     0x1778                       # gen=7c 06  dis=7c 06
    jge    0x1742                       # gen=7d c0  dis=7d c0
    jge    0x1798                       # gen=7d 06  dis=7d 06
    jle    0x1762                       # gen=7e c0  dis=7e c0
    jle    0x17b8                       # gen=7e 06  dis=7e 06
    jg     0x1782                       # gen=7f c0  dis=7f c0
    jg     0x17d8                       # gen=7f 06  dis=7f 06
    add    $0x90,%al                    # gen=80 c0  dis=80 c0 90
    addb   $0x90,-0x6f70                # gen=80 06  dis=80 06 90 90 90
    or     $0x90,%al                    # gen=80 c8  dis=80 c8 90
    adc    $0x90,%al                    # gen=80 d0  dis=80 d0 90
    sbb    $0x90,%al                    # gen=80 d8  dis=80 d8 90
    and    $0x90,%al                    # gen=80 e0  dis=80 e0 90
    sub    $0x90,%al                    # gen=80 e8  dis=80 e8 90
    xor    $0x90,%al                    # gen=80 f0  dis=80 f0 90
    cmp    $0x90,%al                    # gen=80 f8  dis=80 f8 90
    orb    $0x90,-0x6f70                # gen=80 0e  dis=80 0e 90 90 90
    adcb   $0x90,-0x6f70                # gen=80 16  dis=80 16 90 90 90
    sbbb   $0x90,-0x6f70                # gen=80 1e  dis=80 1e 90 90 90
    andb   $0x90,-0x6f70                # gen=80 26  dis=80 26 90 90 90
    subb   $0x90,-0x6f70                # gen=80 2e  dis=80 2e 90 90 90
    xorb   $0x90,-0x6f70                # gen=80 36  dis=80 36 90 90 90
    cmpb   $0x90,-0x6f70                # gen=80 3e  dis=80 3e 90 90 90
    add    $0x9090,%ax                  # gen=81 c0  dis=81 c0 90 90
    addw   $0x9090,-0x6f70              # gen=81 06  dis=81 06 90 90 90 90
    or     $0x9090,%ax                  # gen=81 c8  dis=81 c8 90 90
    adc    $0x9090,%ax                  # gen=81 d0  dis=81 d0 90 90
    sbb    $0x9090,%ax                  # gen=81 d8  dis=81 d8 90 90
    and    $0x9090,%ax                  # gen=81 e0  dis=81 e0 90 90
    sub    $0x9090,%ax                  # gen=81 e8  dis=81 e8 90 90
    xor    $0x9090,%ax                  # gen=81 f0  dis=81 f0 90 90
    cmp    $0x9090,%ax                  # gen=81 f8  dis=81 f8 90 90
    orw    $0x9090,-0x6f70              # gen=81 0e  dis=81 0e 90 90 90 90
    adcw   $0x9090,-0x6f70              # gen=81 16  dis=81 16 90 90 90 90
    sbbw   $0x9090,-0x6f70              # gen=81 1e  dis=81 1e 90 90 90 90
    andw   $0x9090,-0x6f70              # gen=81 26  dis=81 26 90 90 90 90
    subw   $0x9090,-0x6f70              # gen=81 2e  dis=81 2e 90 90 90 90
    xorw   $0x9090,-0x6f70              # gen=81 36  dis=81 36 90 90 90 90
    cmpw   $0x9090,-0x6f70              # gen=81 3e  dis=81 3e 90 90 90 90
    add    $0x90,%al                    # gen=82 c0  dis=82 c0 90
    addb   $0x90,-0x6f70                # gen=82 06  dis=82 06 90 90 90
    or     $0x90,%al                    # gen=82 c8  dis=82 c8 90
    adc    $0x90,%al                    # gen=82 d0  dis=82 d0 90
    sbb    $0x90,%al                    # gen=82 d8  dis=82 d8 90
    and    $0x90,%al                    # gen=82 e0  dis=82 e0 90
    sub    $0x90,%al                    # gen=82 e8  dis=82 e8 90
    xor    $0x90,%al                    # gen=82 f0  dis=82 f0 90
    cmp    $0x90,%al                    # gen=82 f8  dis=82 f8 90
    orb    $0x90,-0x6f70                # gen=82 0e  dis=82 0e 90 90 90
    adcb   $0x90,-0x6f70                # gen=82 16  dis=82 16 90 90 90
    sbbb   $0x90,-0x6f70                # gen=82 1e  dis=82 1e 90 90 90
    andb   $0x90,-0x6f70                # gen=82 26  dis=82 26 90 90 90
    subb   $0x90,-0x6f70                # gen=82 2e  dis=82 2e 90 90 90
    xorb   $0x90,-0x6f70                # gen=82 36  dis=82 36 90 90 90
    cmpb   $0x90,-0x6f70                # gen=82 3e  dis=82 3e 90 90 90
    add    $0xff90,%ax                  # gen=83 c0  dis=83 c0 90
    addw   $0xff90,-0x6f70              # gen=83 06  dis=83 06 90 90 90
    or     $0xff90,%ax                  # gen=83 c8  dis=83 c8 90
    adc    $0xff90,%ax                  # gen=83 d0  dis=83 d0 90
    sbb    $0xff90,%ax                  # gen=83 d8  dis=83 d8 90
    and    $0xff90,%ax                  # gen=83 e0  dis=83 e0 90
    sub    $0xff90,%ax                  # gen=83 e8  dis=83 e8 90
    xor    $0xff90,%ax                  # gen=83 f0  dis=83 f0 90
    cmp    $0xff90,%ax                  # gen=83 f8  dis=83 f8 90
    orw    $0xff90,-0x6f70              # gen=83 0e  dis=83 0e 90 90 90
    adcw   $0xff90,-0x6f70              # gen=83 16  dis=83 16 90 90 90
    sbbw   $0xff90,-0x6f70              # gen=83 1e  dis=83 1e 90 90 90
    andw   $0xff90,-0x6f70              # gen=83 26  dis=83 26 90 90 90
    subw   $0xff90,-0x6f70              # gen=83 2e  dis=83 2e 90 90 90
    xorw   $0xff90,-0x6f70              # gen=83 36  dis=83 36 90 90 90
    cmpw   $0xff90,-0x6f70              # gen=83 3e  dis=83 3e 90 90 90
    test   %al,%al                      # gen=84 c0  dis=84 c0
    test   %al,-0x6f70                  # gen=84 06  dis=84 06 90 90
    test   %ax,%ax                      # gen=85 c0  dis=85 c0
    test   %ax,-0x6f70                  # gen=85 06  dis=85 06 90 90
    xchg   %al,%al                      # gen=86 c0  dis=86 c0
    xchg   %al,-0x6f70                  # gen=86 06  dis=86 06 90 90
    xchg   %ax,%ax                      # gen=87 c0  dis=87 c0
    xchg   %ax,-0x6f70                  # gen=87 06  dis=87 06 90 90
    mov    %al,%al                      # gen=88 c0  dis=88 c0
    mov    %al,-0x6f70                  # gen=88 06  dis=88 06 90 90
    mov    %ax,%ax                      # gen=89 c0  dis=89 c0
    mov    %ax,-0x6f70                  # gen=89 06  dis=89 06 90 90
    mov    %al,%al                      # gen=8a c0  dis=8a c0
    mov    -0x6f70,%al                  # gen=8a 06  dis=8a 06 90 90
    mov    %ax,%ax                      # gen=8b c0  dis=8b c0
    mov    -0x6f70,%ax                  # gen=8b 06  dis=8b 06 90 90
    mov    %es,%ax                      # gen=8c c0  dis=8c c0
    mov    %es,-0x6f70                  # gen=8c 06  dis=8c 06 90 90
    lea    (bad),%ax                    # gen=8d c0  dis=8d
    lea    -0x6f70,%ax                  # gen=8d 06  dis=8d 06 90 90
    mov    %ax,%es                      # gen=8e c0  dis=8e c0
    mov    -0x6f70,%es                  # gen=8e 06  dis=8e 06 90 90
    pop    %ax                          # gen=8f c0  dis=8f c0
    pop    -0x6f70                      # gen=8f 06  dis=8f 06 90 90
    nop                                 # gen=90 c0  dis=90
    nop                                 # gen=90 06  dis=90
    xchg   %ax,%cx                      # gen=91 c0  dis=91
    xchg   %ax,%cx                      # gen=91 06  dis=91
    xchg   %ax,%dx                      # gen=92 c0  dis=92
    xchg   %ax,%dx                      # gen=92 06  dis=92
    xchg   %ax,%bx                      # gen=93 c0  dis=93
    xchg   %ax,%bx                      # gen=93 06  dis=93
    xchg   %ax,%sp                      # gen=94 c0  dis=94
    xchg   %ax,%sp                      # gen=94 06  dis=94
    xchg   %ax,%bp                      # gen=95 c0  dis=95
    xchg   %ax,%bp                      # gen=95 06  dis=95
    xchg   %ax,%si                      # gen=96 c0  dis=96
    xchg   %ax,%si                      # gen=96 06  dis=96
    xchg   %ax,%di                      # gen=97 c0  dis=97
    xchg   %ax,%di                      # gen=97 06  dis=97
    cbtw                                # gen=98 c0  dis=98
    cbtw                                # gen=98 06  dis=98
    cwtd                                # gen=99 c0  dis=99
    cwtd                                # gen=99 06  dis=99
    lcall  $0x9090,$0x90c0              # gen=9a c0  dis=9a c0 90 90 90
    lcall  $0x9090,$0x9006              # gen=9a 06  dis=9a 06 90 90 90
    .byte 0x9b,0xc0                 # fallback; gen=9b c0
    .byte 0x9b,0x06                 # fallback; gen=9b 06
    pushf                               # gen=9c c0  dis=9c
    pushf                               # gen=9c 06  dis=9c
    popf                                # gen=9d c0  dis=9d
    popf                                # gen=9d 06  dis=9d
    sahf                                # gen=9e c0  dis=9e
    sahf                                # gen=9e 06  dis=9e
    lahf                                # gen=9f c0  dis=9f
    lahf                                # gen=9f 06  dis=9f
    mov    0x90c0,%al                   # gen=a0 c0  dis=a0 c0 90
    mov    0x9006,%al                   # gen=a0 06  dis=a0 06 90
    mov    0x90c0,%ax                   # gen=a1 c0  dis=a1 c0 90
    mov    0x9006,%ax                   # gen=a1 06  dis=a1 06 90
    mov    %al,0x90c0                   # gen=a2 c0  dis=a2 c0 90
    mov    %al,0x9006                   # gen=a2 06  dis=a2 06 90
    mov    %ax,0x90c0                   # gen=a3 c0  dis=a3 c0 90
    mov    %ax,0x9006                   # gen=a3 06  dis=a3 06 90
    movsb  %ds:(%si),%es:(%di)          # gen=a4 c0  dis=a4
    movsb  %ds:(%si),%es:(%di)          # gen=a4 06  dis=a4
    movsw  %ds:(%si),%es:(%di)          # gen=a5 c0  dis=a5
    movsw  %ds:(%si),%es:(%di)          # gen=a5 06  dis=a5
    cmpsb  %es:(%di),%ds:(%si)          # gen=a6 c0  dis=a6
    cmpsb  %es:(%di),%ds:(%si)          # gen=a6 06  dis=a6
    cmpsw  %es:(%di),%ds:(%si)          # gen=a7 c0  dis=a7
    cmpsw  %es:(%di),%ds:(%si)          # gen=a7 06  dis=a7
    test   $0xc0,%al                    # gen=a8 c0  dis=a8 c0
    test   $0x6,%al                     # gen=a8 06  dis=a8 06
    test   $0x90c0,%ax                  # gen=a9 c0  dis=a9 c0 90
    test   $0x9006,%ax                  # gen=a9 06  dis=a9 06 90
    stos   %al,%es:(%di)                # gen=aa c0  dis=aa
    stos   %al,%es:(%di)                # gen=aa 06  dis=aa
    stos   %ax,%es:(%di)                # gen=ab c0  dis=ab
    stos   %ax,%es:(%di)                # gen=ab 06  dis=ab
    lods   %ds:(%si),%al                # gen=ac c0  dis=ac
    lods   %ds:(%si),%al                # gen=ac 06  dis=ac
    lods   %ds:(%si),%ax                # gen=ad c0  dis=ad
    lods   %ds:(%si),%ax                # gen=ad 06  dis=ad
    scas   %es:(%di),%al                # gen=ae c0  dis=ae
    scas   %es:(%di),%al                # gen=ae 06  dis=ae
    scas   %es:(%di),%ax                # gen=af c0  dis=af
    scas   %es:(%di),%ax                # gen=af 06  dis=af
    mov    $0xc0,%al                    # gen=b0 c0  dis=b0 c0
    mov    $0x6,%al                     # gen=b0 06  dis=b0 06
    mov    $0xc0,%cl                    # gen=b1 c0  dis=b1 c0
    mov    $0x6,%cl                     # gen=b1 06  dis=b1 06
    mov    $0xc0,%dl                    # gen=b2 c0  dis=b2 c0
    mov    $0x6,%dl                     # gen=b2 06  dis=b2 06
    mov    $0xc0,%bl                    # gen=b3 c0  dis=b3 c0
    mov    $0x6,%bl                     # gen=b3 06  dis=b3 06
    mov    $0xc0,%ah                    # gen=b4 c0  dis=b4 c0
    mov    $0x6,%ah                     # gen=b4 06  dis=b4 06
    mov    $0xc0,%ch                    # gen=b5 c0  dis=b5 c0
    mov    $0x6,%ch                     # gen=b5 06  dis=b5 06
    mov    $0xc0,%dh                    # gen=b6 c0  dis=b6 c0
    mov    $0x6,%dh                     # gen=b6 06  dis=b6 06
    mov    $0xc0,%bh                    # gen=b7 c0  dis=b7 c0
    mov    $0x6,%bh                     # gen=b7 06  dis=b7 06
    mov    $0x90c0,%ax                  # gen=b8 c0  dis=b8 c0 90
    mov    $0x9006,%ax                  # gen=b8 06  dis=b8 06 90
    mov    $0x90c0,%cx                  # gen=b9 c0  dis=b9 c0 90
    mov    $0x9006,%cx                  # gen=b9 06  dis=b9 06 90
    mov    $0x90c0,%dx                  # gen=ba c0  dis=ba c0 90
    mov    $0x9006,%dx                  # gen=ba 06  dis=ba 06 90
    mov    $0x90c0,%bx                  # gen=bb c0  dis=bb c0 90
    mov    $0x9006,%bx                  # gen=bb 06  dis=bb 06 90
    mov    $0x90c0,%sp                  # gen=bc c0  dis=bc c0 90
    mov    $0x9006,%sp                  # gen=bc 06  dis=bc 06 90
    mov    $0x90c0,%bp                  # gen=bd c0  dis=bd c0 90
    mov    $0x9006,%bp                  # gen=bd 06  dis=bd 06 90
    mov    $0x90c0,%si                  # gen=be c0  dis=be c0 90
    mov    $0x9006,%si                  # gen=be 06  dis=be 06 90
    mov    $0x90c0,%di                  # gen=bf c0  dis=bf c0 90
    mov    $0x9006,%di                  # gen=bf 06  dis=bf 06 90
    .byte 0xc0,0xc0                 # fallback; gen=c0 c0
    .byte 0xc0,0x06                 # fallback; gen=c0 06
    .byte 0xc0,0xc8                 # fallback; gen=c0 c8
    .byte 0xc0,0xd0                 # fallback; gen=c0 d0
    .byte 0xc0,0xd8                 # fallback; gen=c0 d8
    .byte 0xc0,0xe0                 # fallback; gen=c0 e0
    .byte 0xc0,0xe8                 # fallback; gen=c0 e8
    .byte 0xc0,0xf0                 # fallback; gen=c0 f0
    .byte 0xc0,0xf8                 # fallback; gen=c0 f8
    .byte 0xc0,0x0e                 # fallback; gen=c0 0e
    .byte 0xc0,0x16                 # fallback; gen=c0 16
    .byte 0xc0,0x1e                 # fallback; gen=c0 1e
    .byte 0xc0,0x26                 # fallback; gen=c0 26
    .byte 0xc0,0x2e                 # fallback; gen=c0 2e
    .byte 0xc0,0x36                 # fallback; gen=c0 36
    .byte 0xc0,0x3e                 # fallback; gen=c0 3e
    .byte 0xc1,0xc0                 # fallback; gen=c1 c0
    .byte 0xc1,0x06                 # fallback; gen=c1 06
    .byte 0xc1,0xc8                 # fallback; gen=c1 c8
    .byte 0xc1,0xd0                 # fallback; gen=c1 d0
    .byte 0xc1,0xd8                 # fallback; gen=c1 d8
    .byte 0xc1,0xe0                 # fallback; gen=c1 e0
    .byte 0xc1,0xe8                 # fallback; gen=c1 e8
    .byte 0xc1,0xf0                 # fallback; gen=c1 f0
    .byte 0xc1,0xf8                 # fallback; gen=c1 f8
    .byte 0xc1,0x0e                 # fallback; gen=c1 0e
    .byte 0xc1,0x16                 # fallback; gen=c1 16
    .byte 0xc1,0x1e                 # fallback; gen=c1 1e
    .byte 0xc1,0x26                 # fallback; gen=c1 26
    .byte 0xc1,0x2e                 # fallback; gen=c1 2e
    .byte 0xc1,0x36                 # fallback; gen=c1 36
    .byte 0xc1,0x3e                 # fallback; gen=c1 3e
    ret    $0x90c0                      # gen=c2 c0  dis=c2 c0 90
    ret    $0x9006                      # gen=c2 06  dis=c2 06 90
    ret                                 # gen=c3 c0  dis=c3
    ret                                 # gen=c3 06  dis=c3
    .byte 0xc4,0xc0                 # fallback; gen=c4 c0
    les    -0x6f70,%ax                  # gen=c4 06  dis=c4 06 90 90
    .byte 0xc5,0xc0                 # fallback; gen=c5 c0
    lds    -0x6f70,%ax                  # gen=c5 06  dis=c5 06 90 90
    .byte 0xc5,0xc8                 # fallback; gen=c5 c8
    .byte 0xc5,0xd0                 # fallback; gen=c5 d0
    .byte 0xc5,0xd8                 # fallback; gen=c5 d8
    .byte 0xc5,0xe0                 # fallback; gen=c5 e0
    .byte 0xc5,0xe8                 # fallback; gen=c5 e8
    .byte 0xc5,0xf0                 # fallback; gen=c5 f0
    .byte 0xc5,0xf8                 # fallback; gen=c5 f8
    lds    -0x6f70,%cx                  # gen=c5 0e  dis=c5 0e 90 90
    lds    -0x6f70,%dx                  # gen=c5 16  dis=c5 16 90 90
    lds    -0x6f70,%bx                  # gen=c5 1e  dis=c5 1e 90 90
    lds    -0x6f70,%sp                  # gen=c5 26  dis=c5 26 90 90
    lds    -0x6f70,%bp                  # gen=c5 2e  dis=c5 2e 90 90
    lds    -0x6f70,%si                  # gen=c5 36  dis=c5 36 90 90
    lds    -0x6f70,%di                  # gen=c5 3e  dis=c5 3e 90 90
    mov    $0x90,%al                    # gen=c6 c0  dis=c6 c0 90
    movb   $0x90,-0x6f70                # gen=c6 06  dis=c6 06 90 90 90
    .byte 0xc6,0xc8                 # fallback; gen=c6 c8
    .byte 0xc6,0xd0                 # fallback; gen=c6 d0
    .byte 0xc6,0xd8                 # fallback; gen=c6 d8
    .byte 0xc6,0xe0                 # fallback; gen=c6 e0
    .byte 0xc6,0xe8                 # fallback; gen=c6 e8
    .byte 0xc6,0xf0                 # fallback; gen=c6 f0
    .byte 0xc6,0xf8                 # fallback; gen=c6 f8
    .byte 0xc6,0x0e                 # fallback; gen=c6 0e
    .byte 0xc6,0x16                 # fallback; gen=c6 16
    .byte 0xc6,0x1e                 # fallback; gen=c6 1e
    .byte 0xc6,0x26                 # fallback; gen=c6 26
    .byte 0xc6,0x2e                 # fallback; gen=c6 2e
    .byte 0xc6,0x36                 # fallback; gen=c6 36
    .byte 0xc6,0x3e                 # fallback; gen=c6 3e
    mov    $0x9090,%ax                  # gen=c7 c0  dis=c7 c0 90 90
    movw   $0x9090,-0x6f70              # gen=c7 06  dis=c7 06 90 90 90 90
    .byte 0xc7,0xc8                 # fallback; gen=c7 c8
    .byte 0xc7,0xd0                 # fallback; gen=c7 d0
    .byte 0xc7,0xd8                 # fallback; gen=c7 d8
    .byte 0xc7,0xe0                 # fallback; gen=c7 e0
    .byte 0xc7,0xe8                 # fallback; gen=c7 e8
    .byte 0xc7,0xf0                 # fallback; gen=c7 f0
    .byte 0xc7,0xf8                 # fallback; gen=c7 f8
    .byte 0xc7,0x0e                 # fallback; gen=c7 0e
    .byte 0xc7,0x16                 # fallback; gen=c7 16
    .byte 0xc7,0x1e                 # fallback; gen=c7 1e
    .byte 0xc7,0x26                 # fallback; gen=c7 26
    .byte 0xc7,0x2e                 # fallback; gen=c7 2e
    .byte 0xc7,0x36                 # fallback; gen=c7 36
    .byte 0xc7,0x3e                 # fallback; gen=c7 3e
    .byte 0xc8,0xc0                 # fallback; gen=c8 c0
    .byte 0xc8,0x06                 # fallback; gen=c8 06
    .byte 0xc9,0xc0                 # fallback; gen=c9 c0
    .byte 0xc9,0x06                 # fallback; gen=c9 06
    lret   $0x90c0                      # gen=ca c0  dis=ca c0 90
    lret   $0x9006                      # gen=ca 06  dis=ca 06 90
    lret                                # gen=cb c0  dis=cb
    lret                                # gen=cb 06  dis=cb
    int3                                # gen=cc c0  dis=cc
    int3                                # gen=cc 06  dis=cc
    int    $0xc0                        # gen=cd c0  dis=cd c0
    int    $0x6                         # gen=cd 06  dis=cd 06
    into                                # gen=ce c0  dis=ce
    into                                # gen=ce 06  dis=ce
    iret                                # gen=cf c0  dis=cf
    iret                                # gen=cf 06  dis=cf
    rol    $1,%al                       # gen=d0 c0  dis=d0 c0
    rolb   $1,-0x6f70                   # gen=d0 06  dis=d0 06 90 90
    ror    $1,%al                       # gen=d0 c8  dis=d0 c8
    rcl    $1,%al                       # gen=d0 d0  dis=d0 d0
    rcr    $1,%al                       # gen=d0 d8  dis=d0 d8
    shl    $1,%al                       # gen=d0 e0  dis=d0 e0
    shr    $1,%al                       # gen=d0 e8  dis=d0 e8
    shl    $1,%al                       # gen=d0 f0  dis=d0 f0
    sar    $1,%al                       # gen=d0 f8  dis=d0 f8
    rorb   $1,-0x6f70                   # gen=d0 0e  dis=d0 0e 90 90
    rclb   $1,-0x6f70                   # gen=d0 16  dis=d0 16 90 90
    rcrb   $1,-0x6f70                   # gen=d0 1e  dis=d0 1e 90 90
    shlb   $1,-0x6f70                   # gen=d0 26  dis=d0 26 90 90
    shrb   $1,-0x6f70                   # gen=d0 2e  dis=d0 2e 90 90
    shlb   $1,-0x6f70                   # gen=d0 36  dis=d0 36 90 90
    sarb   $1,-0x6f70                   # gen=d0 3e  dis=d0 3e 90 90
    rol    $1,%ax                       # gen=d1 c0  dis=d1 c0
    rolw   $1,-0x6f70                   # gen=d1 06  dis=d1 06 90 90
    ror    $1,%ax                       # gen=d1 c8  dis=d1 c8
    rcl    $1,%ax                       # gen=d1 d0  dis=d1 d0
    rcr    $1,%ax                       # gen=d1 d8  dis=d1 d8
    shl    $1,%ax                       # gen=d1 e0  dis=d1 e0
    shr    $1,%ax                       # gen=d1 e8  dis=d1 e8
    shl    $1,%ax                       # gen=d1 f0  dis=d1 f0
    sar    $1,%ax                       # gen=d1 f8  dis=d1 f8
    rorw   $1,-0x6f70                   # gen=d1 0e  dis=d1 0e 90 90
    rclw   $1,-0x6f70                   # gen=d1 16  dis=d1 16 90 90
    rcrw   $1,-0x6f70                   # gen=d1 1e  dis=d1 1e 90 90
    shlw   $1,-0x6f70                   # gen=d1 26  dis=d1 26 90 90
    shrw   $1,-0x6f70                   # gen=d1 2e  dis=d1 2e 90 90
    shlw   $1,-0x6f70                   # gen=d1 36  dis=d1 36 90 90
    sarw   $1,-0x6f70                   # gen=d1 3e  dis=d1 3e 90 90
    rol    %cl,%al                      # gen=d2 c0  dis=d2 c0
    rolb   %cl,-0x6f70                  # gen=d2 06  dis=d2 06 90 90
    ror    %cl,%al                      # gen=d2 c8  dis=d2 c8
    rcl    %cl,%al                      # gen=d2 d0  dis=d2 d0
    rcr    %cl,%al                      # gen=d2 d8  dis=d2 d8
    shl    %cl,%al                      # gen=d2 e0  dis=d2 e0
    shr    %cl,%al                      # gen=d2 e8  dis=d2 e8
    shl    %cl,%al                      # gen=d2 f0  dis=d2 f0
    sar    %cl,%al                      # gen=d2 f8  dis=d2 f8
    rorb   %cl,-0x6f70                  # gen=d2 0e  dis=d2 0e 90 90
    rclb   %cl,-0x6f70                  # gen=d2 16  dis=d2 16 90 90
    rcrb   %cl,-0x6f70                  # gen=d2 1e  dis=d2 1e 90 90
    shlb   %cl,-0x6f70                  # gen=d2 26  dis=d2 26 90 90
    shrb   %cl,-0x6f70                  # gen=d2 2e  dis=d2 2e 90 90
    shlb   %cl,-0x6f70                  # gen=d2 36  dis=d2 36 90 90
    sarb   %cl,-0x6f70                  # gen=d2 3e  dis=d2 3e 90 90
    rol    %cl,%ax                      # gen=d3 c0  dis=d3 c0
    rolw   %cl,-0x6f70                  # gen=d3 06  dis=d3 06 90 90
    ror    %cl,%ax                      # gen=d3 c8  dis=d3 c8
    rcl    %cl,%ax                      # gen=d3 d0  dis=d3 d0
    rcr    %cl,%ax                      # gen=d3 d8  dis=d3 d8
    shl    %cl,%ax                      # gen=d3 e0  dis=d3 e0
    shr    %cl,%ax                      # gen=d3 e8  dis=d3 e8
    shl    %cl,%ax                      # gen=d3 f0  dis=d3 f0
    sar    %cl,%ax                      # gen=d3 f8  dis=d3 f8
    rorw   %cl,-0x6f70                  # gen=d3 0e  dis=d3 0e 90 90
    rclw   %cl,-0x6f70                  # gen=d3 16  dis=d3 16 90 90
    rcrw   %cl,-0x6f70                  # gen=d3 1e  dis=d3 1e 90 90
    shlw   %cl,-0x6f70                  # gen=d3 26  dis=d3 26 90 90
    shrw   %cl,-0x6f70                  # gen=d3 2e  dis=d3 2e 90 90
    shlw   %cl,-0x6f70                  # gen=d3 36  dis=d3 36 90 90
    sarw   %cl,-0x6f70                  # gen=d3 3e  dis=d3 3e 90 90
    aam    $0xc0                        # gen=d4 c0  dis=d4 c0
    aam    $0x6                         # gen=d4 06  dis=d4 06
    aad    $0xc0                        # gen=d5 c0  dis=d5 c0
    aad    $0x6                         # gen=d5 06  dis=d5 06
    salc                                # gen=d6 c0  dis=d6
    salc                                # gen=d6 06  dis=d6
    xlat   %ds:(%bx)                    # gen=d7 c0  dis=d7
    xlat   %ds:(%bx)                    # gen=d7 06  dis=d7
    .byte 0xd8,0xc0                 # fallback; gen=d8 c0
    .byte 0xd8,0x06                 # fallback; gen=d8 06
    .byte 0xd8,0xc8                 # fallback; gen=d8 c8
    .byte 0xd8,0xd0                 # fallback; gen=d8 d0
    .byte 0xd8,0xd8                 # fallback; gen=d8 d8
    .byte 0xd8,0xe0                 # fallback; gen=d8 e0
    .byte 0xd8,0xe8                 # fallback; gen=d8 e8
    .byte 0xd8,0xf0                 # fallback; gen=d8 f0
    .byte 0xd8,0xf8                 # fallback; gen=d8 f8
    .byte 0xd8,0x0e                 # fallback; gen=d8 0e
    .byte 0xd8,0x16                 # fallback; gen=d8 16
    .byte 0xd8,0x1e                 # fallback; gen=d8 1e
    .byte 0xd8,0x26                 # fallback; gen=d8 26
    .byte 0xd8,0x2e                 # fallback; gen=d8 2e
    .byte 0xd8,0x36                 # fallback; gen=d8 36
    .byte 0xd8,0x3e                 # fallback; gen=d8 3e
    .byte 0xd9,0xc0                 # fallback; gen=d9 c0
    .byte 0xd9,0x06                 # fallback; gen=d9 06
    .byte 0xd9,0xc8                 # fallback; gen=d9 c8
    .byte 0xd9,0xd0                 # fallback; gen=d9 d0
    .byte 0xd9,0xd8                 # fallback; gen=d9 d8
    .byte 0xd9,0xe0                 # fallback; gen=d9 e0
    .byte 0xd9,0xe8                 # fallback; gen=d9 e8
    .byte 0xd9,0xf0                 # fallback; gen=d9 f0
    .byte 0xd9,0xf8                 # fallback; gen=d9 f8
    .byte 0xd9,0x0e                 # fallback; gen=d9 0e
    .byte 0xd9,0x16                 # fallback; gen=d9 16
    .byte 0xd9,0x1e                 # fallback; gen=d9 1e
    .byte 0xd9,0x26                 # fallback; gen=d9 26
    .byte 0xd9,0x2e                 # fallback; gen=d9 2e
    .byte 0xd9,0x36                 # fallback; gen=d9 36
    .byte 0xd9,0x3e                 # fallback; gen=d9 3e
    .byte 0xda,0xc0                 # fallback; gen=da c0
    .byte 0xda,0x06                 # fallback; gen=da 06
    .byte 0xda,0xc8                 # fallback; gen=da c8
    .byte 0xda,0xd0                 # fallback; gen=da d0
    .byte 0xda,0xd8                 # fallback; gen=da d8
    .byte 0xda,0xe0                 # fallback; gen=da e0
    .byte 0xda,0xe8                 # fallback; gen=da e8
    .byte 0xda,0xf0                 # fallback; gen=da f0
    .byte 0xda,0xf8                 # fallback; gen=da f8
    .byte 0xda,0x0e                 # fallback; gen=da 0e
    .byte 0xda,0x16                 # fallback; gen=da 16
    .byte 0xda,0x1e                 # fallback; gen=da 1e
    .byte 0xda,0x26                 # fallback; gen=da 26
    .byte 0xda,0x2e                 # fallback; gen=da 2e
    .byte 0xda,0x36                 # fallback; gen=da 36
    .byte 0xda,0x3e                 # fallback; gen=da 3e
    .byte 0xdb,0xc0                 # fallback; gen=db c0
    .byte 0xdb,0x06                 # fallback; gen=db 06
    .byte 0xdb,0xc8                 # fallback; gen=db c8
    .byte 0xdb,0xd0                 # fallback; gen=db d0
    .byte 0xdb,0xd8                 # fallback; gen=db d8
    .byte 0xdb,0xe0                 # fallback; gen=db e0
    .byte 0xdb,0xe8                 # fallback; gen=db e8
    .byte 0xdb,0xf0                 # fallback; gen=db f0
    .byte 0xdb,0xf8                 # fallback; gen=db f8
    .byte 0xdb,0x0e                 # fallback; gen=db 0e
    .byte 0xdb,0x16                 # fallback; gen=db 16
    .byte 0xdb,0x1e                 # fallback; gen=db 1e
    .byte 0xdb,0x26                 # fallback; gen=db 26
    .byte 0xdb,0x2e                 # fallback; gen=db 2e
    .byte 0xdb,0x36                 # fallback; gen=db 36
    .byte 0xdb,0x3e                 # fallback; gen=db 3e
    .byte 0xdc,0xc0                 # fallback; gen=dc c0
    .byte 0xdc,0x06                 # fallback; gen=dc 06
    .byte 0xdc,0xc8                 # fallback; gen=dc c8
    .byte 0xdc,0xd0                 # fallback; gen=dc d0
    .byte 0xdc,0xd8                 # fallback; gen=dc d8
    .byte 0xdc,0xe0                 # fallback; gen=dc e0
    .byte 0xdc,0xe8                 # fallback; gen=dc e8
    .byte 0xdc,0xf0                 # fallback; gen=dc f0
    .byte 0xdc,0xf8                 # fallback; gen=dc f8
    .byte 0xdc,0x0e                 # fallback; gen=dc 0e
    .byte 0xdc,0x16                 # fallback; gen=dc 16
    .byte 0xdc,0x1e                 # fallback; gen=dc 1e
    .byte 0xdc,0x26                 # fallback; gen=dc 26
    .byte 0xdc,0x2e                 # fallback; gen=dc 2e
    .byte 0xdc,0x36                 # fallback; gen=dc 36
    .byte 0xdc,0x3e                 # fallback; gen=dc 3e
    .byte 0xdd,0xc0                 # fallback; gen=dd c0
    .byte 0xdd,0x06                 # fallback; gen=dd 06
    .byte 0xdd,0xc8                 # fallback; gen=dd c8
    .byte 0xdd,0xd0                 # fallback; gen=dd d0
    .byte 0xdd,0xd8                 # fallback; gen=dd d8
    .byte 0xdd,0xe0                 # fallback; gen=dd e0
    .byte 0xdd,0xe8                 # fallback; gen=dd e8
    .byte 0xdd,0xf0                 # fallback; gen=dd f0
    .byte 0xdd,0xf8                 # fallback; gen=dd f8
    .byte 0xdd,0x0e                 # fallback; gen=dd 0e
    .byte 0xdd,0x16                 # fallback; gen=dd 16
    .byte 0xdd,0x1e                 # fallback; gen=dd 1e
    .byte 0xdd,0x26                 # fallback; gen=dd 26
    .byte 0xdd,0x2e                 # fallback; gen=dd 2e
    .byte 0xdd,0x36                 # fallback; gen=dd 36
    .byte 0xdd,0x3e                 # fallback; gen=dd 3e
    .byte 0xde,0xc0                 # fallback; gen=de c0
    .byte 0xde,0x06                 # fallback; gen=de 06
    .byte 0xde,0xc8                 # fallback; gen=de c8
    .byte 0xde,0xd0                 # fallback; gen=de d0
    .byte 0xde,0xd8                 # fallback; gen=de d8
    .byte 0xde,0xe0                 # fallback; gen=de e0
    .byte 0xde,0xe8                 # fallback; gen=de e8
    .byte 0xde,0xf0                 # fallback; gen=de f0
    .byte 0xde,0xf8                 # fallback; gen=de f8
    .byte 0xde,0x0e                 # fallback; gen=de 0e
    .byte 0xde,0x16                 # fallback; gen=de 16
    .byte 0xde,0x1e                 # fallback; gen=de 1e
    .byte 0xde,0x26                 # fallback; gen=de 26
    .byte 0xde,0x2e                 # fallback; gen=de 2e
    .byte 0xde,0x36                 # fallback; gen=de 36
    .byte 0xde,0x3e                 # fallback; gen=de 3e
    .byte 0xdf,0xc0                 # fallback; gen=df c0
    .byte 0xdf,0x06                 # fallback; gen=df 06
    .byte 0xdf,0xc8                 # fallback; gen=df c8
    .byte 0xdf,0xd0                 # fallback; gen=df d0
    .byte 0xdf,0xd8                 # fallback; gen=df d8
    .byte 0xdf,0xe0                 # fallback; gen=df e0
    .byte 0xdf,0xe8                 # fallback; gen=df e8
    .byte 0xdf,0xf0                 # fallback; gen=df f0
    .byte 0xdf,0xf8                 # fallback; gen=df f8
    .byte 0xdf,0x0e                 # fallback; gen=df 0e
    .byte 0xdf,0x16                 # fallback; gen=df 16
    .byte 0xdf,0x1e                 # fallback; gen=df 1e
    .byte 0xdf,0x26                 # fallback; gen=df 26
    .byte 0xdf,0x2e                 # fallback; gen=df 2e
    .byte 0xdf,0x36                 # fallback; gen=df 36
    .byte 0xdf,0x3e                 # fallback; gen=df 3e
    .byte 0xe0,0xc0                 # fallback; gen=e0 c0
    .byte 0xe0,0x06                 # fallback; gen=e0 06
    .byte 0xe1,0xc0                 # fallback; gen=e1 c0
    .byte 0xe1,0x06                 # fallback; gen=e1 06
    .byte 0xe2,0xc0                 # fallback; gen=e2 c0
    .byte 0xe2,0x06                 # fallback; gen=e2 06
    .byte 0xe3,0xc0                 # fallback; gen=e3 c0
    .byte 0xe3,0x06                 # fallback; gen=e3 06
    in     $0xc0,%al                    # gen=e4 c0  dis=e4 c0
    in     $0x6,%al                     # gen=e4 06  dis=e4 06
    in     $0xc0,%ax                    # gen=e5 c0  dis=e5 c0
    in     $0x6,%ax                     # gen=e5 06  dis=e5 06
    out    %al,$0xc0                    # gen=e6 c0  dis=e6 c0
    out    %al,$0x6                     # gen=e6 06  dis=e6 06
    out    %ax,$0xc0                    # gen=e7 c0  dis=e7 c0
    out    %ax,$0x6                     # gen=e7 06  dis=e7 06
    call   0xc803                       # gen=e8 c0  dis=e8 c0 90
    call   0xc759                       # gen=e8 06  dis=e8 06 90
    jmp    0xc823                       # gen=e9 c0  dis=e9 c0 90
    jmp    0xc779                       # gen=e9 06  dis=e9 06 90
    ljmp   $0x9090,$0x90c0              # gen=ea c0  dis=ea c0 90 90 90
    ljmp   $0x9090,$0x9006              # gen=ea 06  dis=ea 06 90 90 90
    jmp    0x3762                       # gen=eb c0  dis=eb c0
    jmp    0x37b8                       # gen=eb 06  dis=eb 06
    in     (%dx),%al                    # gen=ec c0  dis=ec
    in     (%dx),%al                    # gen=ec 06  dis=ec
    in     (%dx),%ax                    # gen=ed c0  dis=ed
    in     (%dx),%ax                    # gen=ed 06  dis=ed
    out    %al,(%dx)                    # gen=ee c0  dis=ee
    out    %al,(%dx)                    # gen=ee 06  dis=ee
    out    %ax,(%dx)                    # gen=ef c0  dis=ef
    out    %ax,(%dx)                    # gen=ef 06  dis=ef
    .byte 0xf0,0xc0                 # fallback; gen=f0 c0
    .byte 0xf0,0x06                 # fallback; gen=f0 06
    int1                                # gen=f1 c0  dis=f1
    int1                                # gen=f1 06  dis=f1
    .byte 0xf2,0xc0                 # fallback; gen=f2 c0
    .byte 0xf2,0x06                 # fallback; gen=f2 06
    .byte 0xf3,0xc0                 # fallback; gen=f3 c0
    .byte 0xf3,0x06                 # fallback; gen=f3 06
    .byte 0xf3,0xc8                 # fallback; gen=f3 c8
    .byte 0xf3,0xd0                 # fallback; gen=f3 d0
    .byte 0xf3,0xd8                 # fallback; gen=f3 d8
    .byte 0xf3,0xe0                 # fallback; gen=f3 e0
    .byte 0xf3,0xe8                 # fallback; gen=f3 e8
    .byte 0xf3,0xf0                 # fallback; gen=f3 f0
    .byte 0xf3,0xf8                 # fallback; gen=f3 f8
    .byte 0xf3,0x0e                 # fallback; gen=f3 0e
    .byte 0xf3,0x16                 # fallback; gen=f3 16
    .byte 0xf3,0x1e                 # fallback; gen=f3 1e
    .byte 0xf3,0x26                 # fallback; gen=f3 26
    .byte 0xf3,0x2e                 # fallback; gen=f3 2e
    .byte 0xf3,0x36                 # fallback; gen=f3 36
    .byte 0xf3,0x3e                 # fallback; gen=f3 3e
    hlt                                 # gen=f4 c0  dis=f4
    hlt                                 # gen=f4 06  dis=f4
    cmc                                 # gen=f5 c0  dis=f5
    cmc                                 # gen=f5 06  dis=f5
    test   $0x90,%al                    # gen=f6 c0  dis=f6 c0 90
    testb  $0x90,-0x6f70                # gen=f6 06  dis=f6 06 90 90 90
    test   $0x90,%al                    # gen=f6 c8  dis=f6 c8 90
    not    %al                          # gen=f6 d0  dis=f6 d0
    neg    %al                          # gen=f6 d8  dis=f6 d8
    mul    %al                          # gen=f6 e0  dis=f6 e0
    imul   %al                          # gen=f6 e8  dis=f6 e8
    div    %al                          # gen=f6 f0  dis=f6 f0
    idiv   %al                          # gen=f6 f8  dis=f6 f8
    testb  $0x90,-0x6f70                # gen=f6 0e  dis=f6 0e 90 90 90
    notb   -0x6f70                      # gen=f6 16  dis=f6 16 90 90
    negb   -0x6f70                      # gen=f6 1e  dis=f6 1e 90 90
    mulb   -0x6f70                      # gen=f6 26  dis=f6 26 90 90
    imulb  -0x6f70                      # gen=f6 2e  dis=f6 2e 90 90
    divb   -0x6f70                      # gen=f6 36  dis=f6 36 90 90
    idivb  -0x6f70                      # gen=f6 3e  dis=f6 3e 90 90
    test   $0x9090,%ax                  # gen=f7 c0  dis=f7 c0 90 90
    testw  $0x9090,-0x6f70              # gen=f7 06  dis=f7 06 90 90 90 90
    test   $0x9090,%ax                  # gen=f7 c8  dis=f7 c8 90 90
    not    %ax                          # gen=f7 d0  dis=f7 d0
    neg    %ax                          # gen=f7 d8  dis=f7 d8
    mul    %ax                          # gen=f7 e0  dis=f7 e0
    imul   %ax                          # gen=f7 e8  dis=f7 e8
    div    %ax                          # gen=f7 f0  dis=f7 f0
    idiv   %ax                          # gen=f7 f8  dis=f7 f8
    testw  $0x9090,-0x6f70              # gen=f7 0e  dis=f7 0e 90 90 90 90
    notw   -0x6f70                      # gen=f7 16  dis=f7 16 90 90
    negw   -0x6f70                      # gen=f7 1e  dis=f7 1e 90 90
    mulw   -0x6f70                      # gen=f7 26  dis=f7 26 90 90
    imulw  -0x6f70                      # gen=f7 2e  dis=f7 2e 90 90
    divw   -0x6f70                      # gen=f7 36  dis=f7 36 90 90
    idivw  -0x6f70                      # gen=f7 3e  dis=f7 3e 90 90
    clc                                 # gen=f8 c0  dis=f8
    clc                                 # gen=f8 06  dis=f8
    stc                                 # gen=f9 c0  dis=f9
    stc                                 # gen=f9 06  dis=f9
    cli                                 # gen=fa c0  dis=fa
    cli                                 # gen=fa 06  dis=fa
    sti                                 # gen=fb c0  dis=fb
    sti                                 # gen=fb 06  dis=fb
    cld                                 # gen=fc c0  dis=fc
    cld                                 # gen=fc 06  dis=fc
    std                                 # gen=fd c0  dis=fd
    std                                 # gen=fd 06  dis=fd
    inc    %al                          # gen=fe c0  dis=fe c0
    incb   -0x6f70                      # gen=fe 06  dis=fe 06 90 90
    dec    %al                          # gen=fe c8  dis=fe c8
    .byte 0xfe,0xd0                 # fallback; gen=fe d0
    .byte 0xfe,0xd8                 # fallback; gen=fe d8
    .byte 0xfe,0xe0                 # fallback; gen=fe e0
    .byte 0xfe,0xe8                 # fallback; gen=fe e8
    .byte 0xfe,0xf0                 # fallback; gen=fe f0
    .byte 0xfe,0xf8                 # fallback; gen=fe f8
    decb   -0x6f70                      # gen=fe 0e  dis=fe 0e 90 90
    .byte 0xfe,0x16                 # fallback; gen=fe 16
    .byte 0xfe,0x1e                 # fallback; gen=fe 1e
    .byte 0xfe,0x26                 # fallback; gen=fe 26
    .byte 0xfe,0x2e                 # fallback; gen=fe 2e
    .byte 0xfe,0x36                 # fallback; gen=fe 36
    .byte 0xfe,0x3e                 # fallback; gen=fe 3e
    inc    %ax                          # gen=ff c0  dis=ff c0
    incw   -0x6f70                      # gen=ff 06  dis=ff 06 90 90
    dec    %ax                          # gen=ff c8  dis=ff c8
    call   *%ax                         # gen=ff d0  dis=ff d0
    .byte 0xff,0xd8                 # fallback; gen=ff d8
    jmp    *%ax                         # gen=ff e0  dis=ff e0
    .byte 0xff,0xe8                 # fallback; gen=ff e8
    push   %ax                          # gen=ff f0  dis=ff f0
    .byte 0xff,0xf8                 # fallback; gen=ff f8
    decw   -0x6f70                      # gen=ff 0e  dis=ff 0e 90 90
    call   *-0x6f70                     # gen=ff 16  dis=ff 16 90 90
    lcall  *-0x6f70                     # gen=ff 1e  dis=ff 1e 90 90
    jmp    *-0x6f70                     # gen=ff 26  dis=ff 26 90 90
    ljmp   *-0x6f70                     # gen=ff 2e  dis=ff 2e 90 90
    push   -0x6f70                      # gen=ff 36  dis=ff 36 90 90
    .byte 0xff,0x3e                 # fallback; gen=ff 3e
    .byte 0xf0,0x00,0xc0            # fallback; gen=f0 00 c0
    lock add %al,-0x6f70                # gen=f0 00 06  dis=f0 00 06 90 90
    .byte 0xf0,0x01,0xc0            # fallback; gen=f0 01 c0
    lock add %ax,-0x6f70                # gen=f0 01 06  dis=f0 01 06 90 90
    .byte 0xf0,0x02,0xc0            # fallback; gen=f0 02 c0
    .byte 0xf0,0x02,0x06            # fallback; gen=f0 02 06
    .byte 0xf0,0x03,0xc0            # fallback; gen=f0 03 c0
    .byte 0xf0,0x03,0x06            # fallback; gen=f0 03 06
    .byte 0xf0,0x04,0xc0            # fallback; gen=f0 04 c0
    .byte 0xf0,0x04,0x06            # fallback; gen=f0 04 06
    .byte 0xf0,0x05,0xc0            # fallback; gen=f0 05 c0
    .byte 0xf0,0x05,0x06            # fallback; gen=f0 05 06
    .byte 0xf0,0x06,0xc0            # fallback; gen=f0 06 c0
    .byte 0xf0,0x06,0x06            # fallback; gen=f0 06 06
    .byte 0xf0,0x07,0xc0            # fallback; gen=f0 07 c0
    .byte 0xf0,0x07,0x06            # fallback; gen=f0 07 06
    .byte 0xf0,0x08,0xc0            # fallback; gen=f0 08 c0
    lock or %al,-0x6f70                 # gen=f0 08 06  dis=f0 08 06 90 90
    .byte 0xf0,0x09,0xc0            # fallback; gen=f0 09 c0
    lock or %ax,-0x6f70                 # gen=f0 09 06  dis=f0 09 06 90 90
    .byte 0xf0,0x0a,0xc0            # fallback; gen=f0 0a c0
    .byte 0xf0,0x0a,0x06            # fallback; gen=f0 0a 06
    .byte 0xf0,0x0b,0xc0            # fallback; gen=f0 0b c0
    .byte 0xf0,0x0b,0x06            # fallback; gen=f0 0b 06
    .byte 0xf0,0x0c,0xc0            # fallback; gen=f0 0c c0
    .byte 0xf0,0x0c,0x06            # fallback; gen=f0 0c 06
    .byte 0xf0,0x0d,0xc0            # fallback; gen=f0 0d c0
    .byte 0xf0,0x0d,0x06            # fallback; gen=f0 0d 06
    .byte 0xf0,0x0e,0xc0            # fallback; gen=f0 0e c0
    .byte 0xf0,0x0e,0x06            # fallback; gen=f0 0e 06
    .byte 0xf0,0x0f,0xc0            # fallback; gen=f0 0f c0
    .byte 0xf0,0x0f,0x06            # fallback; gen=f0 0f 06
    .byte 0xf0,0x10,0xc0            # fallback; gen=f0 10 c0
    lock adc %al,-0x6f70                # gen=f0 10 06  dis=f0 10 06 90 90
    .byte 0xf0,0x11,0xc0            # fallback; gen=f0 11 c0
    lock adc %ax,-0x6f70                # gen=f0 11 06  dis=f0 11 06 90 90
    .byte 0xf0,0x12,0xc0            # fallback; gen=f0 12 c0
    .byte 0xf0,0x12,0x06            # fallback; gen=f0 12 06
    .byte 0xf0,0x13,0xc0            # fallback; gen=f0 13 c0
    .byte 0xf0,0x13,0x06            # fallback; gen=f0 13 06
    .byte 0xf0,0x14,0xc0            # fallback; gen=f0 14 c0
    .byte 0xf0,0x14,0x06            # fallback; gen=f0 14 06
    .byte 0xf0,0x15,0xc0            # fallback; gen=f0 15 c0
    .byte 0xf0,0x15,0x06            # fallback; gen=f0 15 06
    .byte 0xf0,0x16,0xc0            # fallback; gen=f0 16 c0
    .byte 0xf0,0x16,0x06            # fallback; gen=f0 16 06
    .byte 0xf0,0x17,0xc0            # fallback; gen=f0 17 c0
    .byte 0xf0,0x17,0x06            # fallback; gen=f0 17 06
    .byte 0xf0,0x18,0xc0            # fallback; gen=f0 18 c0
    lock sbb %al,-0x6f70                # gen=f0 18 06  dis=f0 18 06 90 90
    .byte 0xf0,0x19,0xc0            # fallback; gen=f0 19 c0
    lock sbb %ax,-0x6f70                # gen=f0 19 06  dis=f0 19 06 90 90
    .byte 0xf0,0x1a,0xc0            # fallback; gen=f0 1a c0
    .byte 0xf0,0x1a,0x06            # fallback; gen=f0 1a 06
    .byte 0xf0,0x1b,0xc0            # fallback; gen=f0 1b c0
    .byte 0xf0,0x1b,0x06            # fallback; gen=f0 1b 06
    .byte 0xf0,0x1c,0xc0            # fallback; gen=f0 1c c0
    .byte 0xf0,0x1c,0x06            # fallback; gen=f0 1c 06
    .byte 0xf0,0x1d,0xc0            # fallback; gen=f0 1d c0
    .byte 0xf0,0x1d,0x06            # fallback; gen=f0 1d 06
    .byte 0xf0,0x1e,0xc0            # fallback; gen=f0 1e c0
    .byte 0xf0,0x1e,0x06            # fallback; gen=f0 1e 06
    .byte 0xf0,0x1f,0xc0            # fallback; gen=f0 1f c0
    .byte 0xf0,0x1f,0x06            # fallback; gen=f0 1f 06
    .byte 0xf0,0x20,0xc0            # fallback; gen=f0 20 c0
    lock and %al,-0x6f70                # gen=f0 20 06  dis=f0 20 06 90 90
    .byte 0xf0,0x21,0xc0            # fallback; gen=f0 21 c0
    lock and %ax,-0x6f70                # gen=f0 21 06  dis=f0 21 06 90 90
    .byte 0xf0,0x22,0xc0            # fallback; gen=f0 22 c0
    .byte 0xf0,0x22,0x06            # fallback; gen=f0 22 06
    .byte 0xf0,0x23,0xc0            # fallback; gen=f0 23 c0
    .byte 0xf0,0x23,0x06            # fallback; gen=f0 23 06
    .byte 0xf0,0x24,0xc0            # fallback; gen=f0 24 c0
    .byte 0xf0,0x24,0x06            # fallback; gen=f0 24 06
    .byte 0xf0,0x25,0xc0            # fallback; gen=f0 25 c0
    .byte 0xf0,0x25,0x06            # fallback; gen=f0 25 06
    .byte 0xf0,0x26,0xc0            # fallback; gen=f0 26 c0
    .byte 0xf0,0x26,0x06            # fallback; gen=f0 26 06
    .byte 0xf0,0x27,0xc0            # fallback; gen=f0 27 c0
    .byte 0xf0,0x27,0x06            # fallback; gen=f0 27 06
    .byte 0xf0,0x28,0xc0            # fallback; gen=f0 28 c0
    lock sub %al,-0x6f70                # gen=f0 28 06  dis=f0 28 06 90 90
    .byte 0xf0,0x29,0xc0            # fallback; gen=f0 29 c0
    lock sub %ax,-0x6f70                # gen=f0 29 06  dis=f0 29 06 90 90
    .byte 0xf0,0x2a,0xc0            # fallback; gen=f0 2a c0
    .byte 0xf0,0x2a,0x06            # fallback; gen=f0 2a 06
    .byte 0xf0,0x2b,0xc0            # fallback; gen=f0 2b c0
    .byte 0xf0,0x2b,0x06            # fallback; gen=f0 2b 06
    .byte 0xf0,0x2c,0xc0            # fallback; gen=f0 2c c0
    .byte 0xf0,0x2c,0x06            # fallback; gen=f0 2c 06
    .byte 0xf0,0x2d,0xc0            # fallback; gen=f0 2d c0
    .byte 0xf0,0x2d,0x06            # fallback; gen=f0 2d 06
    .byte 0xf0,0x2e,0xc0            # fallback; gen=f0 2e c0
    .byte 0xf0,0x2e,0x06            # fallback; gen=f0 2e 06
    .byte 0xf0,0x2f,0xc0            # fallback; gen=f0 2f c0
    .byte 0xf0,0x2f,0x06            # fallback; gen=f0 2f 06
    .byte 0xf0,0x30,0xc0            # fallback; gen=f0 30 c0
    lock xor %al,-0x6f70                # gen=f0 30 06  dis=f0 30 06 90 90
    .byte 0xf0,0x31,0xc0            # fallback; gen=f0 31 c0
    lock xor %ax,-0x6f70                # gen=f0 31 06  dis=f0 31 06 90 90
    .byte 0xf0,0x32,0xc0            # fallback; gen=f0 32 c0
    .byte 0xf0,0x32,0x06            # fallback; gen=f0 32 06
    .byte 0xf0,0x33,0xc0            # fallback; gen=f0 33 c0
    .byte 0xf0,0x33,0x06            # fallback; gen=f0 33 06
    .byte 0xf0,0x34,0xc0            # fallback; gen=f0 34 c0
    .byte 0xf0,0x34,0x06            # fallback; gen=f0 34 06
    .byte 0xf0,0x35,0xc0            # fallback; gen=f0 35 c0
    .byte 0xf0,0x35,0x06            # fallback; gen=f0 35 06
    .byte 0xf0,0x36,0xc0            # fallback; gen=f0 36 c0
    .byte 0xf0,0x36,0x06            # fallback; gen=f0 36 06
    .byte 0xf0,0x37,0xc0            # fallback; gen=f0 37 c0
    .byte 0xf0,0x37,0x06            # fallback; gen=f0 37 06
    .byte 0xf0,0x38,0xc0            # fallback; gen=f0 38 c0
    .byte 0xf0,0x38,0x06            # fallback; gen=f0 38 06
    .byte 0xf0,0x39,0xc0            # fallback; gen=f0 39 c0
    .byte 0xf0,0x39,0x06            # fallback; gen=f0 39 06
    .byte 0xf0,0x3a,0xc0            # fallback; gen=f0 3a c0
    .byte 0xf0,0x3a,0x06            # fallback; gen=f0 3a 06
    .byte 0xf0,0x3b,0xc0            # fallback; gen=f0 3b c0
    .byte 0xf0,0x3b,0x06            # fallback; gen=f0 3b 06
    .byte 0xf0,0x3c,0xc0            # fallback; gen=f0 3c c0
    .byte 0xf0,0x3c,0x06            # fallback; gen=f0 3c 06
    .byte 0xf0,0x3d,0xc0            # fallback; gen=f0 3d c0
    .byte 0xf0,0x3d,0x06            # fallback; gen=f0 3d 06
    .byte 0xf0,0x3e,0xc0            # fallback; gen=f0 3e c0
    .byte 0xf0,0x3e,0x06            # fallback; gen=f0 3e 06
    .byte 0xf0,0x3f,0xc0            # fallback; gen=f0 3f c0
    .byte 0xf0,0x3f,0x06            # fallback; gen=f0 3f 06
    .byte 0xf0,0x40,0xc0            # fallback; gen=f0 40 c0
    .byte 0xf0,0x40,0x06            # fallback; gen=f0 40 06
    .byte 0xf0,0x41,0xc0            # fallback; gen=f0 41 c0
    .byte 0xf0,0x41,0x06            # fallback; gen=f0 41 06
    .byte 0xf0,0x42,0xc0            # fallback; gen=f0 42 c0
    .byte 0xf0,0x42,0x06            # fallback; gen=f0 42 06
    .byte 0xf0,0x43,0xc0            # fallback; gen=f0 43 c0
    .byte 0xf0,0x43,0x06            # fallback; gen=f0 43 06
    .byte 0xf0,0x44,0xc0            # fallback; gen=f0 44 c0
    .byte 0xf0,0x44,0x06            # fallback; gen=f0 44 06
    .byte 0xf0,0x45,0xc0            # fallback; gen=f0 45 c0
    .byte 0xf0,0x45,0x06            # fallback; gen=f0 45 06
    .byte 0xf0,0x46,0xc0            # fallback; gen=f0 46 c0
    .byte 0xf0,0x46,0x06            # fallback; gen=f0 46 06
    .byte 0xf0,0x47,0xc0            # fallback; gen=f0 47 c0
    .byte 0xf0,0x47,0x06            # fallback; gen=f0 47 06
    .byte 0xf0,0x48,0xc0            # fallback; gen=f0 48 c0
    .byte 0xf0,0x48,0x06            # fallback; gen=f0 48 06
    .byte 0xf0,0x49,0xc0            # fallback; gen=f0 49 c0
    .byte 0xf0,0x49,0x06            # fallback; gen=f0 49 06
    .byte 0xf0,0x4a,0xc0            # fallback; gen=f0 4a c0
    .byte 0xf0,0x4a,0x06            # fallback; gen=f0 4a 06
    .byte 0xf0,0x4b,0xc0            # fallback; gen=f0 4b c0
    .byte 0xf0,0x4b,0x06            # fallback; gen=f0 4b 06
    .byte 0xf0,0x4c,0xc0            # fallback; gen=f0 4c c0
    .byte 0xf0,0x4c,0x06            # fallback; gen=f0 4c 06
    .byte 0xf0,0x4d,0xc0            # fallback; gen=f0 4d c0
    .byte 0xf0,0x4d,0x06            # fallback; gen=f0 4d 06
    .byte 0xf0,0x4e,0xc0            # fallback; gen=f0 4e c0
    .byte 0xf0,0x4e,0x06            # fallback; gen=f0 4e 06
    .byte 0xf0,0x4f,0xc0            # fallback; gen=f0 4f c0
    .byte 0xf0,0x4f,0x06            # fallback; gen=f0 4f 06
    .byte 0xf0,0x50,0xc0            # fallback; gen=f0 50 c0
    .byte 0xf0,0x50,0x06            # fallback; gen=f0 50 06
    .byte 0xf0,0x51,0xc0            # fallback; gen=f0 51 c0
    .byte 0xf0,0x51,0x06            # fallback; gen=f0 51 06
    .byte 0xf0,0x52,0xc0            # fallback; gen=f0 52 c0
    .byte 0xf0,0x52,0x06            # fallback; gen=f0 52 06
    .byte 0xf0,0x53,0xc0            # fallback; gen=f0 53 c0
    .byte 0xf0,0x53,0x06            # fallback; gen=f0 53 06
    .byte 0xf0,0x54,0xc0            # fallback; gen=f0 54 c0
    .byte 0xf0,0x54,0x06            # fallback; gen=f0 54 06
    .byte 0xf0,0x55,0xc0            # fallback; gen=f0 55 c0
    .byte 0xf0,0x55,0x06            # fallback; gen=f0 55 06
    .byte 0xf0,0x56,0xc0            # fallback; gen=f0 56 c0
    .byte 0xf0,0x56,0x06            # fallback; gen=f0 56 06
    .byte 0xf0,0x57,0xc0            # fallback; gen=f0 57 c0
    .byte 0xf0,0x57,0x06            # fallback; gen=f0 57 06
    .byte 0xf0,0x58,0xc0            # fallback; gen=f0 58 c0
    .byte 0xf0,0x58,0x06            # fallback; gen=f0 58 06
    .byte 0xf0,0x59,0xc0            # fallback; gen=f0 59 c0
    .byte 0xf0,0x59,0x06            # fallback; gen=f0 59 06
    .byte 0xf0,0x5a,0xc0            # fallback; gen=f0 5a c0
    .byte 0xf0,0x5a,0x06            # fallback; gen=f0 5a 06
    .byte 0xf0,0x5b,0xc0            # fallback; gen=f0 5b c0
    .byte 0xf0,0x5b,0x06            # fallback; gen=f0 5b 06
    .byte 0xf0,0x5c,0xc0            # fallback; gen=f0 5c c0
    .byte 0xf0,0x5c,0x06            # fallback; gen=f0 5c 06
    .byte 0xf0,0x5d,0xc0            # fallback; gen=f0 5d c0
    .byte 0xf0,0x5d,0x06            # fallback; gen=f0 5d 06
    .byte 0xf0,0x5e,0xc0            # fallback; gen=f0 5e c0
    .byte 0xf0,0x5e,0x06            # fallback; gen=f0 5e 06
    .byte 0xf0,0x5f,0xc0            # fallback; gen=f0 5f c0
    .byte 0xf0,0x5f,0x06            # fallback; gen=f0 5f 06
    .byte 0xf0,0x60,0xc0            # fallback; gen=f0 60 c0
    .byte 0xf0,0x60,0x06            # fallback; gen=f0 60 06
    .byte 0xf0,0x61,0xc0            # fallback; gen=f0 61 c0
    .byte 0xf0,0x61,0x06            # fallback; gen=f0 61 06
    .byte 0xf0,0x62,0xc0            # fallback; gen=f0 62 c0
    .byte 0xf0,0x62,0x06            # fallback; gen=f0 62 06
    .byte 0xf0,0x63,0xc0            # fallback; gen=f0 63 c0
    .byte 0xf0,0x63,0x06            # fallback; gen=f0 63 06
    .byte 0xf0,0x64,0xc0            # fallback; gen=f0 64 c0
    .byte 0xf0,0x64,0x06            # fallback; gen=f0 64 06
    .byte 0xf0,0x65,0xc0            # fallback; gen=f0 65 c0
    .byte 0xf0,0x65,0x06            # fallback; gen=f0 65 06
    .byte 0xf0,0x66,0xc0            # fallback; gen=f0 66 c0
    .byte 0xf0,0x66,0x06            # fallback; gen=f0 66 06
    .byte 0xf0,0x67,0xc0            # fallback; gen=f0 67 c0
    .byte 0xf0,0x67,0x06            # fallback; gen=f0 67 06
    .byte 0xf0,0x68,0xc0            # fallback; gen=f0 68 c0
    .byte 0xf0,0x68,0x06            # fallback; gen=f0 68 06
    .byte 0xf0,0x69,0xc0            # fallback; gen=f0 69 c0
    .byte 0xf0,0x69,0x06            # fallback; gen=f0 69 06
    .byte 0xf0,0x6a,0xc0            # fallback; gen=f0 6a c0
    .byte 0xf0,0x6a,0x06            # fallback; gen=f0 6a 06
    .byte 0xf0,0x6b,0xc0            # fallback; gen=f0 6b c0
    .byte 0xf0,0x6b,0x06            # fallback; gen=f0 6b 06
    .byte 0xf0,0x6c,0xc0            # fallback; gen=f0 6c c0
    .byte 0xf0,0x6c,0x06            # fallback; gen=f0 6c 06
    .byte 0xf0,0x6d,0xc0            # fallback; gen=f0 6d c0
    .byte 0xf0,0x6d,0x06            # fallback; gen=f0 6d 06
    .byte 0xf0,0x6e,0xc0            # fallback; gen=f0 6e c0
    .byte 0xf0,0x6e,0x06            # fallback; gen=f0 6e 06
    .byte 0xf0,0x6f,0xc0            # fallback; gen=f0 6f c0
    .byte 0xf0,0x6f,0x06            # fallback; gen=f0 6f 06
    .byte 0xf0,0x70,0xc0            # fallback; gen=f0 70 c0
    .byte 0xf0,0x70,0x06            # fallback; gen=f0 70 06
    .byte 0xf0,0x71,0xc0            # fallback; gen=f0 71 c0
    .byte 0xf0,0x71,0x06            # fallback; gen=f0 71 06
    .byte 0xf0,0x72,0xc0            # fallback; gen=f0 72 c0
    .byte 0xf0,0x72,0x06            # fallback; gen=f0 72 06
    .byte 0xf0,0x73,0xc0            # fallback; gen=f0 73 c0
    .byte 0xf0,0x73,0x06            # fallback; gen=f0 73 06
    .byte 0xf0,0x74,0xc0            # fallback; gen=f0 74 c0
    .byte 0xf0,0x74,0x06            # fallback; gen=f0 74 06
    .byte 0xf0,0x75,0xc0            # fallback; gen=f0 75 c0
    .byte 0xf0,0x75,0x06            # fallback; gen=f0 75 06
    .byte 0xf0,0x76,0xc0            # fallback; gen=f0 76 c0
    .byte 0xf0,0x76,0x06            # fallback; gen=f0 76 06
    .byte 0xf0,0x77,0xc0            # fallback; gen=f0 77 c0
    .byte 0xf0,0x77,0x06            # fallback; gen=f0 77 06
    .byte 0xf0,0x78,0xc0            # fallback; gen=f0 78 c0
    .byte 0xf0,0x78,0x06            # fallback; gen=f0 78 06
    .byte 0xf0,0x79,0xc0            # fallback; gen=f0 79 c0
    .byte 0xf0,0x79,0x06            # fallback; gen=f0 79 06
    .byte 0xf0,0x7a,0xc0            # fallback; gen=f0 7a c0
    .byte 0xf0,0x7a,0x06            # fallback; gen=f0 7a 06
    .byte 0xf0,0x7b,0xc0            # fallback; gen=f0 7b c0
    .byte 0xf0,0x7b,0x06            # fallback; gen=f0 7b 06
    .byte 0xf0,0x7c,0xc0            # fallback; gen=f0 7c c0
    .byte 0xf0,0x7c,0x06            # fallback; gen=f0 7c 06
    .byte 0xf0,0x7d,0xc0            # fallback; gen=f0 7d c0
    .byte 0xf0,0x7d,0x06            # fallback; gen=f0 7d 06
    .byte 0xf0,0x7e,0xc0            # fallback; gen=f0 7e c0
    .byte 0xf0,0x7e,0x06            # fallback; gen=f0 7e 06
    .byte 0xf0,0x7f,0xc0            # fallback; gen=f0 7f c0
    .byte 0xf0,0x7f,0x06            # fallback; gen=f0 7f 06
    .byte 0xf0,0x80,0xc0            # fallback; gen=f0 80 c0
    lock addb $0x90,-0x6f70             # gen=f0 80 06  dis=f0 80 06 90 90 90
    .byte 0xf0,0x81,0xc0            # fallback; gen=f0 81 c0
    lock addw $0x9090,-0x6f70           # gen=f0 81 06  dis=f0 81 06 90 90 90 90
    .byte 0xf0,0x82,0xc0            # fallback; gen=f0 82 c0
    lock addb $0x90,-0x6f70             # gen=f0 82 06  dis=f0 82 06 90 90 90
    .byte 0xf0,0x83,0xc0            # fallback; gen=f0 83 c0
    lock addw $0xff90,-0x6f70           # gen=f0 83 06  dis=f0 83 06 90 90 90
    .byte 0xf0,0x84,0xc0            # fallback; gen=f0 84 c0
    .byte 0xf0,0x84,0x06            # fallback; gen=f0 84 06
    .byte 0xf0,0x85,0xc0            # fallback; gen=f0 85 c0
    .byte 0xf0,0x85,0x06            # fallback; gen=f0 85 06
    .byte 0xf0,0x86,0xc0            # fallback; gen=f0 86 c0
    lock xchg %al,-0x6f70               # gen=f0 86 06  dis=f0 86 06 90 90
    .byte 0xf0,0x87,0xc0            # fallback; gen=f0 87 c0
    lock xchg %ax,-0x6f70               # gen=f0 87 06  dis=f0 87 06 90 90
    .byte 0xf0,0x88,0xc0            # fallback; gen=f0 88 c0
    .byte 0xf0,0x88,0x06            # fallback; gen=f0 88 06
    .byte 0xf0,0x89,0xc0            # fallback; gen=f0 89 c0
    .byte 0xf0,0x89,0x06            # fallback; gen=f0 89 06
    .byte 0xf0,0x8a,0xc0            # fallback; gen=f0 8a c0
    .byte 0xf0,0x8a,0x06            # fallback; gen=f0 8a 06
    .byte 0xf0,0x8b,0xc0            # fallback; gen=f0 8b c0
    .byte 0xf0,0x8b,0x06            # fallback; gen=f0 8b 06
    .byte 0xf0,0x8c,0xc0            # fallback; gen=f0 8c c0
    .byte 0xf0,0x8c,0x06            # fallback; gen=f0 8c 06
    .byte 0xf0,0x8d,0xc0            # fallback; gen=f0 8d c0
    .byte 0xf0,0x8d,0x06            # fallback; gen=f0 8d 06
    .byte 0xf0,0x8e,0xc0            # fallback; gen=f0 8e c0
    .byte 0xf0,0x8e,0x06            # fallback; gen=f0 8e 06
    .byte 0xf0,0x8f,0xc0            # fallback; gen=f0 8f c0
    .byte 0xf0,0x8f,0x06            # fallback; gen=f0 8f 06
    .byte 0xf0,0x90,0xc0            # fallback; gen=f0 90 c0
    .byte 0xf0,0x90,0x06            # fallback; gen=f0 90 06
    .byte 0xf0,0x91,0xc0            # fallback; gen=f0 91 c0
    .byte 0xf0,0x91,0x06            # fallback; gen=f0 91 06
    .byte 0xf0,0x92,0xc0            # fallback; gen=f0 92 c0
    .byte 0xf0,0x92,0x06            # fallback; gen=f0 92 06
    .byte 0xf0,0x93,0xc0            # fallback; gen=f0 93 c0
    .byte 0xf0,0x93,0x06            # fallback; gen=f0 93 06
    .byte 0xf0,0x94,0xc0            # fallback; gen=f0 94 c0
    .byte 0xf0,0x94,0x06            # fallback; gen=f0 94 06
    .byte 0xf0,0x95,0xc0            # fallback; gen=f0 95 c0
    .byte 0xf0,0x95,0x06            # fallback; gen=f0 95 06
    .byte 0xf0,0x96,0xc0            # fallback; gen=f0 96 c0
    .byte 0xf0,0x96,0x06            # fallback; gen=f0 96 06
    .byte 0xf0,0x97,0xc0            # fallback; gen=f0 97 c0
    .byte 0xf0,0x97,0x06            # fallback; gen=f0 97 06
    .byte 0xf0,0x98,0xc0            # fallback; gen=f0 98 c0
    .byte 0xf0,0x98,0x06            # fallback; gen=f0 98 06
    .byte 0xf0,0x99,0xc0            # fallback; gen=f0 99 c0
    .byte 0xf0,0x99,0x06            # fallback; gen=f0 99 06
    .byte 0xf0,0x9a,0xc0            # fallback; gen=f0 9a c0
    .byte 0xf0,0x9a,0x06            # fallback; gen=f0 9a 06
    .byte 0xf0,0x9b,0xc0            # fallback; gen=f0 9b c0
    .byte 0xf0,0x9b,0x06            # fallback; gen=f0 9b 06
    .byte 0xf0,0x9c,0xc0            # fallback; gen=f0 9c c0
    .byte 0xf0,0x9c,0x06            # fallback; gen=f0 9c 06
    .byte 0xf0,0x9d,0xc0            # fallback; gen=f0 9d c0
    .byte 0xf0,0x9d,0x06            # fallback; gen=f0 9d 06
    .byte 0xf0,0x9e,0xc0            # fallback; gen=f0 9e c0
    .byte 0xf0,0x9e,0x06            # fallback; gen=f0 9e 06
    .byte 0xf0,0x9f,0xc0            # fallback; gen=f0 9f c0
    .byte 0xf0,0x9f,0x06            # fallback; gen=f0 9f 06
    .byte 0xf0,0xa0,0xc0            # fallback; gen=f0 a0 c0
    .byte 0xf0,0xa0,0x06            # fallback; gen=f0 a0 06
    .byte 0xf0,0xa1,0xc0            # fallback; gen=f0 a1 c0
    .byte 0xf0,0xa1,0x06            # fallback; gen=f0 a1 06
    .byte 0xf0,0xa2,0xc0            # fallback; gen=f0 a2 c0
    .byte 0xf0,0xa2,0x06            # fallback; gen=f0 a2 06
    .byte 0xf0,0xa3,0xc0            # fallback; gen=f0 a3 c0
    .byte 0xf0,0xa3,0x06            # fallback; gen=f0 a3 06
    .byte 0xf0,0xa4,0xc0            # fallback; gen=f0 a4 c0
    .byte 0xf0,0xa4,0x06            # fallback; gen=f0 a4 06
    .byte 0xf0,0xa5,0xc0            # fallback; gen=f0 a5 c0
    .byte 0xf0,0xa5,0x06            # fallback; gen=f0 a5 06
    .byte 0xf0,0xa6,0xc0            # fallback; gen=f0 a6 c0
    .byte 0xf0,0xa6,0x06            # fallback; gen=f0 a6 06
    .byte 0xf0,0xa7,0xc0            # fallback; gen=f0 a7 c0
    .byte 0xf0,0xa7,0x06            # fallback; gen=f0 a7 06
    .byte 0xf0,0xa8,0xc0            # fallback; gen=f0 a8 c0
    .byte 0xf0,0xa8,0x06            # fallback; gen=f0 a8 06
    .byte 0xf0,0xa9,0xc0            # fallback; gen=f0 a9 c0
    .byte 0xf0,0xa9,0x06            # fallback; gen=f0 a9 06
    .byte 0xf0,0xaa,0xc0            # fallback; gen=f0 aa c0
    .byte 0xf0,0xaa,0x06            # fallback; gen=f0 aa 06
    .byte 0xf0,0xab,0xc0            # fallback; gen=f0 ab c0
    .byte 0xf0,0xab,0x06            # fallback; gen=f0 ab 06
    .byte 0xf0,0xac,0xc0            # fallback; gen=f0 ac c0
    .byte 0xf0,0xac,0x06            # fallback; gen=f0 ac 06
    .byte 0xf0,0xad,0xc0            # fallback; gen=f0 ad c0
    .byte 0xf0,0xad,0x06            # fallback; gen=f0 ad 06
    .byte 0xf0,0xae,0xc0            # fallback; gen=f0 ae c0
    .byte 0xf0,0xae,0x06            # fallback; gen=f0 ae 06
    .byte 0xf0,0xaf,0xc0            # fallback; gen=f0 af c0
    .byte 0xf0,0xaf,0x06            # fallback; gen=f0 af 06
    .byte 0xf0,0xb0,0xc0            # fallback; gen=f0 b0 c0
    .byte 0xf0,0xb0,0x06            # fallback; gen=f0 b0 06
    .byte 0xf0,0xb1,0xc0            # fallback; gen=f0 b1 c0
    .byte 0xf0,0xb1,0x06            # fallback; gen=f0 b1 06
    .byte 0xf0,0xb2,0xc0            # fallback; gen=f0 b2 c0
    .byte 0xf0,0xb2,0x06            # fallback; gen=f0 b2 06
    .byte 0xf0,0xb3,0xc0            # fallback; gen=f0 b3 c0
    .byte 0xf0,0xb3,0x06            # fallback; gen=f0 b3 06
    .byte 0xf0,0xb4,0xc0            # fallback; gen=f0 b4 c0
    .byte 0xf0,0xb4,0x06            # fallback; gen=f0 b4 06
    .byte 0xf0,0xb5,0xc0            # fallback; gen=f0 b5 c0
    .byte 0xf0,0xb5,0x06            # fallback; gen=f0 b5 06
    .byte 0xf0,0xb6,0xc0            # fallback; gen=f0 b6 c0
    .byte 0xf0,0xb6,0x06            # fallback; gen=f0 b6 06
    .byte 0xf0,0xb7,0xc0            # fallback; gen=f0 b7 c0
    .byte 0xf0,0xb7,0x06            # fallback; gen=f0 b7 06
    .byte 0xf0,0xb8,0xc0            # fallback; gen=f0 b8 c0
    .byte 0xf0,0xb8,0x06            # fallback; gen=f0 b8 06
    .byte 0xf0,0xb9,0xc0            # fallback; gen=f0 b9 c0
    .byte 0xf0,0xb9,0x06            # fallback; gen=f0 b9 06
    .byte 0xf0,0xba,0xc0            # fallback; gen=f0 ba c0
    .byte 0xf0,0xba,0x06            # fallback; gen=f0 ba 06
    .byte 0xf0,0xbb,0xc0            # fallback; gen=f0 bb c0
    .byte 0xf0,0xbb,0x06            # fallback; gen=f0 bb 06
    .byte 0xf0,0xbc,0xc0            # fallback; gen=f0 bc c0
    .byte 0xf0,0xbc,0x06            # fallback; gen=f0 bc 06
    .byte 0xf0,0xbd,0xc0            # fallback; gen=f0 bd c0
    .byte 0xf0,0xbd,0x06            # fallback; gen=f0 bd 06
    .byte 0xf0,0xbe,0xc0            # fallback; gen=f0 be c0
    .byte 0xf0,0xbe,0x06            # fallback; gen=f0 be 06
    .byte 0xf0,0xbf,0xc0            # fallback; gen=f0 bf c0
    .byte 0xf0,0xbf,0x06            # fallback; gen=f0 bf 06
    .byte 0xf0,0xc0,0xc0            # fallback; gen=f0 c0 c0
    .byte 0xf0,0xc0,0x06            # fallback; gen=f0 c0 06
    .byte 0xf0,0xc1,0xc0            # fallback; gen=f0 c1 c0
    .byte 0xf0,0xc1,0x06            # fallback; gen=f0 c1 06
    .byte 0xf0,0xc2,0xc0            # fallback; gen=f0 c2 c0
    .byte 0xf0,0xc2,0x06            # fallback; gen=f0 c2 06
    .byte 0xf0,0xc3,0xc0            # fallback; gen=f0 c3 c0
    .byte 0xf0,0xc3,0x06            # fallback; gen=f0 c3 06
    .byte 0xf0,0xc4,0xc0            # fallback; gen=f0 c4 c0
    .byte 0xf0,0xc4,0x06            # fallback; gen=f0 c4 06
    .byte 0xf0,0xc5,0xc0            # fallback; gen=f0 c5 c0
    .byte 0xf0,0xc5,0x06            # fallback; gen=f0 c5 06
    .byte 0xf0,0xc6,0xc0            # fallback; gen=f0 c6 c0
    .byte 0xf0,0xc6,0x06            # fallback; gen=f0 c6 06
    .byte 0xf0,0xc7,0xc0            # fallback; gen=f0 c7 c0
    .byte 0xf0,0xc7,0x06            # fallback; gen=f0 c7 06
    .byte 0xf0,0xc8,0xc0            # fallback; gen=f0 c8 c0
    .byte 0xf0,0xc8,0x06            # fallback; gen=f0 c8 06
    .byte 0xf0,0xc9,0xc0            # fallback; gen=f0 c9 c0
    .byte 0xf0,0xc9,0x06            # fallback; gen=f0 c9 06
    .byte 0xf0,0xca,0xc0            # fallback; gen=f0 ca c0
    .byte 0xf0,0xca,0x06            # fallback; gen=f0 ca 06
    .byte 0xf0,0xcb,0xc0            # fallback; gen=f0 cb c0
    .byte 0xf0,0xcb,0x06            # fallback; gen=f0 cb 06
    .byte 0xf0,0xcc,0xc0            # fallback; gen=f0 cc c0
    .byte 0xf0,0xcc,0x06            # fallback; gen=f0 cc 06
    .byte 0xf0,0xcd,0xc0            # fallback; gen=f0 cd c0
    .byte 0xf0,0xcd,0x06            # fallback; gen=f0 cd 06
    .byte 0xf0,0xce,0xc0            # fallback; gen=f0 ce c0
    .byte 0xf0,0xce,0x06            # fallback; gen=f0 ce 06
    .byte 0xf0,0xcf,0xc0            # fallback; gen=f0 cf c0
    .byte 0xf0,0xcf,0x06            # fallback; gen=f0 cf 06
    .byte 0xf0,0xd0,0xc0            # fallback; gen=f0 d0 c0
    .byte 0xf0,0xd0,0x06            # fallback; gen=f0 d0 06
    .byte 0xf0,0xd1,0xc0            # fallback; gen=f0 d1 c0
    .byte 0xf0,0xd1,0x06            # fallback; gen=f0 d1 06
    .byte 0xf0,0xd2,0xc0            # fallback; gen=f0 d2 c0
    .byte 0xf0,0xd2,0x06            # fallback; gen=f0 d2 06
    .byte 0xf0,0xd3,0xc0            # fallback; gen=f0 d3 c0
    .byte 0xf0,0xd3,0x06            # fallback; gen=f0 d3 06
    .byte 0xf0,0xd4,0xc0            # fallback; gen=f0 d4 c0
    .byte 0xf0,0xd4,0x06            # fallback; gen=f0 d4 06
    .byte 0xf0,0xd5,0xc0            # fallback; gen=f0 d5 c0
    .byte 0xf0,0xd5,0x06            # fallback; gen=f0 d5 06
    .byte 0xf0,0xd6,0xc0            # fallback; gen=f0 d6 c0
    .byte 0xf0,0xd6,0x06            # fallback; gen=f0 d6 06
    .byte 0xf0,0xd7,0xc0            # fallback; gen=f0 d7 c0
    .byte 0xf0,0xd7,0x06            # fallback; gen=f0 d7 06
    .byte 0xf0,0xd8,0xc0            # fallback; gen=f0 d8 c0
    .byte 0xf0,0xd8,0x06            # fallback; gen=f0 d8 06
    .byte 0xf0,0xd9,0xc0            # fallback; gen=f0 d9 c0
    .byte 0xf0,0xd9,0x06            # fallback; gen=f0 d9 06
    .byte 0xf0,0xda,0xc0            # fallback; gen=f0 da c0
    .byte 0xf0,0xda,0x06            # fallback; gen=f0 da 06
    .byte 0xf0,0xdb,0xc0            # fallback; gen=f0 db c0
    .byte 0xf0,0xdb,0x06            # fallback; gen=f0 db 06
    .byte 0xf0,0xdc,0xc0            # fallback; gen=f0 dc c0
    .byte 0xf0,0xdc,0x06            # fallback; gen=f0 dc 06
    .byte 0xf0,0xdd,0xc0            # fallback; gen=f0 dd c0
    .byte 0xf0,0xdd,0x06            # fallback; gen=f0 dd 06
    .byte 0xf0,0xde,0xc0            # fallback; gen=f0 de c0
    .byte 0xf0,0xde,0x06            # fallback; gen=f0 de 06
    .byte 0xf0,0xdf,0xc0            # fallback; gen=f0 df c0
    .byte 0xf0,0xdf,0x06            # fallback; gen=f0 df 06
    .byte 0xf0,0xe0,0xc0            # fallback; gen=f0 e0 c0
    .byte 0xf0,0xe0,0x06            # fallback; gen=f0 e0 06
    .byte 0xf0,0xe1,0xc0            # fallback; gen=f0 e1 c0
    .byte 0xf0,0xe1,0x06            # fallback; gen=f0 e1 06
    .byte 0xf0,0xe2,0xc0            # fallback; gen=f0 e2 c0
    .byte 0xf0,0xe2,0x06            # fallback; gen=f0 e2 06
    .byte 0xf0,0xe3,0xc0            # fallback; gen=f0 e3 c0
    .byte 0xf0,0xe3,0x06            # fallback; gen=f0 e3 06
    .byte 0xf0,0xe4,0xc0            # fallback; gen=f0 e4 c0
    .byte 0xf0,0xe4,0x06            # fallback; gen=f0 e4 06
    .byte 0xf0,0xe5,0xc0            # fallback; gen=f0 e5 c0
    .byte 0xf0,0xe5,0x06            # fallback; gen=f0 e5 06
    .byte 0xf0,0xe6,0xc0            # fallback; gen=f0 e6 c0
    .byte 0xf0,0xe6,0x06            # fallback; gen=f0 e6 06
    .byte 0xf0,0xe7,0xc0            # fallback; gen=f0 e7 c0
    .byte 0xf0,0xe7,0x06            # fallback; gen=f0 e7 06
    .byte 0xf0,0xe8,0xc0            # fallback; gen=f0 e8 c0
    .byte 0xf0,0xe8,0x06            # fallback; gen=f0 e8 06
    .byte 0xf0,0xe9,0xc0            # fallback; gen=f0 e9 c0
    .byte 0xf0,0xe9,0x06            # fallback; gen=f0 e9 06
    .byte 0xf0,0xea,0xc0            # fallback; gen=f0 ea c0
    .byte 0xf0,0xea,0x06            # fallback; gen=f0 ea 06
    .byte 0xf0,0xeb,0xc0            # fallback; gen=f0 eb c0
    .byte 0xf0,0xeb,0x06            # fallback; gen=f0 eb 06
    .byte 0xf0,0xec,0xc0            # fallback; gen=f0 ec c0
    .byte 0xf0,0xec,0x06            # fallback; gen=f0 ec 06
    .byte 0xf0,0xed,0xc0            # fallback; gen=f0 ed c0
    .byte 0xf0,0xed,0x06            # fallback; gen=f0 ed 06
    .byte 0xf0,0xee,0xc0            # fallback; gen=f0 ee c0
    .byte 0xf0,0xee,0x06            # fallback; gen=f0 ee 06
    .byte 0xf0,0xef,0xc0            # fallback; gen=f0 ef c0
    .byte 0xf0,0xef,0x06            # fallback; gen=f0 ef 06
    .byte 0xf0,0xf0,0xc0            # fallback; gen=f0 f0 c0
    .byte 0xf0,0xf0,0x06            # fallback; gen=f0 f0 06
    .byte 0xf0,0xf1,0xc0            # fallback; gen=f0 f1 c0
    .byte 0xf0,0xf1,0x06            # fallback; gen=f0 f1 06
    .byte 0xf0,0xf2,0xc0            # fallback; gen=f0 f2 c0
    .byte 0xf0,0xf2,0x06            # fallback; gen=f0 f2 06
    .byte 0xf0,0xf3,0xc0            # fallback; gen=f0 f3 c0
    .byte 0xf0,0xf3,0x06            # fallback; gen=f0 f3 06
    .byte 0xf0,0xf4,0xc0            # fallback; gen=f0 f4 c0
    .byte 0xf0,0xf4,0x06            # fallback; gen=f0 f4 06
    .byte 0xf0,0xf5,0xc0            # fallback; gen=f0 f5 c0
    .byte 0xf0,0xf5,0x06            # fallback; gen=f0 f5 06
    .byte 0xf0,0xf6,0xc0            # fallback; gen=f0 f6 c0
    .byte 0xf0,0xf6,0x06            # fallback; gen=f0 f6 06
    .byte 0xf0,0xf7,0xc0            # fallback; gen=f0 f7 c0
    .byte 0xf0,0xf7,0x06            # fallback; gen=f0 f7 06
    .byte 0xf0,0xf8,0xc0            # fallback; gen=f0 f8 c0
    .byte 0xf0,0xf8,0x06            # fallback; gen=f0 f8 06
    .byte 0xf0,0xf9,0xc0            # fallback; gen=f0 f9 c0
    .byte 0xf0,0xf9,0x06            # fallback; gen=f0 f9 06
    .byte 0xf0,0xfa,0xc0            # fallback; gen=f0 fa c0
    .byte 0xf0,0xfa,0x06            # fallback; gen=f0 fa 06
    .byte 0xf0,0xfb,0xc0            # fallback; gen=f0 fb c0
    .byte 0xf0,0xfb,0x06            # fallback; gen=f0 fb 06
    .byte 0xf0,0xfc,0xc0            # fallback; gen=f0 fc c0
    .byte 0xf0,0xfc,0x06            # fallback; gen=f0 fc 06
    .byte 0xf0,0xfd,0xc0            # fallback; gen=f0 fd c0
    .byte 0xf0,0xfd,0x06            # fallback; gen=f0 fd 06
    .byte 0xf0,0xfe,0xc0            # fallback; gen=f0 fe c0
    lock incb -0x6f70                   # gen=f0 fe 06  dis=f0 fe 06 90 90
    .byte 0xf0,0xff,0xc0            # fallback; gen=f0 ff c0
    lock incw -0x6f70                   # gen=f0 ff 06  dis=f0 ff 06 90 90
    .byte 0xf2,0x00,0xc0            # fallback; gen=f2 00 c0
    .byte 0xf2,0x00,0x06            # fallback; gen=f2 00 06
    .byte 0xf2,0x01,0xc0            # fallback; gen=f2 01 c0
    .byte 0xf2,0x01,0x06            # fallback; gen=f2 01 06
    .byte 0xf2,0x02,0xc0            # fallback; gen=f2 02 c0
    .byte 0xf2,0x02,0x06            # fallback; gen=f2 02 06
    .byte 0xf2,0x03,0xc0            # fallback; gen=f2 03 c0
    .byte 0xf2,0x03,0x06            # fallback; gen=f2 03 06
    .byte 0xf2,0x04,0xc0            # fallback; gen=f2 04 c0
    .byte 0xf2,0x04,0x06            # fallback; gen=f2 04 06
    .byte 0xf2,0x05,0xc0            # fallback; gen=f2 05 c0
    .byte 0xf2,0x05,0x06            # fallback; gen=f2 05 06
    .byte 0xf2,0x06,0xc0            # fallback; gen=f2 06 c0
    .byte 0xf2,0x06,0x06            # fallback; gen=f2 06 06
    .byte 0xf2,0x07,0xc0            # fallback; gen=f2 07 c0
    .byte 0xf2,0x07,0x06            # fallback; gen=f2 07 06
    .byte 0xf2,0x08,0xc0            # fallback; gen=f2 08 c0
    .byte 0xf2,0x08,0x06            # fallback; gen=f2 08 06
    .byte 0xf2,0x09,0xc0            # fallback; gen=f2 09 c0
    .byte 0xf2,0x09,0x06            # fallback; gen=f2 09 06
    .byte 0xf2,0x0a,0xc0            # fallback; gen=f2 0a c0
    .byte 0xf2,0x0a,0x06            # fallback; gen=f2 0a 06
    .byte 0xf2,0x0b,0xc0            # fallback; gen=f2 0b c0
    .byte 0xf2,0x0b,0x06            # fallback; gen=f2 0b 06
    .byte 0xf2,0x0c,0xc0            # fallback; gen=f2 0c c0
    .byte 0xf2,0x0c,0x06            # fallback; gen=f2 0c 06
    .byte 0xf2,0x0d,0xc0            # fallback; gen=f2 0d c0
    .byte 0xf2,0x0d,0x06            # fallback; gen=f2 0d 06
    .byte 0xf2,0x0e,0xc0            # fallback; gen=f2 0e c0
    .byte 0xf2,0x0e,0x06            # fallback; gen=f2 0e 06
    .byte 0xf2,0x0f,0xc0            # fallback; gen=f2 0f c0
    .byte 0xf2,0x0f,0x06            # fallback; gen=f2 0f 06
    .byte 0xf2,0x10,0xc0            # fallback; gen=f2 10 c0
    .byte 0xf2,0x10,0x06            # fallback; gen=f2 10 06
    .byte 0xf2,0x11,0xc0            # fallback; gen=f2 11 c0
    .byte 0xf2,0x11,0x06            # fallback; gen=f2 11 06
    .byte 0xf2,0x12,0xc0            # fallback; gen=f2 12 c0
    .byte 0xf2,0x12,0x06            # fallback; gen=f2 12 06
    .byte 0xf2,0x13,0xc0            # fallback; gen=f2 13 c0
    .byte 0xf2,0x13,0x06            # fallback; gen=f2 13 06
    .byte 0xf2,0x14,0xc0            # fallback; gen=f2 14 c0
    .byte 0xf2,0x14,0x06            # fallback; gen=f2 14 06
    .byte 0xf2,0x15,0xc0            # fallback; gen=f2 15 c0
    .byte 0xf2,0x15,0x06            # fallback; gen=f2 15 06
    .byte 0xf2,0x16,0xc0            # fallback; gen=f2 16 c0
    .byte 0xf2,0x16,0x06            # fallback; gen=f2 16 06
    .byte 0xf2,0x17,0xc0            # fallback; gen=f2 17 c0
    .byte 0xf2,0x17,0x06            # fallback; gen=f2 17 06
    .byte 0xf2,0x18,0xc0            # fallback; gen=f2 18 c0
    .byte 0xf2,0x18,0x06            # fallback; gen=f2 18 06
    .byte 0xf2,0x19,0xc0            # fallback; gen=f2 19 c0
    .byte 0xf2,0x19,0x06            # fallback; gen=f2 19 06
    .byte 0xf2,0x1a,0xc0            # fallback; gen=f2 1a c0
    .byte 0xf2,0x1a,0x06            # fallback; gen=f2 1a 06
    .byte 0xf2,0x1b,0xc0            # fallback; gen=f2 1b c0
    .byte 0xf2,0x1b,0x06            # fallback; gen=f2 1b 06
    .byte 0xf2,0x1c,0xc0            # fallback; gen=f2 1c c0
    .byte 0xf2,0x1c,0x06            # fallback; gen=f2 1c 06
    .byte 0xf2,0x1d,0xc0            # fallback; gen=f2 1d c0
    .byte 0xf2,0x1d,0x06            # fallback; gen=f2 1d 06
    .byte 0xf2,0x1e,0xc0            # fallback; gen=f2 1e c0
    .byte 0xf2,0x1e,0x06            # fallback; gen=f2 1e 06
    .byte 0xf2,0x1f,0xc0            # fallback; gen=f2 1f c0
    .byte 0xf2,0x1f,0x06            # fallback; gen=f2 1f 06
    .byte 0xf2,0x20,0xc0            # fallback; gen=f2 20 c0
    .byte 0xf2,0x20,0x06            # fallback; gen=f2 20 06
    .byte 0xf2,0x21,0xc0            # fallback; gen=f2 21 c0
    .byte 0xf2,0x21,0x06            # fallback; gen=f2 21 06
    .byte 0xf2,0x22,0xc0            # fallback; gen=f2 22 c0
    .byte 0xf2,0x22,0x06            # fallback; gen=f2 22 06
    .byte 0xf2,0x23,0xc0            # fallback; gen=f2 23 c0
    .byte 0xf2,0x23,0x06            # fallback; gen=f2 23 06
    .byte 0xf2,0x24,0xc0            # fallback; gen=f2 24 c0
    .byte 0xf2,0x24,0x06            # fallback; gen=f2 24 06
    .byte 0xf2,0x25,0xc0            # fallback; gen=f2 25 c0
    .byte 0xf2,0x25,0x06            # fallback; gen=f2 25 06
    .byte 0xf2,0x26,0xc0            # fallback; gen=f2 26 c0
    .byte 0xf2,0x26,0x06            # fallback; gen=f2 26 06
    .byte 0xf2,0x27,0xc0            # fallback; gen=f2 27 c0
    .byte 0xf2,0x27,0x06            # fallback; gen=f2 27 06
    .byte 0xf2,0x28,0xc0            # fallback; gen=f2 28 c0
    .byte 0xf2,0x28,0x06            # fallback; gen=f2 28 06
    .byte 0xf2,0x29,0xc0            # fallback; gen=f2 29 c0
    .byte 0xf2,0x29,0x06            # fallback; gen=f2 29 06
    .byte 0xf2,0x2a,0xc0            # fallback; gen=f2 2a c0
    .byte 0xf2,0x2a,0x06            # fallback; gen=f2 2a 06
    .byte 0xf2,0x2b,0xc0            # fallback; gen=f2 2b c0
    .byte 0xf2,0x2b,0x06            # fallback; gen=f2 2b 06
    .byte 0xf2,0x2c,0xc0            # fallback; gen=f2 2c c0
    .byte 0xf2,0x2c,0x06            # fallback; gen=f2 2c 06
    .byte 0xf2,0x2d,0xc0            # fallback; gen=f2 2d c0
    .byte 0xf2,0x2d,0x06            # fallback; gen=f2 2d 06
    .byte 0xf2,0x2e,0xc0            # fallback; gen=f2 2e c0
    .byte 0xf2,0x2e,0x06            # fallback; gen=f2 2e 06
    .byte 0xf2,0x2f,0xc0            # fallback; gen=f2 2f c0
    .byte 0xf2,0x2f,0x06            # fallback; gen=f2 2f 06
    .byte 0xf2,0x30,0xc0            # fallback; gen=f2 30 c0
    .byte 0xf2,0x30,0x06            # fallback; gen=f2 30 06
    .byte 0xf2,0x31,0xc0            # fallback; gen=f2 31 c0
    .byte 0xf2,0x31,0x06            # fallback; gen=f2 31 06
    .byte 0xf2,0x32,0xc0            # fallback; gen=f2 32 c0
    .byte 0xf2,0x32,0x06            # fallback; gen=f2 32 06
    .byte 0xf2,0x33,0xc0            # fallback; gen=f2 33 c0
    .byte 0xf2,0x33,0x06            # fallback; gen=f2 33 06
    .byte 0xf2,0x34,0xc0            # fallback; gen=f2 34 c0
    .byte 0xf2,0x34,0x06            # fallback; gen=f2 34 06
    .byte 0xf2,0x35,0xc0            # fallback; gen=f2 35 c0
    .byte 0xf2,0x35,0x06            # fallback; gen=f2 35 06
    .byte 0xf2,0x36,0xc0            # fallback; gen=f2 36 c0
    .byte 0xf2,0x36,0x06            # fallback; gen=f2 36 06
    .byte 0xf2,0x37,0xc0            # fallback; gen=f2 37 c0
    .byte 0xf2,0x37,0x06            # fallback; gen=f2 37 06
    .byte 0xf2,0x38,0xc0            # fallback; gen=f2 38 c0
    .byte 0xf2,0x38,0x06            # fallback; gen=f2 38 06
    .byte 0xf2,0x39,0xc0            # fallback; gen=f2 39 c0
    .byte 0xf2,0x39,0x06            # fallback; gen=f2 39 06
    .byte 0xf2,0x3a,0xc0            # fallback; gen=f2 3a c0
    .byte 0xf2,0x3a,0x06            # fallback; gen=f2 3a 06
    .byte 0xf2,0x3b,0xc0            # fallback; gen=f2 3b c0
    .byte 0xf2,0x3b,0x06            # fallback; gen=f2 3b 06
    .byte 0xf2,0x3c,0xc0            # fallback; gen=f2 3c c0
    .byte 0xf2,0x3c,0x06            # fallback; gen=f2 3c 06
    .byte 0xf2,0x3d,0xc0            # fallback; gen=f2 3d c0
    .byte 0xf2,0x3d,0x06            # fallback; gen=f2 3d 06
    .byte 0xf2,0x3e,0xc0            # fallback; gen=f2 3e c0
    .byte 0xf2,0x3e,0x06            # fallback; gen=f2 3e 06
    .byte 0xf2,0x3f,0xc0            # fallback; gen=f2 3f c0
    .byte 0xf2,0x3f,0x06            # fallback; gen=f2 3f 06
    .byte 0xf2,0x40,0xc0            # fallback; gen=f2 40 c0
    .byte 0xf2,0x40,0x06            # fallback; gen=f2 40 06
    .byte 0xf2,0x41,0xc0            # fallback; gen=f2 41 c0
    .byte 0xf2,0x41,0x06            # fallback; gen=f2 41 06
    .byte 0xf2,0x42,0xc0            # fallback; gen=f2 42 c0
    .byte 0xf2,0x42,0x06            # fallback; gen=f2 42 06
    .byte 0xf2,0x43,0xc0            # fallback; gen=f2 43 c0
    .byte 0xf2,0x43,0x06            # fallback; gen=f2 43 06
    .byte 0xf2,0x44,0xc0            # fallback; gen=f2 44 c0
    .byte 0xf2,0x44,0x06            # fallback; gen=f2 44 06
    .byte 0xf2,0x45,0xc0            # fallback; gen=f2 45 c0
    .byte 0xf2,0x45,0x06            # fallback; gen=f2 45 06
    .byte 0xf2,0x46,0xc0            # fallback; gen=f2 46 c0
    .byte 0xf2,0x46,0x06            # fallback; gen=f2 46 06
    .byte 0xf2,0x47,0xc0            # fallback; gen=f2 47 c0
    .byte 0xf2,0x47,0x06            # fallback; gen=f2 47 06
    .byte 0xf2,0x48,0xc0            # fallback; gen=f2 48 c0
    .byte 0xf2,0x48,0x06            # fallback; gen=f2 48 06
    .byte 0xf2,0x49,0xc0            # fallback; gen=f2 49 c0
    .byte 0xf2,0x49,0x06            # fallback; gen=f2 49 06
    .byte 0xf2,0x4a,0xc0            # fallback; gen=f2 4a c0
    .byte 0xf2,0x4a,0x06            # fallback; gen=f2 4a 06
    .byte 0xf2,0x4b,0xc0            # fallback; gen=f2 4b c0
    .byte 0xf2,0x4b,0x06            # fallback; gen=f2 4b 06
    .byte 0xf2,0x4c,0xc0            # fallback; gen=f2 4c c0
    .byte 0xf2,0x4c,0x06            # fallback; gen=f2 4c 06
    .byte 0xf2,0x4d,0xc0            # fallback; gen=f2 4d c0
    .byte 0xf2,0x4d,0x06            # fallback; gen=f2 4d 06
    .byte 0xf2,0x4e,0xc0            # fallback; gen=f2 4e c0
    .byte 0xf2,0x4e,0x06            # fallback; gen=f2 4e 06
    .byte 0xf2,0x4f,0xc0            # fallback; gen=f2 4f c0
    .byte 0xf2,0x4f,0x06            # fallback; gen=f2 4f 06
    .byte 0xf2,0x50,0xc0            # fallback; gen=f2 50 c0
    .byte 0xf2,0x50,0x06            # fallback; gen=f2 50 06
    .byte 0xf2,0x51,0xc0            # fallback; gen=f2 51 c0
    .byte 0xf2,0x51,0x06            # fallback; gen=f2 51 06
    .byte 0xf2,0x52,0xc0            # fallback; gen=f2 52 c0
    .byte 0xf2,0x52,0x06            # fallback; gen=f2 52 06
    .byte 0xf2,0x53,0xc0            # fallback; gen=f2 53 c0
    .byte 0xf2,0x53,0x06            # fallback; gen=f2 53 06
    .byte 0xf2,0x54,0xc0            # fallback; gen=f2 54 c0
    .byte 0xf2,0x54,0x06            # fallback; gen=f2 54 06
    .byte 0xf2,0x55,0xc0            # fallback; gen=f2 55 c0
    .byte 0xf2,0x55,0x06            # fallback; gen=f2 55 06
    .byte 0xf2,0x56,0xc0            # fallback; gen=f2 56 c0
    .byte 0xf2,0x56,0x06            # fallback; gen=f2 56 06
    .byte 0xf2,0x57,0xc0            # fallback; gen=f2 57 c0
    .byte 0xf2,0x57,0x06            # fallback; gen=f2 57 06
    .byte 0xf2,0x58,0xc0            # fallback; gen=f2 58 c0
    .byte 0xf2,0x58,0x06            # fallback; gen=f2 58 06
    .byte 0xf2,0x59,0xc0            # fallback; gen=f2 59 c0
    .byte 0xf2,0x59,0x06            # fallback; gen=f2 59 06
    .byte 0xf2,0x5a,0xc0            # fallback; gen=f2 5a c0
    .byte 0xf2,0x5a,0x06            # fallback; gen=f2 5a 06
    .byte 0xf2,0x5b,0xc0            # fallback; gen=f2 5b c0
    .byte 0xf2,0x5b,0x06            # fallback; gen=f2 5b 06
    .byte 0xf2,0x5c,0xc0            # fallback; gen=f2 5c c0
    .byte 0xf2,0x5c,0x06            # fallback; gen=f2 5c 06
    .byte 0xf2,0x5d,0xc0            # fallback; gen=f2 5d c0
    .byte 0xf2,0x5d,0x06            # fallback; gen=f2 5d 06
    .byte 0xf2,0x5e,0xc0            # fallback; gen=f2 5e c0
    .byte 0xf2,0x5e,0x06            # fallback; gen=f2 5e 06
    .byte 0xf2,0x5f,0xc0            # fallback; gen=f2 5f c0
    .byte 0xf2,0x5f,0x06            # fallback; gen=f2 5f 06
    .byte 0xf2,0x60,0xc0            # fallback; gen=f2 60 c0
    .byte 0xf2,0x60,0x06            # fallback; gen=f2 60 06
    .byte 0xf2,0x61,0xc0            # fallback; gen=f2 61 c0
    .byte 0xf2,0x61,0x06            # fallback; gen=f2 61 06
    .byte 0xf2,0x62,0xc0            # fallback; gen=f2 62 c0
    .byte 0xf2,0x62,0x06            # fallback; gen=f2 62 06
    .byte 0xf2,0x63,0xc0            # fallback; gen=f2 63 c0
    .byte 0xf2,0x63,0x06            # fallback; gen=f2 63 06
    .byte 0xf2,0x64,0xc0            # fallback; gen=f2 64 c0
    .byte 0xf2,0x64,0x06            # fallback; gen=f2 64 06
    .byte 0xf2,0x65,0xc0            # fallback; gen=f2 65 c0
    .byte 0xf2,0x65,0x06            # fallback; gen=f2 65 06
    .byte 0xf2,0x66,0xc0            # fallback; gen=f2 66 c0
    .byte 0xf2,0x66,0x06            # fallback; gen=f2 66 06
    .byte 0xf2,0x67,0xc0            # fallback; gen=f2 67 c0
    .byte 0xf2,0x67,0x06            # fallback; gen=f2 67 06
    .byte 0xf2,0x68,0xc0            # fallback; gen=f2 68 c0
    .byte 0xf2,0x68,0x06            # fallback; gen=f2 68 06
    .byte 0xf2,0x69,0xc0            # fallback; gen=f2 69 c0
    .byte 0xf2,0x69,0x06            # fallback; gen=f2 69 06
    .byte 0xf2,0x6a,0xc0            # fallback; gen=f2 6a c0
    .byte 0xf2,0x6a,0x06            # fallback; gen=f2 6a 06
    .byte 0xf2,0x6b,0xc0            # fallback; gen=f2 6b c0
    .byte 0xf2,0x6b,0x06            # fallback; gen=f2 6b 06
    .byte 0xf2,0x6c,0xc0            # fallback; gen=f2 6c c0
    .byte 0xf2,0x6c,0x06            # fallback; gen=f2 6c 06
    .byte 0xf2,0x6d,0xc0            # fallback; gen=f2 6d c0
    .byte 0xf2,0x6d,0x06            # fallback; gen=f2 6d 06
    .byte 0xf2,0x6e,0xc0            # fallback; gen=f2 6e c0
    .byte 0xf2,0x6e,0x06            # fallback; gen=f2 6e 06
    .byte 0xf2,0x6f,0xc0            # fallback; gen=f2 6f c0
    .byte 0xf2,0x6f,0x06            # fallback; gen=f2 6f 06
    .byte 0xf2,0x70,0xc0            # fallback; gen=f2 70 c0
    .byte 0xf2,0x70,0x06            # fallback; gen=f2 70 06
    .byte 0xf2,0x71,0xc0            # fallback; gen=f2 71 c0
    .byte 0xf2,0x71,0x06            # fallback; gen=f2 71 06
    .byte 0xf2,0x72,0xc0            # fallback; gen=f2 72 c0
    .byte 0xf2,0x72,0x06            # fallback; gen=f2 72 06
    .byte 0xf2,0x73,0xc0            # fallback; gen=f2 73 c0
    .byte 0xf2,0x73,0x06            # fallback; gen=f2 73 06
    .byte 0xf2,0x74,0xc0            # fallback; gen=f2 74 c0
    .byte 0xf2,0x74,0x06            # fallback; gen=f2 74 06
    .byte 0xf2,0x75,0xc0            # fallback; gen=f2 75 c0
    .byte 0xf2,0x75,0x06            # fallback; gen=f2 75 06
    .byte 0xf2,0x76,0xc0            # fallback; gen=f2 76 c0
    .byte 0xf2,0x76,0x06            # fallback; gen=f2 76 06
    .byte 0xf2,0x77,0xc0            # fallback; gen=f2 77 c0
    .byte 0xf2,0x77,0x06            # fallback; gen=f2 77 06
    .byte 0xf2,0x78,0xc0            # fallback; gen=f2 78 c0
    .byte 0xf2,0x78,0x06            # fallback; gen=f2 78 06
    .byte 0xf2,0x79,0xc0            # fallback; gen=f2 79 c0
    .byte 0xf2,0x79,0x06            # fallback; gen=f2 79 06
    .byte 0xf2,0x7a,0xc0            # fallback; gen=f2 7a c0
    .byte 0xf2,0x7a,0x06            # fallback; gen=f2 7a 06
    .byte 0xf2,0x7b,0xc0            # fallback; gen=f2 7b c0
    .byte 0xf2,0x7b,0x06            # fallback; gen=f2 7b 06
    .byte 0xf2,0x7c,0xc0            # fallback; gen=f2 7c c0
    .byte 0xf2,0x7c,0x06            # fallback; gen=f2 7c 06
    .byte 0xf2,0x7d,0xc0            # fallback; gen=f2 7d c0
    .byte 0xf2,0x7d,0x06            # fallback; gen=f2 7d 06
    .byte 0xf2,0x7e,0xc0            # fallback; gen=f2 7e c0
    .byte 0xf2,0x7e,0x06            # fallback; gen=f2 7e 06
    .byte 0xf2,0x7f,0xc0            # fallback; gen=f2 7f c0
    .byte 0xf2,0x7f,0x06            # fallback; gen=f2 7f 06
    .byte 0xf2,0x80,0xc0            # fallback; gen=f2 80 c0
    .byte 0xf2,0x80,0x06            # fallback; gen=f2 80 06
    .byte 0xf2,0x81,0xc0            # fallback; gen=f2 81 c0
    .byte 0xf2,0x81,0x06            # fallback; gen=f2 81 06
    .byte 0xf2,0x82,0xc0            # fallback; gen=f2 82 c0
    .byte 0xf2,0x82,0x06            # fallback; gen=f2 82 06
    .byte 0xf2,0x83,0xc0            # fallback; gen=f2 83 c0
    .byte 0xf2,0x83,0x06            # fallback; gen=f2 83 06
    .byte 0xf2,0x84,0xc0            # fallback; gen=f2 84 c0
    .byte 0xf2,0x84,0x06            # fallback; gen=f2 84 06
    .byte 0xf2,0x85,0xc0            # fallback; gen=f2 85 c0
    .byte 0xf2,0x85,0x06            # fallback; gen=f2 85 06
    .byte 0xf2,0x86,0xc0            # fallback; gen=f2 86 c0
    .byte 0xf2,0x86,0x06            # fallback; gen=f2 86 06
    .byte 0xf2,0x86,0xc8            # fallback; gen=f2 86 c8
    .byte 0xf2,0x86,0xd0            # fallback; gen=f2 86 d0
    .byte 0xf2,0x86,0xd8            # fallback; gen=f2 86 d8
    .byte 0xf2,0x86,0xe0            # fallback; gen=f2 86 e0
    .byte 0xf2,0x86,0xe8            # fallback; gen=f2 86 e8
    .byte 0xf2,0x86,0xf0            # fallback; gen=f2 86 f0
    .byte 0xf2,0x86,0xf8            # fallback; gen=f2 86 f8
    .byte 0xf2,0x86,0x0e            # fallback; gen=f2 86 0e
    .byte 0xf2,0x86,0x16            # fallback; gen=f2 86 16
    .byte 0xf2,0x86,0x1e            # fallback; gen=f2 86 1e
    .byte 0xf2,0x86,0x26            # fallback; gen=f2 86 26
    .byte 0xf2,0x86,0x2e            # fallback; gen=f2 86 2e
    .byte 0xf2,0x86,0x36            # fallback; gen=f2 86 36
    .byte 0xf2,0x86,0x3e            # fallback; gen=f2 86 3e
    .byte 0xf2,0x87,0xc0            # fallback; gen=f2 87 c0
    .byte 0xf2,0x87,0x06            # fallback; gen=f2 87 06
    .byte 0xf2,0x87,0xc8            # fallback; gen=f2 87 c8
    .byte 0xf2,0x87,0xd0            # fallback; gen=f2 87 d0
    .byte 0xf2,0x87,0xd8            # fallback; gen=f2 87 d8
    .byte 0xf2,0x87,0xe0            # fallback; gen=f2 87 e0
    .byte 0xf2,0x87,0xe8            # fallback; gen=f2 87 e8
    .byte 0xf2,0x87,0xf0            # fallback; gen=f2 87 f0
    .byte 0xf2,0x87,0xf8            # fallback; gen=f2 87 f8
    .byte 0xf2,0x87,0x0e            # fallback; gen=f2 87 0e
    .byte 0xf2,0x87,0x16            # fallback; gen=f2 87 16
    .byte 0xf2,0x87,0x1e            # fallback; gen=f2 87 1e
    .byte 0xf2,0x87,0x26            # fallback; gen=f2 87 26
    .byte 0xf2,0x87,0x2e            # fallback; gen=f2 87 2e
    .byte 0xf2,0x87,0x36            # fallback; gen=f2 87 36
    .byte 0xf2,0x87,0x3e            # fallback; gen=f2 87 3e
    .byte 0xf2,0x88,0xc0            # fallback; gen=f2 88 c0
    .byte 0xf2,0x88,0x06            # fallback; gen=f2 88 06
    .byte 0xf2,0x89,0xc0            # fallback; gen=f2 89 c0
    .byte 0xf2,0x89,0x06            # fallback; gen=f2 89 06
    .byte 0xf2,0x8a,0xc0            # fallback; gen=f2 8a c0
    .byte 0xf2,0x8a,0x06            # fallback; gen=f2 8a 06
    .byte 0xf2,0x8b,0xc0            # fallback; gen=f2 8b c0
    .byte 0xf2,0x8b,0x06            # fallback; gen=f2 8b 06
    .byte 0xf2,0x8c,0xc0            # fallback; gen=f2 8c c0
    .byte 0xf2,0x8c,0x06            # fallback; gen=f2 8c 06
    .byte 0xf2,0x8d,0xc0            # fallback; gen=f2 8d c0
    .byte 0xf2,0x8d,0x06            # fallback; gen=f2 8d 06
    .byte 0xf2,0x8e,0xc0            # fallback; gen=f2 8e c0
    .byte 0xf2,0x8e,0x06            # fallback; gen=f2 8e 06
    .byte 0xf2,0x8f,0xc0            # fallback; gen=f2 8f c0
    .byte 0xf2,0x8f,0x06            # fallback; gen=f2 8f 06
    repnz nop                           # gen=f2 90 c0  dis=f2 90
    repnz nop                           # gen=f2 90 06  dis=f2 90
    .byte 0xf2,0x91,0xc0            # fallback; gen=f2 91 c0
    .byte 0xf2,0x91,0x06            # fallback; gen=f2 91 06
    .byte 0xf2,0x92,0xc0            # fallback; gen=f2 92 c0
    .byte 0xf2,0x92,0x06            # fallback; gen=f2 92 06
    .byte 0xf2,0x93,0xc0            # fallback; gen=f2 93 c0
    .byte 0xf2,0x93,0x06            # fallback; gen=f2 93 06
    .byte 0xf2,0x94,0xc0            # fallback; gen=f2 94 c0
    .byte 0xf2,0x94,0x06            # fallback; gen=f2 94 06
    .byte 0xf2,0x95,0xc0            # fallback; gen=f2 95 c0
    .byte 0xf2,0x95,0x06            # fallback; gen=f2 95 06
    .byte 0xf2,0x96,0xc0            # fallback; gen=f2 96 c0
    .byte 0xf2,0x96,0x06            # fallback; gen=f2 96 06
    .byte 0xf2,0x97,0xc0            # fallback; gen=f2 97 c0
    .byte 0xf2,0x97,0x06            # fallback; gen=f2 97 06
    .byte 0xf2,0x98,0xc0            # fallback; gen=f2 98 c0
    .byte 0xf2,0x98,0x06            # fallback; gen=f2 98 06
    .byte 0xf2,0x99,0xc0            # fallback; gen=f2 99 c0
    .byte 0xf2,0x99,0x06            # fallback; gen=f2 99 06
    .byte 0xf2,0x9a,0xc0            # fallback; gen=f2 9a c0
    .byte 0xf2,0x9a,0x06            # fallback; gen=f2 9a 06
    .byte 0xf2,0x9b,0xc0            # fallback; gen=f2 9b c0
    .byte 0xf2,0x9b,0x06            # fallback; gen=f2 9b 06
    .byte 0xf2,0x9c,0xc0            # fallback; gen=f2 9c c0
    .byte 0xf2,0x9c,0x06            # fallback; gen=f2 9c 06
    .byte 0xf2,0x9d,0xc0            # fallback; gen=f2 9d c0
    .byte 0xf2,0x9d,0x06            # fallback; gen=f2 9d 06
    .byte 0xf2,0x9e,0xc0            # fallback; gen=f2 9e c0
    .byte 0xf2,0x9e,0x06            # fallback; gen=f2 9e 06
    .byte 0xf2,0x9f,0xc0            # fallback; gen=f2 9f c0
    .byte 0xf2,0x9f,0x06            # fallback; gen=f2 9f 06
    .byte 0xf2,0xa0,0xc0            # fallback; gen=f2 a0 c0
    .byte 0xf2,0xa0,0x06            # fallback; gen=f2 a0 06
    .byte 0xf2,0xa1,0xc0            # fallback; gen=f2 a1 c0
    .byte 0xf2,0xa1,0x06            # fallback; gen=f2 a1 06
    .byte 0xf2,0xa2,0xc0            # fallback; gen=f2 a2 c0
    .byte 0xf2,0xa2,0x06            # fallback; gen=f2 a2 06
    .byte 0xf2,0xa3,0xc0            # fallback; gen=f2 a3 c0
    .byte 0xf2,0xa3,0x06            # fallback; gen=f2 a3 06
    repnz movsb %ds:(%si),%es:(%di)     # gen=f2 a4 c0  dis=f2 a4
    repnz movsb %ds:(%si),%es:(%di)     # gen=f2 a4 06  dis=f2 a4
    repnz movsw %ds:(%si),%es:(%di)     # gen=f2 a5 c0  dis=f2 a5
    repnz movsw %ds:(%si),%es:(%di)     # gen=f2 a5 06  dis=f2 a5
    repnz cmpsb %es:(%di),%ds:(%si)     # gen=f2 a6 c0  dis=f2 a6
    repnz cmpsb %es:(%di),%ds:(%si)     # gen=f2 a6 06  dis=f2 a6
    repnz cmpsw %es:(%di),%ds:(%si)     # gen=f2 a7 c0  dis=f2 a7
    repnz cmpsw %es:(%di),%ds:(%si)     # gen=f2 a7 06  dis=f2 a7
    .byte 0xf2,0xa8,0xc0            # fallback; gen=f2 a8 c0
    .byte 0xf2,0xa8,0x06            # fallback; gen=f2 a8 06
    .byte 0xf2,0xa9,0xc0            # fallback; gen=f2 a9 c0
    .byte 0xf2,0xa9,0x06            # fallback; gen=f2 a9 06
    repnz stos %al,%es:(%di)            # gen=f2 aa c0  dis=f2 aa
    repnz stos %al,%es:(%di)            # gen=f2 aa 06  dis=f2 aa
    repnz stos %ax,%es:(%di)            # gen=f2 ab c0  dis=f2 ab
    repnz stos %ax,%es:(%di)            # gen=f2 ab 06  dis=f2 ab
    repnz lods %ds:(%si),%al            # gen=f2 ac c0  dis=f2 ac
    repnz lods %ds:(%si),%al            # gen=f2 ac 06  dis=f2 ac
    repnz lods %ds:(%si),%ax            # gen=f2 ad c0  dis=f2 ad
    repnz lods %ds:(%si),%ax            # gen=f2 ad 06  dis=f2 ad
    repnz scas %es:(%di),%al            # gen=f2 ae c0  dis=f2 ae
    repnz scas %es:(%di),%al            # gen=f2 ae 06  dis=f2 ae
    repnz scas %es:(%di),%ax            # gen=f2 af c0  dis=f2 af
    repnz scas %es:(%di),%ax            # gen=f2 af 06  dis=f2 af
    .byte 0xf2,0xb0,0xc0            # fallback; gen=f2 b0 c0
    .byte 0xf2,0xb0,0x06            # fallback; gen=f2 b0 06
    .byte 0xf2,0xb1,0xc0            # fallback; gen=f2 b1 c0
    .byte 0xf2,0xb1,0x06            # fallback; gen=f2 b1 06
    .byte 0xf2,0xb2,0xc0            # fallback; gen=f2 b2 c0
    .byte 0xf2,0xb2,0x06            # fallback; gen=f2 b2 06
    .byte 0xf2,0xb3,0xc0            # fallback; gen=f2 b3 c0
    .byte 0xf2,0xb3,0x06            # fallback; gen=f2 b3 06
    .byte 0xf2,0xb4,0xc0            # fallback; gen=f2 b4 c0
    .byte 0xf2,0xb4,0x06            # fallback; gen=f2 b4 06
    .byte 0xf2,0xb5,0xc0            # fallback; gen=f2 b5 c0
    .byte 0xf2,0xb5,0x06            # fallback; gen=f2 b5 06
    .byte 0xf2,0xb6,0xc0            # fallback; gen=f2 b6 c0
    .byte 0xf2,0xb6,0x06            # fallback; gen=f2 b6 06
    .byte 0xf2,0xb7,0xc0            # fallback; gen=f2 b7 c0
    .byte 0xf2,0xb7,0x06            # fallback; gen=f2 b7 06
    .byte 0xf2,0xb8,0xc0            # fallback; gen=f2 b8 c0
    .byte 0xf2,0xb8,0x06            # fallback; gen=f2 b8 06
    .byte 0xf2,0xb9,0xc0            # fallback; gen=f2 b9 c0
    .byte 0xf2,0xb9,0x06            # fallback; gen=f2 b9 06
    .byte 0xf2,0xba,0xc0            # fallback; gen=f2 ba c0
    .byte 0xf2,0xba,0x06            # fallback; gen=f2 ba 06
    .byte 0xf2,0xbb,0xc0            # fallback; gen=f2 bb c0
    .byte 0xf2,0xbb,0x06            # fallback; gen=f2 bb 06
    .byte 0xf2,0xbc,0xc0            # fallback; gen=f2 bc c0
    .byte 0xf2,0xbc,0x06            # fallback; gen=f2 bc 06
    .byte 0xf2,0xbd,0xc0            # fallback; gen=f2 bd c0
    .byte 0xf2,0xbd,0x06            # fallback; gen=f2 bd 06
    .byte 0xf2,0xbe,0xc0            # fallback; gen=f2 be c0
    .byte 0xf2,0xbe,0x06            # fallback; gen=f2 be 06
    .byte 0xf2,0xbf,0xc0            # fallback; gen=f2 bf c0
    .byte 0xf2,0xbf,0x06            # fallback; gen=f2 bf 06
    .byte 0xf2,0xc0,0xc0            # fallback; gen=f2 c0 c0
    .byte 0xf2,0xc0,0x06            # fallback; gen=f2 c0 06
    .byte 0xf2,0xc1,0xc0            # fallback; gen=f2 c1 c0
    .byte 0xf2,0xc1,0x06            # fallback; gen=f2 c1 06
    .byte 0xf2,0xc2,0xc0            # fallback; gen=f2 c2 c0
    .byte 0xf2,0xc2,0x06            # fallback; gen=f2 c2 06
    .byte 0xf2,0xc3,0xc0            # fallback; gen=f2 c3 c0
    .byte 0xf2,0xc3,0x06            # fallback; gen=f2 c3 06
    .byte 0xf2,0xc4,0xc0            # fallback; gen=f2 c4 c0
    .byte 0xf2,0xc4,0x06            # fallback; gen=f2 c4 06
    .byte 0xf2,0xc5,0xc0            # fallback; gen=f2 c5 c0
    .byte 0xf2,0xc5,0x06            # fallback; gen=f2 c5 06
    .byte 0xf2,0xc6,0xc0            # fallback; gen=f2 c6 c0
    .byte 0xf2,0xc6,0x06            # fallback; gen=f2 c6 06
    .byte 0xf2,0xc7,0xc0            # fallback; gen=f2 c7 c0
    .byte 0xf2,0xc7,0x06            # fallback; gen=f2 c7 06
    .byte 0xf2,0xc8,0xc0            # fallback; gen=f2 c8 c0
    .byte 0xf2,0xc8,0x06            # fallback; gen=f2 c8 06
    .byte 0xf2,0xc9,0xc0            # fallback; gen=f2 c9 c0
    .byte 0xf2,0xc9,0x06            # fallback; gen=f2 c9 06
    .byte 0xf2,0xca,0xc0            # fallback; gen=f2 ca c0
    .byte 0xf2,0xca,0x06            # fallback; gen=f2 ca 06
    .byte 0xf2,0xcb,0xc0            # fallback; gen=f2 cb c0
    .byte 0xf2,0xcb,0x06            # fallback; gen=f2 cb 06
    .byte 0xf2,0xcc,0xc0            # fallback; gen=f2 cc c0
    .byte 0xf2,0xcc,0x06            # fallback; gen=f2 cc 06
    .byte 0xf2,0xcd,0xc0            # fallback; gen=f2 cd c0
    .byte 0xf2,0xcd,0x06            # fallback; gen=f2 cd 06
    .byte 0xf2,0xce,0xc0            # fallback; gen=f2 ce c0
    .byte 0xf2,0xce,0x06            # fallback; gen=f2 ce 06
    .byte 0xf2,0xcf,0xc0            # fallback; gen=f2 cf c0
    .byte 0xf2,0xcf,0x06            # fallback; gen=f2 cf 06
    .byte 0xf2,0xd0,0xc0            # fallback; gen=f2 d0 c0
    .byte 0xf2,0xd0,0x06            # fallback; gen=f2 d0 06
    .byte 0xf2,0xd1,0xc0            # fallback; gen=f2 d1 c0
    .byte 0xf2,0xd1,0x06            # fallback; gen=f2 d1 06
    .byte 0xf2,0xd2,0xc0            # fallback; gen=f2 d2 c0
    .byte 0xf2,0xd2,0x06            # fallback; gen=f2 d2 06
    .byte 0xf2,0xd3,0xc0            # fallback; gen=f2 d3 c0
    .byte 0xf2,0xd3,0x06            # fallback; gen=f2 d3 06
    .byte 0xf2,0xd4,0xc0            # fallback; gen=f2 d4 c0
    .byte 0xf2,0xd4,0x06            # fallback; gen=f2 d4 06
    .byte 0xf2,0xd5,0xc0            # fallback; gen=f2 d5 c0
    .byte 0xf2,0xd5,0x06            # fallback; gen=f2 d5 06
    .byte 0xf2,0xd6,0xc0            # fallback; gen=f2 d6 c0
    .byte 0xf2,0xd6,0x06            # fallback; gen=f2 d6 06
    .byte 0xf2,0xd7,0xc0            # fallback; gen=f2 d7 c0
    .byte 0xf2,0xd7,0x06            # fallback; gen=f2 d7 06
    .byte 0xf2,0xd8,0xc0            # fallback; gen=f2 d8 c0
    .byte 0xf2,0xd8,0x06            # fallback; gen=f2 d8 06
    .byte 0xf2,0xd9,0xc0            # fallback; gen=f2 d9 c0
    .byte 0xf2,0xd9,0x06            # fallback; gen=f2 d9 06
    .byte 0xf2,0xda,0xc0            # fallback; gen=f2 da c0
    .byte 0xf2,0xda,0x06            # fallback; gen=f2 da 06
    .byte 0xf2,0xdb,0xc0            # fallback; gen=f2 db c0
    .byte 0xf2,0xdb,0x06            # fallback; gen=f2 db 06
    .byte 0xf2,0xdc,0xc0            # fallback; gen=f2 dc c0
    .byte 0xf2,0xdc,0x06            # fallback; gen=f2 dc 06
    .byte 0xf2,0xdd,0xc0            # fallback; gen=f2 dd c0
    .byte 0xf2,0xdd,0x06            # fallback; gen=f2 dd 06
    .byte 0xf2,0xde,0xc0            # fallback; gen=f2 de c0
    .byte 0xf2,0xde,0x06            # fallback; gen=f2 de 06
    .byte 0xf2,0xdf,0xc0            # fallback; gen=f2 df c0
    .byte 0xf2,0xdf,0x06            # fallback; gen=f2 df 06
    .byte 0xf2,0xe0,0xc0            # fallback; gen=f2 e0 c0
    .byte 0xf2,0xe0,0x06            # fallback; gen=f2 e0 06
    .byte 0xf2,0xe1,0xc0            # fallback; gen=f2 e1 c0
    .byte 0xf2,0xe1,0x06            # fallback; gen=f2 e1 06
    .byte 0xf2,0xe2,0xc0            # fallback; gen=f2 e2 c0
    .byte 0xf2,0xe2,0x06            # fallback; gen=f2 e2 06
    .byte 0xf2,0xe3,0xc0            # fallback; gen=f2 e3 c0
    .byte 0xf2,0xe3,0x06            # fallback; gen=f2 e3 06
    .byte 0xf2,0xe4,0xc0            # fallback; gen=f2 e4 c0
    .byte 0xf2,0xe4,0x06            # fallback; gen=f2 e4 06
    .byte 0xf2,0xe5,0xc0            # fallback; gen=f2 e5 c0
    .byte 0xf2,0xe5,0x06            # fallback; gen=f2 e5 06
    .byte 0xf2,0xe6,0xc0            # fallback; gen=f2 e6 c0
    .byte 0xf2,0xe6,0x06            # fallback; gen=f2 e6 06
    .byte 0xf2,0xe7,0xc0            # fallback; gen=f2 e7 c0
    .byte 0xf2,0xe7,0x06            # fallback; gen=f2 e7 06
    .byte 0xf2,0xe8,0xc0            # fallback; gen=f2 e8 c0
    .byte 0xf2,0xe8,0x06            # fallback; gen=f2 e8 06
    .byte 0xf2,0xe9,0xc0            # fallback; gen=f2 e9 c0
    .byte 0xf2,0xe9,0x06            # fallback; gen=f2 e9 06
    .byte 0xf2,0xea,0xc0            # fallback; gen=f2 ea c0
    .byte 0xf2,0xea,0x06            # fallback; gen=f2 ea 06
    .byte 0xf2,0xeb,0xc0            # fallback; gen=f2 eb c0
    .byte 0xf2,0xeb,0x06            # fallback; gen=f2 eb 06
    .byte 0xf2,0xec,0xc0            # fallback; gen=f2 ec c0
    .byte 0xf2,0xec,0x06            # fallback; gen=f2 ec 06
    .byte 0xf2,0xed,0xc0            # fallback; gen=f2 ed c0
    .byte 0xf2,0xed,0x06            # fallback; gen=f2 ed 06
    .byte 0xf2,0xee,0xc0            # fallback; gen=f2 ee c0
    .byte 0xf2,0xee,0x06            # fallback; gen=f2 ee 06
    .byte 0xf2,0xef,0xc0            # fallback; gen=f2 ef c0
    .byte 0xf2,0xef,0x06            # fallback; gen=f2 ef 06
    .byte 0xf2,0xf0,0xc0            # fallback; gen=f2 f0 c0
    .byte 0xf2,0xf0,0x06            # fallback; gen=f2 f0 06
    .byte 0xf2,0xf1,0xc0            # fallback; gen=f2 f1 c0
    .byte 0xf2,0xf1,0x06            # fallback; gen=f2 f1 06
    .byte 0xf2,0xf2,0xc0            # fallback; gen=f2 f2 c0
    .byte 0xf2,0xf2,0x06            # fallback; gen=f2 f2 06
    .byte 0xf2,0xf3,0xc0            # fallback; gen=f2 f3 c0
    .byte 0xf2,0xf3,0x06            # fallback; gen=f2 f3 06
    .byte 0xf2,0xf4,0xc0            # fallback; gen=f2 f4 c0
    .byte 0xf2,0xf4,0x06            # fallback; gen=f2 f4 06
    .byte 0xf2,0xf5,0xc0            # fallback; gen=f2 f5 c0
    .byte 0xf2,0xf5,0x06            # fallback; gen=f2 f5 06
    .byte 0xf2,0xf6,0xc0            # fallback; gen=f2 f6 c0
    .byte 0xf2,0xf6,0x06            # fallback; gen=f2 f6 06
    .byte 0xf2,0xf7,0xc0            # fallback; gen=f2 f7 c0
    .byte 0xf2,0xf7,0x06            # fallback; gen=f2 f7 06
    .byte 0xf2,0xf8,0xc0            # fallback; gen=f2 f8 c0
    .byte 0xf2,0xf8,0x06            # fallback; gen=f2 f8 06
    .byte 0xf2,0xf9,0xc0            # fallback; gen=f2 f9 c0
    .byte 0xf2,0xf9,0x06            # fallback; gen=f2 f9 06
    .byte 0xf2,0xfa,0xc0            # fallback; gen=f2 fa c0
    .byte 0xf2,0xfa,0x06            # fallback; gen=f2 fa 06
    .byte 0xf2,0xfb,0xc0            # fallback; gen=f2 fb c0
    .byte 0xf2,0xfb,0x06            # fallback; gen=f2 fb 06
    .byte 0xf2,0xfc,0xc0            # fallback; gen=f2 fc c0
    .byte 0xf2,0xfc,0x06            # fallback; gen=f2 fc 06
    .byte 0xf2,0xfd,0xc0            # fallback; gen=f2 fd c0
    .byte 0xf2,0xfd,0x06            # fallback; gen=f2 fd 06
    .byte 0xf2,0xfe,0xc0            # fallback; gen=f2 fe c0
    .byte 0xf2,0xfe,0x06            # fallback; gen=f2 fe 06
    .byte 0xf2,0xff,0xc0            # fallback; gen=f2 ff c0
    .byte 0xf2,0xff,0x06            # fallback; gen=f2 ff 06
    .byte 0xf3,0x00,0xc0            # fallback; gen=f3 00 c0
    .byte 0xf3,0x00,0x06            # fallback; gen=f3 00 06
    .byte 0xf3,0x01,0xc0            # fallback; gen=f3 01 c0
    .byte 0xf3,0x01,0x06            # fallback; gen=f3 01 06
    .byte 0xf3,0x02,0xc0            # fallback; gen=f3 02 c0
    .byte 0xf3,0x02,0x06            # fallback; gen=f3 02 06
    .byte 0xf3,0x03,0xc0            # fallback; gen=f3 03 c0
    .byte 0xf3,0x03,0x06            # fallback; gen=f3 03 06
    .byte 0xf3,0x04,0xc0            # fallback; gen=f3 04 c0
    .byte 0xf3,0x04,0x06            # fallback; gen=f3 04 06
    .byte 0xf3,0x05,0xc0            # fallback; gen=f3 05 c0
    .byte 0xf3,0x05,0x06            # fallback; gen=f3 05 06
    .byte 0xf3,0x06,0xc0            # fallback; gen=f3 06 c0
    .byte 0xf3,0x06,0x06            # fallback; gen=f3 06 06
    .byte 0xf3,0x07,0xc0            # fallback; gen=f3 07 c0
    .byte 0xf3,0x07,0x06            # fallback; gen=f3 07 06
    .byte 0xf3,0x08,0xc0            # fallback; gen=f3 08 c0
    .byte 0xf3,0x08,0x06            # fallback; gen=f3 08 06
    .byte 0xf3,0x09,0xc0            # fallback; gen=f3 09 c0
    .byte 0xf3,0x09,0x06            # fallback; gen=f3 09 06
    .byte 0xf3,0x0a,0xc0            # fallback; gen=f3 0a c0
    .byte 0xf3,0x0a,0x06            # fallback; gen=f3 0a 06
    .byte 0xf3,0x0b,0xc0            # fallback; gen=f3 0b c0
    .byte 0xf3,0x0b,0x06            # fallback; gen=f3 0b 06
    .byte 0xf3,0x0c,0xc0            # fallback; gen=f3 0c c0
    .byte 0xf3,0x0c,0x06            # fallback; gen=f3 0c 06
    .byte 0xf3,0x0d,0xc0            # fallback; gen=f3 0d c0
    .byte 0xf3,0x0d,0x06            # fallback; gen=f3 0d 06
    .byte 0xf3,0x0e,0xc0            # fallback; gen=f3 0e c0
    .byte 0xf3,0x0e,0x06            # fallback; gen=f3 0e 06
    .byte 0xf3,0x0f,0xc0            # fallback; gen=f3 0f c0
    .byte 0xf3,0x0f,0x06            # fallback; gen=f3 0f 06
    .byte 0xf3,0x10,0xc0            # fallback; gen=f3 10 c0
    .byte 0xf3,0x10,0x06            # fallback; gen=f3 10 06
    .byte 0xf3,0x11,0xc0            # fallback; gen=f3 11 c0
    .byte 0xf3,0x11,0x06            # fallback; gen=f3 11 06
    .byte 0xf3,0x12,0xc0            # fallback; gen=f3 12 c0
    .byte 0xf3,0x12,0x06            # fallback; gen=f3 12 06
    .byte 0xf3,0x13,0xc0            # fallback; gen=f3 13 c0
    .byte 0xf3,0x13,0x06            # fallback; gen=f3 13 06
    .byte 0xf3,0x14,0xc0            # fallback; gen=f3 14 c0
    .byte 0xf3,0x14,0x06            # fallback; gen=f3 14 06
    .byte 0xf3,0x15,0xc0            # fallback; gen=f3 15 c0
    .byte 0xf3,0x15,0x06            # fallback; gen=f3 15 06
    .byte 0xf3,0x16,0xc0            # fallback; gen=f3 16 c0
    .byte 0xf3,0x16,0x06            # fallback; gen=f3 16 06
    .byte 0xf3,0x17,0xc0            # fallback; gen=f3 17 c0
    .byte 0xf3,0x17,0x06            # fallback; gen=f3 17 06
    .byte 0xf3,0x18,0xc0            # fallback; gen=f3 18 c0
    .byte 0xf3,0x18,0x06            # fallback; gen=f3 18 06
    .byte 0xf3,0x19,0xc0            # fallback; gen=f3 19 c0
    .byte 0xf3,0x19,0x06            # fallback; gen=f3 19 06
    .byte 0xf3,0x1a,0xc0            # fallback; gen=f3 1a c0
    .byte 0xf3,0x1a,0x06            # fallback; gen=f3 1a 06
    .byte 0xf3,0x1b,0xc0            # fallback; gen=f3 1b c0
    .byte 0xf3,0x1b,0x06            # fallback; gen=f3 1b 06
    .byte 0xf3,0x1c,0xc0            # fallback; gen=f3 1c c0
    .byte 0xf3,0x1c,0x06            # fallback; gen=f3 1c 06
    .byte 0xf3,0x1d,0xc0            # fallback; gen=f3 1d c0
    .byte 0xf3,0x1d,0x06            # fallback; gen=f3 1d 06
    .byte 0xf3,0x1e,0xc0            # fallback; gen=f3 1e c0
    .byte 0xf3,0x1e,0x06            # fallback; gen=f3 1e 06
    .byte 0xf3,0x1f,0xc0            # fallback; gen=f3 1f c0
    .byte 0xf3,0x1f,0x06            # fallback; gen=f3 1f 06
    .byte 0xf3,0x20,0xc0            # fallback; gen=f3 20 c0
    .byte 0xf3,0x20,0x06            # fallback; gen=f3 20 06
    .byte 0xf3,0x21,0xc0            # fallback; gen=f3 21 c0
    .byte 0xf3,0x21,0x06            # fallback; gen=f3 21 06
    .byte 0xf3,0x22,0xc0            # fallback; gen=f3 22 c0
    .byte 0xf3,0x22,0x06            # fallback; gen=f3 22 06
    .byte 0xf3,0x23,0xc0            # fallback; gen=f3 23 c0
    .byte 0xf3,0x23,0x06            # fallback; gen=f3 23 06
    .byte 0xf3,0x24,0xc0            # fallback; gen=f3 24 c0
    .byte 0xf3,0x24,0x06            # fallback; gen=f3 24 06
    .byte 0xf3,0x25,0xc0            # fallback; gen=f3 25 c0
    .byte 0xf3,0x25,0x06            # fallback; gen=f3 25 06
    .byte 0xf3,0x26,0xc0            # fallback; gen=f3 26 c0
    .byte 0xf3,0x26,0x06            # fallback; gen=f3 26 06
    .byte 0xf3,0x26,0xc8            # fallback; gen=f3 26 c8
    .byte 0xf3,0x26,0xd0            # fallback; gen=f3 26 d0
    .byte 0xf3,0x26,0xd8            # fallback; gen=f3 26 d8
    .byte 0xf3,0x26,0xe0            # fallback; gen=f3 26 e0
    .byte 0xf3,0x26,0xe8            # fallback; gen=f3 26 e8
    .byte 0xf3,0x26,0xf0            # fallback; gen=f3 26 f0
    .byte 0xf3,0x26,0xf8            # fallback; gen=f3 26 f8
    .byte 0xf3,0x26,0x0e            # fallback; gen=f3 26 0e
    .byte 0xf3,0x26,0x16            # fallback; gen=f3 26 16
    .byte 0xf3,0x26,0x1e            # fallback; gen=f3 26 1e
    .byte 0xf3,0x26,0x26            # fallback; gen=f3 26 26
    .byte 0xf3,0x26,0x2e            # fallback; gen=f3 26 2e
    .byte 0xf3,0x26,0x36            # fallback; gen=f3 26 36
    .byte 0xf3,0x26,0x3e            # fallback; gen=f3 26 3e
    .byte 0xf3,0x27,0xc0            # fallback; gen=f3 27 c0
    .byte 0xf3,0x27,0x06            # fallback; gen=f3 27 06
    .byte 0xf3,0x28,0xc0            # fallback; gen=f3 28 c0
    .byte 0xf3,0x28,0x06            # fallback; gen=f3 28 06
    .byte 0xf3,0x29,0xc0            # fallback; gen=f3 29 c0
    .byte 0xf3,0x29,0x06            # fallback; gen=f3 29 06
    .byte 0xf3,0x2a,0xc0            # fallback; gen=f3 2a c0
    .byte 0xf3,0x2a,0x06            # fallback; gen=f3 2a 06
    .byte 0xf3,0x2b,0xc0            # fallback; gen=f3 2b c0
    .byte 0xf3,0x2b,0x06            # fallback; gen=f3 2b 06
    .byte 0xf3,0x2c,0xc0            # fallback; gen=f3 2c c0
    .byte 0xf3,0x2c,0x06            # fallback; gen=f3 2c 06
    .byte 0xf3,0x2d,0xc0            # fallback; gen=f3 2d c0
    .byte 0xf3,0x2d,0x06            # fallback; gen=f3 2d 06
    .byte 0xf3,0x2e,0xc0            # fallback; gen=f3 2e c0
    .byte 0xf3,0x2e,0x06            # fallback; gen=f3 2e 06
    .byte 0xf3,0x2e,0xc8            # fallback; gen=f3 2e c8
    .byte 0xf3,0x2e,0xd0            # fallback; gen=f3 2e d0
    .byte 0xf3,0x2e,0xd8            # fallback; gen=f3 2e d8
    .byte 0xf3,0x2e,0xe0            # fallback; gen=f3 2e e0
    .byte 0xf3,0x2e,0xe8            # fallback; gen=f3 2e e8
    .byte 0xf3,0x2e,0xf0            # fallback; gen=f3 2e f0
    .byte 0xf3,0x2e,0xf8            # fallback; gen=f3 2e f8
    .byte 0xf3,0x2e,0x0e            # fallback; gen=f3 2e 0e
    .byte 0xf3,0x2e,0x16            # fallback; gen=f3 2e 16
    .byte 0xf3,0x2e,0x1e            # fallback; gen=f3 2e 1e
    .byte 0xf3,0x2e,0x26            # fallback; gen=f3 2e 26
    .byte 0xf3,0x2e,0x2e            # fallback; gen=f3 2e 2e
    .byte 0xf3,0x2e,0x36            # fallback; gen=f3 2e 36
    .byte 0xf3,0x2e,0x3e            # fallback; gen=f3 2e 3e
    .byte 0xf3,0x2f,0xc0            # fallback; gen=f3 2f c0
    .byte 0xf3,0x2f,0x06            # fallback; gen=f3 2f 06
    .byte 0xf3,0x30,0xc0            # fallback; gen=f3 30 c0
    .byte 0xf3,0x30,0x06            # fallback; gen=f3 30 06
    .byte 0xf3,0x31,0xc0            # fallback; gen=f3 31 c0
    .byte 0xf3,0x31,0x06            # fallback; gen=f3 31 06
    .byte 0xf3,0x32,0xc0            # fallback; gen=f3 32 c0
    .byte 0xf3,0x32,0x06            # fallback; gen=f3 32 06
    .byte 0xf3,0x33,0xc0            # fallback; gen=f3 33 c0
    .byte 0xf3,0x33,0x06            # fallback; gen=f3 33 06
    .byte 0xf3,0x34,0xc0            # fallback; gen=f3 34 c0
    .byte 0xf3,0x34,0x06            # fallback; gen=f3 34 06
    .byte 0xf3,0x35,0xc0            # fallback; gen=f3 35 c0
    .byte 0xf3,0x35,0x06            # fallback; gen=f3 35 06
    .byte 0xf3,0x36,0xc0            # fallback; gen=f3 36 c0
    .byte 0xf3,0x36,0x06            # fallback; gen=f3 36 06
    .byte 0xf3,0x36,0xc8            # fallback; gen=f3 36 c8
    .byte 0xf3,0x36,0xd0            # fallback; gen=f3 36 d0
    .byte 0xf3,0x36,0xd8            # fallback; gen=f3 36 d8
    .byte 0xf3,0x36,0xe0            # fallback; gen=f3 36 e0
    .byte 0xf3,0x36,0xe8            # fallback; gen=f3 36 e8
    .byte 0xf3,0x36,0xf0            # fallback; gen=f3 36 f0
    .byte 0xf3,0x36,0xf8            # fallback; gen=f3 36 f8
    .byte 0xf3,0x36,0x0e            # fallback; gen=f3 36 0e
    .byte 0xf3,0x36,0x16            # fallback; gen=f3 36 16
    .byte 0xf3,0x36,0x1e            # fallback; gen=f3 36 1e
    .byte 0xf3,0x36,0x26            # fallback; gen=f3 36 26
    .byte 0xf3,0x36,0x2e            # fallback; gen=f3 36 2e
    .byte 0xf3,0x36,0x36            # fallback; gen=f3 36 36
    .byte 0xf3,0x36,0x3e            # fallback; gen=f3 36 3e
    .byte 0xf3,0x37,0xc0            # fallback; gen=f3 37 c0
    .byte 0xf3,0x37,0x06            # fallback; gen=f3 37 06
    .byte 0xf3,0x38,0xc0            # fallback; gen=f3 38 c0
    .byte 0xf3,0x38,0x06            # fallback; gen=f3 38 06
    .byte 0xf3,0x39,0xc0            # fallback; gen=f3 39 c0
    .byte 0xf3,0x39,0x06            # fallback; gen=f3 39 06
    .byte 0xf3,0x3a,0xc0            # fallback; gen=f3 3a c0
    .byte 0xf3,0x3a,0x06            # fallback; gen=f3 3a 06
    .byte 0xf3,0x3b,0xc0            # fallback; gen=f3 3b c0
    .byte 0xf3,0x3b,0x06            # fallback; gen=f3 3b 06
    .byte 0xf3,0x3c,0xc0            # fallback; gen=f3 3c c0
    .byte 0xf3,0x3c,0x06            # fallback; gen=f3 3c 06
    .byte 0xf3,0x3d,0xc0            # fallback; gen=f3 3d c0
    .byte 0xf3,0x3d,0x06            # fallback; gen=f3 3d 06
    .byte 0xf3,0x3e,0xc0            # fallback; gen=f3 3e c0
    .byte 0xf3,0x3e,0x06            # fallback; gen=f3 3e 06
    .byte 0xf3,0x3e,0xc8            # fallback; gen=f3 3e c8
    .byte 0xf3,0x3e,0xd0            # fallback; gen=f3 3e d0
    .byte 0xf3,0x3e,0xd8            # fallback; gen=f3 3e d8
    .byte 0xf3,0x3e,0xe0            # fallback; gen=f3 3e e0
    .byte 0xf3,0x3e,0xe8            # fallback; gen=f3 3e e8
    .byte 0xf3,0x3e,0xf0            # fallback; gen=f3 3e f0
    .byte 0xf3,0x3e,0xf8            # fallback; gen=f3 3e f8
    .byte 0xf3,0x3e,0x0e            # fallback; gen=f3 3e 0e
    .byte 0xf3,0x3e,0x16            # fallback; gen=f3 3e 16
    .byte 0xf3,0x3e,0x1e            # fallback; gen=f3 3e 1e
    .byte 0xf3,0x3e,0x26            # fallback; gen=f3 3e 26
    .byte 0xf3,0x3e,0x2e            # fallback; gen=f3 3e 2e
    .byte 0xf3,0x3e,0x36            # fallback; gen=f3 3e 36
    .byte 0xf3,0x3e,0x3e            # fallback; gen=f3 3e 3e
    .byte 0xf3,0x3f,0xc0            # fallback; gen=f3 3f c0
    .byte 0xf3,0x3f,0x06            # fallback; gen=f3 3f 06
    .byte 0xf3,0x40,0xc0            # fallback; gen=f3 40 c0
    .byte 0xf3,0x40,0x06            # fallback; gen=f3 40 06
    .byte 0xf3,0x41,0xc0            # fallback; gen=f3 41 c0
    .byte 0xf3,0x41,0x06            # fallback; gen=f3 41 06
    .byte 0xf3,0x42,0xc0            # fallback; gen=f3 42 c0
    .byte 0xf3,0x42,0x06            # fallback; gen=f3 42 06
    .byte 0xf3,0x43,0xc0            # fallback; gen=f3 43 c0
    .byte 0xf3,0x43,0x06            # fallback; gen=f3 43 06
    .byte 0xf3,0x44,0xc0            # fallback; gen=f3 44 c0
    .byte 0xf3,0x44,0x06            # fallback; gen=f3 44 06
    .byte 0xf3,0x45,0xc0            # fallback; gen=f3 45 c0
    .byte 0xf3,0x45,0x06            # fallback; gen=f3 45 06
    .byte 0xf3,0x46,0xc0            # fallback; gen=f3 46 c0
    .byte 0xf3,0x46,0x06            # fallback; gen=f3 46 06
    .byte 0xf3,0x47,0xc0            # fallback; gen=f3 47 c0
    .byte 0xf3,0x47,0x06            # fallback; gen=f3 47 06
    .byte 0xf3,0x48,0xc0            # fallback; gen=f3 48 c0
    .byte 0xf3,0x48,0x06            # fallback; gen=f3 48 06
    .byte 0xf3,0x49,0xc0            # fallback; gen=f3 49 c0
    .byte 0xf3,0x49,0x06            # fallback; gen=f3 49 06
    .byte 0xf3,0x4a,0xc0            # fallback; gen=f3 4a c0
    .byte 0xf3,0x4a,0x06            # fallback; gen=f3 4a 06
    .byte 0xf3,0x4b,0xc0            # fallback; gen=f3 4b c0
    .byte 0xf3,0x4b,0x06            # fallback; gen=f3 4b 06
    .byte 0xf3,0x4c,0xc0            # fallback; gen=f3 4c c0
    .byte 0xf3,0x4c,0x06            # fallback; gen=f3 4c 06
    .byte 0xf3,0x4d,0xc0            # fallback; gen=f3 4d c0
    .byte 0xf3,0x4d,0x06            # fallback; gen=f3 4d 06
    .byte 0xf3,0x4e,0xc0            # fallback; gen=f3 4e c0
    .byte 0xf3,0x4e,0x06            # fallback; gen=f3 4e 06
    .byte 0xf3,0x4f,0xc0            # fallback; gen=f3 4f c0
    .byte 0xf3,0x4f,0x06            # fallback; gen=f3 4f 06
    .byte 0xf3,0x50,0xc0            # fallback; gen=f3 50 c0
    .byte 0xf3,0x50,0x06            # fallback; gen=f3 50 06
    .byte 0xf3,0x51,0xc0            # fallback; gen=f3 51 c0
    .byte 0xf3,0x51,0x06            # fallback; gen=f3 51 06
    .byte 0xf3,0x52,0xc0            # fallback; gen=f3 52 c0
    .byte 0xf3,0x52,0x06            # fallback; gen=f3 52 06
    .byte 0xf3,0x53,0xc0            # fallback; gen=f3 53 c0
    .byte 0xf3,0x53,0x06            # fallback; gen=f3 53 06
    .byte 0xf3,0x54,0xc0            # fallback; gen=f3 54 c0
    .byte 0xf3,0x54,0x06            # fallback; gen=f3 54 06
    .byte 0xf3,0x55,0xc0            # fallback; gen=f3 55 c0
    .byte 0xf3,0x55,0x06            # fallback; gen=f3 55 06
    .byte 0xf3,0x56,0xc0            # fallback; gen=f3 56 c0
    .byte 0xf3,0x56,0x06            # fallback; gen=f3 56 06
    .byte 0xf3,0x57,0xc0            # fallback; gen=f3 57 c0
    .byte 0xf3,0x57,0x06            # fallback; gen=f3 57 06
    .byte 0xf3,0x58,0xc0            # fallback; gen=f3 58 c0
    .byte 0xf3,0x58,0x06            # fallback; gen=f3 58 06
    .byte 0xf3,0x59,0xc0            # fallback; gen=f3 59 c0
    .byte 0xf3,0x59,0x06            # fallback; gen=f3 59 06
    .byte 0xf3,0x5a,0xc0            # fallback; gen=f3 5a c0
    .byte 0xf3,0x5a,0x06            # fallback; gen=f3 5a 06
    .byte 0xf3,0x5b,0xc0            # fallback; gen=f3 5b c0
    .byte 0xf3,0x5b,0x06            # fallback; gen=f3 5b 06
    .byte 0xf3,0x5c,0xc0            # fallback; gen=f3 5c c0
    .byte 0xf3,0x5c,0x06            # fallback; gen=f3 5c 06
    .byte 0xf3,0x5d,0xc0            # fallback; gen=f3 5d c0
    .byte 0xf3,0x5d,0x06            # fallback; gen=f3 5d 06
    .byte 0xf3,0x5e,0xc0            # fallback; gen=f3 5e c0
    .byte 0xf3,0x5e,0x06            # fallback; gen=f3 5e 06
    .byte 0xf3,0x5f,0xc0            # fallback; gen=f3 5f c0
    .byte 0xf3,0x5f,0x06            # fallback; gen=f3 5f 06
    .byte 0xf3,0x60,0xc0            # fallback; gen=f3 60 c0
    .byte 0xf3,0x60,0x06            # fallback; gen=f3 60 06
    .byte 0xf3,0x61,0xc0            # fallback; gen=f3 61 c0
    .byte 0xf3,0x61,0x06            # fallback; gen=f3 61 06
    .byte 0xf3,0x62,0xc0            # fallback; gen=f3 62 c0
    .byte 0xf3,0x62,0x06            # fallback; gen=f3 62 06
    .byte 0xf3,0x63,0xc0            # fallback; gen=f3 63 c0
    .byte 0xf3,0x63,0x06            # fallback; gen=f3 63 06
    .byte 0xf3,0x64,0xc0            # fallback; gen=f3 64 c0
    .byte 0xf3,0x64,0x06            # fallback; gen=f3 64 06
    .byte 0xf3,0x64,0xc8            # fallback; gen=f3 64 c8
    .byte 0xf3,0x64,0xd0            # fallback; gen=f3 64 d0
    .byte 0xf3,0x64,0xd8            # fallback; gen=f3 64 d8
    .byte 0xf3,0x64,0xe0            # fallback; gen=f3 64 e0
    .byte 0xf3,0x64,0xe8            # fallback; gen=f3 64 e8
    .byte 0xf3,0x64,0xf0            # fallback; gen=f3 64 f0
    .byte 0xf3,0x64,0xf8            # fallback; gen=f3 64 f8
    .byte 0xf3,0x64,0x0e            # fallback; gen=f3 64 0e
    .byte 0xf3,0x64,0x16            # fallback; gen=f3 64 16
    .byte 0xf3,0x64,0x1e            # fallback; gen=f3 64 1e
    .byte 0xf3,0x64,0x26            # fallback; gen=f3 64 26
    .byte 0xf3,0x64,0x2e            # fallback; gen=f3 64 2e
    .byte 0xf3,0x64,0x36            # fallback; gen=f3 64 36
    .byte 0xf3,0x64,0x3e            # fallback; gen=f3 64 3e
    .byte 0xf3,0x65,0xc0            # fallback; gen=f3 65 c0
    .byte 0xf3,0x65,0x06            # fallback; gen=f3 65 06
    .byte 0xf3,0x65,0xc8            # fallback; gen=f3 65 c8
    .byte 0xf3,0x65,0xd0            # fallback; gen=f3 65 d0
    .byte 0xf3,0x65,0xd8            # fallback; gen=f3 65 d8
    .byte 0xf3,0x65,0xe0            # fallback; gen=f3 65 e0
    .byte 0xf3,0x65,0xe8            # fallback; gen=f3 65 e8
    .byte 0xf3,0x65,0xf0            # fallback; gen=f3 65 f0
    .byte 0xf3,0x65,0xf8            # fallback; gen=f3 65 f8
    .byte 0xf3,0x65,0x0e            # fallback; gen=f3 65 0e
    .byte 0xf3,0x65,0x16            # fallback; gen=f3 65 16
    .byte 0xf3,0x65,0x1e            # fallback; gen=f3 65 1e
    .byte 0xf3,0x65,0x26            # fallback; gen=f3 65 26
    .byte 0xf3,0x65,0x2e            # fallback; gen=f3 65 2e
    .byte 0xf3,0x65,0x36            # fallback; gen=f3 65 36
    .byte 0xf3,0x65,0x3e            # fallback; gen=f3 65 3e
    .byte 0xf3,0x66,0xc0            # fallback; gen=f3 66 c0
    .byte 0xf3,0x66,0x06            # fallback; gen=f3 66 06
    .byte 0xf3,0x66,0xc8            # fallback; gen=f3 66 c8
    .byte 0xf3,0x66,0xd0            # fallback; gen=f3 66 d0
    .byte 0xf3,0x66,0xd8            # fallback; gen=f3 66 d8
    .byte 0xf3,0x66,0xe0            # fallback; gen=f3 66 e0
    .byte 0xf3,0x66,0xe8            # fallback; gen=f3 66 e8
    .byte 0xf3,0x66,0xf0            # fallback; gen=f3 66 f0
    .byte 0xf3,0x66,0xf8            # fallback; gen=f3 66 f8
    .byte 0xf3,0x66,0x0e            # fallback; gen=f3 66 0e
    .byte 0xf3,0x66,0x16            # fallback; gen=f3 66 16
    .byte 0xf3,0x66,0x1e            # fallback; gen=f3 66 1e
    .byte 0xf3,0x66,0x26            # fallback; gen=f3 66 26
    .byte 0xf3,0x66,0x2e            # fallback; gen=f3 66 2e
    .byte 0xf3,0x66,0x36            # fallback; gen=f3 66 36
    .byte 0xf3,0x66,0x3e            # fallback; gen=f3 66 3e
    .byte 0xf3,0x67,0xc0            # fallback; gen=f3 67 c0
    .byte 0xf3,0x67,0x06            # fallback; gen=f3 67 06
    .byte 0xf3,0x67,0xc8            # fallback; gen=f3 67 c8
    .byte 0xf3,0x67,0xd0            # fallback; gen=f3 67 d0
    .byte 0xf3,0x67,0xd8            # fallback; gen=f3 67 d8
    .byte 0xf3,0x67,0xe0            # fallback; gen=f3 67 e0
    .byte 0xf3,0x67,0xe8            # fallback; gen=f3 67 e8
    .byte 0xf3,0x67,0xf0            # fallback; gen=f3 67 f0
    .byte 0xf3,0x67,0xf8            # fallback; gen=f3 67 f8
    .byte 0xf3,0x67,0x0e            # fallback; gen=f3 67 0e
    .byte 0xf3,0x67,0x16            # fallback; gen=f3 67 16
    .byte 0xf3,0x67,0x1e            # fallback; gen=f3 67 1e
    .byte 0xf3,0x67,0x26            # fallback; gen=f3 67 26
    .byte 0xf3,0x67,0x2e            # fallback; gen=f3 67 2e
    .byte 0xf3,0x67,0x36            # fallback; gen=f3 67 36
    .byte 0xf3,0x67,0x3e            # fallback; gen=f3 67 3e
    .byte 0xf3,0x68,0xc0            # fallback; gen=f3 68 c0
    .byte 0xf3,0x68,0x06            # fallback; gen=f3 68 06
    .byte 0xf3,0x69,0xc0            # fallback; gen=f3 69 c0
    .byte 0xf3,0x69,0x06            # fallback; gen=f3 69 06
    .byte 0xf3,0x6a,0xc0            # fallback; gen=f3 6a c0
    .byte 0xf3,0x6a,0x06            # fallback; gen=f3 6a 06
    .byte 0xf3,0x6b,0xc0            # fallback; gen=f3 6b c0
    .byte 0xf3,0x6b,0x06            # fallback; gen=f3 6b 06
    .byte 0xf3,0x6c,0xc0            # fallback; gen=f3 6c c0
    .byte 0xf3,0x6c,0x06            # fallback; gen=f3 6c 06
    .byte 0xf3,0x6d,0xc0            # fallback; gen=f3 6d c0
    .byte 0xf3,0x6d,0x06            # fallback; gen=f3 6d 06
    .byte 0xf3,0x6e,0xc0            # fallback; gen=f3 6e c0
    .byte 0xf3,0x6e,0x06            # fallback; gen=f3 6e 06
    .byte 0xf3,0x6f,0xc0            # fallback; gen=f3 6f c0
    .byte 0xf3,0x6f,0x06            # fallback; gen=f3 6f 06
    .byte 0xf3,0x70,0xc0            # fallback; gen=f3 70 c0
    .byte 0xf3,0x70,0x06            # fallback; gen=f3 70 06
    .byte 0xf3,0x71,0xc0            # fallback; gen=f3 71 c0
    .byte 0xf3,0x71,0x06            # fallback; gen=f3 71 06
    .byte 0xf3,0x72,0xc0            # fallback; gen=f3 72 c0
    .byte 0xf3,0x72,0x06            # fallback; gen=f3 72 06
    .byte 0xf3,0x73,0xc0            # fallback; gen=f3 73 c0
    .byte 0xf3,0x73,0x06            # fallback; gen=f3 73 06
    .byte 0xf3,0x74,0xc0            # fallback; gen=f3 74 c0
    .byte 0xf3,0x74,0x06            # fallback; gen=f3 74 06
    .byte 0xf3,0x75,0xc0            # fallback; gen=f3 75 c0
    .byte 0xf3,0x75,0x06            # fallback; gen=f3 75 06
    .byte 0xf3,0x76,0xc0            # fallback; gen=f3 76 c0
    .byte 0xf3,0x76,0x06            # fallback; gen=f3 76 06
    .byte 0xf3,0x77,0xc0            # fallback; gen=f3 77 c0
    .byte 0xf3,0x77,0x06            # fallback; gen=f3 77 06
    .byte 0xf3,0x78,0xc0            # fallback; gen=f3 78 c0
    .byte 0xf3,0x78,0x06            # fallback; gen=f3 78 06
    .byte 0xf3,0x79,0xc0            # fallback; gen=f3 79 c0
    .byte 0xf3,0x79,0x06            # fallback; gen=f3 79 06
    .byte 0xf3,0x7a,0xc0            # fallback; gen=f3 7a c0
    .byte 0xf3,0x7a,0x06            # fallback; gen=f3 7a 06
    .byte 0xf3,0x7b,0xc0            # fallback; gen=f3 7b c0
    .byte 0xf3,0x7b,0x06            # fallback; gen=f3 7b 06
    .byte 0xf3,0x7c,0xc0            # fallback; gen=f3 7c c0
    .byte 0xf3,0x7c,0x06            # fallback; gen=f3 7c 06
    .byte 0xf3,0x7d,0xc0            # fallback; gen=f3 7d c0
    .byte 0xf3,0x7d,0x06            # fallback; gen=f3 7d 06
    .byte 0xf3,0x7e,0xc0            # fallback; gen=f3 7e c0
    .byte 0xf3,0x7e,0x06            # fallback; gen=f3 7e 06
    .byte 0xf3,0x7f,0xc0            # fallback; gen=f3 7f c0
    .byte 0xf3,0x7f,0x06            # fallback; gen=f3 7f 06
    .byte 0xf3,0x80,0xc0            # fallback; gen=f3 80 c0
    .byte 0xf3,0x80,0x06            # fallback; gen=f3 80 06
    .byte 0xf3,0x81,0xc0            # fallback; gen=f3 81 c0
    .byte 0xf3,0x81,0x06            # fallback; gen=f3 81 06
    .byte 0xf3,0x82,0xc0            # fallback; gen=f3 82 c0
    .byte 0xf3,0x82,0x06            # fallback; gen=f3 82 06
    .byte 0xf3,0x83,0xc0            # fallback; gen=f3 83 c0
    .byte 0xf3,0x83,0x06            # fallback; gen=f3 83 06
    .byte 0xf3,0x84,0xc0            # fallback; gen=f3 84 c0
    .byte 0xf3,0x84,0x06            # fallback; gen=f3 84 06
    .byte 0xf3,0x85,0xc0            # fallback; gen=f3 85 c0
    .byte 0xf3,0x85,0x06            # fallback; gen=f3 85 06
    .byte 0xf3,0x86,0xc0            # fallback; gen=f3 86 c0
    .byte 0xf3,0x86,0x06            # fallback; gen=f3 86 06
    .byte 0xf3,0x86,0xc8            # fallback; gen=f3 86 c8
    .byte 0xf3,0x86,0xd0            # fallback; gen=f3 86 d0
    .byte 0xf3,0x86,0xd8            # fallback; gen=f3 86 d8
    .byte 0xf3,0x86,0xe0            # fallback; gen=f3 86 e0
    .byte 0xf3,0x86,0xe8            # fallback; gen=f3 86 e8
    .byte 0xf3,0x86,0xf0            # fallback; gen=f3 86 f0
    .byte 0xf3,0x86,0xf8            # fallback; gen=f3 86 f8
    .byte 0xf3,0x86,0x0e            # fallback; gen=f3 86 0e
    .byte 0xf3,0x86,0x16            # fallback; gen=f3 86 16
    .byte 0xf3,0x86,0x1e            # fallback; gen=f3 86 1e
    .byte 0xf3,0x86,0x26            # fallback; gen=f3 86 26
    .byte 0xf3,0x86,0x2e            # fallback; gen=f3 86 2e
    .byte 0xf3,0x86,0x36            # fallback; gen=f3 86 36
    .byte 0xf3,0x86,0x3e            # fallback; gen=f3 86 3e
    .byte 0xf3,0x87,0xc0            # fallback; gen=f3 87 c0
    .byte 0xf3,0x87,0x06            # fallback; gen=f3 87 06
    .byte 0xf3,0x87,0xc8            # fallback; gen=f3 87 c8
    .byte 0xf3,0x87,0xd0            # fallback; gen=f3 87 d0
    .byte 0xf3,0x87,0xd8            # fallback; gen=f3 87 d8
    .byte 0xf3,0x87,0xe0            # fallback; gen=f3 87 e0
    .byte 0xf3,0x87,0xe8            # fallback; gen=f3 87 e8
    .byte 0xf3,0x87,0xf0            # fallback; gen=f3 87 f0
    .byte 0xf3,0x87,0xf8            # fallback; gen=f3 87 f8
    .byte 0xf3,0x87,0x0e            # fallback; gen=f3 87 0e
    .byte 0xf3,0x87,0x16            # fallback; gen=f3 87 16
    .byte 0xf3,0x87,0x1e            # fallback; gen=f3 87 1e
    .byte 0xf3,0x87,0x26            # fallback; gen=f3 87 26
    .byte 0xf3,0x87,0x2e            # fallback; gen=f3 87 2e
    .byte 0xf3,0x87,0x36            # fallback; gen=f3 87 36
    .byte 0xf3,0x87,0x3e            # fallback; gen=f3 87 3e
    .byte 0xf3,0x88,0xc0            # fallback; gen=f3 88 c0
    .byte 0xf3,0x88,0x06            # fallback; gen=f3 88 06
    .byte 0xf3,0x88,0xc8            # fallback; gen=f3 88 c8
    .byte 0xf3,0x88,0xd0            # fallback; gen=f3 88 d0
    .byte 0xf3,0x88,0xd8            # fallback; gen=f3 88 d8
    .byte 0xf3,0x88,0xe0            # fallback; gen=f3 88 e0
    .byte 0xf3,0x88,0xe8            # fallback; gen=f3 88 e8
    .byte 0xf3,0x88,0xf0            # fallback; gen=f3 88 f0
    .byte 0xf3,0x88,0xf8            # fallback; gen=f3 88 f8
    .byte 0xf3,0x88,0x0e            # fallback; gen=f3 88 0e
    .byte 0xf3,0x88,0x16            # fallback; gen=f3 88 16
    .byte 0xf3,0x88,0x1e            # fallback; gen=f3 88 1e
    .byte 0xf3,0x88,0x26            # fallback; gen=f3 88 26
    .byte 0xf3,0x88,0x2e            # fallback; gen=f3 88 2e
    .byte 0xf3,0x88,0x36            # fallback; gen=f3 88 36
    .byte 0xf3,0x88,0x3e            # fallback; gen=f3 88 3e
    .byte 0xf3,0x89,0xc0            # fallback; gen=f3 89 c0
    .byte 0xf3,0x89,0x06            # fallback; gen=f3 89 06
    .byte 0xf3,0x89,0xc8            # fallback; gen=f3 89 c8
    .byte 0xf3,0x89,0xd0            # fallback; gen=f3 89 d0
    .byte 0xf3,0x89,0xd8            # fallback; gen=f3 89 d8
    .byte 0xf3,0x89,0xe0            # fallback; gen=f3 89 e0
    .byte 0xf3,0x89,0xe8            # fallback; gen=f3 89 e8
    .byte 0xf3,0x89,0xf0            # fallback; gen=f3 89 f0
    .byte 0xf3,0x89,0xf8            # fallback; gen=f3 89 f8
    .byte 0xf3,0x89,0x0e            # fallback; gen=f3 89 0e
    .byte 0xf3,0x89,0x16            # fallback; gen=f3 89 16
    .byte 0xf3,0x89,0x1e            # fallback; gen=f3 89 1e
    .byte 0xf3,0x89,0x26            # fallback; gen=f3 89 26
    .byte 0xf3,0x89,0x2e            # fallback; gen=f3 89 2e
    .byte 0xf3,0x89,0x36            # fallback; gen=f3 89 36
    .byte 0xf3,0x89,0x3e            # fallback; gen=f3 89 3e
    .byte 0xf3,0x8a,0xc0            # fallback; gen=f3 8a c0
    .byte 0xf3,0x8a,0x06            # fallback; gen=f3 8a 06
    .byte 0xf3,0x8b,0xc0            # fallback; gen=f3 8b c0
    .byte 0xf3,0x8b,0x06            # fallback; gen=f3 8b 06
    .byte 0xf3,0x8c,0xc0            # fallback; gen=f3 8c c0
    .byte 0xf3,0x8c,0x06            # fallback; gen=f3 8c 06
    .byte 0xf3,0x8d,0xc0            # fallback; gen=f3 8d c0
    .byte 0xf3,0x8d,0x06            # fallback; gen=f3 8d 06
    .byte 0xf3,0x8e,0xc0            # fallback; gen=f3 8e c0
    .byte 0xf3,0x8e,0x06            # fallback; gen=f3 8e 06
    .byte 0xf3,0x8f,0xc0            # fallback; gen=f3 8f c0
    .byte 0xf3,0x8f,0x06            # fallback; gen=f3 8f 06
    .byte 0xf3,0x90,0xc0            # fallback; gen=f3 90 c0
    .byte 0xf3,0x90,0x06            # fallback; gen=f3 90 06
    .byte 0xf3,0x91,0xc0            # fallback; gen=f3 91 c0
    .byte 0xf3,0x91,0x06            # fallback; gen=f3 91 06
    .byte 0xf3,0x92,0xc0            # fallback; gen=f3 92 c0
    .byte 0xf3,0x92,0x06            # fallback; gen=f3 92 06
    .byte 0xf3,0x93,0xc0            # fallback; gen=f3 93 c0
    .byte 0xf3,0x93,0x06            # fallback; gen=f3 93 06
    .byte 0xf3,0x94,0xc0            # fallback; gen=f3 94 c0
    .byte 0xf3,0x94,0x06            # fallback; gen=f3 94 06
    .byte 0xf3,0x95,0xc0            # fallback; gen=f3 95 c0
    .byte 0xf3,0x95,0x06            # fallback; gen=f3 95 06
    .byte 0xf3,0x96,0xc0            # fallback; gen=f3 96 c0
    .byte 0xf3,0x96,0x06            # fallback; gen=f3 96 06
    .byte 0xf3,0x97,0xc0            # fallback; gen=f3 97 c0
    .byte 0xf3,0x97,0x06            # fallback; gen=f3 97 06
    .byte 0xf3,0x98,0xc0            # fallback; gen=f3 98 c0
    .byte 0xf3,0x98,0x06            # fallback; gen=f3 98 06
    .byte 0xf3,0x99,0xc0            # fallback; gen=f3 99 c0
    .byte 0xf3,0x99,0x06            # fallback; gen=f3 99 06
    .byte 0xf3,0x9a,0xc0            # fallback; gen=f3 9a c0
    .byte 0xf3,0x9a,0x06            # fallback; gen=f3 9a 06
    .byte 0xf3,0x9b,0xc0            # fallback; gen=f3 9b c0
    .byte 0xf3,0x9b,0x06            # fallback; gen=f3 9b 06
    .byte 0xf3,0x9c,0xc0            # fallback; gen=f3 9c c0
    .byte 0xf3,0x9c,0x06            # fallback; gen=f3 9c 06
    .byte 0xf3,0x9d,0xc0            # fallback; gen=f3 9d c0
    .byte 0xf3,0x9d,0x06            # fallback; gen=f3 9d 06
    .byte 0xf3,0x9e,0xc0            # fallback; gen=f3 9e c0
    .byte 0xf3,0x9e,0x06            # fallback; gen=f3 9e 06
    .byte 0xf3,0x9f,0xc0            # fallback; gen=f3 9f c0
    .byte 0xf3,0x9f,0x06            # fallback; gen=f3 9f 06
    .byte 0xf3,0xa0,0xc0            # fallback; gen=f3 a0 c0
    .byte 0xf3,0xa0,0x06            # fallback; gen=f3 a0 06
    .byte 0xf3,0xa1,0xc0            # fallback; gen=f3 a1 c0
    .byte 0xf3,0xa1,0x06            # fallback; gen=f3 a1 06
    .byte 0xf3,0xa2,0xc0            # fallback; gen=f3 a2 c0
    .byte 0xf3,0xa2,0x06            # fallback; gen=f3 a2 06
    .byte 0xf3,0xa3,0xc0            # fallback; gen=f3 a3 c0
    .byte 0xf3,0xa3,0x06            # fallback; gen=f3 a3 06
    rep movsb %ds:(%si),%es:(%di)       # gen=f3 a4 c0  dis=f3 a4
    rep movsb %ds:(%si),%es:(%di)       # gen=f3 a4 06  dis=f3 a4
    rep movsw %ds:(%si),%es:(%di)       # gen=f3 a5 c0  dis=f3 a5
    rep movsw %ds:(%si),%es:(%di)       # gen=f3 a5 06  dis=f3 a5
    repz cmpsb %es:(%di),%ds:(%si)      # gen=f3 a6 c0  dis=f3 a6
    repz cmpsb %es:(%di),%ds:(%si)      # gen=f3 a6 06  dis=f3 a6
    repz cmpsw %es:(%di),%ds:(%si)      # gen=f3 a7 c0  dis=f3 a7
    repz cmpsw %es:(%di),%ds:(%si)      # gen=f3 a7 06  dis=f3 a7
    .byte 0xf3,0xa8,0xc0            # fallback; gen=f3 a8 c0
    .byte 0xf3,0xa8,0x06            # fallback; gen=f3 a8 06
    .byte 0xf3,0xa9,0xc0            # fallback; gen=f3 a9 c0
    .byte 0xf3,0xa9,0x06            # fallback; gen=f3 a9 06
    rep stos %al,%es:(%di)              # gen=f3 aa c0  dis=f3 aa
    rep stos %al,%es:(%di)              # gen=f3 aa 06  dis=f3 aa
    rep stos %ax,%es:(%di)              # gen=f3 ab c0  dis=f3 ab
    rep stos %ax,%es:(%di)              # gen=f3 ab 06  dis=f3 ab
    rep lods %ds:(%si),%al              # gen=f3 ac c0  dis=f3 ac
    rep lods %ds:(%si),%al              # gen=f3 ac 06  dis=f3 ac
    rep lods %ds:(%si),%ax              # gen=f3 ad c0  dis=f3 ad
    rep lods %ds:(%si),%ax              # gen=f3 ad 06  dis=f3 ad
    repz scas %es:(%di),%al             # gen=f3 ae c0  dis=f3 ae
    repz scas %es:(%di),%al             # gen=f3 ae 06  dis=f3 ae
    repz scas %es:(%di),%ax             # gen=f3 af c0  dis=f3 af
    repz scas %es:(%di),%ax             # gen=f3 af 06  dis=f3 af
    .byte 0xf3,0xb0,0xc0            # fallback; gen=f3 b0 c0
    .byte 0xf3,0xb0,0x06            # fallback; gen=f3 b0 06
    .byte 0xf3,0xb1,0xc0            # fallback; gen=f3 b1 c0
    .byte 0xf3,0xb1,0x06            # fallback; gen=f3 b1 06
    .byte 0xf3,0xb2,0xc0            # fallback; gen=f3 b2 c0
    .byte 0xf3,0xb2,0x06            # fallback; gen=f3 b2 06
    .byte 0xf3,0xb3,0xc0            # fallback; gen=f3 b3 c0
    .byte 0xf3,0xb3,0x06            # fallback; gen=f3 b3 06
    .byte 0xf3,0xb4,0xc0            # fallback; gen=f3 b4 c0
    .byte 0xf3,0xb4,0x06            # fallback; gen=f3 b4 06
    .byte 0xf3,0xb5,0xc0            # fallback; gen=f3 b5 c0
    .byte 0xf3,0xb5,0x06            # fallback; gen=f3 b5 06
    .byte 0xf3,0xb6,0xc0            # fallback; gen=f3 b6 c0
    .byte 0xf3,0xb6,0x06            # fallback; gen=f3 b6 06
    .byte 0xf3,0xb7,0xc0            # fallback; gen=f3 b7 c0
    .byte 0xf3,0xb7,0x06            # fallback; gen=f3 b7 06
    .byte 0xf3,0xb8,0xc0            # fallback; gen=f3 b8 c0
    .byte 0xf3,0xb8,0x06            # fallback; gen=f3 b8 06
    .byte 0xf3,0xb9,0xc0            # fallback; gen=f3 b9 c0
    .byte 0xf3,0xb9,0x06            # fallback; gen=f3 b9 06
    .byte 0xf3,0xba,0xc0            # fallback; gen=f3 ba c0
    .byte 0xf3,0xba,0x06            # fallback; gen=f3 ba 06
    .byte 0xf3,0xbb,0xc0            # fallback; gen=f3 bb c0
    .byte 0xf3,0xbb,0x06            # fallback; gen=f3 bb 06
    .byte 0xf3,0xbc,0xc0            # fallback; gen=f3 bc c0
    .byte 0xf3,0xbc,0x06            # fallback; gen=f3 bc 06
    .byte 0xf3,0xbd,0xc0            # fallback; gen=f3 bd c0
    .byte 0xf3,0xbd,0x06            # fallback; gen=f3 bd 06
    .byte 0xf3,0xbe,0xc0            # fallback; gen=f3 be c0
    .byte 0xf3,0xbe,0x06            # fallback; gen=f3 be 06
    .byte 0xf3,0xbf,0xc0            # fallback; gen=f3 bf c0
    .byte 0xf3,0xbf,0x06            # fallback; gen=f3 bf 06
    .byte 0xf3,0xc0,0xc0            # fallback; gen=f3 c0 c0
    .byte 0xf3,0xc0,0x06            # fallback; gen=f3 c0 06
    .byte 0xf3,0xc1,0xc0            # fallback; gen=f3 c1 c0
    .byte 0xf3,0xc1,0x06            # fallback; gen=f3 c1 06
    repz ret $0x90c0                    # gen=f3 c2 c0  dis=f3 c2 c0 90
    repz ret $0x9006                    # gen=f3 c2 06  dis=f3 c2 06 90
    repz ret                            # gen=f3 c3 c0  dis=f3 c3
    repz ret                            # gen=f3 c3 06  dis=f3 c3
    .byte 0xf3,0xc4,0xc0            # fallback; gen=f3 c4 c0
    .byte 0xf3,0xc4,0x06            # fallback; gen=f3 c4 06
    .byte 0xf3,0xc5,0xc0            # fallback; gen=f3 c5 c0
    .byte 0xf3,0xc5,0x06            # fallback; gen=f3 c5 06
    .byte 0xf3,0xc6,0xc0            # fallback; gen=f3 c6 c0
    .byte 0xf3,0xc6,0x06            # fallback; gen=f3 c6 06
    .byte 0xf3,0xc6,0xc8            # fallback; gen=f3 c6 c8
    .byte 0xf3,0xc6,0xd0            # fallback; gen=f3 c6 d0
    .byte 0xf3,0xc6,0xd8            # fallback; gen=f3 c6 d8
    .byte 0xf3,0xc6,0xe0            # fallback; gen=f3 c6 e0
    .byte 0xf3,0xc6,0xe8            # fallback; gen=f3 c6 e8
    .byte 0xf3,0xc6,0xf0            # fallback; gen=f3 c6 f0
    .byte 0xf3,0xc6,0xf8            # fallback; gen=f3 c6 f8
    .byte 0xf3,0xc6,0x0e            # fallback; gen=f3 c6 0e
    .byte 0xf3,0xc6,0x16            # fallback; gen=f3 c6 16
    .byte 0xf3,0xc6,0x1e            # fallback; gen=f3 c6 1e
    .byte 0xf3,0xc6,0x26            # fallback; gen=f3 c6 26
    .byte 0xf3,0xc6,0x2e            # fallback; gen=f3 c6 2e
    .byte 0xf3,0xc6,0x36            # fallback; gen=f3 c6 36
    .byte 0xf3,0xc6,0x3e            # fallback; gen=f3 c6 3e
    .byte 0xf3,0xc7,0xc0            # fallback; gen=f3 c7 c0
    .byte 0xf3,0xc7,0x06            # fallback; gen=f3 c7 06
    .byte 0xf3,0xc7,0xc8            # fallback; gen=f3 c7 c8
    .byte 0xf3,0xc7,0xd0            # fallback; gen=f3 c7 d0
    .byte 0xf3,0xc7,0xd8            # fallback; gen=f3 c7 d8
    .byte 0xf3,0xc7,0xe0            # fallback; gen=f3 c7 e0
    .byte 0xf3,0xc7,0xe8            # fallback; gen=f3 c7 e8
    .byte 0xf3,0xc7,0xf0            # fallback; gen=f3 c7 f0
    .byte 0xf3,0xc7,0xf8            # fallback; gen=f3 c7 f8
    .byte 0xf3,0xc7,0x0e            # fallback; gen=f3 c7 0e
    .byte 0xf3,0xc7,0x16            # fallback; gen=f3 c7 16
    .byte 0xf3,0xc7,0x1e            # fallback; gen=f3 c7 1e
    .byte 0xf3,0xc7,0x26            # fallback; gen=f3 c7 26
    .byte 0xf3,0xc7,0x2e            # fallback; gen=f3 c7 2e
    .byte 0xf3,0xc7,0x36            # fallback; gen=f3 c7 36
    .byte 0xf3,0xc7,0x3e            # fallback; gen=f3 c7 3e
    .byte 0xf3,0xc8,0xc0            # fallback; gen=f3 c8 c0
    .byte 0xf3,0xc8,0x06            # fallback; gen=f3 c8 06
    .byte 0xf3,0xc9,0xc0            # fallback; gen=f3 c9 c0
    .byte 0xf3,0xc9,0x06            # fallback; gen=f3 c9 06
    .byte 0xf3,0xca,0xc0            # fallback; gen=f3 ca c0
    .byte 0xf3,0xca,0x06            # fallback; gen=f3 ca 06
    .byte 0xf3,0xcb,0xc0            # fallback; gen=f3 cb c0
    .byte 0xf3,0xcb,0x06            # fallback; gen=f3 cb 06
    .byte 0xf3,0xcc,0xc0            # fallback; gen=f3 cc c0
    .byte 0xf3,0xcc,0x06            # fallback; gen=f3 cc 06
    .byte 0xf3,0xcd,0xc0            # fallback; gen=f3 cd c0
    .byte 0xf3,0xcd,0x06            # fallback; gen=f3 cd 06
    .byte 0xf3,0xce,0xc0            # fallback; gen=f3 ce c0
    .byte 0xf3,0xce,0x06            # fallback; gen=f3 ce 06
    .byte 0xf3,0xcf,0xc0            # fallback; gen=f3 cf c0
    .byte 0xf3,0xcf,0x06            # fallback; gen=f3 cf 06
    .byte 0xf3,0xd0,0xc0            # fallback; gen=f3 d0 c0
    .byte 0xf3,0xd0,0x06            # fallback; gen=f3 d0 06
    .byte 0xf3,0xd1,0xc0            # fallback; gen=f3 d1 c0
    .byte 0xf3,0xd1,0x06            # fallback; gen=f3 d1 06
    .byte 0xf3,0xd2,0xc0            # fallback; gen=f3 d2 c0
    .byte 0xf3,0xd2,0x06            # fallback; gen=f3 d2 06
    .byte 0xf3,0xd3,0xc0            # fallback; gen=f3 d3 c0
    .byte 0xf3,0xd3,0x06            # fallback; gen=f3 d3 06
    .byte 0xf3,0xd4,0xc0            # fallback; gen=f3 d4 c0
    .byte 0xf3,0xd4,0x06            # fallback; gen=f3 d4 06
    .byte 0xf3,0xd5,0xc0            # fallback; gen=f3 d5 c0
    .byte 0xf3,0xd5,0x06            # fallback; gen=f3 d5 06
    .byte 0xf3,0xd6,0xc0            # fallback; gen=f3 d6 c0
    .byte 0xf3,0xd6,0x06            # fallback; gen=f3 d6 06
    .byte 0xf3,0xd7,0xc0            # fallback; gen=f3 d7 c0
    .byte 0xf3,0xd7,0x06            # fallback; gen=f3 d7 06
    .byte 0xf3,0xd8,0xc0            # fallback; gen=f3 d8 c0
    .byte 0xf3,0xd8,0x06            # fallback; gen=f3 d8 06
    .byte 0xf3,0xd9,0xc0            # fallback; gen=f3 d9 c0
    .byte 0xf3,0xd9,0x06            # fallback; gen=f3 d9 06
    .byte 0xf3,0xda,0xc0            # fallback; gen=f3 da c0
    .byte 0xf3,0xda,0x06            # fallback; gen=f3 da 06
    .byte 0xf3,0xdb,0xc0            # fallback; gen=f3 db c0
    .byte 0xf3,0xdb,0x06            # fallback; gen=f3 db 06
    .byte 0xf3,0xdc,0xc0            # fallback; gen=f3 dc c0
    .byte 0xf3,0xdc,0x06            # fallback; gen=f3 dc 06
    .byte 0xf3,0xdd,0xc0            # fallback; gen=f3 dd c0
    .byte 0xf3,0xdd,0x06            # fallback; gen=f3 dd 06
    .byte 0xf3,0xde,0xc0            # fallback; gen=f3 de c0
    .byte 0xf3,0xde,0x06            # fallback; gen=f3 de 06
    .byte 0xf3,0xdf,0xc0            # fallback; gen=f3 df c0
    .byte 0xf3,0xdf,0x06            # fallback; gen=f3 df 06
    .byte 0xf3,0xe0,0xc0            # fallback; gen=f3 e0 c0
    .byte 0xf3,0xe0,0x06            # fallback; gen=f3 e0 06
    .byte 0xf3,0xe1,0xc0            # fallback; gen=f3 e1 c0
    .byte 0xf3,0xe1,0x06            # fallback; gen=f3 e1 06
    .byte 0xf3,0xe2,0xc0            # fallback; gen=f3 e2 c0
    .byte 0xf3,0xe2,0x06            # fallback; gen=f3 e2 06
    .byte 0xf3,0xe3,0xc0            # fallback; gen=f3 e3 c0
    .byte 0xf3,0xe3,0x06            # fallback; gen=f3 e3 06
    .byte 0xf3,0xe4,0xc0            # fallback; gen=f3 e4 c0
    .byte 0xf3,0xe4,0x06            # fallback; gen=f3 e4 06
    .byte 0xf3,0xe5,0xc0            # fallback; gen=f3 e5 c0
    .byte 0xf3,0xe5,0x06            # fallback; gen=f3 e5 06
    .byte 0xf3,0xe6,0xc0            # fallback; gen=f3 e6 c0
    .byte 0xf3,0xe6,0x06            # fallback; gen=f3 e6 06
    .byte 0xf3,0xe7,0xc0            # fallback; gen=f3 e7 c0
    .byte 0xf3,0xe7,0x06            # fallback; gen=f3 e7 06
    .byte 0xf3,0xe8,0xc0            # fallback; gen=f3 e8 c0
    .byte 0xf3,0xe8,0x06            # fallback; gen=f3 e8 06
    .byte 0xf3,0xe9,0xc0            # fallback; gen=f3 e9 c0
    .byte 0xf3,0xe9,0x06            # fallback; gen=f3 e9 06
    .byte 0xf3,0xea,0xc0            # fallback; gen=f3 ea c0
    .byte 0xf3,0xea,0x06            # fallback; gen=f3 ea 06
    .byte 0xf3,0xeb,0xc0            # fallback; gen=f3 eb c0
    .byte 0xf3,0xeb,0x06            # fallback; gen=f3 eb 06
    .byte 0xf3,0xec,0xc0            # fallback; gen=f3 ec c0
    .byte 0xf3,0xec,0x06            # fallback; gen=f3 ec 06
    .byte 0xf3,0xed,0xc0            # fallback; gen=f3 ed c0
    .byte 0xf3,0xed,0x06            # fallback; gen=f3 ed 06
    .byte 0xf3,0xee,0xc0            # fallback; gen=f3 ee c0
    .byte 0xf3,0xee,0x06            # fallback; gen=f3 ee 06
    .byte 0xf3,0xef,0xc0            # fallback; gen=f3 ef c0
    .byte 0xf3,0xef,0x06            # fallback; gen=f3 ef 06
    .byte 0xf3,0xf0,0xc0            # fallback; gen=f3 f0 c0
    .byte 0xf3,0xf0,0x06            # fallback; gen=f3 f0 06
    .byte 0xf3,0xf0,0xc8            # fallback; gen=f3 f0 c8
    .byte 0xf3,0xf0,0xd0            # fallback; gen=f3 f0 d0
    .byte 0xf3,0xf0,0xd8            # fallback; gen=f3 f0 d8
    .byte 0xf3,0xf0,0xe0            # fallback; gen=f3 f0 e0
    .byte 0xf3,0xf0,0xe8            # fallback; gen=f3 f0 e8
    .byte 0xf3,0xf0,0xf0            # fallback; gen=f3 f0 f0
    .byte 0xf3,0xf0,0xf8            # fallback; gen=f3 f0 f8
    .byte 0xf3,0xf0,0x0e            # fallback; gen=f3 f0 0e
    .byte 0xf3,0xf0,0x16            # fallback; gen=f3 f0 16
    .byte 0xf3,0xf0,0x1e            # fallback; gen=f3 f0 1e
    .byte 0xf3,0xf0,0x26            # fallback; gen=f3 f0 26
    .byte 0xf3,0xf0,0x2e            # fallback; gen=f3 f0 2e
    .byte 0xf3,0xf0,0x36            # fallback; gen=f3 f0 36
    .byte 0xf3,0xf0,0x3e            # fallback; gen=f3 f0 3e
    .byte 0xf3,0xf1,0xc0            # fallback; gen=f3 f1 c0
    .byte 0xf3,0xf1,0x06            # fallback; gen=f3 f1 06
    .byte 0xf3,0xf2,0xc0            # fallback; gen=f3 f2 c0
    .byte 0xf3,0xf2,0x06            # fallback; gen=f3 f2 06
    .byte 0xf3,0xf3,0xc0            # fallback; gen=f3 f3 c0
    .byte 0xf3,0xf3,0x06            # fallback; gen=f3 f3 06
    .byte 0xf3,0xf4,0xc0            # fallback; gen=f3 f4 c0
    .byte 0xf3,0xf4,0x06            # fallback; gen=f3 f4 06
    .byte 0xf3,0xf5,0xc0            # fallback; gen=f3 f5 c0
    .byte 0xf3,0xf5,0x06            # fallback; gen=f3 f5 06
    .byte 0xf3,0xf6,0xc0            # fallback; gen=f3 f6 c0
    .byte 0xf3,0xf6,0x06            # fallback; gen=f3 f6 06
    .byte 0xf3,0xf7,0xc0            # fallback; gen=f3 f7 c0
    .byte 0xf3,0xf7,0x06            # fallback; gen=f3 f7 06
    .byte 0xf3,0xf8,0xc0            # fallback; gen=f3 f8 c0
    .byte 0xf3,0xf8,0x06            # fallback; gen=f3 f8 06
    .byte 0xf3,0xf9,0xc0            # fallback; gen=f3 f9 c0
    .byte 0xf3,0xf9,0x06            # fallback; gen=f3 f9 06
    .byte 0xf3,0xfa,0xc0            # fallback; gen=f3 fa c0
    .byte 0xf3,0xfa,0x06            # fallback; gen=f3 fa 06
    .byte 0xf3,0xfb,0xc0            # fallback; gen=f3 fb c0
    .byte 0xf3,0xfb,0x06            # fallback; gen=f3 fb 06
    .byte 0xf3,0xfc,0xc0            # fallback; gen=f3 fc c0
    .byte 0xf3,0xfc,0x06            # fallback; gen=f3 fc 06
    .byte 0xf3,0xfd,0xc0            # fallback; gen=f3 fd c0
    .byte 0xf3,0xfd,0x06            # fallback; gen=f3 fd 06
    .byte 0xf3,0xfe,0xc0            # fallback; gen=f3 fe c0
    .byte 0xf3,0xfe,0x06            # fallback; gen=f3 fe 06
    .byte 0xf3,0xff,0xc0            # fallback; gen=f3 ff c0
    .byte 0xf3,0xff,0x06            # fallback; gen=f3 ff 06
    es add %al,%al                      # gen=26 00 c0  dis=26 00 c0
    add    %al,%es:-0x6f70              # gen=26 00 06  dis=26 00 06 90 90
    es add %cl,%al                      # gen=26 00 c8  dis=26 00 c8
    es add %dl,%al                      # gen=26 00 d0  dis=26 00 d0
    es add %bl,%al                      # gen=26 00 d8  dis=26 00 d8
    es add %ah,%al                      # gen=26 00 e0  dis=26 00 e0
    es add %ch,%al                      # gen=26 00 e8  dis=26 00 e8
    es add %dh,%al                      # gen=26 00 f0  dis=26 00 f0
    es add %bh,%al                      # gen=26 00 f8  dis=26 00 f8
    add    %cl,%es:-0x6f70              # gen=26 00 0e  dis=26 00 0e 90 90
    add    %dl,%es:-0x6f70              # gen=26 00 16  dis=26 00 16 90 90
    add    %bl,%es:-0x6f70              # gen=26 00 1e  dis=26 00 1e 90 90
    add    %ah,%es:-0x6f70              # gen=26 00 26  dis=26 00 26 90 90
    add    %ch,%es:-0x6f70              # gen=26 00 2e  dis=26 00 2e 90 90
    add    %dh,%es:-0x6f70              # gen=26 00 36  dis=26 00 36 90 90
    add    %bh,%es:-0x6f70              # gen=26 00 3e  dis=26 00 3e 90 90
    es add %ax,%ax                      # gen=26 01 c0  dis=26 01 c0
    add    %ax,%es:-0x6f70              # gen=26 01 06  dis=26 01 06 90 90
    es add %cx,%ax                      # gen=26 01 c8  dis=26 01 c8
    es add %dx,%ax                      # gen=26 01 d0  dis=26 01 d0
    es add %bx,%ax                      # gen=26 01 d8  dis=26 01 d8
    es add %sp,%ax                      # gen=26 01 e0  dis=26 01 e0
    es add %bp,%ax                      # gen=26 01 e8  dis=26 01 e8
    es add %si,%ax                      # gen=26 01 f0  dis=26 01 f0
    es add %di,%ax                      # gen=26 01 f8  dis=26 01 f8
    add    %cx,%es:-0x6f70              # gen=26 01 0e  dis=26 01 0e 90 90
    add    %dx,%es:-0x6f70              # gen=26 01 16  dis=26 01 16 90 90
    add    %bx,%es:-0x6f70              # gen=26 01 1e  dis=26 01 1e 90 90
    add    %sp,%es:-0x6f70              # gen=26 01 26  dis=26 01 26 90 90
    add    %bp,%es:-0x6f70              # gen=26 01 2e  dis=26 01 2e 90 90
    add    %si,%es:-0x6f70              # gen=26 01 36  dis=26 01 36 90 90
    add    %di,%es:-0x6f70              # gen=26 01 3e  dis=26 01 3e 90 90
    es add %al,%al                      # gen=26 02 c0  dis=26 02 c0
    add    %es:-0x6f70,%al              # gen=26 02 06  dis=26 02 06 90 90
    es add %al,%cl                      # gen=26 02 c8  dis=26 02 c8
    es add %al,%dl                      # gen=26 02 d0  dis=26 02 d0
    es add %al,%bl                      # gen=26 02 d8  dis=26 02 d8
    es add %al,%ah                      # gen=26 02 e0  dis=26 02 e0
    es add %al,%ch                      # gen=26 02 e8  dis=26 02 e8
    es add %al,%dh                      # gen=26 02 f0  dis=26 02 f0
    es add %al,%bh                      # gen=26 02 f8  dis=26 02 f8
    add    %es:-0x6f70,%cl              # gen=26 02 0e  dis=26 02 0e 90 90
    add    %es:-0x6f70,%dl              # gen=26 02 16  dis=26 02 16 90 90
    add    %es:-0x6f70,%bl              # gen=26 02 1e  dis=26 02 1e 90 90
    add    %es:-0x6f70,%ah              # gen=26 02 26  dis=26 02 26 90 90
    add    %es:-0x6f70,%ch              # gen=26 02 2e  dis=26 02 2e 90 90
    add    %es:-0x6f70,%dh              # gen=26 02 36  dis=26 02 36 90 90
    add    %es:-0x6f70,%bh              # gen=26 02 3e  dis=26 02 3e 90 90
    es add %ax,%ax                      # gen=26 03 c0  dis=26 03 c0
    add    %es:-0x6f70,%ax              # gen=26 03 06  dis=26 03 06 90 90
    es add %ax,%cx                      # gen=26 03 c8  dis=26 03 c8
    es add %ax,%dx                      # gen=26 03 d0  dis=26 03 d0
    es add %ax,%bx                      # gen=26 03 d8  dis=26 03 d8
    es add %ax,%sp                      # gen=26 03 e0  dis=26 03 e0
    es add %ax,%bp                      # gen=26 03 e8  dis=26 03 e8
    es add %ax,%si                      # gen=26 03 f0  dis=26 03 f0
    es add %ax,%di                      # gen=26 03 f8  dis=26 03 f8
    add    %es:-0x6f70,%cx              # gen=26 03 0e  dis=26 03 0e 90 90
    add    %es:-0x6f70,%dx              # gen=26 03 16  dis=26 03 16 90 90
    add    %es:-0x6f70,%bx              # gen=26 03 1e  dis=26 03 1e 90 90
    add    %es:-0x6f70,%sp              # gen=26 03 26  dis=26 03 26 90 90
    add    %es:-0x6f70,%bp              # gen=26 03 2e  dis=26 03 2e 90 90
    add    %es:-0x6f70,%si              # gen=26 03 36  dis=26 03 36 90 90
    add    %es:-0x6f70,%di              # gen=26 03 3e  dis=26 03 3e 90 90
    es add $0xc0,%al                    # gen=26 04 c0  dis=26 04 c0
    es add $0x6,%al                     # gen=26 04 06  dis=26 04 06
    es add $0x90c0,%ax                  # gen=26 05 c0  dis=26 05 c0 90
    es add $0x9006,%ax                  # gen=26 05 06  dis=26 05 06 90
    es push %es                         # gen=26 06 c0  dis=26 06
    es push %es                         # gen=26 06 06  dis=26 06
    es pop %es                          # gen=26 07 c0  dis=26 07
    es pop %es                          # gen=26 07 06  dis=26 07
    es or  %al,%al                      # gen=26 08 c0  dis=26 08 c0
    or     %al,%es:-0x6f70              # gen=26 08 06  dis=26 08 06 90 90
    es or  %cl,%al                      # gen=26 08 c8  dis=26 08 c8
    es or  %dl,%al                      # gen=26 08 d0  dis=26 08 d0
    es or  %bl,%al                      # gen=26 08 d8  dis=26 08 d8
    es or  %ah,%al                      # gen=26 08 e0  dis=26 08 e0
    es or  %ch,%al                      # gen=26 08 e8  dis=26 08 e8
    es or  %dh,%al                      # gen=26 08 f0  dis=26 08 f0
    es or  %bh,%al                      # gen=26 08 f8  dis=26 08 f8
    or     %cl,%es:-0x6f70              # gen=26 08 0e  dis=26 08 0e 90 90
    or     %dl,%es:-0x6f70              # gen=26 08 16  dis=26 08 16 90 90
    or     %bl,%es:-0x6f70              # gen=26 08 1e  dis=26 08 1e 90 90
    or     %ah,%es:-0x6f70              # gen=26 08 26  dis=26 08 26 90 90
    or     %ch,%es:-0x6f70              # gen=26 08 2e  dis=26 08 2e 90 90
    or     %dh,%es:-0x6f70              # gen=26 08 36  dis=26 08 36 90 90
    or     %bh,%es:-0x6f70              # gen=26 08 3e  dis=26 08 3e 90 90
    es or  %ax,%ax                      # gen=26 09 c0  dis=26 09 c0
    or     %ax,%es:-0x6f70              # gen=26 09 06  dis=26 09 06 90 90
    es or  %cx,%ax                      # gen=26 09 c8  dis=26 09 c8
    es or  %dx,%ax                      # gen=26 09 d0  dis=26 09 d0
    es or  %bx,%ax                      # gen=26 09 d8  dis=26 09 d8
    es or  %sp,%ax                      # gen=26 09 e0  dis=26 09 e0
    es or  %bp,%ax                      # gen=26 09 e8  dis=26 09 e8
    es or  %si,%ax                      # gen=26 09 f0  dis=26 09 f0
    es or  %di,%ax                      # gen=26 09 f8  dis=26 09 f8
    or     %cx,%es:-0x6f70              # gen=26 09 0e  dis=26 09 0e 90 90
    or     %dx,%es:-0x6f70              # gen=26 09 16  dis=26 09 16 90 90
    or     %bx,%es:-0x6f70              # gen=26 09 1e  dis=26 09 1e 90 90
    or     %sp,%es:-0x6f70              # gen=26 09 26  dis=26 09 26 90 90
    or     %bp,%es:-0x6f70              # gen=26 09 2e  dis=26 09 2e 90 90
    or     %si,%es:-0x6f70              # gen=26 09 36  dis=26 09 36 90 90
    or     %di,%es:-0x6f70              # gen=26 09 3e  dis=26 09 3e 90 90
    es or  %al,%al                      # gen=26 0a c0  dis=26 0a c0
    or     %es:-0x6f70,%al              # gen=26 0a 06  dis=26 0a 06 90 90
    es or  %al,%cl                      # gen=26 0a c8  dis=26 0a c8
    es or  %al,%dl                      # gen=26 0a d0  dis=26 0a d0
    es or  %al,%bl                      # gen=26 0a d8  dis=26 0a d8
    es or  %al,%ah                      # gen=26 0a e0  dis=26 0a e0
    es or  %al,%ch                      # gen=26 0a e8  dis=26 0a e8
    es or  %al,%dh                      # gen=26 0a f0  dis=26 0a f0
    es or  %al,%bh                      # gen=26 0a f8  dis=26 0a f8
    or     %es:-0x6f70,%cl              # gen=26 0a 0e  dis=26 0a 0e 90 90
    or     %es:-0x6f70,%dl              # gen=26 0a 16  dis=26 0a 16 90 90
    or     %es:-0x6f70,%bl              # gen=26 0a 1e  dis=26 0a 1e 90 90
    or     %es:-0x6f70,%ah              # gen=26 0a 26  dis=26 0a 26 90 90
    or     %es:-0x6f70,%ch              # gen=26 0a 2e  dis=26 0a 2e 90 90
    or     %es:-0x6f70,%dh              # gen=26 0a 36  dis=26 0a 36 90 90
    or     %es:-0x6f70,%bh              # gen=26 0a 3e  dis=26 0a 3e 90 90
    es or  %ax,%ax                      # gen=26 0b c0  dis=26 0b c0
    or     %es:-0x6f70,%ax              # gen=26 0b 06  dis=26 0b 06 90 90
    es or  %ax,%cx                      # gen=26 0b c8  dis=26 0b c8
    es or  %ax,%dx                      # gen=26 0b d0  dis=26 0b d0
    es or  %ax,%bx                      # gen=26 0b d8  dis=26 0b d8
    es or  %ax,%sp                      # gen=26 0b e0  dis=26 0b e0
    es or  %ax,%bp                      # gen=26 0b e8  dis=26 0b e8
    es or  %ax,%si                      # gen=26 0b f0  dis=26 0b f0
    es or  %ax,%di                      # gen=26 0b f8  dis=26 0b f8
    or     %es:-0x6f70,%cx              # gen=26 0b 0e  dis=26 0b 0e 90 90
    or     %es:-0x6f70,%dx              # gen=26 0b 16  dis=26 0b 16 90 90
    or     %es:-0x6f70,%bx              # gen=26 0b 1e  dis=26 0b 1e 90 90
    or     %es:-0x6f70,%sp              # gen=26 0b 26  dis=26 0b 26 90 90
    or     %es:-0x6f70,%bp              # gen=26 0b 2e  dis=26 0b 2e 90 90
    or     %es:-0x6f70,%si              # gen=26 0b 36  dis=26 0b 36 90 90
    or     %es:-0x6f70,%di              # gen=26 0b 3e  dis=26 0b 3e 90 90
    es or  $0xc0,%al                    # gen=26 0c c0  dis=26 0c c0
    es or  $0x6,%al                     # gen=26 0c 06  dis=26 0c 06
    es or  $0x90c0,%ax                  # gen=26 0d c0  dis=26 0d c0 90
    es or  $0x9006,%ax                  # gen=26 0d 06  dis=26 0d 06 90
    es push %cs                         # gen=26 0e c0  dis=26 0e
    es push %cs                         # gen=26 0e 06  dis=26 0e
    .byte 0x26,0x0f,0xc0            # fallback; gen=26 0f c0
    .byte 0x26,0x0f,0x06            # fallback; gen=26 0f 06
    .byte 0x26,0x0f,0xc8            # fallback; gen=26 0f c8
    .byte 0x26,0x0f,0xd0            # fallback; gen=26 0f d0
    .byte 0x26,0x0f,0xd8            # fallback; gen=26 0f d8
    .byte 0x26,0x0f,0xe0            # fallback; gen=26 0f e0
    .byte 0x26,0x0f,0xe8            # fallback; gen=26 0f e8
    .byte 0x26,0x0f,0xf0            # fallback; gen=26 0f f0
    .byte 0x26,0x0f,0xf8            # fallback; gen=26 0f f8
    .byte 0x26,0x0f,0x0e            # fallback; gen=26 0f 0e
    .byte 0x26,0x0f,0x16            # fallback; gen=26 0f 16
    .byte 0x26,0x0f,0x1e            # fallback; gen=26 0f 1e
    .byte 0x26,0x0f,0x26            # fallback; gen=26 0f 26
    .byte 0x26,0x0f,0x2e            # fallback; gen=26 0f 2e
    .byte 0x26,0x0f,0x36            # fallback; gen=26 0f 36
    .byte 0x26,0x0f,0x3e            # fallback; gen=26 0f 3e
    es adc %al,%al                      # gen=26 10 c0  dis=26 10 c0
    adc    %al,%es:-0x6f70              # gen=26 10 06  dis=26 10 06 90 90
    es adc %cl,%al                      # gen=26 10 c8  dis=26 10 c8
    es adc %dl,%al                      # gen=26 10 d0  dis=26 10 d0
    es adc %bl,%al                      # gen=26 10 d8  dis=26 10 d8
    es adc %ah,%al                      # gen=26 10 e0  dis=26 10 e0
    es adc %ch,%al                      # gen=26 10 e8  dis=26 10 e8
    es adc %dh,%al                      # gen=26 10 f0  dis=26 10 f0
    es adc %bh,%al                      # gen=26 10 f8  dis=26 10 f8
    adc    %cl,%es:-0x6f70              # gen=26 10 0e  dis=26 10 0e 90 90
    adc    %dl,%es:-0x6f70              # gen=26 10 16  dis=26 10 16 90 90
    adc    %bl,%es:-0x6f70              # gen=26 10 1e  dis=26 10 1e 90 90
    adc    %ah,%es:-0x6f70              # gen=26 10 26  dis=26 10 26 90 90
    adc    %ch,%es:-0x6f70              # gen=26 10 2e  dis=26 10 2e 90 90
    adc    %dh,%es:-0x6f70              # gen=26 10 36  dis=26 10 36 90 90
    adc    %bh,%es:-0x6f70              # gen=26 10 3e  dis=26 10 3e 90 90
    es adc %ax,%ax                      # gen=26 11 c0  dis=26 11 c0
    adc    %ax,%es:-0x6f70              # gen=26 11 06  dis=26 11 06 90 90
    es adc %cx,%ax                      # gen=26 11 c8  dis=26 11 c8
    es adc %dx,%ax                      # gen=26 11 d0  dis=26 11 d0
    es adc %bx,%ax                      # gen=26 11 d8  dis=26 11 d8
    es adc %sp,%ax                      # gen=26 11 e0  dis=26 11 e0
    es adc %bp,%ax                      # gen=26 11 e8  dis=26 11 e8
    es adc %si,%ax                      # gen=26 11 f0  dis=26 11 f0
    es adc %di,%ax                      # gen=26 11 f8  dis=26 11 f8
    adc    %cx,%es:-0x6f70              # gen=26 11 0e  dis=26 11 0e 90 90
    adc    %dx,%es:-0x6f70              # gen=26 11 16  dis=26 11 16 90 90
    adc    %bx,%es:-0x6f70              # gen=26 11 1e  dis=26 11 1e 90 90
    adc    %sp,%es:-0x6f70              # gen=26 11 26  dis=26 11 26 90 90
    adc    %bp,%es:-0x6f70              # gen=26 11 2e  dis=26 11 2e 90 90
    adc    %si,%es:-0x6f70              # gen=26 11 36  dis=26 11 36 90 90
    adc    %di,%es:-0x6f70              # gen=26 11 3e  dis=26 11 3e 90 90
    es adc %al,%al                      # gen=26 12 c0  dis=26 12 c0
    adc    %es:-0x6f70,%al              # gen=26 12 06  dis=26 12 06 90 90
    es adc %al,%cl                      # gen=26 12 c8  dis=26 12 c8
    es adc %al,%dl                      # gen=26 12 d0  dis=26 12 d0
    es adc %al,%bl                      # gen=26 12 d8  dis=26 12 d8
    es adc %al,%ah                      # gen=26 12 e0  dis=26 12 e0
    es adc %al,%ch                      # gen=26 12 e8  dis=26 12 e8
    es adc %al,%dh                      # gen=26 12 f0  dis=26 12 f0
    es adc %al,%bh                      # gen=26 12 f8  dis=26 12 f8
    adc    %es:-0x6f70,%cl              # gen=26 12 0e  dis=26 12 0e 90 90
    adc    %es:-0x6f70,%dl              # gen=26 12 16  dis=26 12 16 90 90
    adc    %es:-0x6f70,%bl              # gen=26 12 1e  dis=26 12 1e 90 90
    adc    %es:-0x6f70,%ah              # gen=26 12 26  dis=26 12 26 90 90
    adc    %es:-0x6f70,%ch              # gen=26 12 2e  dis=26 12 2e 90 90
    adc    %es:-0x6f70,%dh              # gen=26 12 36  dis=26 12 36 90 90
    adc    %es:-0x6f70,%bh              # gen=26 12 3e  dis=26 12 3e 90 90
    es adc %ax,%ax                      # gen=26 13 c0  dis=26 13 c0
    adc    %es:-0x6f70,%ax              # gen=26 13 06  dis=26 13 06 90 90
    es adc %ax,%cx                      # gen=26 13 c8  dis=26 13 c8
    es adc %ax,%dx                      # gen=26 13 d0  dis=26 13 d0
    es adc %ax,%bx                      # gen=26 13 d8  dis=26 13 d8
    es adc %ax,%sp                      # gen=26 13 e0  dis=26 13 e0
    es adc %ax,%bp                      # gen=26 13 e8  dis=26 13 e8
    es adc %ax,%si                      # gen=26 13 f0  dis=26 13 f0
    es adc %ax,%di                      # gen=26 13 f8  dis=26 13 f8
    adc    %es:-0x6f70,%cx              # gen=26 13 0e  dis=26 13 0e 90 90
    adc    %es:-0x6f70,%dx              # gen=26 13 16  dis=26 13 16 90 90
    adc    %es:-0x6f70,%bx              # gen=26 13 1e  dis=26 13 1e 90 90
    adc    %es:-0x6f70,%sp              # gen=26 13 26  dis=26 13 26 90 90
    adc    %es:-0x6f70,%bp              # gen=26 13 2e  dis=26 13 2e 90 90
    adc    %es:-0x6f70,%si              # gen=26 13 36  dis=26 13 36 90 90
    adc    %es:-0x6f70,%di              # gen=26 13 3e  dis=26 13 3e 90 90
    es adc $0xc0,%al                    # gen=26 14 c0  dis=26 14 c0
    es adc $0x6,%al                     # gen=26 14 06  dis=26 14 06
    es adc $0x90c0,%ax                  # gen=26 15 c0  dis=26 15 c0 90
    es adc $0x9006,%ax                  # gen=26 15 06  dis=26 15 06 90
    es push %ss                         # gen=26 16 c0  dis=26 16
    es push %ss                         # gen=26 16 06  dis=26 16
    es pop %ss                          # gen=26 17 c0  dis=26 17
    es pop %ss                          # gen=26 17 06  dis=26 17
    es sbb %al,%al                      # gen=26 18 c0  dis=26 18 c0
    sbb    %al,%es:-0x6f70              # gen=26 18 06  dis=26 18 06 90 90
    es sbb %cl,%al                      # gen=26 18 c8  dis=26 18 c8
    es sbb %dl,%al                      # gen=26 18 d0  dis=26 18 d0
    es sbb %bl,%al                      # gen=26 18 d8  dis=26 18 d8
    es sbb %ah,%al                      # gen=26 18 e0  dis=26 18 e0
    es sbb %ch,%al                      # gen=26 18 e8  dis=26 18 e8
    es sbb %dh,%al                      # gen=26 18 f0  dis=26 18 f0
    es sbb %bh,%al                      # gen=26 18 f8  dis=26 18 f8
    sbb    %cl,%es:-0x6f70              # gen=26 18 0e  dis=26 18 0e 90 90
    sbb    %dl,%es:-0x6f70              # gen=26 18 16  dis=26 18 16 90 90
    sbb    %bl,%es:-0x6f70              # gen=26 18 1e  dis=26 18 1e 90 90
    sbb    %ah,%es:-0x6f70              # gen=26 18 26  dis=26 18 26 90 90
    sbb    %ch,%es:-0x6f70              # gen=26 18 2e  dis=26 18 2e 90 90
    sbb    %dh,%es:-0x6f70              # gen=26 18 36  dis=26 18 36 90 90
    sbb    %bh,%es:-0x6f70              # gen=26 18 3e  dis=26 18 3e 90 90
    es sbb %ax,%ax                      # gen=26 19 c0  dis=26 19 c0
    sbb    %ax,%es:-0x6f70              # gen=26 19 06  dis=26 19 06 90 90
    es sbb %cx,%ax                      # gen=26 19 c8  dis=26 19 c8
    es sbb %dx,%ax                      # gen=26 19 d0  dis=26 19 d0
    es sbb %bx,%ax                      # gen=26 19 d8  dis=26 19 d8
    es sbb %sp,%ax                      # gen=26 19 e0  dis=26 19 e0
    es sbb %bp,%ax                      # gen=26 19 e8  dis=26 19 e8
    es sbb %si,%ax                      # gen=26 19 f0  dis=26 19 f0
    es sbb %di,%ax                      # gen=26 19 f8  dis=26 19 f8
    sbb    %cx,%es:-0x6f70              # gen=26 19 0e  dis=26 19 0e 90 90
    sbb    %dx,%es:-0x6f70              # gen=26 19 16  dis=26 19 16 90 90
    sbb    %bx,%es:-0x6f70              # gen=26 19 1e  dis=26 19 1e 90 90
    sbb    %sp,%es:-0x6f70              # gen=26 19 26  dis=26 19 26 90 90
    sbb    %bp,%es:-0x6f70              # gen=26 19 2e  dis=26 19 2e 90 90
    sbb    %si,%es:-0x6f70              # gen=26 19 36  dis=26 19 36 90 90
    sbb    %di,%es:-0x6f70              # gen=26 19 3e  dis=26 19 3e 90 90
    es sbb %al,%al                      # gen=26 1a c0  dis=26 1a c0
    sbb    %es:-0x6f70,%al              # gen=26 1a 06  dis=26 1a 06 90 90
    es sbb %al,%cl                      # gen=26 1a c8  dis=26 1a c8
    es sbb %al,%dl                      # gen=26 1a d0  dis=26 1a d0
    es sbb %al,%bl                      # gen=26 1a d8  dis=26 1a d8
    es sbb %al,%ah                      # gen=26 1a e0  dis=26 1a e0
    es sbb %al,%ch                      # gen=26 1a e8  dis=26 1a e8
    es sbb %al,%dh                      # gen=26 1a f0  dis=26 1a f0
    es sbb %al,%bh                      # gen=26 1a f8  dis=26 1a f8
    sbb    %es:-0x6f70,%cl              # gen=26 1a 0e  dis=26 1a 0e 90 90
    sbb    %es:-0x6f70,%dl              # gen=26 1a 16  dis=26 1a 16 90 90
    sbb    %es:-0x6f70,%bl              # gen=26 1a 1e  dis=26 1a 1e 90 90
    sbb    %es:-0x6f70,%ah              # gen=26 1a 26  dis=26 1a 26 90 90
    sbb    %es:-0x6f70,%ch              # gen=26 1a 2e  dis=26 1a 2e 90 90
    sbb    %es:-0x6f70,%dh              # gen=26 1a 36  dis=26 1a 36 90 90
    sbb    %es:-0x6f70,%bh              # gen=26 1a 3e  dis=26 1a 3e 90 90
    es sbb %ax,%ax                      # gen=26 1b c0  dis=26 1b c0
    sbb    %es:-0x6f70,%ax              # gen=26 1b 06  dis=26 1b 06 90 90
    es sbb %ax,%cx                      # gen=26 1b c8  dis=26 1b c8
    es sbb %ax,%dx                      # gen=26 1b d0  dis=26 1b d0
    es sbb %ax,%bx                      # gen=26 1b d8  dis=26 1b d8
    es sbb %ax,%sp                      # gen=26 1b e0  dis=26 1b e0
    es sbb %ax,%bp                      # gen=26 1b e8  dis=26 1b e8
    es sbb %ax,%si                      # gen=26 1b f0  dis=26 1b f0
    es sbb %ax,%di                      # gen=26 1b f8  dis=26 1b f8
    sbb    %es:-0x6f70,%cx              # gen=26 1b 0e  dis=26 1b 0e 90 90
    sbb    %es:-0x6f70,%dx              # gen=26 1b 16  dis=26 1b 16 90 90
    sbb    %es:-0x6f70,%bx              # gen=26 1b 1e  dis=26 1b 1e 90 90
    sbb    %es:-0x6f70,%sp              # gen=26 1b 26  dis=26 1b 26 90 90
    sbb    %es:-0x6f70,%bp              # gen=26 1b 2e  dis=26 1b 2e 90 90
    sbb    %es:-0x6f70,%si              # gen=26 1b 36  dis=26 1b 36 90 90
    sbb    %es:-0x6f70,%di              # gen=26 1b 3e  dis=26 1b 3e 90 90
    es sbb $0xc0,%al                    # gen=26 1c c0  dis=26 1c c0
    es sbb $0x6,%al                     # gen=26 1c 06  dis=26 1c 06
    es sbb $0x90c0,%ax                  # gen=26 1d c0  dis=26 1d c0 90
    es sbb $0x9006,%ax                  # gen=26 1d 06  dis=26 1d 06 90
    es push %ds                         # gen=26 1e c0  dis=26 1e
    es push %ds                         # gen=26 1e 06  dis=26 1e
    es pop %ds                          # gen=26 1f c0  dis=26 1f
    es pop %ds                          # gen=26 1f 06  dis=26 1f
    es and %al,%al                      # gen=26 20 c0  dis=26 20 c0
    and    %al,%es:-0x6f70              # gen=26 20 06  dis=26 20 06 90 90
    es and %cl,%al                      # gen=26 20 c8  dis=26 20 c8
    es and %dl,%al                      # gen=26 20 d0  dis=26 20 d0
    es and %bl,%al                      # gen=26 20 d8  dis=26 20 d8
    es and %ah,%al                      # gen=26 20 e0  dis=26 20 e0
    es and %ch,%al                      # gen=26 20 e8  dis=26 20 e8
    es and %dh,%al                      # gen=26 20 f0  dis=26 20 f0
    es and %bh,%al                      # gen=26 20 f8  dis=26 20 f8
    and    %cl,%es:-0x6f70              # gen=26 20 0e  dis=26 20 0e 90 90
    and    %dl,%es:-0x6f70              # gen=26 20 16  dis=26 20 16 90 90
    and    %bl,%es:-0x6f70              # gen=26 20 1e  dis=26 20 1e 90 90
    and    %ah,%es:-0x6f70              # gen=26 20 26  dis=26 20 26 90 90
    and    %ch,%es:-0x6f70              # gen=26 20 2e  dis=26 20 2e 90 90
    and    %dh,%es:-0x6f70              # gen=26 20 36  dis=26 20 36 90 90
    and    %bh,%es:-0x6f70              # gen=26 20 3e  dis=26 20 3e 90 90
    es and %ax,%ax                      # gen=26 21 c0  dis=26 21 c0
    and    %ax,%es:-0x6f70              # gen=26 21 06  dis=26 21 06 90 90
    es and %cx,%ax                      # gen=26 21 c8  dis=26 21 c8
    es and %dx,%ax                      # gen=26 21 d0  dis=26 21 d0
    es and %bx,%ax                      # gen=26 21 d8  dis=26 21 d8
    es and %sp,%ax                      # gen=26 21 e0  dis=26 21 e0
    es and %bp,%ax                      # gen=26 21 e8  dis=26 21 e8
    es and %si,%ax                      # gen=26 21 f0  dis=26 21 f0
    es and %di,%ax                      # gen=26 21 f8  dis=26 21 f8
    and    %cx,%es:-0x6f70              # gen=26 21 0e  dis=26 21 0e 90 90
    and    %dx,%es:-0x6f70              # gen=26 21 16  dis=26 21 16 90 90
    and    %bx,%es:-0x6f70              # gen=26 21 1e  dis=26 21 1e 90 90
    and    %sp,%es:-0x6f70              # gen=26 21 26  dis=26 21 26 90 90
    and    %bp,%es:-0x6f70              # gen=26 21 2e  dis=26 21 2e 90 90
    and    %si,%es:-0x6f70              # gen=26 21 36  dis=26 21 36 90 90
    and    %di,%es:-0x6f70              # gen=26 21 3e  dis=26 21 3e 90 90
    es and %al,%al                      # gen=26 22 c0  dis=26 22 c0
    and    %es:-0x6f70,%al              # gen=26 22 06  dis=26 22 06 90 90
    es and %al,%cl                      # gen=26 22 c8  dis=26 22 c8
    es and %al,%dl                      # gen=26 22 d0  dis=26 22 d0
    es and %al,%bl                      # gen=26 22 d8  dis=26 22 d8
    es and %al,%ah                      # gen=26 22 e0  dis=26 22 e0
    es and %al,%ch                      # gen=26 22 e8  dis=26 22 e8
    es and %al,%dh                      # gen=26 22 f0  dis=26 22 f0
    es and %al,%bh                      # gen=26 22 f8  dis=26 22 f8
    and    %es:-0x6f70,%cl              # gen=26 22 0e  dis=26 22 0e 90 90
    and    %es:-0x6f70,%dl              # gen=26 22 16  dis=26 22 16 90 90
    and    %es:-0x6f70,%bl              # gen=26 22 1e  dis=26 22 1e 90 90
    and    %es:-0x6f70,%ah              # gen=26 22 26  dis=26 22 26 90 90
    and    %es:-0x6f70,%ch              # gen=26 22 2e  dis=26 22 2e 90 90
    and    %es:-0x6f70,%dh              # gen=26 22 36  dis=26 22 36 90 90
    and    %es:-0x6f70,%bh              # gen=26 22 3e  dis=26 22 3e 90 90
    es and %ax,%ax                      # gen=26 23 c0  dis=26 23 c0
    and    %es:-0x6f70,%ax              # gen=26 23 06  dis=26 23 06 90 90
    es and %ax,%cx                      # gen=26 23 c8  dis=26 23 c8
    es and %ax,%dx                      # gen=26 23 d0  dis=26 23 d0
    es and %ax,%bx                      # gen=26 23 d8  dis=26 23 d8
    es and %ax,%sp                      # gen=26 23 e0  dis=26 23 e0
    es and %ax,%bp                      # gen=26 23 e8  dis=26 23 e8
    es and %ax,%si                      # gen=26 23 f0  dis=26 23 f0
    es and %ax,%di                      # gen=26 23 f8  dis=26 23 f8
    and    %es:-0x6f70,%cx              # gen=26 23 0e  dis=26 23 0e 90 90
    and    %es:-0x6f70,%dx              # gen=26 23 16  dis=26 23 16 90 90
    and    %es:-0x6f70,%bx              # gen=26 23 1e  dis=26 23 1e 90 90
    and    %es:-0x6f70,%sp              # gen=26 23 26  dis=26 23 26 90 90
    and    %es:-0x6f70,%bp              # gen=26 23 2e  dis=26 23 2e 90 90
    and    %es:-0x6f70,%si              # gen=26 23 36  dis=26 23 36 90 90
    and    %es:-0x6f70,%di              # gen=26 23 3e  dis=26 23 3e 90 90
    es and $0xc0,%al                    # gen=26 24 c0  dis=26 24 c0
    es and $0x6,%al                     # gen=26 24 06  dis=26 24 06
    es and $0x90c0,%ax                  # gen=26 25 c0  dis=26 25 c0 90
    es and $0x9006,%ax                  # gen=26 25 06  dis=26 25 06 90
    .byte 0x26,0x26,0xc0            # fallback; gen=26 26 c0
    .byte 0x26,0x26,0x06            # fallback; gen=26 26 06
    es daa                              # gen=26 27 c0  dis=26 27
    es daa                              # gen=26 27 06  dis=26 27
    es sub %al,%al                      # gen=26 28 c0  dis=26 28 c0
    sub    %al,%es:-0x6f70              # gen=26 28 06  dis=26 28 06 90 90
    es sub %cl,%al                      # gen=26 28 c8  dis=26 28 c8
    es sub %dl,%al                      # gen=26 28 d0  dis=26 28 d0
    es sub %bl,%al                      # gen=26 28 d8  dis=26 28 d8
    es sub %ah,%al                      # gen=26 28 e0  dis=26 28 e0
    es sub %ch,%al                      # gen=26 28 e8  dis=26 28 e8
    es sub %dh,%al                      # gen=26 28 f0  dis=26 28 f0
    es sub %bh,%al                      # gen=26 28 f8  dis=26 28 f8
    sub    %cl,%es:-0x6f70              # gen=26 28 0e  dis=26 28 0e 90 90
    sub    %dl,%es:-0x6f70              # gen=26 28 16  dis=26 28 16 90 90
    sub    %bl,%es:-0x6f70              # gen=26 28 1e  dis=26 28 1e 90 90
    sub    %ah,%es:-0x6f70              # gen=26 28 26  dis=26 28 26 90 90
    sub    %ch,%es:-0x6f70              # gen=26 28 2e  dis=26 28 2e 90 90
    sub    %dh,%es:-0x6f70              # gen=26 28 36  dis=26 28 36 90 90
    sub    %bh,%es:-0x6f70              # gen=26 28 3e  dis=26 28 3e 90 90
    es sub %ax,%ax                      # gen=26 29 c0  dis=26 29 c0
    sub    %ax,%es:-0x6f70              # gen=26 29 06  dis=26 29 06 90 90
    es sub %cx,%ax                      # gen=26 29 c8  dis=26 29 c8
    es sub %dx,%ax                      # gen=26 29 d0  dis=26 29 d0
    es sub %bx,%ax                      # gen=26 29 d8  dis=26 29 d8
    es sub %sp,%ax                      # gen=26 29 e0  dis=26 29 e0
    es sub %bp,%ax                      # gen=26 29 e8  dis=26 29 e8
    es sub %si,%ax                      # gen=26 29 f0  dis=26 29 f0
    es sub %di,%ax                      # gen=26 29 f8  dis=26 29 f8
    sub    %cx,%es:-0x6f70              # gen=26 29 0e  dis=26 29 0e 90 90
    sub    %dx,%es:-0x6f70              # gen=26 29 16  dis=26 29 16 90 90
    sub    %bx,%es:-0x6f70              # gen=26 29 1e  dis=26 29 1e 90 90
    sub    %sp,%es:-0x6f70              # gen=26 29 26  dis=26 29 26 90 90
    sub    %bp,%es:-0x6f70              # gen=26 29 2e  dis=26 29 2e 90 90
    sub    %si,%es:-0x6f70              # gen=26 29 36  dis=26 29 36 90 90
    sub    %di,%es:-0x6f70              # gen=26 29 3e  dis=26 29 3e 90 90
    es sub %al,%al                      # gen=26 2a c0  dis=26 2a c0
    sub    %es:-0x6f70,%al              # gen=26 2a 06  dis=26 2a 06 90 90
    es sub %al,%cl                      # gen=26 2a c8  dis=26 2a c8
    es sub %al,%dl                      # gen=26 2a d0  dis=26 2a d0
    es sub %al,%bl                      # gen=26 2a d8  dis=26 2a d8
    es sub %al,%ah                      # gen=26 2a e0  dis=26 2a e0
    es sub %al,%ch                      # gen=26 2a e8  dis=26 2a e8
    es sub %al,%dh                      # gen=26 2a f0  dis=26 2a f0
    es sub %al,%bh                      # gen=26 2a f8  dis=26 2a f8
    sub    %es:-0x6f70,%cl              # gen=26 2a 0e  dis=26 2a 0e 90 90
    sub    %es:-0x6f70,%dl              # gen=26 2a 16  dis=26 2a 16 90 90
    sub    %es:-0x6f70,%bl              # gen=26 2a 1e  dis=26 2a 1e 90 90
    sub    %es:-0x6f70,%ah              # gen=26 2a 26  dis=26 2a 26 90 90
    sub    %es:-0x6f70,%ch              # gen=26 2a 2e  dis=26 2a 2e 90 90
    sub    %es:-0x6f70,%dh              # gen=26 2a 36  dis=26 2a 36 90 90
    sub    %es:-0x6f70,%bh              # gen=26 2a 3e  dis=26 2a 3e 90 90
    es sub %ax,%ax                      # gen=26 2b c0  dis=26 2b c0
    sub    %es:-0x6f70,%ax              # gen=26 2b 06  dis=26 2b 06 90 90
    es sub %ax,%cx                      # gen=26 2b c8  dis=26 2b c8
    es sub %ax,%dx                      # gen=26 2b d0  dis=26 2b d0
    es sub %ax,%bx                      # gen=26 2b d8  dis=26 2b d8
    es sub %ax,%sp                      # gen=26 2b e0  dis=26 2b e0
    es sub %ax,%bp                      # gen=26 2b e8  dis=26 2b e8
    es sub %ax,%si                      # gen=26 2b f0  dis=26 2b f0
    es sub %ax,%di                      # gen=26 2b f8  dis=26 2b f8
    sub    %es:-0x6f70,%cx              # gen=26 2b 0e  dis=26 2b 0e 90 90
    sub    %es:-0x6f70,%dx              # gen=26 2b 16  dis=26 2b 16 90 90
    sub    %es:-0x6f70,%bx              # gen=26 2b 1e  dis=26 2b 1e 90 90
    sub    %es:-0x6f70,%sp              # gen=26 2b 26  dis=26 2b 26 90 90
    sub    %es:-0x6f70,%bp              # gen=26 2b 2e  dis=26 2b 2e 90 90
    sub    %es:-0x6f70,%si              # gen=26 2b 36  dis=26 2b 36 90 90
    sub    %es:-0x6f70,%di              # gen=26 2b 3e  dis=26 2b 3e 90 90
    es sub $0xc0,%al                    # gen=26 2c c0  dis=26 2c c0
    es sub $0x6,%al                     # gen=26 2c 06  dis=26 2c 06
    es sub $0x90c0,%ax                  # gen=26 2d c0  dis=26 2d c0 90
    es sub $0x9006,%ax                  # gen=26 2d 06  dis=26 2d 06 90
    .byte 0x26,0x2e,0xc0            # fallback; gen=26 2e c0
    .byte 0x26,0x2e,0x06            # fallback; gen=26 2e 06
    es das                              # gen=26 2f c0  dis=26 2f
    es das                              # gen=26 2f 06  dis=26 2f
    es xor %al,%al                      # gen=26 30 c0  dis=26 30 c0
    xor    %al,%es:-0x6f70              # gen=26 30 06  dis=26 30 06 90 90
    es xor %cl,%al                      # gen=26 30 c8  dis=26 30 c8
    es xor %dl,%al                      # gen=26 30 d0  dis=26 30 d0
    es xor %bl,%al                      # gen=26 30 d8  dis=26 30 d8
    es xor %ah,%al                      # gen=26 30 e0  dis=26 30 e0
    es xor %ch,%al                      # gen=26 30 e8  dis=26 30 e8
    es xor %dh,%al                      # gen=26 30 f0  dis=26 30 f0
    es xor %bh,%al                      # gen=26 30 f8  dis=26 30 f8
    xor    %cl,%es:-0x6f70              # gen=26 30 0e  dis=26 30 0e 90 90
    xor    %dl,%es:-0x6f70              # gen=26 30 16  dis=26 30 16 90 90
    xor    %bl,%es:-0x6f70              # gen=26 30 1e  dis=26 30 1e 90 90
    xor    %ah,%es:-0x6f70              # gen=26 30 26  dis=26 30 26 90 90
    xor    %ch,%es:-0x6f70              # gen=26 30 2e  dis=26 30 2e 90 90
    xor    %dh,%es:-0x6f70              # gen=26 30 36  dis=26 30 36 90 90
    xor    %bh,%es:-0x6f70              # gen=26 30 3e  dis=26 30 3e 90 90
    es xor %ax,%ax                      # gen=26 31 c0  dis=26 31 c0
    xor    %ax,%es:-0x6f70              # gen=26 31 06  dis=26 31 06 90 90
    es xor %cx,%ax                      # gen=26 31 c8  dis=26 31 c8
    es xor %dx,%ax                      # gen=26 31 d0  dis=26 31 d0
    es xor %bx,%ax                      # gen=26 31 d8  dis=26 31 d8
    es xor %sp,%ax                      # gen=26 31 e0  dis=26 31 e0
    es xor %bp,%ax                      # gen=26 31 e8  dis=26 31 e8
    es xor %si,%ax                      # gen=26 31 f0  dis=26 31 f0
    es xor %di,%ax                      # gen=26 31 f8  dis=26 31 f8
    xor    %cx,%es:-0x6f70              # gen=26 31 0e  dis=26 31 0e 90 90
    xor    %dx,%es:-0x6f70              # gen=26 31 16  dis=26 31 16 90 90
    xor    %bx,%es:-0x6f70              # gen=26 31 1e  dis=26 31 1e 90 90
    xor    %sp,%es:-0x6f70              # gen=26 31 26  dis=26 31 26 90 90
    xor    %bp,%es:-0x6f70              # gen=26 31 2e  dis=26 31 2e 90 90
    xor    %si,%es:-0x6f70              # gen=26 31 36  dis=26 31 36 90 90
    xor    %di,%es:-0x6f70              # gen=26 31 3e  dis=26 31 3e 90 90
    es xor %al,%al                      # gen=26 32 c0  dis=26 32 c0
    xor    %es:-0x6f70,%al              # gen=26 32 06  dis=26 32 06 90 90
    es xor %al,%cl                      # gen=26 32 c8  dis=26 32 c8
    es xor %al,%dl                      # gen=26 32 d0  dis=26 32 d0
    es xor %al,%bl                      # gen=26 32 d8  dis=26 32 d8
    es xor %al,%ah                      # gen=26 32 e0  dis=26 32 e0
    es xor %al,%ch                      # gen=26 32 e8  dis=26 32 e8
    es xor %al,%dh                      # gen=26 32 f0  dis=26 32 f0
    es xor %al,%bh                      # gen=26 32 f8  dis=26 32 f8
    xor    %es:-0x6f70,%cl              # gen=26 32 0e  dis=26 32 0e 90 90
    xor    %es:-0x6f70,%dl              # gen=26 32 16  dis=26 32 16 90 90
    xor    %es:-0x6f70,%bl              # gen=26 32 1e  dis=26 32 1e 90 90
    xor    %es:-0x6f70,%ah              # gen=26 32 26  dis=26 32 26 90 90
    xor    %es:-0x6f70,%ch              # gen=26 32 2e  dis=26 32 2e 90 90
    xor    %es:-0x6f70,%dh              # gen=26 32 36  dis=26 32 36 90 90
    xor    %es:-0x6f70,%bh              # gen=26 32 3e  dis=26 32 3e 90 90
    es xor %ax,%ax                      # gen=26 33 c0  dis=26 33 c0
    xor    %es:-0x6f70,%ax              # gen=26 33 06  dis=26 33 06 90 90
    es xor %ax,%cx                      # gen=26 33 c8  dis=26 33 c8
    es xor %ax,%dx                      # gen=26 33 d0  dis=26 33 d0
    es xor %ax,%bx                      # gen=26 33 d8  dis=26 33 d8
    es xor %ax,%sp                      # gen=26 33 e0  dis=26 33 e0
    es xor %ax,%bp                      # gen=26 33 e8  dis=26 33 e8
    es xor %ax,%si                      # gen=26 33 f0  dis=26 33 f0
    es xor %ax,%di                      # gen=26 33 f8  dis=26 33 f8
    xor    %es:-0x6f70,%cx              # gen=26 33 0e  dis=26 33 0e 90 90
    xor    %es:-0x6f70,%dx              # gen=26 33 16  dis=26 33 16 90 90
    xor    %es:-0x6f70,%bx              # gen=26 33 1e  dis=26 33 1e 90 90
    xor    %es:-0x6f70,%sp              # gen=26 33 26  dis=26 33 26 90 90
    xor    %es:-0x6f70,%bp              # gen=26 33 2e  dis=26 33 2e 90 90
    xor    %es:-0x6f70,%si              # gen=26 33 36  dis=26 33 36 90 90
    xor    %es:-0x6f70,%di              # gen=26 33 3e  dis=26 33 3e 90 90
    es xor $0xc0,%al                    # gen=26 34 c0  dis=26 34 c0
    es xor $0x6,%al                     # gen=26 34 06  dis=26 34 06
    es xor $0x90c0,%ax                  # gen=26 35 c0  dis=26 35 c0 90
    es xor $0x9006,%ax                  # gen=26 35 06  dis=26 35 06 90
    .byte 0x26,0x36,0xc0            # fallback; gen=26 36 c0
    .byte 0x26,0x36,0x06            # fallback; gen=26 36 06
    es aaa                              # gen=26 37 c0  dis=26 37
    es aaa                              # gen=26 37 06  dis=26 37
    es cmp %al,%al                      # gen=26 38 c0  dis=26 38 c0
    cmp    %al,%es:-0x6f70              # gen=26 38 06  dis=26 38 06 90 90
    es cmp %cl,%al                      # gen=26 38 c8  dis=26 38 c8
    es cmp %dl,%al                      # gen=26 38 d0  dis=26 38 d0
    es cmp %bl,%al                      # gen=26 38 d8  dis=26 38 d8
    es cmp %ah,%al                      # gen=26 38 e0  dis=26 38 e0
    es cmp %ch,%al                      # gen=26 38 e8  dis=26 38 e8
    es cmp %dh,%al                      # gen=26 38 f0  dis=26 38 f0
    es cmp %bh,%al                      # gen=26 38 f8  dis=26 38 f8
    cmp    %cl,%es:-0x6f70              # gen=26 38 0e  dis=26 38 0e 90 90
    cmp    %dl,%es:-0x6f70              # gen=26 38 16  dis=26 38 16 90 90
    cmp    %bl,%es:-0x6f70              # gen=26 38 1e  dis=26 38 1e 90 90
    cmp    %ah,%es:-0x6f70              # gen=26 38 26  dis=26 38 26 90 90
    cmp    %ch,%es:-0x6f70              # gen=26 38 2e  dis=26 38 2e 90 90
    cmp    %dh,%es:-0x6f70              # gen=26 38 36  dis=26 38 36 90 90
    cmp    %bh,%es:-0x6f70              # gen=26 38 3e  dis=26 38 3e 90 90
    es cmp %ax,%ax                      # gen=26 39 c0  dis=26 39 c0
    cmp    %ax,%es:-0x6f70              # gen=26 39 06  dis=26 39 06 90 90
    es cmp %cx,%ax                      # gen=26 39 c8  dis=26 39 c8
    es cmp %dx,%ax                      # gen=26 39 d0  dis=26 39 d0
    es cmp %bx,%ax                      # gen=26 39 d8  dis=26 39 d8
    es cmp %sp,%ax                      # gen=26 39 e0  dis=26 39 e0
    es cmp %bp,%ax                      # gen=26 39 e8  dis=26 39 e8
    es cmp %si,%ax                      # gen=26 39 f0  dis=26 39 f0
    es cmp %di,%ax                      # gen=26 39 f8  dis=26 39 f8
    cmp    %cx,%es:-0x6f70              # gen=26 39 0e  dis=26 39 0e 90 90
    cmp    %dx,%es:-0x6f70              # gen=26 39 16  dis=26 39 16 90 90
    cmp    %bx,%es:-0x6f70              # gen=26 39 1e  dis=26 39 1e 90 90
    cmp    %sp,%es:-0x6f70              # gen=26 39 26  dis=26 39 26 90 90
    cmp    %bp,%es:-0x6f70              # gen=26 39 2e  dis=26 39 2e 90 90
    cmp    %si,%es:-0x6f70              # gen=26 39 36  dis=26 39 36 90 90
    cmp    %di,%es:-0x6f70              # gen=26 39 3e  dis=26 39 3e 90 90
    es cmp %al,%al                      # gen=26 3a c0  dis=26 3a c0
    cmp    %es:-0x6f70,%al              # gen=26 3a 06  dis=26 3a 06 90 90
    es cmp %al,%cl                      # gen=26 3a c8  dis=26 3a c8
    es cmp %al,%dl                      # gen=26 3a d0  dis=26 3a d0
    es cmp %al,%bl                      # gen=26 3a d8  dis=26 3a d8
    es cmp %al,%ah                      # gen=26 3a e0  dis=26 3a e0
    es cmp %al,%ch                      # gen=26 3a e8  dis=26 3a e8
    es cmp %al,%dh                      # gen=26 3a f0  dis=26 3a f0
    es cmp %al,%bh                      # gen=26 3a f8  dis=26 3a f8
    cmp    %es:-0x6f70,%cl              # gen=26 3a 0e  dis=26 3a 0e 90 90
    cmp    %es:-0x6f70,%dl              # gen=26 3a 16  dis=26 3a 16 90 90
    cmp    %es:-0x6f70,%bl              # gen=26 3a 1e  dis=26 3a 1e 90 90
    cmp    %es:-0x6f70,%ah              # gen=26 3a 26  dis=26 3a 26 90 90
    cmp    %es:-0x6f70,%ch              # gen=26 3a 2e  dis=26 3a 2e 90 90
    cmp    %es:-0x6f70,%dh              # gen=26 3a 36  dis=26 3a 36 90 90
    cmp    %es:-0x6f70,%bh              # gen=26 3a 3e  dis=26 3a 3e 90 90
    es cmp %ax,%ax                      # gen=26 3b c0  dis=26 3b c0
    cmp    %es:-0x6f70,%ax              # gen=26 3b 06  dis=26 3b 06 90 90
    es cmp %ax,%cx                      # gen=26 3b c8  dis=26 3b c8
    es cmp %ax,%dx                      # gen=26 3b d0  dis=26 3b d0
    es cmp %ax,%bx                      # gen=26 3b d8  dis=26 3b d8
    es cmp %ax,%sp                      # gen=26 3b e0  dis=26 3b e0
    es cmp %ax,%bp                      # gen=26 3b e8  dis=26 3b e8
    es cmp %ax,%si                      # gen=26 3b f0  dis=26 3b f0
    es cmp %ax,%di                      # gen=26 3b f8  dis=26 3b f8
    cmp    %es:-0x6f70,%cx              # gen=26 3b 0e  dis=26 3b 0e 90 90
    cmp    %es:-0x6f70,%dx              # gen=26 3b 16  dis=26 3b 16 90 90
    cmp    %es:-0x6f70,%bx              # gen=26 3b 1e  dis=26 3b 1e 90 90
    cmp    %es:-0x6f70,%sp              # gen=26 3b 26  dis=26 3b 26 90 90
    cmp    %es:-0x6f70,%bp              # gen=26 3b 2e  dis=26 3b 2e 90 90
    cmp    %es:-0x6f70,%si              # gen=26 3b 36  dis=26 3b 36 90 90
    cmp    %es:-0x6f70,%di              # gen=26 3b 3e  dis=26 3b 3e 90 90
    es cmp $0xc0,%al                    # gen=26 3c c0  dis=26 3c c0
    es cmp $0x6,%al                     # gen=26 3c 06  dis=26 3c 06
    es cmp $0x90c0,%ax                  # gen=26 3d c0  dis=26 3d c0 90
    es cmp $0x9006,%ax                  # gen=26 3d 06  dis=26 3d 06 90
    .byte 0x26,0x3e,0xc0            # fallback; gen=26 3e c0
    .byte 0x26,0x3e,0x06            # fallback; gen=26 3e 06
    es aas                              # gen=26 3f c0  dis=26 3f
    es aas                              # gen=26 3f 06  dis=26 3f
    es inc %ax                          # gen=26 40 c0  dis=26 40
    es inc %ax                          # gen=26 40 06  dis=26 40
    es inc %cx                          # gen=26 41 c0  dis=26 41
    es inc %cx                          # gen=26 41 06  dis=26 41
    es inc %dx                          # gen=26 42 c0  dis=26 42
    es inc %dx                          # gen=26 42 06  dis=26 42
    es inc %bx                          # gen=26 43 c0  dis=26 43
    es inc %bx                          # gen=26 43 06  dis=26 43
    es inc %sp                          # gen=26 44 c0  dis=26 44
    es inc %sp                          # gen=26 44 06  dis=26 44
    es inc %bp                          # gen=26 45 c0  dis=26 45
    es inc %bp                          # gen=26 45 06  dis=26 45
    es inc %si                          # gen=26 46 c0  dis=26 46
    es inc %si                          # gen=26 46 06  dis=26 46
    es inc %di                          # gen=26 47 c0  dis=26 47
    es inc %di                          # gen=26 47 06  dis=26 47
    es dec %ax                          # gen=26 48 c0  dis=26 48
    es dec %ax                          # gen=26 48 06  dis=26 48
    es dec %cx                          # gen=26 49 c0  dis=26 49
    es dec %cx                          # gen=26 49 06  dis=26 49
    es dec %dx                          # gen=26 4a c0  dis=26 4a
    es dec %dx                          # gen=26 4a 06  dis=26 4a
    es dec %bx                          # gen=26 4b c0  dis=26 4b
    es dec %bx                          # gen=26 4b 06  dis=26 4b
    es dec %sp                          # gen=26 4c c0  dis=26 4c
    es dec %sp                          # gen=26 4c 06  dis=26 4c
    es dec %bp                          # gen=26 4d c0  dis=26 4d
    es dec %bp                          # gen=26 4d 06  dis=26 4d
    es dec %si                          # gen=26 4e c0  dis=26 4e
    es dec %si                          # gen=26 4e 06  dis=26 4e
    es dec %di                          # gen=26 4f c0  dis=26 4f
    es dec %di                          # gen=26 4f 06  dis=26 4f
    es push %ax                         # gen=26 50 c0  dis=26 50
    es push %ax                         # gen=26 50 06  dis=26 50
    es push %cx                         # gen=26 51 c0  dis=26 51
    es push %cx                         # gen=26 51 06  dis=26 51
    es push %dx                         # gen=26 52 c0  dis=26 52
    es push %dx                         # gen=26 52 06  dis=26 52
    es push %bx                         # gen=26 53 c0  dis=26 53
    es push %bx                         # gen=26 53 06  dis=26 53
    es push %sp                         # gen=26 54 c0  dis=26 54
    es push %sp                         # gen=26 54 06  dis=26 54
    es push %bp                         # gen=26 55 c0  dis=26 55
    es push %bp                         # gen=26 55 06  dis=26 55
    es push %si                         # gen=26 56 c0  dis=26 56
    es push %si                         # gen=26 56 06  dis=26 56
    es push %di                         # gen=26 57 c0  dis=26 57
    es push %di                         # gen=26 57 06  dis=26 57
    es pop %ax                          # gen=26 58 c0  dis=26 58
    es pop %ax                          # gen=26 58 06  dis=26 58
    es pop %cx                          # gen=26 59 c0  dis=26 59
    es pop %cx                          # gen=26 59 06  dis=26 59
    es pop %dx                          # gen=26 5a c0  dis=26 5a
    es pop %dx                          # gen=26 5a 06  dis=26 5a
    es pop %bx                          # gen=26 5b c0  dis=26 5b
    es pop %bx                          # gen=26 5b 06  dis=26 5b
    es pop %sp                          # gen=26 5c c0  dis=26 5c
    es pop %sp                          # gen=26 5c 06  dis=26 5c
    es pop %bp                          # gen=26 5d c0  dis=26 5d
    es pop %bp                          # gen=26 5d 06  dis=26 5d
    es pop %si                          # gen=26 5e c0  dis=26 5e
    es pop %si                          # gen=26 5e 06  dis=26 5e
    es pop %di                          # gen=26 5f c0  dis=26 5f
    es pop %di                          # gen=26 5f 06  dis=26 5f
    .byte 0x26,0x60,0xc0            # fallback; gen=26 60 c0
    .byte 0x26,0x60,0x06            # fallback; gen=26 60 06
    .byte 0x26,0x61,0xc0            # fallback; gen=26 61 c0
    .byte 0x26,0x61,0x06            # fallback; gen=26 61 06
    .byte 0x26,0x62,0xc0            # fallback; gen=26 62 c0
    .byte 0x26,0x62,0x06            # fallback; gen=26 62 06
    .byte 0x26,0x62,0xc8            # fallback; gen=26 62 c8
    .byte 0x26,0x62,0xd0            # fallback; gen=26 62 d0
    .byte 0x26,0x62,0xd8            # fallback; gen=26 62 d8
    .byte 0x26,0x62,0xe0            # fallback; gen=26 62 e0
    .byte 0x26,0x62,0xe8            # fallback; gen=26 62 e8
    .byte 0x26,0x62,0xf0            # fallback; gen=26 62 f0
    .byte 0x26,0x62,0xf8            # fallback; gen=26 62 f8
    .byte 0x26,0x62,0x0e            # fallback; gen=26 62 0e
    .byte 0x26,0x62,0x16            # fallback; gen=26 62 16
    .byte 0x26,0x62,0x1e            # fallback; gen=26 62 1e
    .byte 0x26,0x62,0x26            # fallback; gen=26 62 26
    .byte 0x26,0x62,0x2e            # fallback; gen=26 62 2e
    .byte 0x26,0x62,0x36            # fallback; gen=26 62 36
    .byte 0x26,0x62,0x3e            # fallback; gen=26 62 3e
    .byte 0x26,0x63,0xc0            # fallback; gen=26 63 c0
    .byte 0x26,0x63,0x06            # fallback; gen=26 63 06
    .byte 0x26,0x63,0xc8            # fallback; gen=26 63 c8
    .byte 0x26,0x63,0xd0            # fallback; gen=26 63 d0
    .byte 0x26,0x63,0xd8            # fallback; gen=26 63 d8
    .byte 0x26,0x63,0xe0            # fallback; gen=26 63 e0
    .byte 0x26,0x63,0xe8            # fallback; gen=26 63 e8
    .byte 0x26,0x63,0xf0            # fallback; gen=26 63 f0
    .byte 0x26,0x63,0xf8            # fallback; gen=26 63 f8
    .byte 0x26,0x63,0x0e            # fallback; gen=26 63 0e
    .byte 0x26,0x63,0x16            # fallback; gen=26 63 16
    .byte 0x26,0x63,0x1e            # fallback; gen=26 63 1e
    .byte 0x26,0x63,0x26            # fallback; gen=26 63 26
    .byte 0x26,0x63,0x2e            # fallback; gen=26 63 2e
    .byte 0x26,0x63,0x36            # fallback; gen=26 63 36
    .byte 0x26,0x63,0x3e            # fallback; gen=26 63 3e
    .byte 0x26,0x64,0xc0            # fallback; gen=26 64 c0
    .byte 0x26,0x64,0x06            # fallback; gen=26 64 06
    .byte 0x26,0x65,0xc0            # fallback; gen=26 65 c0
    .byte 0x26,0x65,0x06            # fallback; gen=26 65 06
    .byte 0x26,0x66,0xc0            # fallback; gen=26 66 c0
    .byte 0x26,0x66,0x06            # fallback; gen=26 66 06
    .byte 0x26,0x66,0xc8            # fallback; gen=26 66 c8
    .byte 0x26,0x66,0xd0            # fallback; gen=26 66 d0
    .byte 0x26,0x66,0xd8            # fallback; gen=26 66 d8
    .byte 0x26,0x66,0xe0            # fallback; gen=26 66 e0
    .byte 0x26,0x66,0xe8            # fallback; gen=26 66 e8
    .byte 0x26,0x66,0xf0            # fallback; gen=26 66 f0
    .byte 0x26,0x66,0xf8            # fallback; gen=26 66 f8
    .byte 0x26,0x66,0x0e            # fallback; gen=26 66 0e
    .byte 0x26,0x66,0x16            # fallback; gen=26 66 16
    .byte 0x26,0x66,0x1e            # fallback; gen=26 66 1e
    .byte 0x26,0x66,0x26            # fallback; gen=26 66 26
    .byte 0x26,0x66,0x2e            # fallback; gen=26 66 2e
    .byte 0x26,0x66,0x36            # fallback; gen=26 66 36
    .byte 0x26,0x66,0x3e            # fallback; gen=26 66 3e
    .byte 0x26,0x67,0xc0            # fallback; gen=26 67 c0
    .byte 0x26,0x67,0x06            # fallback; gen=26 67 06
    .byte 0x26,0x67,0xc8            # fallback; gen=26 67 c8
    .byte 0x26,0x67,0xd0            # fallback; gen=26 67 d0
    .byte 0x26,0x67,0xd8            # fallback; gen=26 67 d8
    .byte 0x26,0x67,0xe0            # fallback; gen=26 67 e0
    .byte 0x26,0x67,0xe8            # fallback; gen=26 67 e8
    .byte 0x26,0x67,0xf0            # fallback; gen=26 67 f0
    .byte 0x26,0x67,0xf8            # fallback; gen=26 67 f8
    .byte 0x26,0x67,0x0e            # fallback; gen=26 67 0e
    .byte 0x26,0x67,0x16            # fallback; gen=26 67 16
    .byte 0x26,0x67,0x1e            # fallback; gen=26 67 1e
    .byte 0x26,0x67,0x26            # fallback; gen=26 67 26
    .byte 0x26,0x67,0x2e            # fallback; gen=26 67 2e
    .byte 0x26,0x67,0x36            # fallback; gen=26 67 36
    .byte 0x26,0x67,0x3e            # fallback; gen=26 67 3e
    .byte 0x26,0x68,0xc0            # fallback; gen=26 68 c0
    .byte 0x26,0x68,0x06            # fallback; gen=26 68 06
    .byte 0x26,0x69,0xc0            # fallback; gen=26 69 c0
    .byte 0x26,0x69,0x06            # fallback; gen=26 69 06
    .byte 0x26,0x69,0xc8            # fallback; gen=26 69 c8
    .byte 0x26,0x69,0xd0            # fallback; gen=26 69 d0
    .byte 0x26,0x69,0xd8            # fallback; gen=26 69 d8
    .byte 0x26,0x69,0xe0            # fallback; gen=26 69 e0
    .byte 0x26,0x69,0xe8            # fallback; gen=26 69 e8
    .byte 0x26,0x69,0xf0            # fallback; gen=26 69 f0
    .byte 0x26,0x69,0xf8            # fallback; gen=26 69 f8
    .byte 0x26,0x69,0x0e            # fallback; gen=26 69 0e
    .byte 0x26,0x69,0x16            # fallback; gen=26 69 16
    .byte 0x26,0x69,0x1e            # fallback; gen=26 69 1e
    .byte 0x26,0x69,0x26            # fallback; gen=26 69 26
    .byte 0x26,0x69,0x2e            # fallback; gen=26 69 2e
    .byte 0x26,0x69,0x36            # fallback; gen=26 69 36
    .byte 0x26,0x69,0x3e            # fallback; gen=26 69 3e
    .byte 0x26,0x6a,0xc0            # fallback; gen=26 6a c0
    .byte 0x26,0x6a,0x06            # fallback; gen=26 6a 06
    .byte 0x26,0x6b,0xc0            # fallback; gen=26 6b c0
    .byte 0x26,0x6b,0x06            # fallback; gen=26 6b 06
    .byte 0x26,0x6b,0xc8            # fallback; gen=26 6b c8
    .byte 0x26,0x6b,0xd0            # fallback; gen=26 6b d0
    .byte 0x26,0x6b,0xd8            # fallback; gen=26 6b d8
    .byte 0x26,0x6b,0xe0            # fallback; gen=26 6b e0
    .byte 0x26,0x6b,0xe8            # fallback; gen=26 6b e8
    .byte 0x26,0x6b,0xf0            # fallback; gen=26 6b f0
    .byte 0x26,0x6b,0xf8            # fallback; gen=26 6b f8
    .byte 0x26,0x6b,0x0e            # fallback; gen=26 6b 0e
    .byte 0x26,0x6b,0x16            # fallback; gen=26 6b 16
    .byte 0x26,0x6b,0x1e            # fallback; gen=26 6b 1e
    .byte 0x26,0x6b,0x26            # fallback; gen=26 6b 26
    .byte 0x26,0x6b,0x2e            # fallback; gen=26 6b 2e
    .byte 0x26,0x6b,0x36            # fallback; gen=26 6b 36
    .byte 0x26,0x6b,0x3e            # fallback; gen=26 6b 3e
    .byte 0x26,0x6c,0xc0            # fallback; gen=26 6c c0
    .byte 0x26,0x6c,0x06            # fallback; gen=26 6c 06
    .byte 0x26,0x6d,0xc0            # fallback; gen=26 6d c0
    .byte 0x26,0x6d,0x06            # fallback; gen=26 6d 06
    .byte 0x26,0x6e,0xc0            # fallback; gen=26 6e c0
    .byte 0x26,0x6e,0x06            # fallback; gen=26 6e 06
    .byte 0x26,0x6f,0xc0            # fallback; gen=26 6f c0
    .byte 0x26,0x6f,0x06            # fallback; gen=26 6f 06
    .byte 0x26,0x70,0xc0            # fallback; gen=26 70 c0
    .byte 0x26,0x70,0x06            # fallback; gen=26 70 06
    .byte 0x26,0x71,0xc0            # fallback; gen=26 71 c0
    .byte 0x26,0x71,0x06            # fallback; gen=26 71 06
    .byte 0x26,0x72,0xc0            # fallback; gen=26 72 c0
    .byte 0x26,0x72,0x06            # fallback; gen=26 72 06
    .byte 0x26,0x73,0xc0            # fallback; gen=26 73 c0
    .byte 0x26,0x73,0x06            # fallback; gen=26 73 06
    .byte 0x26,0x74,0xc0            # fallback; gen=26 74 c0
    .byte 0x26,0x74,0x06            # fallback; gen=26 74 06
    .byte 0x26,0x75,0xc0            # fallback; gen=26 75 c0
    .byte 0x26,0x75,0x06            # fallback; gen=26 75 06
    .byte 0x26,0x76,0xc0            # fallback; gen=26 76 c0
    .byte 0x26,0x76,0x06            # fallback; gen=26 76 06
    .byte 0x26,0x77,0xc0            # fallback; gen=26 77 c0
    .byte 0x26,0x77,0x06            # fallback; gen=26 77 06
    .byte 0x26,0x78,0xc0            # fallback; gen=26 78 c0
    .byte 0x26,0x78,0x06            # fallback; gen=26 78 06
    .byte 0x26,0x79,0xc0            # fallback; gen=26 79 c0
    .byte 0x26,0x79,0x06            # fallback; gen=26 79 06
    .byte 0x26,0x7a,0xc0            # fallback; gen=26 7a c0
    .byte 0x26,0x7a,0x06            # fallback; gen=26 7a 06
    .byte 0x26,0x7b,0xc0            # fallback; gen=26 7b c0
    .byte 0x26,0x7b,0x06            # fallback; gen=26 7b 06
    .byte 0x26,0x7c,0xc0            # fallback; gen=26 7c c0
    .byte 0x26,0x7c,0x06            # fallback; gen=26 7c 06
    .byte 0x26,0x7d,0xc0            # fallback; gen=26 7d c0
    .byte 0x26,0x7d,0x06            # fallback; gen=26 7d 06
    .byte 0x26,0x7e,0xc0            # fallback; gen=26 7e c0
    .byte 0x26,0x7e,0x06            # fallback; gen=26 7e 06
    .byte 0x26,0x7f,0xc0            # fallback; gen=26 7f c0
    .byte 0x26,0x7f,0x06            # fallback; gen=26 7f 06
    es add $0x90,%al                    # gen=26 80 c0  dis=26 80 c0 90
    addb   $0x90,%es:-0x6f70            # gen=26 80 06  dis=26 80 06 90 90 90
    es or  $0x90,%al                    # gen=26 80 c8  dis=26 80 c8 90
    es adc $0x90,%al                    # gen=26 80 d0  dis=26 80 d0 90
    es sbb $0x90,%al                    # gen=26 80 d8  dis=26 80 d8 90
    es and $0x90,%al                    # gen=26 80 e0  dis=26 80 e0 90
    es sub $0x90,%al                    # gen=26 80 e8  dis=26 80 e8 90
    es xor $0x90,%al                    # gen=26 80 f0  dis=26 80 f0 90
    es cmp $0x90,%al                    # gen=26 80 f8  dis=26 80 f8 90
    orb    $0x90,%es:-0x6f70            # gen=26 80 0e  dis=26 80 0e 90 90 90
    adcb   $0x90,%es:-0x6f70            # gen=26 80 16  dis=26 80 16 90 90 90
    sbbb   $0x90,%es:-0x6f70            # gen=26 80 1e  dis=26 80 1e 90 90 90
    andb   $0x90,%es:-0x6f70            # gen=26 80 26  dis=26 80 26 90 90 90
    subb   $0x90,%es:-0x6f70            # gen=26 80 2e  dis=26 80 2e 90 90 90
    xorb   $0x90,%es:-0x6f70            # gen=26 80 36  dis=26 80 36 90 90 90
    cmpb   $0x90,%es:-0x6f70            # gen=26 80 3e  dis=26 80 3e 90 90 90
    es add $0x9090,%ax                  # gen=26 81 c0  dis=26 81 c0 90 90
    addw   $0x9090,%es:-0x6f70          # gen=26 81 06  dis=26 81 06 90 90 90 90
    es or  $0x9090,%ax                  # gen=26 81 c8  dis=26 81 c8 90 90
    es adc $0x9090,%ax                  # gen=26 81 d0  dis=26 81 d0 90 90
    es sbb $0x9090,%ax                  # gen=26 81 d8  dis=26 81 d8 90 90
    es and $0x9090,%ax                  # gen=26 81 e0  dis=26 81 e0 90 90
    es sub $0x9090,%ax                  # gen=26 81 e8  dis=26 81 e8 90 90
    es xor $0x9090,%ax                  # gen=26 81 f0  dis=26 81 f0 90 90
    es cmp $0x9090,%ax                  # gen=26 81 f8  dis=26 81 f8 90 90
    orw    $0x9090,%es:-0x6f70          # gen=26 81 0e  dis=26 81 0e 90 90 90 90
    adcw   $0x9090,%es:-0x6f70          # gen=26 81 16  dis=26 81 16 90 90 90 90
    sbbw   $0x9090,%es:-0x6f70          # gen=26 81 1e  dis=26 81 1e 90 90 90 90
    andw   $0x9090,%es:-0x6f70          # gen=26 81 26  dis=26 81 26 90 90 90 90
    subw   $0x9090,%es:-0x6f70          # gen=26 81 2e  dis=26 81 2e 90 90 90 90
    xorw   $0x9090,%es:-0x6f70          # gen=26 81 36  dis=26 81 36 90 90 90 90
    cmpw   $0x9090,%es:-0x6f70          # gen=26 81 3e  dis=26 81 3e 90 90 90 90
    es add $0x90,%al                    # gen=26 82 c0  dis=26 82 c0 90
    addb   $0x90,%es:-0x6f70            # gen=26 82 06  dis=26 82 06 90 90 90
    es or  $0x90,%al                    # gen=26 82 c8  dis=26 82 c8 90
    es adc $0x90,%al                    # gen=26 82 d0  dis=26 82 d0 90
    es sbb $0x90,%al                    # gen=26 82 d8  dis=26 82 d8 90
    es and $0x90,%al                    # gen=26 82 e0  dis=26 82 e0 90
    es sub $0x90,%al                    # gen=26 82 e8  dis=26 82 e8 90
    es xor $0x90,%al                    # gen=26 82 f0  dis=26 82 f0 90
    es cmp $0x90,%al                    # gen=26 82 f8  dis=26 82 f8 90
    orb    $0x90,%es:-0x6f70            # gen=26 82 0e  dis=26 82 0e 90 90 90
    adcb   $0x90,%es:-0x6f70            # gen=26 82 16  dis=26 82 16 90 90 90
    sbbb   $0x90,%es:-0x6f70            # gen=26 82 1e  dis=26 82 1e 90 90 90
    andb   $0x90,%es:-0x6f70            # gen=26 82 26  dis=26 82 26 90 90 90
    subb   $0x90,%es:-0x6f70            # gen=26 82 2e  dis=26 82 2e 90 90 90
    xorb   $0x90,%es:-0x6f70            # gen=26 82 36  dis=26 82 36 90 90 90
    cmpb   $0x90,%es:-0x6f70            # gen=26 82 3e  dis=26 82 3e 90 90 90
    es add $0xff90,%ax                  # gen=26 83 c0  dis=26 83 c0 90
    addw   $0xff90,%es:-0x6f70          # gen=26 83 06  dis=26 83 06 90 90 90
    es or  $0xff90,%ax                  # gen=26 83 c8  dis=26 83 c8 90
    es adc $0xff90,%ax                  # gen=26 83 d0  dis=26 83 d0 90
    es sbb $0xff90,%ax                  # gen=26 83 d8  dis=26 83 d8 90
    es and $0xff90,%ax                  # gen=26 83 e0  dis=26 83 e0 90
    es sub $0xff90,%ax                  # gen=26 83 e8  dis=26 83 e8 90
    es xor $0xff90,%ax                  # gen=26 83 f0  dis=26 83 f0 90
    es cmp $0xff90,%ax                  # gen=26 83 f8  dis=26 83 f8 90
    orw    $0xff90,%es:-0x6f70          # gen=26 83 0e  dis=26 83 0e 90 90 90
    adcw   $0xff90,%es:-0x6f70          # gen=26 83 16  dis=26 83 16 90 90 90
    sbbw   $0xff90,%es:-0x6f70          # gen=26 83 1e  dis=26 83 1e 90 90 90
    andw   $0xff90,%es:-0x6f70          # gen=26 83 26  dis=26 83 26 90 90 90
    subw   $0xff90,%es:-0x6f70          # gen=26 83 2e  dis=26 83 2e 90 90 90
    xorw   $0xff90,%es:-0x6f70          # gen=26 83 36  dis=26 83 36 90 90 90
    cmpw   $0xff90,%es:-0x6f70          # gen=26 83 3e  dis=26 83 3e 90 90 90
    es test %al,%al                     # gen=26 84 c0  dis=26 84 c0
    test   %al,%es:-0x6f70              # gen=26 84 06  dis=26 84 06 90 90
    es test %cl,%al                     # gen=26 84 c8  dis=26 84 c8
    es test %dl,%al                     # gen=26 84 d0  dis=26 84 d0
    es test %bl,%al                     # gen=26 84 d8  dis=26 84 d8
    es test %ah,%al                     # gen=26 84 e0  dis=26 84 e0
    es test %ch,%al                     # gen=26 84 e8  dis=26 84 e8
    es test %dh,%al                     # gen=26 84 f0  dis=26 84 f0
    es test %bh,%al                     # gen=26 84 f8  dis=26 84 f8
    test   %cl,%es:-0x6f70              # gen=26 84 0e  dis=26 84 0e 90 90
    test   %dl,%es:-0x6f70              # gen=26 84 16  dis=26 84 16 90 90
    test   %bl,%es:-0x6f70              # gen=26 84 1e  dis=26 84 1e 90 90
    test   %ah,%es:-0x6f70              # gen=26 84 26  dis=26 84 26 90 90
    test   %ch,%es:-0x6f70              # gen=26 84 2e  dis=26 84 2e 90 90
    test   %dh,%es:-0x6f70              # gen=26 84 36  dis=26 84 36 90 90
    test   %bh,%es:-0x6f70              # gen=26 84 3e  dis=26 84 3e 90 90
    es test %ax,%ax                     # gen=26 85 c0  dis=26 85 c0
    test   %ax,%es:-0x6f70              # gen=26 85 06  dis=26 85 06 90 90
    es test %cx,%ax                     # gen=26 85 c8  dis=26 85 c8
    es test %dx,%ax                     # gen=26 85 d0  dis=26 85 d0
    es test %bx,%ax                     # gen=26 85 d8  dis=26 85 d8
    es test %sp,%ax                     # gen=26 85 e0  dis=26 85 e0
    es test %bp,%ax                     # gen=26 85 e8  dis=26 85 e8
    es test %si,%ax                     # gen=26 85 f0  dis=26 85 f0
    es test %di,%ax                     # gen=26 85 f8  dis=26 85 f8
    test   %cx,%es:-0x6f70              # gen=26 85 0e  dis=26 85 0e 90 90
    test   %dx,%es:-0x6f70              # gen=26 85 16  dis=26 85 16 90 90
    test   %bx,%es:-0x6f70              # gen=26 85 1e  dis=26 85 1e 90 90
    test   %sp,%es:-0x6f70              # gen=26 85 26  dis=26 85 26 90 90
    test   %bp,%es:-0x6f70              # gen=26 85 2e  dis=26 85 2e 90 90
    test   %si,%es:-0x6f70              # gen=26 85 36  dis=26 85 36 90 90
    test   %di,%es:-0x6f70              # gen=26 85 3e  dis=26 85 3e 90 90
    es xchg %al,%al                     # gen=26 86 c0  dis=26 86 c0
    xchg   %al,%es:-0x6f70              # gen=26 86 06  dis=26 86 06 90 90
    es xchg %cl,%al                     # gen=26 86 c8  dis=26 86 c8
    es xchg %dl,%al                     # gen=26 86 d0  dis=26 86 d0
    es xchg %bl,%al                     # gen=26 86 d8  dis=26 86 d8
    es xchg %ah,%al                     # gen=26 86 e0  dis=26 86 e0
    es xchg %ch,%al                     # gen=26 86 e8  dis=26 86 e8
    es xchg %dh,%al                     # gen=26 86 f0  dis=26 86 f0
    es xchg %bh,%al                     # gen=26 86 f8  dis=26 86 f8
    xchg   %cl,%es:-0x6f70              # gen=26 86 0e  dis=26 86 0e 90 90
    xchg   %dl,%es:-0x6f70              # gen=26 86 16  dis=26 86 16 90 90
    xchg   %bl,%es:-0x6f70              # gen=26 86 1e  dis=26 86 1e 90 90
    xchg   %ah,%es:-0x6f70              # gen=26 86 26  dis=26 86 26 90 90
    xchg   %ch,%es:-0x6f70              # gen=26 86 2e  dis=26 86 2e 90 90
    xchg   %dh,%es:-0x6f70              # gen=26 86 36  dis=26 86 36 90 90
    xchg   %bh,%es:-0x6f70              # gen=26 86 3e  dis=26 86 3e 90 90
    es xchg %ax,%ax                     # gen=26 87 c0  dis=26 87 c0
    xchg   %ax,%es:-0x6f70              # gen=26 87 06  dis=26 87 06 90 90
    es xchg %cx,%ax                     # gen=26 87 c8  dis=26 87 c8
    es xchg %dx,%ax                     # gen=26 87 d0  dis=26 87 d0
    es xchg %bx,%ax                     # gen=26 87 d8  dis=26 87 d8
    es xchg %sp,%ax                     # gen=26 87 e0  dis=26 87 e0
    es xchg %bp,%ax                     # gen=26 87 e8  dis=26 87 e8
    es xchg %si,%ax                     # gen=26 87 f0  dis=26 87 f0
    es xchg %di,%ax                     # gen=26 87 f8  dis=26 87 f8
    xchg   %cx,%es:-0x6f70              # gen=26 87 0e  dis=26 87 0e 90 90
    xchg   %dx,%es:-0x6f70              # gen=26 87 16  dis=26 87 16 90 90
    xchg   %bx,%es:-0x6f70              # gen=26 87 1e  dis=26 87 1e 90 90
    xchg   %sp,%es:-0x6f70              # gen=26 87 26  dis=26 87 26 90 90
    xchg   %bp,%es:-0x6f70              # gen=26 87 2e  dis=26 87 2e 90 90
    xchg   %si,%es:-0x6f70              # gen=26 87 36  dis=26 87 36 90 90
    xchg   %di,%es:-0x6f70              # gen=26 87 3e  dis=26 87 3e 90 90
    es mov %al,%al                      # gen=26 88 c0  dis=26 88 c0
    mov    %al,%es:-0x6f70              # gen=26 88 06  dis=26 88 06 90 90
    es mov %cl,%al                      # gen=26 88 c8  dis=26 88 c8
    es mov %dl,%al                      # gen=26 88 d0  dis=26 88 d0
    es mov %bl,%al                      # gen=26 88 d8  dis=26 88 d8
    es mov %ah,%al                      # gen=26 88 e0  dis=26 88 e0
    es mov %ch,%al                      # gen=26 88 e8  dis=26 88 e8
    es mov %dh,%al                      # gen=26 88 f0  dis=26 88 f0
    es mov %bh,%al                      # gen=26 88 f8  dis=26 88 f8
    mov    %cl,%es:-0x6f70              # gen=26 88 0e  dis=26 88 0e 90 90
    mov    %dl,%es:-0x6f70              # gen=26 88 16  dis=26 88 16 90 90
    mov    %bl,%es:-0x6f70              # gen=26 88 1e  dis=26 88 1e 90 90
    mov    %ah,%es:-0x6f70              # gen=26 88 26  dis=26 88 26 90 90
    mov    %ch,%es:-0x6f70              # gen=26 88 2e  dis=26 88 2e 90 90
    mov    %dh,%es:-0x6f70              # gen=26 88 36  dis=26 88 36 90 90
    mov    %bh,%es:-0x6f70              # gen=26 88 3e  dis=26 88 3e 90 90
    es mov %ax,%ax                      # gen=26 89 c0  dis=26 89 c0
    mov    %ax,%es:-0x6f70              # gen=26 89 06  dis=26 89 06 90 90
    es mov %cx,%ax                      # gen=26 89 c8  dis=26 89 c8
    es mov %dx,%ax                      # gen=26 89 d0  dis=26 89 d0
    es mov %bx,%ax                      # gen=26 89 d8  dis=26 89 d8
    es mov %sp,%ax                      # gen=26 89 e0  dis=26 89 e0
    es mov %bp,%ax                      # gen=26 89 e8  dis=26 89 e8
    es mov %si,%ax                      # gen=26 89 f0  dis=26 89 f0
    es mov %di,%ax                      # gen=26 89 f8  dis=26 89 f8
    mov    %cx,%es:-0x6f70              # gen=26 89 0e  dis=26 89 0e 90 90
    mov    %dx,%es:-0x6f70              # gen=26 89 16  dis=26 89 16 90 90
    mov    %bx,%es:-0x6f70              # gen=26 89 1e  dis=26 89 1e 90 90
    mov    %sp,%es:-0x6f70              # gen=26 89 26  dis=26 89 26 90 90
    mov    %bp,%es:-0x6f70              # gen=26 89 2e  dis=26 89 2e 90 90
    mov    %si,%es:-0x6f70              # gen=26 89 36  dis=26 89 36 90 90
    mov    %di,%es:-0x6f70              # gen=26 89 3e  dis=26 89 3e 90 90
    es mov %al,%al                      # gen=26 8a c0  dis=26 8a c0
    mov    %es:-0x6f70,%al              # gen=26 8a 06  dis=26 8a 06 90 90
    es mov %al,%cl                      # gen=26 8a c8  dis=26 8a c8
    es mov %al,%dl                      # gen=26 8a d0  dis=26 8a d0
    es mov %al,%bl                      # gen=26 8a d8  dis=26 8a d8
    es mov %al,%ah                      # gen=26 8a e0  dis=26 8a e0
    es mov %al,%ch                      # gen=26 8a e8  dis=26 8a e8
    es mov %al,%dh                      # gen=26 8a f0  dis=26 8a f0
    es mov %al,%bh                      # gen=26 8a f8  dis=26 8a f8
    mov    %es:-0x6f70,%cl              # gen=26 8a 0e  dis=26 8a 0e 90 90
    mov    %es:-0x6f70,%dl              # gen=26 8a 16  dis=26 8a 16 90 90
    mov    %es:-0x6f70,%bl              # gen=26 8a 1e  dis=26 8a 1e 90 90
    mov    %es:-0x6f70,%ah              # gen=26 8a 26  dis=26 8a 26 90 90
    mov    %es:-0x6f70,%ch              # gen=26 8a 2e  dis=26 8a 2e 90 90
    mov    %es:-0x6f70,%dh              # gen=26 8a 36  dis=26 8a 36 90 90
    mov    %es:-0x6f70,%bh              # gen=26 8a 3e  dis=26 8a 3e 90 90
    es mov %ax,%ax                      # gen=26 8b c0  dis=26 8b c0
    mov    %es:-0x6f70,%ax              # gen=26 8b 06  dis=26 8b 06 90 90
    es mov %ax,%cx                      # gen=26 8b c8  dis=26 8b c8
    es mov %ax,%dx                      # gen=26 8b d0  dis=26 8b d0
    es mov %ax,%bx                      # gen=26 8b d8  dis=26 8b d8
    es mov %ax,%sp                      # gen=26 8b e0  dis=26 8b e0
    es mov %ax,%bp                      # gen=26 8b e8  dis=26 8b e8
    es mov %ax,%si                      # gen=26 8b f0  dis=26 8b f0
    es mov %ax,%di                      # gen=26 8b f8  dis=26 8b f8
    mov    %es:-0x6f70,%cx              # gen=26 8b 0e  dis=26 8b 0e 90 90
    mov    %es:-0x6f70,%dx              # gen=26 8b 16  dis=26 8b 16 90 90
    mov    %es:-0x6f70,%bx              # gen=26 8b 1e  dis=26 8b 1e 90 90
    mov    %es:-0x6f70,%sp              # gen=26 8b 26  dis=26 8b 26 90 90
    mov    %es:-0x6f70,%bp              # gen=26 8b 2e  dis=26 8b 2e 90 90
    mov    %es:-0x6f70,%si              # gen=26 8b 36  dis=26 8b 36 90 90
    mov    %es:-0x6f70,%di              # gen=26 8b 3e  dis=26 8b 3e 90 90
    es mov %es,%ax                      # gen=26 8c c0  dis=26 8c c0
    mov    %es,%es:-0x6f70              # gen=26 8c 06  dis=26 8c 06 90 90
    es mov %cs,%ax                      # gen=26 8c c8  dis=26 8c c8
    es mov %ss,%ax                      # gen=26 8c d0  dis=26 8c d0
    es mov %ds,%ax                      # gen=26 8c d8  dis=26 8c d8
    .byte 0x26,0x8c,0xe0            # fallback; gen=26 8c e0
    .byte 0x26,0x8c,0xe8            # fallback; gen=26 8c e8
    .byte 0x26,0x8c,0xf0            # fallback; gen=26 8c f0
    .byte 0x26,0x8c,0xf8            # fallback; gen=26 8c f8
    mov    %cs,%es:-0x6f70              # gen=26 8c 0e  dis=26 8c 0e 90 90
    mov    %ss,%es:-0x6f70              # gen=26 8c 16  dis=26 8c 16 90 90
    mov    %ds,%es:-0x6f70              # gen=26 8c 1e  dis=26 8c 1e 90 90
    .byte 0x26,0x8c,0x26            # fallback; gen=26 8c 26
    .byte 0x26,0x8c,0x2e            # fallback; gen=26 8c 2e
    .byte 0x26,0x8c,0x36            # fallback; gen=26 8c 36
    .byte 0x26,0x8c,0x3e            # fallback; gen=26 8c 3e
    .byte 0x26,0x8d,0xc0            # fallback; gen=26 8d c0
    .byte 0x26,0x8d,0x06            # fallback; gen=26 8d 06
    .byte 0x26,0x8d,0xc8            # fallback; gen=26 8d c8
    .byte 0x26,0x8d,0xd0            # fallback; gen=26 8d d0
    .byte 0x26,0x8d,0xd8            # fallback; gen=26 8d d8
    .byte 0x26,0x8d,0xe0            # fallback; gen=26 8d e0
    .byte 0x26,0x8d,0xe8            # fallback; gen=26 8d e8
    .byte 0x26,0x8d,0xf0            # fallback; gen=26 8d f0
    .byte 0x26,0x8d,0xf8            # fallback; gen=26 8d f8
    .byte 0x26,0x8d,0x0e            # fallback; gen=26 8d 0e
    .byte 0x26,0x8d,0x16            # fallback; gen=26 8d 16
    .byte 0x26,0x8d,0x1e            # fallback; gen=26 8d 1e
    .byte 0x26,0x8d,0x26            # fallback; gen=26 8d 26
    .byte 0x26,0x8d,0x2e            # fallback; gen=26 8d 2e
    .byte 0x26,0x8d,0x36            # fallback; gen=26 8d 36
    .byte 0x26,0x8d,0x3e            # fallback; gen=26 8d 3e
    es mov %ax,%es                      # gen=26 8e c0  dis=26 8e c0
    mov    %es:-0x6f70,%es              # gen=26 8e 06  dis=26 8e 06 90 90
    es mov %ax,%cs                      # gen=26 8e c8  dis=26 8e c8
    es mov %ax,%ss                      # gen=26 8e d0  dis=26 8e d0
    es mov %ax,%ds                      # gen=26 8e d8  dis=26 8e d8
    .byte 0x26,0x8e,0xe0            # fallback; gen=26 8e e0
    .byte 0x26,0x8e,0xe8            # fallback; gen=26 8e e8
    .byte 0x26,0x8e,0xf0            # fallback; gen=26 8e f0
    .byte 0x26,0x8e,0xf8            # fallback; gen=26 8e f8
    mov    %es:-0x6f70,%cs              # gen=26 8e 0e  dis=26 8e 0e 90 90
    mov    %es:-0x6f70,%ss              # gen=26 8e 16  dis=26 8e 16 90 90
    mov    %es:-0x6f70,%ds              # gen=26 8e 1e  dis=26 8e 1e 90 90
    .byte 0x26,0x8e,0x26            # fallback; gen=26 8e 26
    .byte 0x26,0x8e,0x2e            # fallback; gen=26 8e 2e
    .byte 0x26,0x8e,0x36            # fallback; gen=26 8e 36
    .byte 0x26,0x8e,0x3e            # fallback; gen=26 8e 3e
    es pop %ax                          # gen=26 8f c0  dis=26 8f c0
    pop    %es:-0x6f70                  # gen=26 8f 06  dis=26 8f 06 90 90
    .byte 0x26,0x8f,0xc8            # fallback; gen=26 8f c8
    .byte 0x26,0x8f,0xd0            # fallback; gen=26 8f d0
    .byte 0x26,0x8f,0xd8            # fallback; gen=26 8f d8
    .byte 0x26,0x8f,0xe0            # fallback; gen=26 8f e0
    .byte 0x26,0x8f,0xe8            # fallback; gen=26 8f e8
    .byte 0x26,0x8f,0xf0            # fallback; gen=26 8f f0
    .byte 0x26,0x8f,0xf8            # fallback; gen=26 8f f8
    .byte 0x26,0x8f,0x0e            # fallback; gen=26 8f 0e
    .byte 0x26,0x8f,0x16            # fallback; gen=26 8f 16
    .byte 0x26,0x8f,0x1e            # fallback; gen=26 8f 1e
    .byte 0x26,0x8f,0x26            # fallback; gen=26 8f 26
    .byte 0x26,0x8f,0x2e            # fallback; gen=26 8f 2e
    .byte 0x26,0x8f,0x36            # fallback; gen=26 8f 36
    .byte 0x26,0x8f,0x3e            # fallback; gen=26 8f 3e
    es nop                              # gen=26 90 c0  dis=26 90
    es nop                              # gen=26 90 06  dis=26 90
    es xchg %ax,%cx                     # gen=26 91 c0  dis=26 91
    es xchg %ax,%cx                     # gen=26 91 06  dis=26 91
    es xchg %ax,%dx                     # gen=26 92 c0  dis=26 92
    es xchg %ax,%dx                     # gen=26 92 06  dis=26 92
    es xchg %ax,%bx                     # gen=26 93 c0  dis=26 93
    es xchg %ax,%bx                     # gen=26 93 06  dis=26 93
    es xchg %ax,%sp                     # gen=26 94 c0  dis=26 94
    es xchg %ax,%sp                     # gen=26 94 06  dis=26 94
    es xchg %ax,%bp                     # gen=26 95 c0  dis=26 95
    es xchg %ax,%bp                     # gen=26 95 06  dis=26 95
    es xchg %ax,%si                     # gen=26 96 c0  dis=26 96
    es xchg %ax,%si                     # gen=26 96 06  dis=26 96
    es xchg %ax,%di                     # gen=26 97 c0  dis=26 97
    es xchg %ax,%di                     # gen=26 97 06  dis=26 97
    es cbtw                             # gen=26 98 c0  dis=26 98
    es cbtw                             # gen=26 98 06  dis=26 98
    es cwtd                             # gen=26 99 c0  dis=26 99
    es cwtd                             # gen=26 99 06  dis=26 99
    .byte 0x26,0x9a,0xc0            # fallback; gen=26 9a c0
    .byte 0x26,0x9a,0x06            # fallback; gen=26 9a 06
    .byte 0x26,0x9b,0xc0            # fallback; gen=26 9b c0
    .byte 0x26,0x9b,0x06            # fallback; gen=26 9b 06
    es pushf                            # gen=26 9c c0  dis=26 9c
    es pushf                            # gen=26 9c 06  dis=26 9c
    es popf                             # gen=26 9d c0  dis=26 9d
    es popf                             # gen=26 9d 06  dis=26 9d
    es sahf                             # gen=26 9e c0  dis=26 9e
    es sahf                             # gen=26 9e 06  dis=26 9e
    es lahf                             # gen=26 9f c0  dis=26 9f
    es lahf                             # gen=26 9f 06  dis=26 9f
    mov    %es:0x90c0,%al               # gen=26 a0 c0  dis=26 a0 c0 90
    mov    %es:0x9006,%al               # gen=26 a0 06  dis=26 a0 06 90
    mov    %es:0x90c0,%ax               # gen=26 a1 c0  dis=26 a1 c0 90
    mov    %es:0x9006,%ax               # gen=26 a1 06  dis=26 a1 06 90
    mov    %al,%es:0x90c0               # gen=26 a2 c0  dis=26 a2 c0 90
    mov    %al,%es:0x9006               # gen=26 a2 06  dis=26 a2 06 90
    mov    %ax,%es:0x90c0               # gen=26 a3 c0  dis=26 a3 c0 90
    mov    %ax,%es:0x9006               # gen=26 a3 06  dis=26 a3 06 90
    movsb  %es:(%si),%es:(%di)          # gen=26 a4 c0  dis=26 a4
    movsb  %es:(%si),%es:(%di)          # gen=26 a4 06  dis=26 a4
    movsw  %es:(%si),%es:(%di)          # gen=26 a5 c0  dis=26 a5
    movsw  %es:(%si),%es:(%di)          # gen=26 a5 06  dis=26 a5
    cmpsb  %es:(%di),%es:(%si)          # gen=26 a6 c0  dis=26 a6
    cmpsb  %es:(%di),%es:(%si)          # gen=26 a6 06  dis=26 a6
    cmpsw  %es:(%di),%es:(%si)          # gen=26 a7 c0  dis=26 a7
    cmpsw  %es:(%di),%es:(%si)          # gen=26 a7 06  dis=26 a7
    es test $0xc0,%al                   # gen=26 a8 c0  dis=26 a8 c0
    es test $0x6,%al                    # gen=26 a8 06  dis=26 a8 06
    es test $0x90c0,%ax                 # gen=26 a9 c0  dis=26 a9 c0 90
    es test $0x9006,%ax                 # gen=26 a9 06  dis=26 a9 06 90
    es stos %al,%es:(%di)               # gen=26 aa c0  dis=26 aa
    es stos %al,%es:(%di)               # gen=26 aa 06  dis=26 aa
    es stos %ax,%es:(%di)               # gen=26 ab c0  dis=26 ab
    es stos %ax,%es:(%di)               # gen=26 ab 06  dis=26 ab
    lods   %es:(%si),%al                # gen=26 ac c0  dis=26 ac
    lods   %es:(%si),%al                # gen=26 ac 06  dis=26 ac
    lods   %es:(%si),%ax                # gen=26 ad c0  dis=26 ad
    lods   %es:(%si),%ax                # gen=26 ad 06  dis=26 ad
    es scas %es:(%di),%al               # gen=26 ae c0  dis=26 ae
    es scas %es:(%di),%al               # gen=26 ae 06  dis=26 ae
    es scas %es:(%di),%ax               # gen=26 af c0  dis=26 af
    es scas %es:(%di),%ax               # gen=26 af 06  dis=26 af
    es mov $0xc0,%al                    # gen=26 b0 c0  dis=26 b0 c0
    es mov $0x6,%al                     # gen=26 b0 06  dis=26 b0 06
    es mov $0xc0,%cl                    # gen=26 b1 c0  dis=26 b1 c0
    es mov $0x6,%cl                     # gen=26 b1 06  dis=26 b1 06
    es mov $0xc0,%dl                    # gen=26 b2 c0  dis=26 b2 c0
    es mov $0x6,%dl                     # gen=26 b2 06  dis=26 b2 06
    es mov $0xc0,%bl                    # gen=26 b3 c0  dis=26 b3 c0
    es mov $0x6,%bl                     # gen=26 b3 06  dis=26 b3 06
    es mov $0xc0,%ah                    # gen=26 b4 c0  dis=26 b4 c0
    es mov $0x6,%ah                     # gen=26 b4 06  dis=26 b4 06
    es mov $0xc0,%ch                    # gen=26 b5 c0  dis=26 b5 c0
    es mov $0x6,%ch                     # gen=26 b5 06  dis=26 b5 06
    es mov $0xc0,%dh                    # gen=26 b6 c0  dis=26 b6 c0
    es mov $0x6,%dh                     # gen=26 b6 06  dis=26 b6 06
    es mov $0xc0,%bh                    # gen=26 b7 c0  dis=26 b7 c0
    es mov $0x6,%bh                     # gen=26 b7 06  dis=26 b7 06
    es mov $0x90c0,%ax                  # gen=26 b8 c0  dis=26 b8 c0 90
    es mov $0x9006,%ax                  # gen=26 b8 06  dis=26 b8 06 90
    es mov $0x90c0,%cx                  # gen=26 b9 c0  dis=26 b9 c0 90
    es mov $0x9006,%cx                  # gen=26 b9 06  dis=26 b9 06 90
    es mov $0x90c0,%dx                  # gen=26 ba c0  dis=26 ba c0 90
    es mov $0x9006,%dx                  # gen=26 ba 06  dis=26 ba 06 90
    es mov $0x90c0,%bx                  # gen=26 bb c0  dis=26 bb c0 90
    es mov $0x9006,%bx                  # gen=26 bb 06  dis=26 bb 06 90
    es mov $0x90c0,%sp                  # gen=26 bc c0  dis=26 bc c0 90
    es mov $0x9006,%sp                  # gen=26 bc 06  dis=26 bc 06 90
    es mov $0x90c0,%bp                  # gen=26 bd c0  dis=26 bd c0 90
    es mov $0x9006,%bp                  # gen=26 bd 06  dis=26 bd 06 90
    es mov $0x90c0,%si                  # gen=26 be c0  dis=26 be c0 90
    es mov $0x9006,%si                  # gen=26 be 06  dis=26 be 06 90
    es mov $0x90c0,%di                  # gen=26 bf c0  dis=26 bf c0 90
    es mov $0x9006,%di                  # gen=26 bf 06  dis=26 bf 06 90
    .byte 0x26,0xc0,0xc0            # fallback; gen=26 c0 c0
    .byte 0x26,0xc0,0x06            # fallback; gen=26 c0 06
    .byte 0x26,0xc0,0xc8            # fallback; gen=26 c0 c8
    .byte 0x26,0xc0,0xd0            # fallback; gen=26 c0 d0
    .byte 0x26,0xc0,0xd8            # fallback; gen=26 c0 d8
    .byte 0x26,0xc0,0xe0            # fallback; gen=26 c0 e0
    .byte 0x26,0xc0,0xe8            # fallback; gen=26 c0 e8
    .byte 0x26,0xc0,0xf0            # fallback; gen=26 c0 f0
    .byte 0x26,0xc0,0xf8            # fallback; gen=26 c0 f8
    .byte 0x26,0xc0,0x0e            # fallback; gen=26 c0 0e
    .byte 0x26,0xc0,0x16            # fallback; gen=26 c0 16
    .byte 0x26,0xc0,0x1e            # fallback; gen=26 c0 1e
    .byte 0x26,0xc0,0x26            # fallback; gen=26 c0 26
    .byte 0x26,0xc0,0x2e            # fallback; gen=26 c0 2e
    .byte 0x26,0xc0,0x36            # fallback; gen=26 c0 36
    .byte 0x26,0xc0,0x3e            # fallback; gen=26 c0 3e
    .byte 0x26,0xc1,0xc0            # fallback; gen=26 c1 c0
    .byte 0x26,0xc1,0x06            # fallback; gen=26 c1 06
    .byte 0x26,0xc1,0xc8            # fallback; gen=26 c1 c8
    .byte 0x26,0xc1,0xd0            # fallback; gen=26 c1 d0
    .byte 0x26,0xc1,0xd8            # fallback; gen=26 c1 d8
    .byte 0x26,0xc1,0xe0            # fallback; gen=26 c1 e0
    .byte 0x26,0xc1,0xe8            # fallback; gen=26 c1 e8
    .byte 0x26,0xc1,0xf0            # fallback; gen=26 c1 f0
    .byte 0x26,0xc1,0xf8            # fallback; gen=26 c1 f8
    .byte 0x26,0xc1,0x0e            # fallback; gen=26 c1 0e
    .byte 0x26,0xc1,0x16            # fallback; gen=26 c1 16
    .byte 0x26,0xc1,0x1e            # fallback; gen=26 c1 1e
    .byte 0x26,0xc1,0x26            # fallback; gen=26 c1 26
    .byte 0x26,0xc1,0x2e            # fallback; gen=26 c1 2e
    .byte 0x26,0xc1,0x36            # fallback; gen=26 c1 36
    .byte 0x26,0xc1,0x3e            # fallback; gen=26 c1 3e
    es ret $0x90c0                      # gen=26 c2 c0  dis=26 c2 c0 90
    es ret $0x9006                      # gen=26 c2 06  dis=26 c2 06 90
    es ret                              # gen=26 c3 c0  dis=26 c3
    es ret                              # gen=26 c3 06  dis=26 c3
    .byte 0x26,0xc4,0xc0            # fallback; gen=26 c4 c0
    les    %es:-0x6f70,%ax              # gen=26 c4 06  dis=26 c4 06 90 90
    .byte 0x26,0xc4,0xc8            # fallback; gen=26 c4 c8
    .byte 0x26,0xc4,0xd0            # fallback; gen=26 c4 d0
    .byte 0x26,0xc4,0xd8            # fallback; gen=26 c4 d8
    .byte 0x26,0xc4,0xe0            # fallback; gen=26 c4 e0
    .byte 0x26,0xc4,0xe8            # fallback; gen=26 c4 e8
    .byte 0x26,0xc4,0xf0            # fallback; gen=26 c4 f0
    .byte 0x26,0xc4,0xf8            # fallback; gen=26 c4 f8
    les    %es:-0x6f70,%cx              # gen=26 c4 0e  dis=26 c4 0e 90 90
    les    %es:-0x6f70,%dx              # gen=26 c4 16  dis=26 c4 16 90 90
    les    %es:-0x6f70,%bx              # gen=26 c4 1e  dis=26 c4 1e 90 90
    les    %es:-0x6f70,%sp              # gen=26 c4 26  dis=26 c4 26 90 90
    les    %es:-0x6f70,%bp              # gen=26 c4 2e  dis=26 c4 2e 90 90
    les    %es:-0x6f70,%si              # gen=26 c4 36  dis=26 c4 36 90 90
    les    %es:-0x6f70,%di              # gen=26 c4 3e  dis=26 c4 3e 90 90
    .byte 0x26,0xc5,0xc0            # fallback; gen=26 c5 c0
    lds    %es:-0x6f70,%ax              # gen=26 c5 06  dis=26 c5 06 90 90
    .byte 0x26,0xc5,0xc8            # fallback; gen=26 c5 c8
    .byte 0x26,0xc5,0xd0            # fallback; gen=26 c5 d0
    .byte 0x26,0xc5,0xd8            # fallback; gen=26 c5 d8
    .byte 0x26,0xc5,0xe0            # fallback; gen=26 c5 e0
    .byte 0x26,0xc5,0xe8            # fallback; gen=26 c5 e8
    .byte 0x26,0xc5,0xf0            # fallback; gen=26 c5 f0
    .byte 0x26,0xc5,0xf8            # fallback; gen=26 c5 f8
    lds    %es:-0x6f70,%cx              # gen=26 c5 0e  dis=26 c5 0e 90 90
    lds    %es:-0x6f70,%dx              # gen=26 c5 16  dis=26 c5 16 90 90
    lds    %es:-0x6f70,%bx              # gen=26 c5 1e  dis=26 c5 1e 90 90
    lds    %es:-0x6f70,%sp              # gen=26 c5 26  dis=26 c5 26 90 90
    lds    %es:-0x6f70,%bp              # gen=26 c5 2e  dis=26 c5 2e 90 90
    lds    %es:-0x6f70,%si              # gen=26 c5 36  dis=26 c5 36 90 90
    lds    %es:-0x6f70,%di              # gen=26 c5 3e  dis=26 c5 3e 90 90
    es mov $0x90,%al                    # gen=26 c6 c0  dis=26 c6 c0 90
    movb   $0x90,%es:-0x6f70            # gen=26 c6 06  dis=26 c6 06 90 90 90
    .byte 0x26,0xc6,0xc8            # fallback; gen=26 c6 c8
    .byte 0x26,0xc6,0xd0            # fallback; gen=26 c6 d0
    .byte 0x26,0xc6,0xd8            # fallback; gen=26 c6 d8
    .byte 0x26,0xc6,0xe0            # fallback; gen=26 c6 e0
    .byte 0x26,0xc6,0xe8            # fallback; gen=26 c6 e8
    .byte 0x26,0xc6,0xf0            # fallback; gen=26 c6 f0
    .byte 0x26,0xc6,0xf8            # fallback; gen=26 c6 f8
    .byte 0x26,0xc6,0x0e            # fallback; gen=26 c6 0e
    .byte 0x26,0xc6,0x16            # fallback; gen=26 c6 16
    .byte 0x26,0xc6,0x1e            # fallback; gen=26 c6 1e
    .byte 0x26,0xc6,0x26            # fallback; gen=26 c6 26
    .byte 0x26,0xc6,0x2e            # fallback; gen=26 c6 2e
    .byte 0x26,0xc6,0x36            # fallback; gen=26 c6 36
    .byte 0x26,0xc6,0x3e            # fallback; gen=26 c6 3e
    es mov $0x9090,%ax                  # gen=26 c7 c0  dis=26 c7 c0 90 90
    movw   $0x9090,%es:-0x6f70          # gen=26 c7 06  dis=26 c7 06 90 90 90 90
    .byte 0x26,0xc7,0xc8            # fallback; gen=26 c7 c8
    .byte 0x26,0xc7,0xd0            # fallback; gen=26 c7 d0
    .byte 0x26,0xc7,0xd8            # fallback; gen=26 c7 d8
    .byte 0x26,0xc7,0xe0            # fallback; gen=26 c7 e0
    .byte 0x26,0xc7,0xe8            # fallback; gen=26 c7 e8
    .byte 0x26,0xc7,0xf0            # fallback; gen=26 c7 f0
    .byte 0x26,0xc7,0xf8            # fallback; gen=26 c7 f8
    .byte 0x26,0xc7,0x0e            # fallback; gen=26 c7 0e
    .byte 0x26,0xc7,0x16            # fallback; gen=26 c7 16
    .byte 0x26,0xc7,0x1e            # fallback; gen=26 c7 1e
    .byte 0x26,0xc7,0x26            # fallback; gen=26 c7 26
    .byte 0x26,0xc7,0x2e            # fallback; gen=26 c7 2e
    .byte 0x26,0xc7,0x36            # fallback; gen=26 c7 36
    .byte 0x26,0xc7,0x3e            # fallback; gen=26 c7 3e
    .byte 0x26,0xc8,0xc0            # fallback; gen=26 c8 c0
    .byte 0x26,0xc8,0x06            # fallback; gen=26 c8 06
    .byte 0x26,0xc9,0xc0            # fallback; gen=26 c9 c0
    .byte 0x26,0xc9,0x06            # fallback; gen=26 c9 06
    es lret $0x90c0                     # gen=26 ca c0  dis=26 ca c0 90
    es lret $0x9006                     # gen=26 ca 06  dis=26 ca 06 90
    es lret                             # gen=26 cb c0  dis=26 cb
    es lret                             # gen=26 cb 06  dis=26 cb
    es int3                             # gen=26 cc c0  dis=26 cc
    es int3                             # gen=26 cc 06  dis=26 cc
    es int $0xc0                        # gen=26 cd c0  dis=26 cd c0
    es int $0x6                         # gen=26 cd 06  dis=26 cd 06
    es into                             # gen=26 ce c0  dis=26 ce
    es into                             # gen=26 ce 06  dis=26 ce
    es iret                             # gen=26 cf c0  dis=26 cf
    es iret                             # gen=26 cf 06  dis=26 cf
    es rol $1,%al                       # gen=26 d0 c0  dis=26 d0 c0
    rolb   $1,%es:-0x6f70               # gen=26 d0 06  dis=26 d0 06 90 90
    es ror $1,%al                       # gen=26 d0 c8  dis=26 d0 c8
    es rcl $1,%al                       # gen=26 d0 d0  dis=26 d0 d0
    es rcr $1,%al                       # gen=26 d0 d8  dis=26 d0 d8
    es shl $1,%al                       # gen=26 d0 e0  dis=26 d0 e0
    es shr $1,%al                       # gen=26 d0 e8  dis=26 d0 e8
    es shl $1,%al                       # gen=26 d0 f0  dis=26 d0 f0
    es sar $1,%al                       # gen=26 d0 f8  dis=26 d0 f8
    rorb   $1,%es:-0x6f70               # gen=26 d0 0e  dis=26 d0 0e 90 90
    rclb   $1,%es:-0x6f70               # gen=26 d0 16  dis=26 d0 16 90 90
    rcrb   $1,%es:-0x6f70               # gen=26 d0 1e  dis=26 d0 1e 90 90
    shlb   $1,%es:-0x6f70               # gen=26 d0 26  dis=26 d0 26 90 90
    shrb   $1,%es:-0x6f70               # gen=26 d0 2e  dis=26 d0 2e 90 90
    shlb   $1,%es:-0x6f70               # gen=26 d0 36  dis=26 d0 36 90 90
    sarb   $1,%es:-0x6f70               # gen=26 d0 3e  dis=26 d0 3e 90 90
    es rol $1,%ax                       # gen=26 d1 c0  dis=26 d1 c0
    rolw   $1,%es:-0x6f70               # gen=26 d1 06  dis=26 d1 06 90 90
    es ror $1,%ax                       # gen=26 d1 c8  dis=26 d1 c8
    es rcl $1,%ax                       # gen=26 d1 d0  dis=26 d1 d0
    es rcr $1,%ax                       # gen=26 d1 d8  dis=26 d1 d8
    es shl $1,%ax                       # gen=26 d1 e0  dis=26 d1 e0
    es shr $1,%ax                       # gen=26 d1 e8  dis=26 d1 e8
    es shl $1,%ax                       # gen=26 d1 f0  dis=26 d1 f0
    es sar $1,%ax                       # gen=26 d1 f8  dis=26 d1 f8
    rorw   $1,%es:-0x6f70               # gen=26 d1 0e  dis=26 d1 0e 90 90
    rclw   $1,%es:-0x6f70               # gen=26 d1 16  dis=26 d1 16 90 90
    rcrw   $1,%es:-0x6f70               # gen=26 d1 1e  dis=26 d1 1e 90 90
    shlw   $1,%es:-0x6f70               # gen=26 d1 26  dis=26 d1 26 90 90
    shrw   $1,%es:-0x6f70               # gen=26 d1 2e  dis=26 d1 2e 90 90
    shlw   $1,%es:-0x6f70               # gen=26 d1 36  dis=26 d1 36 90 90
    sarw   $1,%es:-0x6f70               # gen=26 d1 3e  dis=26 d1 3e 90 90
    es rol %cl,%al                      # gen=26 d2 c0  dis=26 d2 c0
    rolb   %cl,%es:-0x6f70              # gen=26 d2 06  dis=26 d2 06 90 90
    es ror %cl,%al                      # gen=26 d2 c8  dis=26 d2 c8
    es rcl %cl,%al                      # gen=26 d2 d0  dis=26 d2 d0
    es rcr %cl,%al                      # gen=26 d2 d8  dis=26 d2 d8
    es shl %cl,%al                      # gen=26 d2 e0  dis=26 d2 e0
    es shr %cl,%al                      # gen=26 d2 e8  dis=26 d2 e8
    es shl %cl,%al                      # gen=26 d2 f0  dis=26 d2 f0
    es sar %cl,%al                      # gen=26 d2 f8  dis=26 d2 f8
    rorb   %cl,%es:-0x6f70              # gen=26 d2 0e  dis=26 d2 0e 90 90
    rclb   %cl,%es:-0x6f70              # gen=26 d2 16  dis=26 d2 16 90 90
    rcrb   %cl,%es:-0x6f70              # gen=26 d2 1e  dis=26 d2 1e 90 90
    shlb   %cl,%es:-0x6f70              # gen=26 d2 26  dis=26 d2 26 90 90
    shrb   %cl,%es:-0x6f70              # gen=26 d2 2e  dis=26 d2 2e 90 90
    shlb   %cl,%es:-0x6f70              # gen=26 d2 36  dis=26 d2 36 90 90
    sarb   %cl,%es:-0x6f70              # gen=26 d2 3e  dis=26 d2 3e 90 90
    es rol %cl,%ax                      # gen=26 d3 c0  dis=26 d3 c0
    rolw   %cl,%es:-0x6f70              # gen=26 d3 06  dis=26 d3 06 90 90
    es ror %cl,%ax                      # gen=26 d3 c8  dis=26 d3 c8
    es rcl %cl,%ax                      # gen=26 d3 d0  dis=26 d3 d0
    es rcr %cl,%ax                      # gen=26 d3 d8  dis=26 d3 d8
    es shl %cl,%ax                      # gen=26 d3 e0  dis=26 d3 e0
    es shr %cl,%ax                      # gen=26 d3 e8  dis=26 d3 e8
    es shl %cl,%ax                      # gen=26 d3 f0  dis=26 d3 f0
    es sar %cl,%ax                      # gen=26 d3 f8  dis=26 d3 f8
    rorw   %cl,%es:-0x6f70              # gen=26 d3 0e  dis=26 d3 0e 90 90
    rclw   %cl,%es:-0x6f70              # gen=26 d3 16  dis=26 d3 16 90 90
    rcrw   %cl,%es:-0x6f70              # gen=26 d3 1e  dis=26 d3 1e 90 90
    shlw   %cl,%es:-0x6f70              # gen=26 d3 26  dis=26 d3 26 90 90
    shrw   %cl,%es:-0x6f70              # gen=26 d3 2e  dis=26 d3 2e 90 90
    shlw   %cl,%es:-0x6f70              # gen=26 d3 36  dis=26 d3 36 90 90
    sarw   %cl,%es:-0x6f70              # gen=26 d3 3e  dis=26 d3 3e 90 90
    es aam $0xc0                        # gen=26 d4 c0  dis=26 d4 c0
    es aam $0x6                         # gen=26 d4 06  dis=26 d4 06
    es aad $0xc0                        # gen=26 d5 c0  dis=26 d5 c0
    es aad $0x6                         # gen=26 d5 06  dis=26 d5 06
    es salc                             # gen=26 d6 c0  dis=26 d6
    es salc                             # gen=26 d6 06  dis=26 d6
    xlat   %es:(%bx)                    # gen=26 d7 c0  dis=26 d7
    xlat   %es:(%bx)                    # gen=26 d7 06  dis=26 d7
    .byte 0x26,0xd8,0xc0            # fallback; gen=26 d8 c0
    .byte 0x26,0xd8,0x06            # fallback; gen=26 d8 06
    .byte 0x26,0xd8,0xc8            # fallback; gen=26 d8 c8
    .byte 0x26,0xd8,0xd0            # fallback; gen=26 d8 d0
    .byte 0x26,0xd8,0xd8            # fallback; gen=26 d8 d8
    .byte 0x26,0xd8,0xe0            # fallback; gen=26 d8 e0
    .byte 0x26,0xd8,0xe8            # fallback; gen=26 d8 e8
    .byte 0x26,0xd8,0xf0            # fallback; gen=26 d8 f0
    .byte 0x26,0xd8,0xf8            # fallback; gen=26 d8 f8
    .byte 0x26,0xd8,0x0e            # fallback; gen=26 d8 0e
    .byte 0x26,0xd8,0x16            # fallback; gen=26 d8 16
    .byte 0x26,0xd8,0x1e            # fallback; gen=26 d8 1e
    .byte 0x26,0xd8,0x26            # fallback; gen=26 d8 26
    .byte 0x26,0xd8,0x2e            # fallback; gen=26 d8 2e
    .byte 0x26,0xd8,0x36            # fallback; gen=26 d8 36
    .byte 0x26,0xd8,0x3e            # fallback; gen=26 d8 3e
    .byte 0x26,0xd9,0xc0            # fallback; gen=26 d9 c0
    .byte 0x26,0xd9,0x06            # fallback; gen=26 d9 06
    .byte 0x26,0xd9,0xc8            # fallback; gen=26 d9 c8
    .byte 0x26,0xd9,0xd0            # fallback; gen=26 d9 d0
    .byte 0x26,0xd9,0xd8            # fallback; gen=26 d9 d8
    .byte 0x26,0xd9,0xe0            # fallback; gen=26 d9 e0
    .byte 0x26,0xd9,0xe8            # fallback; gen=26 d9 e8
    .byte 0x26,0xd9,0xf0            # fallback; gen=26 d9 f0
    .byte 0x26,0xd9,0xf8            # fallback; gen=26 d9 f8
    .byte 0x26,0xd9,0x0e            # fallback; gen=26 d9 0e
    .byte 0x26,0xd9,0x16            # fallback; gen=26 d9 16
    .byte 0x26,0xd9,0x1e            # fallback; gen=26 d9 1e
    .byte 0x26,0xd9,0x26            # fallback; gen=26 d9 26
    .byte 0x26,0xd9,0x2e            # fallback; gen=26 d9 2e
    .byte 0x26,0xd9,0x36            # fallback; gen=26 d9 36
    .byte 0x26,0xd9,0x3e            # fallback; gen=26 d9 3e
    .byte 0x26,0xda,0xc0            # fallback; gen=26 da c0
    .byte 0x26,0xda,0x06            # fallback; gen=26 da 06
    .byte 0x26,0xda,0xc8            # fallback; gen=26 da c8
    .byte 0x26,0xda,0xd0            # fallback; gen=26 da d0
    .byte 0x26,0xda,0xd8            # fallback; gen=26 da d8
    .byte 0x26,0xda,0xe0            # fallback; gen=26 da e0
    .byte 0x26,0xda,0xe8            # fallback; gen=26 da e8
    .byte 0x26,0xda,0xf0            # fallback; gen=26 da f0
    .byte 0x26,0xda,0xf8            # fallback; gen=26 da f8
    .byte 0x26,0xda,0x0e            # fallback; gen=26 da 0e
    .byte 0x26,0xda,0x16            # fallback; gen=26 da 16
    .byte 0x26,0xda,0x1e            # fallback; gen=26 da 1e
    .byte 0x26,0xda,0x26            # fallback; gen=26 da 26
    .byte 0x26,0xda,0x2e            # fallback; gen=26 da 2e
    .byte 0x26,0xda,0x36            # fallback; gen=26 da 36
    .byte 0x26,0xda,0x3e            # fallback; gen=26 da 3e
    .byte 0x26,0xdb,0xc0            # fallback; gen=26 db c0
    .byte 0x26,0xdb,0x06            # fallback; gen=26 db 06
    .byte 0x26,0xdb,0xc8            # fallback; gen=26 db c8
    .byte 0x26,0xdb,0xd0            # fallback; gen=26 db d0
    .byte 0x26,0xdb,0xd8            # fallback; gen=26 db d8
    .byte 0x26,0xdb,0xe0            # fallback; gen=26 db e0
    .byte 0x26,0xdb,0xe8            # fallback; gen=26 db e8
    .byte 0x26,0xdb,0xf0            # fallback; gen=26 db f0
    .byte 0x26,0xdb,0xf8            # fallback; gen=26 db f8
    .byte 0x26,0xdb,0x0e            # fallback; gen=26 db 0e
    .byte 0x26,0xdb,0x16            # fallback; gen=26 db 16
    .byte 0x26,0xdb,0x1e            # fallback; gen=26 db 1e
    .byte 0x26,0xdb,0x26            # fallback; gen=26 db 26
    .byte 0x26,0xdb,0x2e            # fallback; gen=26 db 2e
    .byte 0x26,0xdb,0x36            # fallback; gen=26 db 36
    .byte 0x26,0xdb,0x3e            # fallback; gen=26 db 3e
    .byte 0x26,0xdc,0xc0            # fallback; gen=26 dc c0
    .byte 0x26,0xdc,0x06            # fallback; gen=26 dc 06
    .byte 0x26,0xdc,0xc8            # fallback; gen=26 dc c8
    .byte 0x26,0xdc,0xd0            # fallback; gen=26 dc d0
    .byte 0x26,0xdc,0xd8            # fallback; gen=26 dc d8
    .byte 0x26,0xdc,0xe0            # fallback; gen=26 dc e0
    .byte 0x26,0xdc,0xe8            # fallback; gen=26 dc e8
    .byte 0x26,0xdc,0xf0            # fallback; gen=26 dc f0
    .byte 0x26,0xdc,0xf8            # fallback; gen=26 dc f8
    .byte 0x26,0xdc,0x0e            # fallback; gen=26 dc 0e
    .byte 0x26,0xdc,0x16            # fallback; gen=26 dc 16
    .byte 0x26,0xdc,0x1e            # fallback; gen=26 dc 1e
    .byte 0x26,0xdc,0x26            # fallback; gen=26 dc 26
    .byte 0x26,0xdc,0x2e            # fallback; gen=26 dc 2e
    .byte 0x26,0xdc,0x36            # fallback; gen=26 dc 36
    .byte 0x26,0xdc,0x3e            # fallback; gen=26 dc 3e
    .byte 0x26,0xdd,0xc0            # fallback; gen=26 dd c0
    .byte 0x26,0xdd,0x06            # fallback; gen=26 dd 06
    .byte 0x26,0xdd,0xc8            # fallback; gen=26 dd c8
    .byte 0x26,0xdd,0xd0            # fallback; gen=26 dd d0
    .byte 0x26,0xdd,0xd8            # fallback; gen=26 dd d8
    .byte 0x26,0xdd,0xe0            # fallback; gen=26 dd e0
    .byte 0x26,0xdd,0xe8            # fallback; gen=26 dd e8
    .byte 0x26,0xdd,0xf0            # fallback; gen=26 dd f0
    .byte 0x26,0xdd,0xf8            # fallback; gen=26 dd f8
    .byte 0x26,0xdd,0x0e            # fallback; gen=26 dd 0e
    .byte 0x26,0xdd,0x16            # fallback; gen=26 dd 16
    .byte 0x26,0xdd,0x1e            # fallback; gen=26 dd 1e
    .byte 0x26,0xdd,0x26            # fallback; gen=26 dd 26
    .byte 0x26,0xdd,0x2e            # fallback; gen=26 dd 2e
    .byte 0x26,0xdd,0x36            # fallback; gen=26 dd 36
    .byte 0x26,0xdd,0x3e            # fallback; gen=26 dd 3e
    .byte 0x26,0xde,0xc0            # fallback; gen=26 de c0
    .byte 0x26,0xde,0x06            # fallback; gen=26 de 06
    .byte 0x26,0xde,0xc8            # fallback; gen=26 de c8
    .byte 0x26,0xde,0xd0            # fallback; gen=26 de d0
    .byte 0x26,0xde,0xd8            # fallback; gen=26 de d8
    .byte 0x26,0xde,0xe0            # fallback; gen=26 de e0
    .byte 0x26,0xde,0xe8            # fallback; gen=26 de e8
    .byte 0x26,0xde,0xf0            # fallback; gen=26 de f0
    .byte 0x26,0xde,0xf8            # fallback; gen=26 de f8
    .byte 0x26,0xde,0x0e            # fallback; gen=26 de 0e
    .byte 0x26,0xde,0x16            # fallback; gen=26 de 16
    .byte 0x26,0xde,0x1e            # fallback; gen=26 de 1e
    .byte 0x26,0xde,0x26            # fallback; gen=26 de 26
    .byte 0x26,0xde,0x2e            # fallback; gen=26 de 2e
    .byte 0x26,0xde,0x36            # fallback; gen=26 de 36
    .byte 0x26,0xde,0x3e            # fallback; gen=26 de 3e
    .byte 0x26,0xdf,0xc0            # fallback; gen=26 df c0
    .byte 0x26,0xdf,0x06            # fallback; gen=26 df 06
    .byte 0x26,0xdf,0xc8            # fallback; gen=26 df c8
    .byte 0x26,0xdf,0xd0            # fallback; gen=26 df d0
    .byte 0x26,0xdf,0xd8            # fallback; gen=26 df d8
    .byte 0x26,0xdf,0xe0            # fallback; gen=26 df e0
    .byte 0x26,0xdf,0xe8            # fallback; gen=26 df e8
    .byte 0x26,0xdf,0xf0            # fallback; gen=26 df f0
    .byte 0x26,0xdf,0xf8            # fallback; gen=26 df f8
    .byte 0x26,0xdf,0x0e            # fallback; gen=26 df 0e
    .byte 0x26,0xdf,0x16            # fallback; gen=26 df 16
    .byte 0x26,0xdf,0x1e            # fallback; gen=26 df 1e
    .byte 0x26,0xdf,0x26            # fallback; gen=26 df 26
    .byte 0x26,0xdf,0x2e            # fallback; gen=26 df 2e
    .byte 0x26,0xdf,0x36            # fallback; gen=26 df 36
    .byte 0x26,0xdf,0x3e            # fallback; gen=26 df 3e
    .byte 0x26,0xe0,0xc0            # fallback; gen=26 e0 c0
    .byte 0x26,0xe0,0x06            # fallback; gen=26 e0 06
    .byte 0x26,0xe1,0xc0            # fallback; gen=26 e1 c0
    .byte 0x26,0xe1,0x06            # fallback; gen=26 e1 06
    .byte 0x26,0xe2,0xc0            # fallback; gen=26 e2 c0
    .byte 0x26,0xe2,0x06            # fallback; gen=26 e2 06
    .byte 0x26,0xe3,0xc0            # fallback; gen=26 e3 c0
    .byte 0x26,0xe3,0x06            # fallback; gen=26 e3 06
    es in  $0xc0,%al                    # gen=26 e4 c0  dis=26 e4 c0
    es in  $0x6,%al                     # gen=26 e4 06  dis=26 e4 06
    es in  $0xc0,%ax                    # gen=26 e5 c0  dis=26 e5 c0
    es in  $0x6,%ax                     # gen=26 e5 06  dis=26 e5 06
    es out %al,$0xc0                    # gen=26 e6 c0  dis=26 e6 c0
    es out %al,$0x6                     # gen=26 e6 06  dis=26 e6 06
    es out %ax,$0xc0                    # gen=26 e7 c0  dis=26 e7 c0
    es out %ax,$0x6                     # gen=26 e7 06  dis=26 e7 06
    .byte 0x26,0xe8,0xc0            # fallback; gen=26 e8 c0
    .byte 0x26,0xe8,0x06            # fallback; gen=26 e8 06
    .byte 0x26,0xe9,0xc0            # fallback; gen=26 e9 c0
    .byte 0x26,0xe9,0x06            # fallback; gen=26 e9 06
    .byte 0x26,0xea,0xc0            # fallback; gen=26 ea c0
    .byte 0x26,0xea,0x06            # fallback; gen=26 ea 06
    .byte 0x26,0xeb,0xc0            # fallback; gen=26 eb c0
    .byte 0x26,0xeb,0x06            # fallback; gen=26 eb 06
    es in  (%dx),%al                    # gen=26 ec c0  dis=26 ec
    es in  (%dx),%al                    # gen=26 ec 06  dis=26 ec
    es in  (%dx),%ax                    # gen=26 ed c0  dis=26 ed
    es in  (%dx),%ax                    # gen=26 ed 06  dis=26 ed
    es out %al,(%dx)                    # gen=26 ee c0  dis=26 ee
    es out %al,(%dx)                    # gen=26 ee 06  dis=26 ee
    es out %ax,(%dx)                    # gen=26 ef c0  dis=26 ef
    es out %ax,(%dx)                    # gen=26 ef 06  dis=26 ef
    .byte 0x26,0xf0,0xc0            # fallback; gen=26 f0 c0
    .byte 0x26,0xf0,0x06            # fallback; gen=26 f0 06
    .byte 0x26,0xf0,0xc8            # fallback; gen=26 f0 c8
    .byte 0x26,0xf0,0xd0            # fallback; gen=26 f0 d0
    .byte 0x26,0xf0,0xd8            # fallback; gen=26 f0 d8
    .byte 0x26,0xf0,0xe0            # fallback; gen=26 f0 e0
    .byte 0x26,0xf0,0xe8            # fallback; gen=26 f0 e8
    .byte 0x26,0xf0,0xf0            # fallback; gen=26 f0 f0
    .byte 0x26,0xf0,0xf8            # fallback; gen=26 f0 f8
    .byte 0x26,0xf0,0x0e            # fallback; gen=26 f0 0e
    .byte 0x26,0xf0,0x16            # fallback; gen=26 f0 16
    .byte 0x26,0xf0,0x1e            # fallback; gen=26 f0 1e
    .byte 0x26,0xf0,0x26            # fallback; gen=26 f0 26
    .byte 0x26,0xf0,0x2e            # fallback; gen=26 f0 2e
    .byte 0x26,0xf0,0x36            # fallback; gen=26 f0 36
    .byte 0x26,0xf0,0x3e            # fallback; gen=26 f0 3e
    es int1                             # gen=26 f1 c0  dis=26 f1
    es int1                             # gen=26 f1 06  dis=26 f1
    .byte 0x26,0xf2,0xc0            # fallback; gen=26 f2 c0
    .byte 0x26,0xf2,0x06            # fallback; gen=26 f2 06
    .byte 0x26,0xf2,0xc8            # fallback; gen=26 f2 c8
    .byte 0x26,0xf2,0xd0            # fallback; gen=26 f2 d0
    .byte 0x26,0xf2,0xd8            # fallback; gen=26 f2 d8
    .byte 0x26,0xf2,0xe0            # fallback; gen=26 f2 e0
    .byte 0x26,0xf2,0xe8            # fallback; gen=26 f2 e8
    .byte 0x26,0xf2,0xf0            # fallback; gen=26 f2 f0
    .byte 0x26,0xf2,0xf8            # fallback; gen=26 f2 f8
    .byte 0x26,0xf2,0x0e            # fallback; gen=26 f2 0e
    .byte 0x26,0xf2,0x16            # fallback; gen=26 f2 16
    .byte 0x26,0xf2,0x1e            # fallback; gen=26 f2 1e
    .byte 0x26,0xf2,0x26            # fallback; gen=26 f2 26
    .byte 0x26,0xf2,0x2e            # fallback; gen=26 f2 2e
    .byte 0x26,0xf2,0x36            # fallback; gen=26 f2 36
    .byte 0x26,0xf2,0x3e            # fallback; gen=26 f2 3e
    .byte 0x26,0xf3,0xc0            # fallback; gen=26 f3 c0
    .byte 0x26,0xf3,0x06            # fallback; gen=26 f3 06
    .byte 0x26,0xf3,0xc8            # fallback; gen=26 f3 c8
    .byte 0x26,0xf3,0xd0            # fallback; gen=26 f3 d0
    .byte 0x26,0xf3,0xd8            # fallback; gen=26 f3 d8
    .byte 0x26,0xf3,0xe0            # fallback; gen=26 f3 e0
    .byte 0x26,0xf3,0xe8            # fallback; gen=26 f3 e8
    .byte 0x26,0xf3,0xf0            # fallback; gen=26 f3 f0
    .byte 0x26,0xf3,0xf8            # fallback; gen=26 f3 f8
    .byte 0x26,0xf3,0x0e            # fallback; gen=26 f3 0e
    .byte 0x26,0xf3,0x16            # fallback; gen=26 f3 16
    .byte 0x26,0xf3,0x1e            # fallback; gen=26 f3 1e
    .byte 0x26,0xf3,0x26            # fallback; gen=26 f3 26
    .byte 0x26,0xf3,0x2e            # fallback; gen=26 f3 2e
    .byte 0x26,0xf3,0x36            # fallback; gen=26 f3 36
    .byte 0x26,0xf3,0x3e            # fallback; gen=26 f3 3e
    es hlt                              # gen=26 f4 c0  dis=26 f4
    es hlt                              # gen=26 f4 06  dis=26 f4
    es cmc                              # gen=26 f5 c0  dis=26 f5
    es cmc                              # gen=26 f5 06  dis=26 f5
    es test $0x90,%al                   # gen=26 f6 c0  dis=26 f6 c0 90
    testb  $0x90,%es:-0x6f70            # gen=26 f6 06  dis=26 f6 06 90 90 90
    es test $0x90,%al                   # gen=26 f6 c8  dis=26 f6 c8 90
    es not %al                          # gen=26 f6 d0  dis=26 f6 d0
    es neg %al                          # gen=26 f6 d8  dis=26 f6 d8
    es mul %al                          # gen=26 f6 e0  dis=26 f6 e0
    es imul %al                         # gen=26 f6 e8  dis=26 f6 e8
    es div %al                          # gen=26 f6 f0  dis=26 f6 f0
    es idiv %al                         # gen=26 f6 f8  dis=26 f6 f8
    testb  $0x90,%es:-0x6f70            # gen=26 f6 0e  dis=26 f6 0e 90 90 90
    notb   %es:-0x6f70                  # gen=26 f6 16  dis=26 f6 16 90 90
    negb   %es:-0x6f70                  # gen=26 f6 1e  dis=26 f6 1e 90 90
    mulb   %es:-0x6f70                  # gen=26 f6 26  dis=26 f6 26 90 90
    imulb  %es:-0x6f70                  # gen=26 f6 2e  dis=26 f6 2e 90 90
    divb   %es:-0x6f70                  # gen=26 f6 36  dis=26 f6 36 90 90
    idivb  %es:-0x6f70                  # gen=26 f6 3e  dis=26 f6 3e 90 90
    es test $0x9090,%ax                 # gen=26 f7 c0  dis=26 f7 c0 90 90
    testw  $0x9090,%es:-0x6f70          # gen=26 f7 06  dis=26 f7 06 90 90 90 90
    es test $0x9090,%ax                 # gen=26 f7 c8  dis=26 f7 c8 90 90
    es not %ax                          # gen=26 f7 d0  dis=26 f7 d0
    es neg %ax                          # gen=26 f7 d8  dis=26 f7 d8
    es mul %ax                          # gen=26 f7 e0  dis=26 f7 e0
    es imul %ax                         # gen=26 f7 e8  dis=26 f7 e8
    es div %ax                          # gen=26 f7 f0  dis=26 f7 f0
    es idiv %ax                         # gen=26 f7 f8  dis=26 f7 f8
    testw  $0x9090,%es:-0x6f70          # gen=26 f7 0e  dis=26 f7 0e 90 90 90 90
    notw   %es:-0x6f70                  # gen=26 f7 16  dis=26 f7 16 90 90
    negw   %es:-0x6f70                  # gen=26 f7 1e  dis=26 f7 1e 90 90
    mulw   %es:-0x6f70                  # gen=26 f7 26  dis=26 f7 26 90 90
    imulw  %es:-0x6f70                  # gen=26 f7 2e  dis=26 f7 2e 90 90
    divw   %es:-0x6f70                  # gen=26 f7 36  dis=26 f7 36 90 90
    idivw  %es:-0x6f70                  # gen=26 f7 3e  dis=26 f7 3e 90 90
    es clc                              # gen=26 f8 c0  dis=26 f8
    es clc                              # gen=26 f8 06  dis=26 f8
    es stc                              # gen=26 f9 c0  dis=26 f9
    es stc                              # gen=26 f9 06  dis=26 f9
    es cli                              # gen=26 fa c0  dis=26 fa
    es cli                              # gen=26 fa 06  dis=26 fa
    es sti                              # gen=26 fb c0  dis=26 fb
    es sti                              # gen=26 fb 06  dis=26 fb
    es cld                              # gen=26 fc c0  dis=26 fc
    es cld                              # gen=26 fc 06  dis=26 fc
    es std                              # gen=26 fd c0  dis=26 fd
    es std                              # gen=26 fd 06  dis=26 fd
    es inc %al                          # gen=26 fe c0  dis=26 fe c0
    incb   %es:-0x6f70                  # gen=26 fe 06  dis=26 fe 06 90 90
    es dec %al                          # gen=26 fe c8  dis=26 fe c8
    .byte 0x26,0xfe,0xd0            # fallback; gen=26 fe d0
    .byte 0x26,0xfe,0xd8            # fallback; gen=26 fe d8
    .byte 0x26,0xfe,0xe0            # fallback; gen=26 fe e0
    .byte 0x26,0xfe,0xe8            # fallback; gen=26 fe e8
    .byte 0x26,0xfe,0xf0            # fallback; gen=26 fe f0
    .byte 0x26,0xfe,0xf8            # fallback; gen=26 fe f8
    decb   %es:-0x6f70                  # gen=26 fe 0e  dis=26 fe 0e 90 90
    .byte 0x26,0xfe,0x16            # fallback; gen=26 fe 16
    .byte 0x26,0xfe,0x1e            # fallback; gen=26 fe 1e
    .byte 0x26,0xfe,0x26            # fallback; gen=26 fe 26
    .byte 0x26,0xfe,0x2e            # fallback; gen=26 fe 2e
    .byte 0x26,0xfe,0x36            # fallback; gen=26 fe 36
    .byte 0x26,0xfe,0x3e            # fallback; gen=26 fe 3e
    es inc %ax                          # gen=26 ff c0  dis=26 ff c0
    incw   %es:-0x6f70                  # gen=26 ff 06  dis=26 ff 06 90 90
    es dec %ax                          # gen=26 ff c8  dis=26 ff c8
    es call *%ax                        # gen=26 ff d0  dis=26 ff d0
    .byte 0x26,0xff,0xd8            # fallback; gen=26 ff d8
    es jmp *%ax                         # gen=26 ff e0  dis=26 ff e0
    .byte 0x26,0xff,0xe8            # fallback; gen=26 ff e8
    es push %ax                         # gen=26 ff f0  dis=26 ff f0
    .byte 0x26,0xff,0xf8            # fallback; gen=26 ff f8
    decw   %es:-0x6f70                  # gen=26 ff 0e  dis=26 ff 0e 90 90
    call   *%es:-0x6f70                 # gen=26 ff 16  dis=26 ff 16 90 90
    lcall  *%es:-0x6f70                 # gen=26 ff 1e  dis=26 ff 1e 90 90
    jmp    *%es:-0x6f70                 # gen=26 ff 26  dis=26 ff 26 90 90
    ljmp   *%es:-0x6f70                 # gen=26 ff 2e  dis=26 ff 2e 90 90
    push   %es:-0x6f70                  # gen=26 ff 36  dis=26 ff 36 90 90
    .byte 0x26,0xff,0x3e            # fallback; gen=26 ff 3e
    cs add %al,%al                      # gen=2e 00 c0  dis=2e 00 c0
    add    %al,%cs:-0x6f70              # gen=2e 00 06  dis=2e 00 06 90 90
    cs add %cl,%al                      # gen=2e 00 c8  dis=2e 00 c8
    cs add %dl,%al                      # gen=2e 00 d0  dis=2e 00 d0
    cs add %bl,%al                      # gen=2e 00 d8  dis=2e 00 d8
    cs add %ah,%al                      # gen=2e 00 e0  dis=2e 00 e0
    cs add %ch,%al                      # gen=2e 00 e8  dis=2e 00 e8
    cs add %dh,%al                      # gen=2e 00 f0  dis=2e 00 f0
    cs add %bh,%al                      # gen=2e 00 f8  dis=2e 00 f8
    add    %cl,%cs:-0x6f70              # gen=2e 00 0e  dis=2e 00 0e 90 90
    add    %dl,%cs:-0x6f70              # gen=2e 00 16  dis=2e 00 16 90 90
    add    %bl,%cs:-0x6f70              # gen=2e 00 1e  dis=2e 00 1e 90 90
    add    %ah,%cs:-0x6f70              # gen=2e 00 26  dis=2e 00 26 90 90
    add    %ch,%cs:-0x6f70              # gen=2e 00 2e  dis=2e 00 2e 90 90
    add    %dh,%cs:-0x6f70              # gen=2e 00 36  dis=2e 00 36 90 90
    add    %bh,%cs:-0x6f70              # gen=2e 00 3e  dis=2e 00 3e 90 90
    cs add %ax,%ax                      # gen=2e 01 c0  dis=2e 01 c0
    add    %ax,%cs:-0x6f70              # gen=2e 01 06  dis=2e 01 06 90 90
    cs add %cx,%ax                      # gen=2e 01 c8  dis=2e 01 c8
    cs add %dx,%ax                      # gen=2e 01 d0  dis=2e 01 d0
    cs add %bx,%ax                      # gen=2e 01 d8  dis=2e 01 d8
    cs add %sp,%ax                      # gen=2e 01 e0  dis=2e 01 e0
    cs add %bp,%ax                      # gen=2e 01 e8  dis=2e 01 e8
    cs add %si,%ax                      # gen=2e 01 f0  dis=2e 01 f0
    cs add %di,%ax                      # gen=2e 01 f8  dis=2e 01 f8
    add    %cx,%cs:-0x6f70              # gen=2e 01 0e  dis=2e 01 0e 90 90
    add    %dx,%cs:-0x6f70              # gen=2e 01 16  dis=2e 01 16 90 90
    add    %bx,%cs:-0x6f70              # gen=2e 01 1e  dis=2e 01 1e 90 90
    add    %sp,%cs:-0x6f70              # gen=2e 01 26  dis=2e 01 26 90 90
    add    %bp,%cs:-0x6f70              # gen=2e 01 2e  dis=2e 01 2e 90 90
    add    %si,%cs:-0x6f70              # gen=2e 01 36  dis=2e 01 36 90 90
    add    %di,%cs:-0x6f70              # gen=2e 01 3e  dis=2e 01 3e 90 90
    cs add %al,%al                      # gen=2e 02 c0  dis=2e 02 c0
    add    %cs:-0x6f70,%al              # gen=2e 02 06  dis=2e 02 06 90 90
    cs add %al,%cl                      # gen=2e 02 c8  dis=2e 02 c8
    cs add %al,%dl                      # gen=2e 02 d0  dis=2e 02 d0
    cs add %al,%bl                      # gen=2e 02 d8  dis=2e 02 d8
    cs add %al,%ah                      # gen=2e 02 e0  dis=2e 02 e0
    cs add %al,%ch                      # gen=2e 02 e8  dis=2e 02 e8
    cs add %al,%dh                      # gen=2e 02 f0  dis=2e 02 f0
    cs add %al,%bh                      # gen=2e 02 f8  dis=2e 02 f8
    add    %cs:-0x6f70,%cl              # gen=2e 02 0e  dis=2e 02 0e 90 90
    add    %cs:-0x6f70,%dl              # gen=2e 02 16  dis=2e 02 16 90 90
    add    %cs:-0x6f70,%bl              # gen=2e 02 1e  dis=2e 02 1e 90 90
    add    %cs:-0x6f70,%ah              # gen=2e 02 26  dis=2e 02 26 90 90
    add    %cs:-0x6f70,%ch              # gen=2e 02 2e  dis=2e 02 2e 90 90
    add    %cs:-0x6f70,%dh              # gen=2e 02 36  dis=2e 02 36 90 90
    add    %cs:-0x6f70,%bh              # gen=2e 02 3e  dis=2e 02 3e 90 90
    cs add %ax,%ax                      # gen=2e 03 c0  dis=2e 03 c0
    add    %cs:-0x6f70,%ax              # gen=2e 03 06  dis=2e 03 06 90 90
    cs add %ax,%cx                      # gen=2e 03 c8  dis=2e 03 c8
    cs add %ax,%dx                      # gen=2e 03 d0  dis=2e 03 d0
    cs add %ax,%bx                      # gen=2e 03 d8  dis=2e 03 d8
    cs add %ax,%sp                      # gen=2e 03 e0  dis=2e 03 e0
    cs add %ax,%bp                      # gen=2e 03 e8  dis=2e 03 e8
    cs add %ax,%si                      # gen=2e 03 f0  dis=2e 03 f0
    cs add %ax,%di                      # gen=2e 03 f8  dis=2e 03 f8
    add    %cs:-0x6f70,%cx              # gen=2e 03 0e  dis=2e 03 0e 90 90
    add    %cs:-0x6f70,%dx              # gen=2e 03 16  dis=2e 03 16 90 90
    add    %cs:-0x6f70,%bx              # gen=2e 03 1e  dis=2e 03 1e 90 90
    add    %cs:-0x6f70,%sp              # gen=2e 03 26  dis=2e 03 26 90 90
    add    %cs:-0x6f70,%bp              # gen=2e 03 2e  dis=2e 03 2e 90 90
    add    %cs:-0x6f70,%si              # gen=2e 03 36  dis=2e 03 36 90 90
    add    %cs:-0x6f70,%di              # gen=2e 03 3e  dis=2e 03 3e 90 90
    cs add $0xc0,%al                    # gen=2e 04 c0  dis=2e 04 c0
    cs add $0x6,%al                     # gen=2e 04 06  dis=2e 04 06
    cs add $0x90c0,%ax                  # gen=2e 05 c0  dis=2e 05 c0 90
    cs add $0x9006,%ax                  # gen=2e 05 06  dis=2e 05 06 90
    cs push %es                         # gen=2e 06 c0  dis=2e 06
    cs push %es                         # gen=2e 06 06  dis=2e 06
    cs pop %es                          # gen=2e 07 c0  dis=2e 07
    cs pop %es                          # gen=2e 07 06  dis=2e 07
    cs or  %al,%al                      # gen=2e 08 c0  dis=2e 08 c0
    or     %al,%cs:-0x6f70              # gen=2e 08 06  dis=2e 08 06 90 90
    cs or  %cl,%al                      # gen=2e 08 c8  dis=2e 08 c8
    cs or  %dl,%al                      # gen=2e 08 d0  dis=2e 08 d0
    cs or  %bl,%al                      # gen=2e 08 d8  dis=2e 08 d8
    cs or  %ah,%al                      # gen=2e 08 e0  dis=2e 08 e0
    cs or  %ch,%al                      # gen=2e 08 e8  dis=2e 08 e8
    cs or  %dh,%al                      # gen=2e 08 f0  dis=2e 08 f0
    cs or  %bh,%al                      # gen=2e 08 f8  dis=2e 08 f8
    or     %cl,%cs:-0x6f70              # gen=2e 08 0e  dis=2e 08 0e 90 90
    or     %dl,%cs:-0x6f70              # gen=2e 08 16  dis=2e 08 16 90 90
    or     %bl,%cs:-0x6f70              # gen=2e 08 1e  dis=2e 08 1e 90 90
    or     %ah,%cs:-0x6f70              # gen=2e 08 26  dis=2e 08 26 90 90
    or     %ch,%cs:-0x6f70              # gen=2e 08 2e  dis=2e 08 2e 90 90
    or     %dh,%cs:-0x6f70              # gen=2e 08 36  dis=2e 08 36 90 90
    or     %bh,%cs:-0x6f70              # gen=2e 08 3e  dis=2e 08 3e 90 90
    cs or  %ax,%ax                      # gen=2e 09 c0  dis=2e 09 c0
    or     %ax,%cs:-0x6f70              # gen=2e 09 06  dis=2e 09 06 90 90
    cs or  %cx,%ax                      # gen=2e 09 c8  dis=2e 09 c8
    cs or  %dx,%ax                      # gen=2e 09 d0  dis=2e 09 d0
    cs or  %bx,%ax                      # gen=2e 09 d8  dis=2e 09 d8
    cs or  %sp,%ax                      # gen=2e 09 e0  dis=2e 09 e0
    cs or  %bp,%ax                      # gen=2e 09 e8  dis=2e 09 e8
    cs or  %si,%ax                      # gen=2e 09 f0  dis=2e 09 f0
    cs or  %di,%ax                      # gen=2e 09 f8  dis=2e 09 f8
    or     %cx,%cs:-0x6f70              # gen=2e 09 0e  dis=2e 09 0e 90 90
    or     %dx,%cs:-0x6f70              # gen=2e 09 16  dis=2e 09 16 90 90
    or     %bx,%cs:-0x6f70              # gen=2e 09 1e  dis=2e 09 1e 90 90
    or     %sp,%cs:-0x6f70              # gen=2e 09 26  dis=2e 09 26 90 90
    or     %bp,%cs:-0x6f70              # gen=2e 09 2e  dis=2e 09 2e 90 90
    or     %si,%cs:-0x6f70              # gen=2e 09 36  dis=2e 09 36 90 90
    or     %di,%cs:-0x6f70              # gen=2e 09 3e  dis=2e 09 3e 90 90
    cs or  %al,%al                      # gen=2e 0a c0  dis=2e 0a c0
    or     %cs:-0x6f70,%al              # gen=2e 0a 06  dis=2e 0a 06 90 90
    cs or  %al,%cl                      # gen=2e 0a c8  dis=2e 0a c8
    cs or  %al,%dl                      # gen=2e 0a d0  dis=2e 0a d0
    cs or  %al,%bl                      # gen=2e 0a d8  dis=2e 0a d8
    cs or  %al,%ah                      # gen=2e 0a e0  dis=2e 0a e0
    cs or  %al,%ch                      # gen=2e 0a e8  dis=2e 0a e8
    cs or  %al,%dh                      # gen=2e 0a f0  dis=2e 0a f0
    cs or  %al,%bh                      # gen=2e 0a f8  dis=2e 0a f8
    or     %cs:-0x6f70,%cl              # gen=2e 0a 0e  dis=2e 0a 0e 90 90
    or     %cs:-0x6f70,%dl              # gen=2e 0a 16  dis=2e 0a 16 90 90
    or     %cs:-0x6f70,%bl              # gen=2e 0a 1e  dis=2e 0a 1e 90 90
    or     %cs:-0x6f70,%ah              # gen=2e 0a 26  dis=2e 0a 26 90 90
    or     %cs:-0x6f70,%ch              # gen=2e 0a 2e  dis=2e 0a 2e 90 90
    or     %cs:-0x6f70,%dh              # gen=2e 0a 36  dis=2e 0a 36 90 90
    or     %cs:-0x6f70,%bh              # gen=2e 0a 3e  dis=2e 0a 3e 90 90
    cs or  %ax,%ax                      # gen=2e 0b c0  dis=2e 0b c0
    or     %cs:-0x6f70,%ax              # gen=2e 0b 06  dis=2e 0b 06 90 90
    cs or  %ax,%cx                      # gen=2e 0b c8  dis=2e 0b c8
    cs or  %ax,%dx                      # gen=2e 0b d0  dis=2e 0b d0
    cs or  %ax,%bx                      # gen=2e 0b d8  dis=2e 0b d8
    cs or  %ax,%sp                      # gen=2e 0b e0  dis=2e 0b e0
    cs or  %ax,%bp                      # gen=2e 0b e8  dis=2e 0b e8
    cs or  %ax,%si                      # gen=2e 0b f0  dis=2e 0b f0
    cs or  %ax,%di                      # gen=2e 0b f8  dis=2e 0b f8
    or     %cs:-0x6f70,%cx              # gen=2e 0b 0e  dis=2e 0b 0e 90 90
    or     %cs:-0x6f70,%dx              # gen=2e 0b 16  dis=2e 0b 16 90 90
    or     %cs:-0x6f70,%bx              # gen=2e 0b 1e  dis=2e 0b 1e 90 90
    or     %cs:-0x6f70,%sp              # gen=2e 0b 26  dis=2e 0b 26 90 90
    or     %cs:-0x6f70,%bp              # gen=2e 0b 2e  dis=2e 0b 2e 90 90
    or     %cs:-0x6f70,%si              # gen=2e 0b 36  dis=2e 0b 36 90 90
    or     %cs:-0x6f70,%di              # gen=2e 0b 3e  dis=2e 0b 3e 90 90
    cs or  $0xc0,%al                    # gen=2e 0c c0  dis=2e 0c c0
    cs or  $0x6,%al                     # gen=2e 0c 06  dis=2e 0c 06
    cs or  $0x90c0,%ax                  # gen=2e 0d c0  dis=2e 0d c0 90
    cs or  $0x9006,%ax                  # gen=2e 0d 06  dis=2e 0d 06 90
    cs push %cs                         # gen=2e 0e c0  dis=2e 0e
    cs push %cs                         # gen=2e 0e 06  dis=2e 0e
    .byte 0x2e,0x0f,0xc0            # fallback; gen=2e 0f c0
    .byte 0x2e,0x0f,0x06            # fallback; gen=2e 0f 06
    .byte 0x2e,0x0f,0xc8            # fallback; gen=2e 0f c8
    .byte 0x2e,0x0f,0xd0            # fallback; gen=2e 0f d0
    .byte 0x2e,0x0f,0xd8            # fallback; gen=2e 0f d8
    .byte 0x2e,0x0f,0xe0            # fallback; gen=2e 0f e0
    .byte 0x2e,0x0f,0xe8            # fallback; gen=2e 0f e8
    .byte 0x2e,0x0f,0xf0            # fallback; gen=2e 0f f0
    .byte 0x2e,0x0f,0xf8            # fallback; gen=2e 0f f8
    .byte 0x2e,0x0f,0x0e            # fallback; gen=2e 0f 0e
    .byte 0x2e,0x0f,0x16            # fallback; gen=2e 0f 16
    .byte 0x2e,0x0f,0x1e            # fallback; gen=2e 0f 1e
    .byte 0x2e,0x0f,0x26            # fallback; gen=2e 0f 26
    .byte 0x2e,0x0f,0x2e            # fallback; gen=2e 0f 2e
    .byte 0x2e,0x0f,0x36            # fallback; gen=2e 0f 36
    .byte 0x2e,0x0f,0x3e            # fallback; gen=2e 0f 3e
    cs adc %al,%al                      # gen=2e 10 c0  dis=2e 10 c0
    adc    %al,%cs:-0x6f70              # gen=2e 10 06  dis=2e 10 06 90 90
    cs adc %cl,%al                      # gen=2e 10 c8  dis=2e 10 c8
    cs adc %dl,%al                      # gen=2e 10 d0  dis=2e 10 d0
    cs adc %bl,%al                      # gen=2e 10 d8  dis=2e 10 d8
    cs adc %ah,%al                      # gen=2e 10 e0  dis=2e 10 e0
    cs adc %ch,%al                      # gen=2e 10 e8  dis=2e 10 e8
    cs adc %dh,%al                      # gen=2e 10 f0  dis=2e 10 f0
    cs adc %bh,%al                      # gen=2e 10 f8  dis=2e 10 f8
    adc    %cl,%cs:-0x6f70              # gen=2e 10 0e  dis=2e 10 0e 90 90
    adc    %dl,%cs:-0x6f70              # gen=2e 10 16  dis=2e 10 16 90 90
    adc    %bl,%cs:-0x6f70              # gen=2e 10 1e  dis=2e 10 1e 90 90
    adc    %ah,%cs:-0x6f70              # gen=2e 10 26  dis=2e 10 26 90 90
    adc    %ch,%cs:-0x6f70              # gen=2e 10 2e  dis=2e 10 2e 90 90
    adc    %dh,%cs:-0x6f70              # gen=2e 10 36  dis=2e 10 36 90 90
    adc    %bh,%cs:-0x6f70              # gen=2e 10 3e  dis=2e 10 3e 90 90
    cs adc %ax,%ax                      # gen=2e 11 c0  dis=2e 11 c0
    adc    %ax,%cs:-0x6f70              # gen=2e 11 06  dis=2e 11 06 90 90
    cs adc %cx,%ax                      # gen=2e 11 c8  dis=2e 11 c8
    cs adc %dx,%ax                      # gen=2e 11 d0  dis=2e 11 d0
    cs adc %bx,%ax                      # gen=2e 11 d8  dis=2e 11 d8
    cs adc %sp,%ax                      # gen=2e 11 e0  dis=2e 11 e0
    cs adc %bp,%ax                      # gen=2e 11 e8  dis=2e 11 e8
    cs adc %si,%ax                      # gen=2e 11 f0  dis=2e 11 f0
    cs adc %di,%ax                      # gen=2e 11 f8  dis=2e 11 f8
    adc    %cx,%cs:-0x6f70              # gen=2e 11 0e  dis=2e 11 0e 90 90
    adc    %dx,%cs:-0x6f70              # gen=2e 11 16  dis=2e 11 16 90 90
    adc    %bx,%cs:-0x6f70              # gen=2e 11 1e  dis=2e 11 1e 90 90
    adc    %sp,%cs:-0x6f70              # gen=2e 11 26  dis=2e 11 26 90 90
    adc    %bp,%cs:-0x6f70              # gen=2e 11 2e  dis=2e 11 2e 90 90
    adc    %si,%cs:-0x6f70              # gen=2e 11 36  dis=2e 11 36 90 90
    adc    %di,%cs:-0x6f70              # gen=2e 11 3e  dis=2e 11 3e 90 90
    cs adc %al,%al                      # gen=2e 12 c0  dis=2e 12 c0
    adc    %cs:-0x6f70,%al              # gen=2e 12 06  dis=2e 12 06 90 90
    cs adc %al,%cl                      # gen=2e 12 c8  dis=2e 12 c8
    cs adc %al,%dl                      # gen=2e 12 d0  dis=2e 12 d0
    cs adc %al,%bl                      # gen=2e 12 d8  dis=2e 12 d8
    cs adc %al,%ah                      # gen=2e 12 e0  dis=2e 12 e0
    cs adc %al,%ch                      # gen=2e 12 e8  dis=2e 12 e8
    cs adc %al,%dh                      # gen=2e 12 f0  dis=2e 12 f0
    cs adc %al,%bh                      # gen=2e 12 f8  dis=2e 12 f8
    adc    %cs:-0x6f70,%cl              # gen=2e 12 0e  dis=2e 12 0e 90 90
    adc    %cs:-0x6f70,%dl              # gen=2e 12 16  dis=2e 12 16 90 90
    adc    %cs:-0x6f70,%bl              # gen=2e 12 1e  dis=2e 12 1e 90 90
    adc    %cs:-0x6f70,%ah              # gen=2e 12 26  dis=2e 12 26 90 90
    adc    %cs:-0x6f70,%ch              # gen=2e 12 2e  dis=2e 12 2e 90 90
    adc    %cs:-0x6f70,%dh              # gen=2e 12 36  dis=2e 12 36 90 90
    adc    %cs:-0x6f70,%bh              # gen=2e 12 3e  dis=2e 12 3e 90 90
    cs adc %ax,%ax                      # gen=2e 13 c0  dis=2e 13 c0
    adc    %cs:-0x6f70,%ax              # gen=2e 13 06  dis=2e 13 06 90 90
    cs adc %ax,%cx                      # gen=2e 13 c8  dis=2e 13 c8
    cs adc %ax,%dx                      # gen=2e 13 d0  dis=2e 13 d0
    cs adc %ax,%bx                      # gen=2e 13 d8  dis=2e 13 d8
    cs adc %ax,%sp                      # gen=2e 13 e0  dis=2e 13 e0
    cs adc %ax,%bp                      # gen=2e 13 e8  dis=2e 13 e8
    cs adc %ax,%si                      # gen=2e 13 f0  dis=2e 13 f0
    cs adc %ax,%di                      # gen=2e 13 f8  dis=2e 13 f8
    adc    %cs:-0x6f70,%cx              # gen=2e 13 0e  dis=2e 13 0e 90 90
    adc    %cs:-0x6f70,%dx              # gen=2e 13 16  dis=2e 13 16 90 90
    adc    %cs:-0x6f70,%bx              # gen=2e 13 1e  dis=2e 13 1e 90 90
    adc    %cs:-0x6f70,%sp              # gen=2e 13 26  dis=2e 13 26 90 90
    adc    %cs:-0x6f70,%bp              # gen=2e 13 2e  dis=2e 13 2e 90 90
    adc    %cs:-0x6f70,%si              # gen=2e 13 36  dis=2e 13 36 90 90
    adc    %cs:-0x6f70,%di              # gen=2e 13 3e  dis=2e 13 3e 90 90
    cs adc $0xc0,%al                    # gen=2e 14 c0  dis=2e 14 c0
    cs adc $0x6,%al                     # gen=2e 14 06  dis=2e 14 06
    cs adc $0x90c0,%ax                  # gen=2e 15 c0  dis=2e 15 c0 90
    cs adc $0x9006,%ax                  # gen=2e 15 06  dis=2e 15 06 90
    cs push %ss                         # gen=2e 16 c0  dis=2e 16
    cs push %ss                         # gen=2e 16 06  dis=2e 16
    cs pop %ss                          # gen=2e 17 c0  dis=2e 17
    cs pop %ss                          # gen=2e 17 06  dis=2e 17
    cs sbb %al,%al                      # gen=2e 18 c0  dis=2e 18 c0
    sbb    %al,%cs:-0x6f70              # gen=2e 18 06  dis=2e 18 06 90 90
    cs sbb %cl,%al                      # gen=2e 18 c8  dis=2e 18 c8
    cs sbb %dl,%al                      # gen=2e 18 d0  dis=2e 18 d0
    cs sbb %bl,%al                      # gen=2e 18 d8  dis=2e 18 d8
    cs sbb %ah,%al                      # gen=2e 18 e0  dis=2e 18 e0
    cs sbb %ch,%al                      # gen=2e 18 e8  dis=2e 18 e8
    cs sbb %dh,%al                      # gen=2e 18 f0  dis=2e 18 f0
    cs sbb %bh,%al                      # gen=2e 18 f8  dis=2e 18 f8
    sbb    %cl,%cs:-0x6f70              # gen=2e 18 0e  dis=2e 18 0e 90 90
    sbb    %dl,%cs:-0x6f70              # gen=2e 18 16  dis=2e 18 16 90 90
    sbb    %bl,%cs:-0x6f70              # gen=2e 18 1e  dis=2e 18 1e 90 90
    sbb    %ah,%cs:-0x6f70              # gen=2e 18 26  dis=2e 18 26 90 90
    sbb    %ch,%cs:-0x6f70              # gen=2e 18 2e  dis=2e 18 2e 90 90
    sbb    %dh,%cs:-0x6f70              # gen=2e 18 36  dis=2e 18 36 90 90
    sbb    %bh,%cs:-0x6f70              # gen=2e 18 3e  dis=2e 18 3e 90 90
    cs sbb %ax,%ax                      # gen=2e 19 c0  dis=2e 19 c0
    sbb    %ax,%cs:-0x6f70              # gen=2e 19 06  dis=2e 19 06 90 90
    cs sbb %cx,%ax                      # gen=2e 19 c8  dis=2e 19 c8
    cs sbb %dx,%ax                      # gen=2e 19 d0  dis=2e 19 d0
    cs sbb %bx,%ax                      # gen=2e 19 d8  dis=2e 19 d8
    cs sbb %sp,%ax                      # gen=2e 19 e0  dis=2e 19 e0
    cs sbb %bp,%ax                      # gen=2e 19 e8  dis=2e 19 e8
    cs sbb %si,%ax                      # gen=2e 19 f0  dis=2e 19 f0
    cs sbb %di,%ax                      # gen=2e 19 f8  dis=2e 19 f8
    sbb    %cx,%cs:-0x6f70              # gen=2e 19 0e  dis=2e 19 0e 90 90
    sbb    %dx,%cs:-0x6f70              # gen=2e 19 16  dis=2e 19 16 90 90
    sbb    %bx,%cs:-0x6f70              # gen=2e 19 1e  dis=2e 19 1e 90 90
    sbb    %sp,%cs:-0x6f70              # gen=2e 19 26  dis=2e 19 26 90 90
    sbb    %bp,%cs:-0x6f70              # gen=2e 19 2e  dis=2e 19 2e 90 90
    sbb    %si,%cs:-0x6f70              # gen=2e 19 36  dis=2e 19 36 90 90
    sbb    %di,%cs:-0x6f70              # gen=2e 19 3e  dis=2e 19 3e 90 90
    cs sbb %al,%al                      # gen=2e 1a c0  dis=2e 1a c0
    sbb    %cs:-0x6f70,%al              # gen=2e 1a 06  dis=2e 1a 06 90 90
    cs sbb %al,%cl                      # gen=2e 1a c8  dis=2e 1a c8
    cs sbb %al,%dl                      # gen=2e 1a d0  dis=2e 1a d0
    cs sbb %al,%bl                      # gen=2e 1a d8  dis=2e 1a d8
    cs sbb %al,%ah                      # gen=2e 1a e0  dis=2e 1a e0
    cs sbb %al,%ch                      # gen=2e 1a e8  dis=2e 1a e8
    cs sbb %al,%dh                      # gen=2e 1a f0  dis=2e 1a f0
    cs sbb %al,%bh                      # gen=2e 1a f8  dis=2e 1a f8
    sbb    %cs:-0x6f70,%cl              # gen=2e 1a 0e  dis=2e 1a 0e 90 90
    sbb    %cs:-0x6f70,%dl              # gen=2e 1a 16  dis=2e 1a 16 90 90
    sbb    %cs:-0x6f70,%bl              # gen=2e 1a 1e  dis=2e 1a 1e 90 90
    sbb    %cs:-0x6f70,%ah              # gen=2e 1a 26  dis=2e 1a 26 90 90
    sbb    %cs:-0x6f70,%ch              # gen=2e 1a 2e  dis=2e 1a 2e 90 90
    sbb    %cs:-0x6f70,%dh              # gen=2e 1a 36  dis=2e 1a 36 90 90
    sbb    %cs:-0x6f70,%bh              # gen=2e 1a 3e  dis=2e 1a 3e 90 90
    cs sbb %ax,%ax                      # gen=2e 1b c0  dis=2e 1b c0
    sbb    %cs:-0x6f70,%ax              # gen=2e 1b 06  dis=2e 1b 06 90 90
    cs sbb %ax,%cx                      # gen=2e 1b c8  dis=2e 1b c8
    cs sbb %ax,%dx                      # gen=2e 1b d0  dis=2e 1b d0
    cs sbb %ax,%bx                      # gen=2e 1b d8  dis=2e 1b d8
    cs sbb %ax,%sp                      # gen=2e 1b e0  dis=2e 1b e0
    cs sbb %ax,%bp                      # gen=2e 1b e8  dis=2e 1b e8
    cs sbb %ax,%si                      # gen=2e 1b f0  dis=2e 1b f0
    cs sbb %ax,%di                      # gen=2e 1b f8  dis=2e 1b f8
    sbb    %cs:-0x6f70,%cx              # gen=2e 1b 0e  dis=2e 1b 0e 90 90
    sbb    %cs:-0x6f70,%dx              # gen=2e 1b 16  dis=2e 1b 16 90 90
    sbb    %cs:-0x6f70,%bx              # gen=2e 1b 1e  dis=2e 1b 1e 90 90
    sbb    %cs:-0x6f70,%sp              # gen=2e 1b 26  dis=2e 1b 26 90 90
    sbb    %cs:-0x6f70,%bp              # gen=2e 1b 2e  dis=2e 1b 2e 90 90
    sbb    %cs:-0x6f70,%si              # gen=2e 1b 36  dis=2e 1b 36 90 90
    sbb    %cs:-0x6f70,%di              # gen=2e 1b 3e  dis=2e 1b 3e 90 90
    cs sbb $0xc0,%al                    # gen=2e 1c c0  dis=2e 1c c0
    cs sbb $0x6,%al                     # gen=2e 1c 06  dis=2e 1c 06
    cs sbb $0x90c0,%ax                  # gen=2e 1d c0  dis=2e 1d c0 90
    cs sbb $0x9006,%ax                  # gen=2e 1d 06  dis=2e 1d 06 90
    cs push %ds                         # gen=2e 1e c0  dis=2e 1e
    cs push %ds                         # gen=2e 1e 06  dis=2e 1e
    cs pop %ds                          # gen=2e 1f c0  dis=2e 1f
    cs pop %ds                          # gen=2e 1f 06  dis=2e 1f
    cs and %al,%al                      # gen=2e 20 c0  dis=2e 20 c0
    and    %al,%cs:-0x6f70              # gen=2e 20 06  dis=2e 20 06 90 90
    cs and %cl,%al                      # gen=2e 20 c8  dis=2e 20 c8
    cs and %dl,%al                      # gen=2e 20 d0  dis=2e 20 d0
    cs and %bl,%al                      # gen=2e 20 d8  dis=2e 20 d8
    cs and %ah,%al                      # gen=2e 20 e0  dis=2e 20 e0
    cs and %ch,%al                      # gen=2e 20 e8  dis=2e 20 e8
    cs and %dh,%al                      # gen=2e 20 f0  dis=2e 20 f0
    cs and %bh,%al                      # gen=2e 20 f8  dis=2e 20 f8
    and    %cl,%cs:-0x6f70              # gen=2e 20 0e  dis=2e 20 0e 90 90
    and    %dl,%cs:-0x6f70              # gen=2e 20 16  dis=2e 20 16 90 90
    and    %bl,%cs:-0x6f70              # gen=2e 20 1e  dis=2e 20 1e 90 90
    and    %ah,%cs:-0x6f70              # gen=2e 20 26  dis=2e 20 26 90 90
    and    %ch,%cs:-0x6f70              # gen=2e 20 2e  dis=2e 20 2e 90 90
    and    %dh,%cs:-0x6f70              # gen=2e 20 36  dis=2e 20 36 90 90
    and    %bh,%cs:-0x6f70              # gen=2e 20 3e  dis=2e 20 3e 90 90
    cs and %ax,%ax                      # gen=2e 21 c0  dis=2e 21 c0
    and    %ax,%cs:-0x6f70              # gen=2e 21 06  dis=2e 21 06 90 90
    cs and %cx,%ax                      # gen=2e 21 c8  dis=2e 21 c8
    cs and %dx,%ax                      # gen=2e 21 d0  dis=2e 21 d0
    cs and %bx,%ax                      # gen=2e 21 d8  dis=2e 21 d8
    cs and %sp,%ax                      # gen=2e 21 e0  dis=2e 21 e0
    cs and %bp,%ax                      # gen=2e 21 e8  dis=2e 21 e8
    cs and %si,%ax                      # gen=2e 21 f0  dis=2e 21 f0
    cs and %di,%ax                      # gen=2e 21 f8  dis=2e 21 f8
    and    %cx,%cs:-0x6f70              # gen=2e 21 0e  dis=2e 21 0e 90 90
    and    %dx,%cs:-0x6f70              # gen=2e 21 16  dis=2e 21 16 90 90
    and    %bx,%cs:-0x6f70              # gen=2e 21 1e  dis=2e 21 1e 90 90
    and    %sp,%cs:-0x6f70              # gen=2e 21 26  dis=2e 21 26 90 90
    and    %bp,%cs:-0x6f70              # gen=2e 21 2e  dis=2e 21 2e 90 90
    and    %si,%cs:-0x6f70              # gen=2e 21 36  dis=2e 21 36 90 90
    and    %di,%cs:-0x6f70              # gen=2e 21 3e  dis=2e 21 3e 90 90
    cs and %al,%al                      # gen=2e 22 c0  dis=2e 22 c0
    and    %cs:-0x6f70,%al              # gen=2e 22 06  dis=2e 22 06 90 90
    cs and %al,%cl                      # gen=2e 22 c8  dis=2e 22 c8
    cs and %al,%dl                      # gen=2e 22 d0  dis=2e 22 d0
    cs and %al,%bl                      # gen=2e 22 d8  dis=2e 22 d8
    cs and %al,%ah                      # gen=2e 22 e0  dis=2e 22 e0
    cs and %al,%ch                      # gen=2e 22 e8  dis=2e 22 e8
    cs and %al,%dh                      # gen=2e 22 f0  dis=2e 22 f0
    cs and %al,%bh                      # gen=2e 22 f8  dis=2e 22 f8
    and    %cs:-0x6f70,%cl              # gen=2e 22 0e  dis=2e 22 0e 90 90
    and    %cs:-0x6f70,%dl              # gen=2e 22 16  dis=2e 22 16 90 90
    and    %cs:-0x6f70,%bl              # gen=2e 22 1e  dis=2e 22 1e 90 90
    and    %cs:-0x6f70,%ah              # gen=2e 22 26  dis=2e 22 26 90 90
    and    %cs:-0x6f70,%ch              # gen=2e 22 2e  dis=2e 22 2e 90 90
    and    %cs:-0x6f70,%dh              # gen=2e 22 36  dis=2e 22 36 90 90
    and    %cs:-0x6f70,%bh              # gen=2e 22 3e  dis=2e 22 3e 90 90
    cs and %ax,%ax                      # gen=2e 23 c0  dis=2e 23 c0
    and    %cs:-0x6f70,%ax              # gen=2e 23 06  dis=2e 23 06 90 90
    cs and %ax,%cx                      # gen=2e 23 c8  dis=2e 23 c8
    cs and %ax,%dx                      # gen=2e 23 d0  dis=2e 23 d0
    cs and %ax,%bx                      # gen=2e 23 d8  dis=2e 23 d8
    cs and %ax,%sp                      # gen=2e 23 e0  dis=2e 23 e0
    cs and %ax,%bp                      # gen=2e 23 e8  dis=2e 23 e8
    cs and %ax,%si                      # gen=2e 23 f0  dis=2e 23 f0
    cs and %ax,%di                      # gen=2e 23 f8  dis=2e 23 f8
    and    %cs:-0x6f70,%cx              # gen=2e 23 0e  dis=2e 23 0e 90 90
    and    %cs:-0x6f70,%dx              # gen=2e 23 16  dis=2e 23 16 90 90
    and    %cs:-0x6f70,%bx              # gen=2e 23 1e  dis=2e 23 1e 90 90
    and    %cs:-0x6f70,%sp              # gen=2e 23 26  dis=2e 23 26 90 90
    and    %cs:-0x6f70,%bp              # gen=2e 23 2e  dis=2e 23 2e 90 90
    and    %cs:-0x6f70,%si              # gen=2e 23 36  dis=2e 23 36 90 90
    and    %cs:-0x6f70,%di              # gen=2e 23 3e  dis=2e 23 3e 90 90
    cs and $0xc0,%al                    # gen=2e 24 c0  dis=2e 24 c0
    cs and $0x6,%al                     # gen=2e 24 06  dis=2e 24 06
    cs and $0x90c0,%ax                  # gen=2e 25 c0  dis=2e 25 c0 90
    cs and $0x9006,%ax                  # gen=2e 25 06  dis=2e 25 06 90
    .byte 0x2e,0x26,0xc0            # fallback; gen=2e 26 c0
    .byte 0x2e,0x26,0x06            # fallback; gen=2e 26 06
    cs daa                              # gen=2e 27 c0  dis=2e 27
    cs daa                              # gen=2e 27 06  dis=2e 27
    cs sub %al,%al                      # gen=2e 28 c0  dis=2e 28 c0
    sub    %al,%cs:-0x6f70              # gen=2e 28 06  dis=2e 28 06 90 90
    cs sub %cl,%al                      # gen=2e 28 c8  dis=2e 28 c8
    cs sub %dl,%al                      # gen=2e 28 d0  dis=2e 28 d0
    cs sub %bl,%al                      # gen=2e 28 d8  dis=2e 28 d8
    cs sub %ah,%al                      # gen=2e 28 e0  dis=2e 28 e0
    cs sub %ch,%al                      # gen=2e 28 e8  dis=2e 28 e8
    cs sub %dh,%al                      # gen=2e 28 f0  dis=2e 28 f0
    cs sub %bh,%al                      # gen=2e 28 f8  dis=2e 28 f8
    sub    %cl,%cs:-0x6f70              # gen=2e 28 0e  dis=2e 28 0e 90 90
    sub    %dl,%cs:-0x6f70              # gen=2e 28 16  dis=2e 28 16 90 90
    sub    %bl,%cs:-0x6f70              # gen=2e 28 1e  dis=2e 28 1e 90 90
    sub    %ah,%cs:-0x6f70              # gen=2e 28 26  dis=2e 28 26 90 90
    sub    %ch,%cs:-0x6f70              # gen=2e 28 2e  dis=2e 28 2e 90 90
    sub    %dh,%cs:-0x6f70              # gen=2e 28 36  dis=2e 28 36 90 90
    sub    %bh,%cs:-0x6f70              # gen=2e 28 3e  dis=2e 28 3e 90 90
    cs sub %ax,%ax                      # gen=2e 29 c0  dis=2e 29 c0
    sub    %ax,%cs:-0x6f70              # gen=2e 29 06  dis=2e 29 06 90 90
    cs sub %cx,%ax                      # gen=2e 29 c8  dis=2e 29 c8
    cs sub %dx,%ax                      # gen=2e 29 d0  dis=2e 29 d0
    cs sub %bx,%ax                      # gen=2e 29 d8  dis=2e 29 d8
    cs sub %sp,%ax                      # gen=2e 29 e0  dis=2e 29 e0
    cs sub %bp,%ax                      # gen=2e 29 e8  dis=2e 29 e8
    cs sub %si,%ax                      # gen=2e 29 f0  dis=2e 29 f0
    cs sub %di,%ax                      # gen=2e 29 f8  dis=2e 29 f8
    sub    %cx,%cs:-0x6f70              # gen=2e 29 0e  dis=2e 29 0e 90 90
    sub    %dx,%cs:-0x6f70              # gen=2e 29 16  dis=2e 29 16 90 90
    sub    %bx,%cs:-0x6f70              # gen=2e 29 1e  dis=2e 29 1e 90 90
    sub    %sp,%cs:-0x6f70              # gen=2e 29 26  dis=2e 29 26 90 90
    sub    %bp,%cs:-0x6f70              # gen=2e 29 2e  dis=2e 29 2e 90 90
    sub    %si,%cs:-0x6f70              # gen=2e 29 36  dis=2e 29 36 90 90
    sub    %di,%cs:-0x6f70              # gen=2e 29 3e  dis=2e 29 3e 90 90
    cs sub %al,%al                      # gen=2e 2a c0  dis=2e 2a c0
    sub    %cs:-0x6f70,%al              # gen=2e 2a 06  dis=2e 2a 06 90 90
    cs sub %al,%cl                      # gen=2e 2a c8  dis=2e 2a c8
    cs sub %al,%dl                      # gen=2e 2a d0  dis=2e 2a d0
    cs sub %al,%bl                      # gen=2e 2a d8  dis=2e 2a d8
    cs sub %al,%ah                      # gen=2e 2a e0  dis=2e 2a e0
    cs sub %al,%ch                      # gen=2e 2a e8  dis=2e 2a e8
    cs sub %al,%dh                      # gen=2e 2a f0  dis=2e 2a f0
    cs sub %al,%bh                      # gen=2e 2a f8  dis=2e 2a f8
    sub    %cs:-0x6f70,%cl              # gen=2e 2a 0e  dis=2e 2a 0e 90 90
    sub    %cs:-0x6f70,%dl              # gen=2e 2a 16  dis=2e 2a 16 90 90
    sub    %cs:-0x6f70,%bl              # gen=2e 2a 1e  dis=2e 2a 1e 90 90
    sub    %cs:-0x6f70,%ah              # gen=2e 2a 26  dis=2e 2a 26 90 90
    sub    %cs:-0x6f70,%ch              # gen=2e 2a 2e  dis=2e 2a 2e 90 90
    sub    %cs:-0x6f70,%dh              # gen=2e 2a 36  dis=2e 2a 36 90 90
    sub    %cs:-0x6f70,%bh              # gen=2e 2a 3e  dis=2e 2a 3e 90 90
    cs sub %ax,%ax                      # gen=2e 2b c0  dis=2e 2b c0
    sub    %cs:-0x6f70,%ax              # gen=2e 2b 06  dis=2e 2b 06 90 90
    cs sub %ax,%cx                      # gen=2e 2b c8  dis=2e 2b c8
    cs sub %ax,%dx                      # gen=2e 2b d0  dis=2e 2b d0
    cs sub %ax,%bx                      # gen=2e 2b d8  dis=2e 2b d8
    cs sub %ax,%sp                      # gen=2e 2b e0  dis=2e 2b e0
    cs sub %ax,%bp                      # gen=2e 2b e8  dis=2e 2b e8
    cs sub %ax,%si                      # gen=2e 2b f0  dis=2e 2b f0
    cs sub %ax,%di                      # gen=2e 2b f8  dis=2e 2b f8
    sub    %cs:-0x6f70,%cx              # gen=2e 2b 0e  dis=2e 2b 0e 90 90
    sub    %cs:-0x6f70,%dx              # gen=2e 2b 16  dis=2e 2b 16 90 90
    sub    %cs:-0x6f70,%bx              # gen=2e 2b 1e  dis=2e 2b 1e 90 90
    sub    %cs:-0x6f70,%sp              # gen=2e 2b 26  dis=2e 2b 26 90 90
    sub    %cs:-0x6f70,%bp              # gen=2e 2b 2e  dis=2e 2b 2e 90 90
    sub    %cs:-0x6f70,%si              # gen=2e 2b 36  dis=2e 2b 36 90 90
    sub    %cs:-0x6f70,%di              # gen=2e 2b 3e  dis=2e 2b 3e 90 90
    cs sub $0xc0,%al                    # gen=2e 2c c0  dis=2e 2c c0
    cs sub $0x6,%al                     # gen=2e 2c 06  dis=2e 2c 06
    cs sub $0x90c0,%ax                  # gen=2e 2d c0  dis=2e 2d c0 90
    cs sub $0x9006,%ax                  # gen=2e 2d 06  dis=2e 2d 06 90
    .byte 0x2e,0x2e,0xc0            # fallback; gen=2e 2e c0
    .byte 0x2e,0x2e,0x06            # fallback; gen=2e 2e 06
    cs das                              # gen=2e 2f c0  dis=2e 2f
    cs das                              # gen=2e 2f 06  dis=2e 2f
    cs xor %al,%al                      # gen=2e 30 c0  dis=2e 30 c0
    xor    %al,%cs:-0x6f70              # gen=2e 30 06  dis=2e 30 06 90 90
    cs xor %cl,%al                      # gen=2e 30 c8  dis=2e 30 c8
    cs xor %dl,%al                      # gen=2e 30 d0  dis=2e 30 d0
    cs xor %bl,%al                      # gen=2e 30 d8  dis=2e 30 d8
    cs xor %ah,%al                      # gen=2e 30 e0  dis=2e 30 e0
    cs xor %ch,%al                      # gen=2e 30 e8  dis=2e 30 e8
    cs xor %dh,%al                      # gen=2e 30 f0  dis=2e 30 f0
    cs xor %bh,%al                      # gen=2e 30 f8  dis=2e 30 f8
    xor    %cl,%cs:-0x6f70              # gen=2e 30 0e  dis=2e 30 0e 90 90
    xor    %dl,%cs:-0x6f70              # gen=2e 30 16  dis=2e 30 16 90 90
    xor    %bl,%cs:-0x6f70              # gen=2e 30 1e  dis=2e 30 1e 90 90
    xor    %ah,%cs:-0x6f70              # gen=2e 30 26  dis=2e 30 26 90 90
    xor    %ch,%cs:-0x6f70              # gen=2e 30 2e  dis=2e 30 2e 90 90
    xor    %dh,%cs:-0x6f70              # gen=2e 30 36  dis=2e 30 36 90 90
    xor    %bh,%cs:-0x6f70              # gen=2e 30 3e  dis=2e 30 3e 90 90
    cs xor %ax,%ax                      # gen=2e 31 c0  dis=2e 31 c0
    xor    %ax,%cs:-0x6f70              # gen=2e 31 06  dis=2e 31 06 90 90
    cs xor %cx,%ax                      # gen=2e 31 c8  dis=2e 31 c8
    cs xor %dx,%ax                      # gen=2e 31 d0  dis=2e 31 d0
    cs xor %bx,%ax                      # gen=2e 31 d8  dis=2e 31 d8
    cs xor %sp,%ax                      # gen=2e 31 e0  dis=2e 31 e0
    cs xor %bp,%ax                      # gen=2e 31 e8  dis=2e 31 e8
    cs xor %si,%ax                      # gen=2e 31 f0  dis=2e 31 f0
    cs xor %di,%ax                      # gen=2e 31 f8  dis=2e 31 f8
    xor    %cx,%cs:-0x6f70              # gen=2e 31 0e  dis=2e 31 0e 90 90
    xor    %dx,%cs:-0x6f70              # gen=2e 31 16  dis=2e 31 16 90 90
    xor    %bx,%cs:-0x6f70              # gen=2e 31 1e  dis=2e 31 1e 90 90
    xor    %sp,%cs:-0x6f70              # gen=2e 31 26  dis=2e 31 26 90 90
    xor    %bp,%cs:-0x6f70              # gen=2e 31 2e  dis=2e 31 2e 90 90
    xor    %si,%cs:-0x6f70              # gen=2e 31 36  dis=2e 31 36 90 90
    xor    %di,%cs:-0x6f70              # gen=2e 31 3e  dis=2e 31 3e 90 90
    cs xor %al,%al                      # gen=2e 32 c0  dis=2e 32 c0
    xor    %cs:-0x6f70,%al              # gen=2e 32 06  dis=2e 32 06 90 90
    cs xor %al,%cl                      # gen=2e 32 c8  dis=2e 32 c8
    cs xor %al,%dl                      # gen=2e 32 d0  dis=2e 32 d0
    cs xor %al,%bl                      # gen=2e 32 d8  dis=2e 32 d8
    cs xor %al,%ah                      # gen=2e 32 e0  dis=2e 32 e0
    cs xor %al,%ch                      # gen=2e 32 e8  dis=2e 32 e8
    cs xor %al,%dh                      # gen=2e 32 f0  dis=2e 32 f0
    cs xor %al,%bh                      # gen=2e 32 f8  dis=2e 32 f8
    xor    %cs:-0x6f70,%cl              # gen=2e 32 0e  dis=2e 32 0e 90 90
    xor    %cs:-0x6f70,%dl              # gen=2e 32 16  dis=2e 32 16 90 90
    xor    %cs:-0x6f70,%bl              # gen=2e 32 1e  dis=2e 32 1e 90 90
    xor    %cs:-0x6f70,%ah              # gen=2e 32 26  dis=2e 32 26 90 90
    xor    %cs:-0x6f70,%ch              # gen=2e 32 2e  dis=2e 32 2e 90 90
    xor    %cs:-0x6f70,%dh              # gen=2e 32 36  dis=2e 32 36 90 90
    xor    %cs:-0x6f70,%bh              # gen=2e 32 3e  dis=2e 32 3e 90 90
    cs xor %ax,%ax                      # gen=2e 33 c0  dis=2e 33 c0
    xor    %cs:-0x6f70,%ax              # gen=2e 33 06  dis=2e 33 06 90 90
    cs xor %ax,%cx                      # gen=2e 33 c8  dis=2e 33 c8
    cs xor %ax,%dx                      # gen=2e 33 d0  dis=2e 33 d0
    cs xor %ax,%bx                      # gen=2e 33 d8  dis=2e 33 d8
    cs xor %ax,%sp                      # gen=2e 33 e0  dis=2e 33 e0
    cs xor %ax,%bp                      # gen=2e 33 e8  dis=2e 33 e8
    cs xor %ax,%si                      # gen=2e 33 f0  dis=2e 33 f0
    cs xor %ax,%di                      # gen=2e 33 f8  dis=2e 33 f8
    xor    %cs:-0x6f70,%cx              # gen=2e 33 0e  dis=2e 33 0e 90 90
    xor    %cs:-0x6f70,%dx              # gen=2e 33 16  dis=2e 33 16 90 90
    xor    %cs:-0x6f70,%bx              # gen=2e 33 1e  dis=2e 33 1e 90 90
    xor    %cs:-0x6f70,%sp              # gen=2e 33 26  dis=2e 33 26 90 90
    xor    %cs:-0x6f70,%bp              # gen=2e 33 2e  dis=2e 33 2e 90 90
    xor    %cs:-0x6f70,%si              # gen=2e 33 36  dis=2e 33 36 90 90
    xor    %cs:-0x6f70,%di              # gen=2e 33 3e  dis=2e 33 3e 90 90
    cs xor $0xc0,%al                    # gen=2e 34 c0  dis=2e 34 c0
    cs xor $0x6,%al                     # gen=2e 34 06  dis=2e 34 06
    cs xor $0x90c0,%ax                  # gen=2e 35 c0  dis=2e 35 c0 90
    cs xor $0x9006,%ax                  # gen=2e 35 06  dis=2e 35 06 90
    .byte 0x2e,0x36,0xc0            # fallback; gen=2e 36 c0
    .byte 0x2e,0x36,0x06            # fallback; gen=2e 36 06
    cs aaa                              # gen=2e 37 c0  dis=2e 37
    cs aaa                              # gen=2e 37 06  dis=2e 37
    cs cmp %al,%al                      # gen=2e 38 c0  dis=2e 38 c0
    cmp    %al,%cs:-0x6f70              # gen=2e 38 06  dis=2e 38 06 90 90
    cs cmp %cl,%al                      # gen=2e 38 c8  dis=2e 38 c8
    cs cmp %dl,%al                      # gen=2e 38 d0  dis=2e 38 d0
    cs cmp %bl,%al                      # gen=2e 38 d8  dis=2e 38 d8
    cs cmp %ah,%al                      # gen=2e 38 e0  dis=2e 38 e0
    cs cmp %ch,%al                      # gen=2e 38 e8  dis=2e 38 e8
    cs cmp %dh,%al                      # gen=2e 38 f0  dis=2e 38 f0
    cs cmp %bh,%al                      # gen=2e 38 f8  dis=2e 38 f8
    cmp    %cl,%cs:-0x6f70              # gen=2e 38 0e  dis=2e 38 0e 90 90
    cmp    %dl,%cs:-0x6f70              # gen=2e 38 16  dis=2e 38 16 90 90
    cmp    %bl,%cs:-0x6f70              # gen=2e 38 1e  dis=2e 38 1e 90 90
    cmp    %ah,%cs:-0x6f70              # gen=2e 38 26  dis=2e 38 26 90 90
    cmp    %ch,%cs:-0x6f70              # gen=2e 38 2e  dis=2e 38 2e 90 90
    cmp    %dh,%cs:-0x6f70              # gen=2e 38 36  dis=2e 38 36 90 90
    cmp    %bh,%cs:-0x6f70              # gen=2e 38 3e  dis=2e 38 3e 90 90
    cs cmp %ax,%ax                      # gen=2e 39 c0  dis=2e 39 c0
    cmp    %ax,%cs:-0x6f70              # gen=2e 39 06  dis=2e 39 06 90 90
    cs cmp %cx,%ax                      # gen=2e 39 c8  dis=2e 39 c8
    cs cmp %dx,%ax                      # gen=2e 39 d0  dis=2e 39 d0
    cs cmp %bx,%ax                      # gen=2e 39 d8  dis=2e 39 d8
    cs cmp %sp,%ax                      # gen=2e 39 e0  dis=2e 39 e0
    cs cmp %bp,%ax                      # gen=2e 39 e8  dis=2e 39 e8
    cs cmp %si,%ax                      # gen=2e 39 f0  dis=2e 39 f0
    cs cmp %di,%ax                      # gen=2e 39 f8  dis=2e 39 f8
    cmp    %cx,%cs:-0x6f70              # gen=2e 39 0e  dis=2e 39 0e 90 90
    cmp    %dx,%cs:-0x6f70              # gen=2e 39 16  dis=2e 39 16 90 90
    cmp    %bx,%cs:-0x6f70              # gen=2e 39 1e  dis=2e 39 1e 90 90
    cmp    %sp,%cs:-0x6f70              # gen=2e 39 26  dis=2e 39 26 90 90
    cmp    %bp,%cs:-0x6f70              # gen=2e 39 2e  dis=2e 39 2e 90 90
    cmp    %si,%cs:-0x6f70              # gen=2e 39 36  dis=2e 39 36 90 90
    cmp    %di,%cs:-0x6f70              # gen=2e 39 3e  dis=2e 39 3e 90 90
    cs cmp %al,%al                      # gen=2e 3a c0  dis=2e 3a c0
    cmp    %cs:-0x6f70,%al              # gen=2e 3a 06  dis=2e 3a 06 90 90
    cs cmp %al,%cl                      # gen=2e 3a c8  dis=2e 3a c8
    cs cmp %al,%dl                      # gen=2e 3a d0  dis=2e 3a d0
    cs cmp %al,%bl                      # gen=2e 3a d8  dis=2e 3a d8
    cs cmp %al,%ah                      # gen=2e 3a e0  dis=2e 3a e0
    cs cmp %al,%ch                      # gen=2e 3a e8  dis=2e 3a e8
    cs cmp %al,%dh                      # gen=2e 3a f0  dis=2e 3a f0
    cs cmp %al,%bh                      # gen=2e 3a f8  dis=2e 3a f8
    cmp    %cs:-0x6f70,%cl              # gen=2e 3a 0e  dis=2e 3a 0e 90 90
    cmp    %cs:-0x6f70,%dl              # gen=2e 3a 16  dis=2e 3a 16 90 90
    cmp    %cs:-0x6f70,%bl              # gen=2e 3a 1e  dis=2e 3a 1e 90 90
    cmp    %cs:-0x6f70,%ah              # gen=2e 3a 26  dis=2e 3a 26 90 90
    cmp    %cs:-0x6f70,%ch              # gen=2e 3a 2e  dis=2e 3a 2e 90 90
    cmp    %cs:-0x6f70,%dh              # gen=2e 3a 36  dis=2e 3a 36 90 90
    cmp    %cs:-0x6f70,%bh              # gen=2e 3a 3e  dis=2e 3a 3e 90 90
    cs cmp %ax,%ax                      # gen=2e 3b c0  dis=2e 3b c0
    cmp    %cs:-0x6f70,%ax              # gen=2e 3b 06  dis=2e 3b 06 90 90
    cs cmp %ax,%cx                      # gen=2e 3b c8  dis=2e 3b c8
    cs cmp %ax,%dx                      # gen=2e 3b d0  dis=2e 3b d0
    cs cmp %ax,%bx                      # gen=2e 3b d8  dis=2e 3b d8
    cs cmp %ax,%sp                      # gen=2e 3b e0  dis=2e 3b e0
    cs cmp %ax,%bp                      # gen=2e 3b e8  dis=2e 3b e8
    cs cmp %ax,%si                      # gen=2e 3b f0  dis=2e 3b f0
    cs cmp %ax,%di                      # gen=2e 3b f8  dis=2e 3b f8
    cmp    %cs:-0x6f70,%cx              # gen=2e 3b 0e  dis=2e 3b 0e 90 90
    cmp    %cs:-0x6f70,%dx              # gen=2e 3b 16  dis=2e 3b 16 90 90
    cmp    %cs:-0x6f70,%bx              # gen=2e 3b 1e  dis=2e 3b 1e 90 90
    cmp    %cs:-0x6f70,%sp              # gen=2e 3b 26  dis=2e 3b 26 90 90
    cmp    %cs:-0x6f70,%bp              # gen=2e 3b 2e  dis=2e 3b 2e 90 90
    cmp    %cs:-0x6f70,%si              # gen=2e 3b 36  dis=2e 3b 36 90 90
    cmp    %cs:-0x6f70,%di              # gen=2e 3b 3e  dis=2e 3b 3e 90 90
    cs cmp $0xc0,%al                    # gen=2e 3c c0  dis=2e 3c c0
    cs cmp $0x6,%al                     # gen=2e 3c 06  dis=2e 3c 06
    cs cmp $0x90c0,%ax                  # gen=2e 3d c0  dis=2e 3d c0 90
    cs cmp $0x9006,%ax                  # gen=2e 3d 06  dis=2e 3d 06 90
    .byte 0x2e,0x3e,0xc0            # fallback; gen=2e 3e c0
    .byte 0x2e,0x3e,0x06            # fallback; gen=2e 3e 06
    cs aas                              # gen=2e 3f c0  dis=2e 3f
    cs aas                              # gen=2e 3f 06  dis=2e 3f
    cs inc %ax                          # gen=2e 40 c0  dis=2e 40
    cs inc %ax                          # gen=2e 40 06  dis=2e 40
    cs inc %cx                          # gen=2e 41 c0  dis=2e 41
    cs inc %cx                          # gen=2e 41 06  dis=2e 41
    cs inc %dx                          # gen=2e 42 c0  dis=2e 42
    cs inc %dx                          # gen=2e 42 06  dis=2e 42
    cs inc %bx                          # gen=2e 43 c0  dis=2e 43
    cs inc %bx                          # gen=2e 43 06  dis=2e 43
    cs inc %sp                          # gen=2e 44 c0  dis=2e 44
    cs inc %sp                          # gen=2e 44 06  dis=2e 44
    cs inc %bp                          # gen=2e 45 c0  dis=2e 45
    cs inc %bp                          # gen=2e 45 06  dis=2e 45
    cs inc %si                          # gen=2e 46 c0  dis=2e 46
    cs inc %si                          # gen=2e 46 06  dis=2e 46
    cs inc %di                          # gen=2e 47 c0  dis=2e 47
    cs inc %di                          # gen=2e 47 06  dis=2e 47
    cs dec %ax                          # gen=2e 48 c0  dis=2e 48
    cs dec %ax                          # gen=2e 48 06  dis=2e 48
    cs dec %cx                          # gen=2e 49 c0  dis=2e 49
    cs dec %cx                          # gen=2e 49 06  dis=2e 49
    cs dec %dx                          # gen=2e 4a c0  dis=2e 4a
    cs dec %dx                          # gen=2e 4a 06  dis=2e 4a
    cs dec %bx                          # gen=2e 4b c0  dis=2e 4b
    cs dec %bx                          # gen=2e 4b 06  dis=2e 4b
    cs dec %sp                          # gen=2e 4c c0  dis=2e 4c
    cs dec %sp                          # gen=2e 4c 06  dis=2e 4c
    cs dec %bp                          # gen=2e 4d c0  dis=2e 4d
    cs dec %bp                          # gen=2e 4d 06  dis=2e 4d
    cs dec %si                          # gen=2e 4e c0  dis=2e 4e
    cs dec %si                          # gen=2e 4e 06  dis=2e 4e
    cs dec %di                          # gen=2e 4f c0  dis=2e 4f
    cs dec %di                          # gen=2e 4f 06  dis=2e 4f
    cs push %ax                         # gen=2e 50 c0  dis=2e 50
    cs push %ax                         # gen=2e 50 06  dis=2e 50
    cs push %cx                         # gen=2e 51 c0  dis=2e 51
    cs push %cx                         # gen=2e 51 06  dis=2e 51
    cs push %dx                         # gen=2e 52 c0  dis=2e 52
    cs push %dx                         # gen=2e 52 06  dis=2e 52
    cs push %bx                         # gen=2e 53 c0  dis=2e 53
    cs push %bx                         # gen=2e 53 06  dis=2e 53
    cs push %sp                         # gen=2e 54 c0  dis=2e 54
    cs push %sp                         # gen=2e 54 06  dis=2e 54
    cs push %bp                         # gen=2e 55 c0  dis=2e 55
    cs push %bp                         # gen=2e 55 06  dis=2e 55
    cs push %si                         # gen=2e 56 c0  dis=2e 56
    cs push %si                         # gen=2e 56 06  dis=2e 56
    cs push %di                         # gen=2e 57 c0  dis=2e 57
    cs push %di                         # gen=2e 57 06  dis=2e 57
    cs pop %ax                          # gen=2e 58 c0  dis=2e 58
    cs pop %ax                          # gen=2e 58 06  dis=2e 58
    cs pop %cx                          # gen=2e 59 c0  dis=2e 59
    cs pop %cx                          # gen=2e 59 06  dis=2e 59
    cs pop %dx                          # gen=2e 5a c0  dis=2e 5a
    cs pop %dx                          # gen=2e 5a 06  dis=2e 5a
    cs pop %bx                          # gen=2e 5b c0  dis=2e 5b
    cs pop %bx                          # gen=2e 5b 06  dis=2e 5b
    cs pop %sp                          # gen=2e 5c c0  dis=2e 5c
    cs pop %sp                          # gen=2e 5c 06  dis=2e 5c
    cs pop %bp                          # gen=2e 5d c0  dis=2e 5d
    cs pop %bp                          # gen=2e 5d 06  dis=2e 5d
    cs pop %si                          # gen=2e 5e c0  dis=2e 5e
    cs pop %si                          # gen=2e 5e 06  dis=2e 5e
    cs pop %di                          # gen=2e 5f c0  dis=2e 5f
    cs pop %di                          # gen=2e 5f 06  dis=2e 5f
    .byte 0x2e,0x60,0xc0            # fallback; gen=2e 60 c0
    .byte 0x2e,0x60,0x06            # fallback; gen=2e 60 06
    .byte 0x2e,0x61,0xc0            # fallback; gen=2e 61 c0
    .byte 0x2e,0x61,0x06            # fallback; gen=2e 61 06
    .byte 0x2e,0x62,0xc0            # fallback; gen=2e 62 c0
    .byte 0x2e,0x62,0x06            # fallback; gen=2e 62 06
    .byte 0x2e,0x62,0xc8            # fallback; gen=2e 62 c8
    .byte 0x2e,0x62,0xd0            # fallback; gen=2e 62 d0
    .byte 0x2e,0x62,0xd8            # fallback; gen=2e 62 d8
    .byte 0x2e,0x62,0xe0            # fallback; gen=2e 62 e0
    .byte 0x2e,0x62,0xe8            # fallback; gen=2e 62 e8
    .byte 0x2e,0x62,0xf0            # fallback; gen=2e 62 f0
    .byte 0x2e,0x62,0xf8            # fallback; gen=2e 62 f8
    .byte 0x2e,0x62,0x0e            # fallback; gen=2e 62 0e
    .byte 0x2e,0x62,0x16            # fallback; gen=2e 62 16
    .byte 0x2e,0x62,0x1e            # fallback; gen=2e 62 1e
    .byte 0x2e,0x62,0x26            # fallback; gen=2e 62 26
    .byte 0x2e,0x62,0x2e            # fallback; gen=2e 62 2e
    .byte 0x2e,0x62,0x36            # fallback; gen=2e 62 36
    .byte 0x2e,0x62,0x3e            # fallback; gen=2e 62 3e
    .byte 0x2e,0x63,0xc0            # fallback; gen=2e 63 c0
    .byte 0x2e,0x63,0x06            # fallback; gen=2e 63 06
    .byte 0x2e,0x63,0xc8            # fallback; gen=2e 63 c8
    .byte 0x2e,0x63,0xd0            # fallback; gen=2e 63 d0
    .byte 0x2e,0x63,0xd8            # fallback; gen=2e 63 d8
    .byte 0x2e,0x63,0xe0            # fallback; gen=2e 63 e0
    .byte 0x2e,0x63,0xe8            # fallback; gen=2e 63 e8
    .byte 0x2e,0x63,0xf0            # fallback; gen=2e 63 f0
    .byte 0x2e,0x63,0xf8            # fallback; gen=2e 63 f8
    .byte 0x2e,0x63,0x0e            # fallback; gen=2e 63 0e
    .byte 0x2e,0x63,0x16            # fallback; gen=2e 63 16
    .byte 0x2e,0x63,0x1e            # fallback; gen=2e 63 1e
    .byte 0x2e,0x63,0x26            # fallback; gen=2e 63 26
    .byte 0x2e,0x63,0x2e            # fallback; gen=2e 63 2e
    .byte 0x2e,0x63,0x36            # fallback; gen=2e 63 36
    .byte 0x2e,0x63,0x3e            # fallback; gen=2e 63 3e
    .byte 0x2e,0x64,0xc0            # fallback; gen=2e 64 c0
    .byte 0x2e,0x64,0x06            # fallback; gen=2e 64 06
    .byte 0x2e,0x65,0xc0            # fallback; gen=2e 65 c0
    .byte 0x2e,0x65,0x06            # fallback; gen=2e 65 06
    .byte 0x2e,0x66,0xc0            # fallback; gen=2e 66 c0
    .byte 0x2e,0x66,0x06            # fallback; gen=2e 66 06
    .byte 0x2e,0x66,0xc8            # fallback; gen=2e 66 c8
    .byte 0x2e,0x66,0xd0            # fallback; gen=2e 66 d0
    .byte 0x2e,0x66,0xd8            # fallback; gen=2e 66 d8
    .byte 0x2e,0x66,0xe0            # fallback; gen=2e 66 e0
    .byte 0x2e,0x66,0xe8            # fallback; gen=2e 66 e8
    .byte 0x2e,0x66,0xf0            # fallback; gen=2e 66 f0
    .byte 0x2e,0x66,0xf8            # fallback; gen=2e 66 f8
    .byte 0x2e,0x66,0x0e            # fallback; gen=2e 66 0e
    .byte 0x2e,0x66,0x16            # fallback; gen=2e 66 16
    .byte 0x2e,0x66,0x1e            # fallback; gen=2e 66 1e
    .byte 0x2e,0x66,0x26            # fallback; gen=2e 66 26
    .byte 0x2e,0x66,0x2e            # fallback; gen=2e 66 2e
    .byte 0x2e,0x66,0x36            # fallback; gen=2e 66 36
    .byte 0x2e,0x66,0x3e            # fallback; gen=2e 66 3e
    .byte 0x2e,0x67,0xc0            # fallback; gen=2e 67 c0
    .byte 0x2e,0x67,0x06            # fallback; gen=2e 67 06
    .byte 0x2e,0x67,0xc8            # fallback; gen=2e 67 c8
    .byte 0x2e,0x67,0xd0            # fallback; gen=2e 67 d0
    .byte 0x2e,0x67,0xd8            # fallback; gen=2e 67 d8
    .byte 0x2e,0x67,0xe0            # fallback; gen=2e 67 e0
    .byte 0x2e,0x67,0xe8            # fallback; gen=2e 67 e8
    .byte 0x2e,0x67,0xf0            # fallback; gen=2e 67 f0
    .byte 0x2e,0x67,0xf8            # fallback; gen=2e 67 f8
    .byte 0x2e,0x67,0x0e            # fallback; gen=2e 67 0e
    .byte 0x2e,0x67,0x16            # fallback; gen=2e 67 16
    .byte 0x2e,0x67,0x1e            # fallback; gen=2e 67 1e
    .byte 0x2e,0x67,0x26            # fallback; gen=2e 67 26
    .byte 0x2e,0x67,0x2e            # fallback; gen=2e 67 2e
    .byte 0x2e,0x67,0x36            # fallback; gen=2e 67 36
    .byte 0x2e,0x67,0x3e            # fallback; gen=2e 67 3e
    .byte 0x2e,0x68,0xc0            # fallback; gen=2e 68 c0
    .byte 0x2e,0x68,0x06            # fallback; gen=2e 68 06
    .byte 0x2e,0x69,0xc0            # fallback; gen=2e 69 c0
    .byte 0x2e,0x69,0x06            # fallback; gen=2e 69 06
    .byte 0x2e,0x69,0xc8            # fallback; gen=2e 69 c8
    .byte 0x2e,0x69,0xd0            # fallback; gen=2e 69 d0
    .byte 0x2e,0x69,0xd8            # fallback; gen=2e 69 d8
    .byte 0x2e,0x69,0xe0            # fallback; gen=2e 69 e0
    .byte 0x2e,0x69,0xe8            # fallback; gen=2e 69 e8
    .byte 0x2e,0x69,0xf0            # fallback; gen=2e 69 f0
    .byte 0x2e,0x69,0xf8            # fallback; gen=2e 69 f8
    .byte 0x2e,0x69,0x0e            # fallback; gen=2e 69 0e
    .byte 0x2e,0x69,0x16            # fallback; gen=2e 69 16
    .byte 0x2e,0x69,0x1e            # fallback; gen=2e 69 1e
    .byte 0x2e,0x69,0x26            # fallback; gen=2e 69 26
    .byte 0x2e,0x69,0x2e            # fallback; gen=2e 69 2e
    .byte 0x2e,0x69,0x36            # fallback; gen=2e 69 36
    .byte 0x2e,0x69,0x3e            # fallback; gen=2e 69 3e
    .byte 0x2e,0x6a,0xc0            # fallback; gen=2e 6a c0
    .byte 0x2e,0x6a,0x06            # fallback; gen=2e 6a 06
    .byte 0x2e,0x6b,0xc0            # fallback; gen=2e 6b c0
    .byte 0x2e,0x6b,0x06            # fallback; gen=2e 6b 06
    .byte 0x2e,0x6b,0xc8            # fallback; gen=2e 6b c8
    .byte 0x2e,0x6b,0xd0            # fallback; gen=2e 6b d0
    .byte 0x2e,0x6b,0xd8            # fallback; gen=2e 6b d8
    .byte 0x2e,0x6b,0xe0            # fallback; gen=2e 6b e0
    .byte 0x2e,0x6b,0xe8            # fallback; gen=2e 6b e8
    .byte 0x2e,0x6b,0xf0            # fallback; gen=2e 6b f0
    .byte 0x2e,0x6b,0xf8            # fallback; gen=2e 6b f8
    .byte 0x2e,0x6b,0x0e            # fallback; gen=2e 6b 0e
    .byte 0x2e,0x6b,0x16            # fallback; gen=2e 6b 16
    .byte 0x2e,0x6b,0x1e            # fallback; gen=2e 6b 1e
    .byte 0x2e,0x6b,0x26            # fallback; gen=2e 6b 26
    .byte 0x2e,0x6b,0x2e            # fallback; gen=2e 6b 2e
    .byte 0x2e,0x6b,0x36            # fallback; gen=2e 6b 36
    .byte 0x2e,0x6b,0x3e            # fallback; gen=2e 6b 3e
    .byte 0x2e,0x6c,0xc0            # fallback; gen=2e 6c c0
    .byte 0x2e,0x6c,0x06            # fallback; gen=2e 6c 06
    .byte 0x2e,0x6d,0xc0            # fallback; gen=2e 6d c0
    .byte 0x2e,0x6d,0x06            # fallback; gen=2e 6d 06
    .byte 0x2e,0x6e,0xc0            # fallback; gen=2e 6e c0
    .byte 0x2e,0x6e,0x06            # fallback; gen=2e 6e 06
    .byte 0x2e,0x6f,0xc0            # fallback; gen=2e 6f c0
    .byte 0x2e,0x6f,0x06            # fallback; gen=2e 6f 06
    .byte 0x2e,0x70,0xc0            # fallback; gen=2e 70 c0
    .byte 0x2e,0x70,0x06            # fallback; gen=2e 70 06
    .byte 0x2e,0x71,0xc0            # fallback; gen=2e 71 c0
    .byte 0x2e,0x71,0x06            # fallback; gen=2e 71 06
    .byte 0x2e,0x72,0xc0            # fallback; gen=2e 72 c0
    .byte 0x2e,0x72,0x06            # fallback; gen=2e 72 06
    .byte 0x2e,0x73,0xc0            # fallback; gen=2e 73 c0
    .byte 0x2e,0x73,0x06            # fallback; gen=2e 73 06
    .byte 0x2e,0x74,0xc0            # fallback; gen=2e 74 c0
    .byte 0x2e,0x74,0x06            # fallback; gen=2e 74 06
    .byte 0x2e,0x75,0xc0            # fallback; gen=2e 75 c0
    .byte 0x2e,0x75,0x06            # fallback; gen=2e 75 06
    .byte 0x2e,0x76,0xc0            # fallback; gen=2e 76 c0
    .byte 0x2e,0x76,0x06            # fallback; gen=2e 76 06
    .byte 0x2e,0x77,0xc0            # fallback; gen=2e 77 c0
    .byte 0x2e,0x77,0x06            # fallback; gen=2e 77 06
    .byte 0x2e,0x78,0xc0            # fallback; gen=2e 78 c0
    .byte 0x2e,0x78,0x06            # fallback; gen=2e 78 06
    .byte 0x2e,0x79,0xc0            # fallback; gen=2e 79 c0
    .byte 0x2e,0x79,0x06            # fallback; gen=2e 79 06
    .byte 0x2e,0x7a,0xc0            # fallback; gen=2e 7a c0
    .byte 0x2e,0x7a,0x06            # fallback; gen=2e 7a 06
    .byte 0x2e,0x7b,0xc0            # fallback; gen=2e 7b c0
    .byte 0x2e,0x7b,0x06            # fallback; gen=2e 7b 06
    .byte 0x2e,0x7c,0xc0            # fallback; gen=2e 7c c0
    .byte 0x2e,0x7c,0x06            # fallback; gen=2e 7c 06
    .byte 0x2e,0x7d,0xc0            # fallback; gen=2e 7d c0
    .byte 0x2e,0x7d,0x06            # fallback; gen=2e 7d 06
    .byte 0x2e,0x7e,0xc0            # fallback; gen=2e 7e c0
    .byte 0x2e,0x7e,0x06            # fallback; gen=2e 7e 06
    .byte 0x2e,0x7f,0xc0            # fallback; gen=2e 7f c0
    .byte 0x2e,0x7f,0x06            # fallback; gen=2e 7f 06
    cs add $0x90,%al                    # gen=2e 80 c0  dis=2e 80 c0 90
    addb   $0x90,%cs:-0x6f70            # gen=2e 80 06  dis=2e 80 06 90 90 90
    cs or  $0x90,%al                    # gen=2e 80 c8  dis=2e 80 c8 90
    cs adc $0x90,%al                    # gen=2e 80 d0  dis=2e 80 d0 90
    cs sbb $0x90,%al                    # gen=2e 80 d8  dis=2e 80 d8 90
    cs and $0x90,%al                    # gen=2e 80 e0  dis=2e 80 e0 90
    cs sub $0x90,%al                    # gen=2e 80 e8  dis=2e 80 e8 90
    cs xor $0x90,%al                    # gen=2e 80 f0  dis=2e 80 f0 90
    cs cmp $0x90,%al                    # gen=2e 80 f8  dis=2e 80 f8 90
    orb    $0x90,%cs:-0x6f70            # gen=2e 80 0e  dis=2e 80 0e 90 90 90
    adcb   $0x90,%cs:-0x6f70            # gen=2e 80 16  dis=2e 80 16 90 90 90
    sbbb   $0x90,%cs:-0x6f70            # gen=2e 80 1e  dis=2e 80 1e 90 90 90
    andb   $0x90,%cs:-0x6f70            # gen=2e 80 26  dis=2e 80 26 90 90 90
    subb   $0x90,%cs:-0x6f70            # gen=2e 80 2e  dis=2e 80 2e 90 90 90
    xorb   $0x90,%cs:-0x6f70            # gen=2e 80 36  dis=2e 80 36 90 90 90
    cmpb   $0x90,%cs:-0x6f70            # gen=2e 80 3e  dis=2e 80 3e 90 90 90
    cs add $0x9090,%ax                  # gen=2e 81 c0  dis=2e 81 c0 90 90
    addw   $0x9090,%cs:-0x6f70          # gen=2e 81 06  dis=2e 81 06 90 90 90 90
    cs or  $0x9090,%ax                  # gen=2e 81 c8  dis=2e 81 c8 90 90
    cs adc $0x9090,%ax                  # gen=2e 81 d0  dis=2e 81 d0 90 90
    cs sbb $0x9090,%ax                  # gen=2e 81 d8  dis=2e 81 d8 90 90
    cs and $0x9090,%ax                  # gen=2e 81 e0  dis=2e 81 e0 90 90
    cs sub $0x9090,%ax                  # gen=2e 81 e8  dis=2e 81 e8 90 90
    cs xor $0x9090,%ax                  # gen=2e 81 f0  dis=2e 81 f0 90 90
    cs cmp $0x9090,%ax                  # gen=2e 81 f8  dis=2e 81 f8 90 90
    orw    $0x9090,%cs:-0x6f70          # gen=2e 81 0e  dis=2e 81 0e 90 90 90 90
    adcw   $0x9090,%cs:-0x6f70          # gen=2e 81 16  dis=2e 81 16 90 90 90 90
    sbbw   $0x9090,%cs:-0x6f70          # gen=2e 81 1e  dis=2e 81 1e 90 90 90 90
    andw   $0x9090,%cs:-0x6f70          # gen=2e 81 26  dis=2e 81 26 90 90 90 90
    subw   $0x9090,%cs:-0x6f70          # gen=2e 81 2e  dis=2e 81 2e 90 90 90 90
    xorw   $0x9090,%cs:-0x6f70          # gen=2e 81 36  dis=2e 81 36 90 90 90 90
    cmpw   $0x9090,%cs:-0x6f70          # gen=2e 81 3e  dis=2e 81 3e 90 90 90 90
    cs add $0x90,%al                    # gen=2e 82 c0  dis=2e 82 c0 90
    addb   $0x90,%cs:-0x6f70            # gen=2e 82 06  dis=2e 82 06 90 90 90
    cs or  $0x90,%al                    # gen=2e 82 c8  dis=2e 82 c8 90
    cs adc $0x90,%al                    # gen=2e 82 d0  dis=2e 82 d0 90
    cs sbb $0x90,%al                    # gen=2e 82 d8  dis=2e 82 d8 90
    cs and $0x90,%al                    # gen=2e 82 e0  dis=2e 82 e0 90
    cs sub $0x90,%al                    # gen=2e 82 e8  dis=2e 82 e8 90
    cs xor $0x90,%al                    # gen=2e 82 f0  dis=2e 82 f0 90
    cs cmp $0x90,%al                    # gen=2e 82 f8  dis=2e 82 f8 90
    orb    $0x90,%cs:-0x6f70            # gen=2e 82 0e  dis=2e 82 0e 90 90 90
    adcb   $0x90,%cs:-0x6f70            # gen=2e 82 16  dis=2e 82 16 90 90 90
    sbbb   $0x90,%cs:-0x6f70            # gen=2e 82 1e  dis=2e 82 1e 90 90 90
    andb   $0x90,%cs:-0x6f70            # gen=2e 82 26  dis=2e 82 26 90 90 90
    subb   $0x90,%cs:-0x6f70            # gen=2e 82 2e  dis=2e 82 2e 90 90 90
    xorb   $0x90,%cs:-0x6f70            # gen=2e 82 36  dis=2e 82 36 90 90 90
    cmpb   $0x90,%cs:-0x6f70            # gen=2e 82 3e  dis=2e 82 3e 90 90 90
    cs add $0xff90,%ax                  # gen=2e 83 c0  dis=2e 83 c0 90
    addw   $0xff90,%cs:-0x6f70          # gen=2e 83 06  dis=2e 83 06 90 90 90
    cs or  $0xff90,%ax                  # gen=2e 83 c8  dis=2e 83 c8 90
    cs adc $0xff90,%ax                  # gen=2e 83 d0  dis=2e 83 d0 90
    cs sbb $0xff90,%ax                  # gen=2e 83 d8  dis=2e 83 d8 90
    cs and $0xff90,%ax                  # gen=2e 83 e0  dis=2e 83 e0 90
    cs sub $0xff90,%ax                  # gen=2e 83 e8  dis=2e 83 e8 90
    cs xor $0xff90,%ax                  # gen=2e 83 f0  dis=2e 83 f0 90
    cs cmp $0xff90,%ax                  # gen=2e 83 f8  dis=2e 83 f8 90
    orw    $0xff90,%cs:-0x6f70          # gen=2e 83 0e  dis=2e 83 0e 90 90 90
    adcw   $0xff90,%cs:-0x6f70          # gen=2e 83 16  dis=2e 83 16 90 90 90
    sbbw   $0xff90,%cs:-0x6f70          # gen=2e 83 1e  dis=2e 83 1e 90 90 90
    andw   $0xff90,%cs:-0x6f70          # gen=2e 83 26  dis=2e 83 26 90 90 90
    subw   $0xff90,%cs:-0x6f70          # gen=2e 83 2e  dis=2e 83 2e 90 90 90
    xorw   $0xff90,%cs:-0x6f70          # gen=2e 83 36  dis=2e 83 36 90 90 90
    cmpw   $0xff90,%cs:-0x6f70          # gen=2e 83 3e  dis=2e 83 3e 90 90 90
    cs test %al,%al                     # gen=2e 84 c0  dis=2e 84 c0
    test   %al,%cs:-0x6f70              # gen=2e 84 06  dis=2e 84 06 90 90
    cs test %cl,%al                     # gen=2e 84 c8  dis=2e 84 c8
    cs test %dl,%al                     # gen=2e 84 d0  dis=2e 84 d0
    cs test %bl,%al                     # gen=2e 84 d8  dis=2e 84 d8
    cs test %ah,%al                     # gen=2e 84 e0  dis=2e 84 e0
    cs test %ch,%al                     # gen=2e 84 e8  dis=2e 84 e8
    cs test %dh,%al                     # gen=2e 84 f0  dis=2e 84 f0
    cs test %bh,%al                     # gen=2e 84 f8  dis=2e 84 f8
    test   %cl,%cs:-0x6f70              # gen=2e 84 0e  dis=2e 84 0e 90 90
    test   %dl,%cs:-0x6f70              # gen=2e 84 16  dis=2e 84 16 90 90
    test   %bl,%cs:-0x6f70              # gen=2e 84 1e  dis=2e 84 1e 90 90
    test   %ah,%cs:-0x6f70              # gen=2e 84 26  dis=2e 84 26 90 90
    test   %ch,%cs:-0x6f70              # gen=2e 84 2e  dis=2e 84 2e 90 90
    test   %dh,%cs:-0x6f70              # gen=2e 84 36  dis=2e 84 36 90 90
    test   %bh,%cs:-0x6f70              # gen=2e 84 3e  dis=2e 84 3e 90 90
    cs test %ax,%ax                     # gen=2e 85 c0  dis=2e 85 c0
    test   %ax,%cs:-0x6f70              # gen=2e 85 06  dis=2e 85 06 90 90
    cs test %cx,%ax                     # gen=2e 85 c8  dis=2e 85 c8
    cs test %dx,%ax                     # gen=2e 85 d0  dis=2e 85 d0
    cs test %bx,%ax                     # gen=2e 85 d8  dis=2e 85 d8
    cs test %sp,%ax                     # gen=2e 85 e0  dis=2e 85 e0
    cs test %bp,%ax                     # gen=2e 85 e8  dis=2e 85 e8
    cs test %si,%ax                     # gen=2e 85 f0  dis=2e 85 f0
    cs test %di,%ax                     # gen=2e 85 f8  dis=2e 85 f8
    test   %cx,%cs:-0x6f70              # gen=2e 85 0e  dis=2e 85 0e 90 90
    test   %dx,%cs:-0x6f70              # gen=2e 85 16  dis=2e 85 16 90 90
    test   %bx,%cs:-0x6f70              # gen=2e 85 1e  dis=2e 85 1e 90 90
    test   %sp,%cs:-0x6f70              # gen=2e 85 26  dis=2e 85 26 90 90
    test   %bp,%cs:-0x6f70              # gen=2e 85 2e  dis=2e 85 2e 90 90
    test   %si,%cs:-0x6f70              # gen=2e 85 36  dis=2e 85 36 90 90
    test   %di,%cs:-0x6f70              # gen=2e 85 3e  dis=2e 85 3e 90 90
    cs xchg %al,%al                     # gen=2e 86 c0  dis=2e 86 c0
    xchg   %al,%cs:-0x6f70              # gen=2e 86 06  dis=2e 86 06 90 90
    cs xchg %cl,%al                     # gen=2e 86 c8  dis=2e 86 c8
    cs xchg %dl,%al                     # gen=2e 86 d0  dis=2e 86 d0
    cs xchg %bl,%al                     # gen=2e 86 d8  dis=2e 86 d8
    cs xchg %ah,%al                     # gen=2e 86 e0  dis=2e 86 e0
    cs xchg %ch,%al                     # gen=2e 86 e8  dis=2e 86 e8
    cs xchg %dh,%al                     # gen=2e 86 f0  dis=2e 86 f0
    cs xchg %bh,%al                     # gen=2e 86 f8  dis=2e 86 f8
    xchg   %cl,%cs:-0x6f70              # gen=2e 86 0e  dis=2e 86 0e 90 90
    xchg   %dl,%cs:-0x6f70              # gen=2e 86 16  dis=2e 86 16 90 90
    xchg   %bl,%cs:-0x6f70              # gen=2e 86 1e  dis=2e 86 1e 90 90
    xchg   %ah,%cs:-0x6f70              # gen=2e 86 26  dis=2e 86 26 90 90
    xchg   %ch,%cs:-0x6f70              # gen=2e 86 2e  dis=2e 86 2e 90 90
    xchg   %dh,%cs:-0x6f70              # gen=2e 86 36  dis=2e 86 36 90 90
    xchg   %bh,%cs:-0x6f70              # gen=2e 86 3e  dis=2e 86 3e 90 90
    cs xchg %ax,%ax                     # gen=2e 87 c0  dis=2e 87 c0
    xchg   %ax,%cs:-0x6f70              # gen=2e 87 06  dis=2e 87 06 90 90
    cs xchg %cx,%ax                     # gen=2e 87 c8  dis=2e 87 c8
    cs xchg %dx,%ax                     # gen=2e 87 d0  dis=2e 87 d0
    cs xchg %bx,%ax                     # gen=2e 87 d8  dis=2e 87 d8
    cs xchg %sp,%ax                     # gen=2e 87 e0  dis=2e 87 e0
    cs xchg %bp,%ax                     # gen=2e 87 e8  dis=2e 87 e8
    cs xchg %si,%ax                     # gen=2e 87 f0  dis=2e 87 f0
    cs xchg %di,%ax                     # gen=2e 87 f8  dis=2e 87 f8
    xchg   %cx,%cs:-0x6f70              # gen=2e 87 0e  dis=2e 87 0e 90 90
    xchg   %dx,%cs:-0x6f70              # gen=2e 87 16  dis=2e 87 16 90 90
    xchg   %bx,%cs:-0x6f70              # gen=2e 87 1e  dis=2e 87 1e 90 90
    xchg   %sp,%cs:-0x6f70              # gen=2e 87 26  dis=2e 87 26 90 90
    xchg   %bp,%cs:-0x6f70              # gen=2e 87 2e  dis=2e 87 2e 90 90
    xchg   %si,%cs:-0x6f70              # gen=2e 87 36  dis=2e 87 36 90 90
    xchg   %di,%cs:-0x6f70              # gen=2e 87 3e  dis=2e 87 3e 90 90
    cs mov %al,%al                      # gen=2e 88 c0  dis=2e 88 c0
    mov    %al,%cs:-0x6f70              # gen=2e 88 06  dis=2e 88 06 90 90
    cs mov %cl,%al                      # gen=2e 88 c8  dis=2e 88 c8
    cs mov %dl,%al                      # gen=2e 88 d0  dis=2e 88 d0
    cs mov %bl,%al                      # gen=2e 88 d8  dis=2e 88 d8
    cs mov %ah,%al                      # gen=2e 88 e0  dis=2e 88 e0
    cs mov %ch,%al                      # gen=2e 88 e8  dis=2e 88 e8
    cs mov %dh,%al                      # gen=2e 88 f0  dis=2e 88 f0
    cs mov %bh,%al                      # gen=2e 88 f8  dis=2e 88 f8
    mov    %cl,%cs:-0x6f70              # gen=2e 88 0e  dis=2e 88 0e 90 90
    mov    %dl,%cs:-0x6f70              # gen=2e 88 16  dis=2e 88 16 90 90
    mov    %bl,%cs:-0x6f70              # gen=2e 88 1e  dis=2e 88 1e 90 90
    mov    %ah,%cs:-0x6f70              # gen=2e 88 26  dis=2e 88 26 90 90
    mov    %ch,%cs:-0x6f70              # gen=2e 88 2e  dis=2e 88 2e 90 90
    mov    %dh,%cs:-0x6f70              # gen=2e 88 36  dis=2e 88 36 90 90
    mov    %bh,%cs:-0x6f70              # gen=2e 88 3e  dis=2e 88 3e 90 90
    cs mov %ax,%ax                      # gen=2e 89 c0  dis=2e 89 c0
    mov    %ax,%cs:-0x6f70              # gen=2e 89 06  dis=2e 89 06 90 90
    cs mov %cx,%ax                      # gen=2e 89 c8  dis=2e 89 c8
    cs mov %dx,%ax                      # gen=2e 89 d0  dis=2e 89 d0
    cs mov %bx,%ax                      # gen=2e 89 d8  dis=2e 89 d8
    cs mov %sp,%ax                      # gen=2e 89 e0  dis=2e 89 e0
    cs mov %bp,%ax                      # gen=2e 89 e8  dis=2e 89 e8
    cs mov %si,%ax                      # gen=2e 89 f0  dis=2e 89 f0
    cs mov %di,%ax                      # gen=2e 89 f8  dis=2e 89 f8
    mov    %cx,%cs:-0x6f70              # gen=2e 89 0e  dis=2e 89 0e 90 90
    mov    %dx,%cs:-0x6f70              # gen=2e 89 16  dis=2e 89 16 90 90
    mov    %bx,%cs:-0x6f70              # gen=2e 89 1e  dis=2e 89 1e 90 90
    mov    %sp,%cs:-0x6f70              # gen=2e 89 26  dis=2e 89 26 90 90
    mov    %bp,%cs:-0x6f70              # gen=2e 89 2e  dis=2e 89 2e 90 90
    mov    %si,%cs:-0x6f70              # gen=2e 89 36  dis=2e 89 36 90 90
    mov    %di,%cs:-0x6f70              # gen=2e 89 3e  dis=2e 89 3e 90 90
    cs mov %al,%al                      # gen=2e 8a c0  dis=2e 8a c0
    mov    %cs:-0x6f70,%al              # gen=2e 8a 06  dis=2e 8a 06 90 90
    cs mov %al,%cl                      # gen=2e 8a c8  dis=2e 8a c8
    cs mov %al,%dl                      # gen=2e 8a d0  dis=2e 8a d0
    cs mov %al,%bl                      # gen=2e 8a d8  dis=2e 8a d8
    cs mov %al,%ah                      # gen=2e 8a e0  dis=2e 8a e0
    cs mov %al,%ch                      # gen=2e 8a e8  dis=2e 8a e8
    cs mov %al,%dh                      # gen=2e 8a f0  dis=2e 8a f0
    cs mov %al,%bh                      # gen=2e 8a f8  dis=2e 8a f8
    mov    %cs:-0x6f70,%cl              # gen=2e 8a 0e  dis=2e 8a 0e 90 90
    mov    %cs:-0x6f70,%dl              # gen=2e 8a 16  dis=2e 8a 16 90 90
    mov    %cs:-0x6f70,%bl              # gen=2e 8a 1e  dis=2e 8a 1e 90 90
    mov    %cs:-0x6f70,%ah              # gen=2e 8a 26  dis=2e 8a 26 90 90
    mov    %cs:-0x6f70,%ch              # gen=2e 8a 2e  dis=2e 8a 2e 90 90
    mov    %cs:-0x6f70,%dh              # gen=2e 8a 36  dis=2e 8a 36 90 90
    mov    %cs:-0x6f70,%bh              # gen=2e 8a 3e  dis=2e 8a 3e 90 90
    cs mov %ax,%ax                      # gen=2e 8b c0  dis=2e 8b c0
    mov    %cs:-0x6f70,%ax              # gen=2e 8b 06  dis=2e 8b 06 90 90
    cs mov %ax,%cx                      # gen=2e 8b c8  dis=2e 8b c8
    cs mov %ax,%dx                      # gen=2e 8b d0  dis=2e 8b d0
    cs mov %ax,%bx                      # gen=2e 8b d8  dis=2e 8b d8
    cs mov %ax,%sp                      # gen=2e 8b e0  dis=2e 8b e0
    cs mov %ax,%bp                      # gen=2e 8b e8  dis=2e 8b e8
    cs mov %ax,%si                      # gen=2e 8b f0  dis=2e 8b f0
    cs mov %ax,%di                      # gen=2e 8b f8  dis=2e 8b f8
    mov    %cs:-0x6f70,%cx              # gen=2e 8b 0e  dis=2e 8b 0e 90 90
    mov    %cs:-0x6f70,%dx              # gen=2e 8b 16  dis=2e 8b 16 90 90
    mov    %cs:-0x6f70,%bx              # gen=2e 8b 1e  dis=2e 8b 1e 90 90
    mov    %cs:-0x6f70,%sp              # gen=2e 8b 26  dis=2e 8b 26 90 90
    mov    %cs:-0x6f70,%bp              # gen=2e 8b 2e  dis=2e 8b 2e 90 90
    mov    %cs:-0x6f70,%si              # gen=2e 8b 36  dis=2e 8b 36 90 90
    mov    %cs:-0x6f70,%di              # gen=2e 8b 3e  dis=2e 8b 3e 90 90
    cs mov %es,%ax                      # gen=2e 8c c0  dis=2e 8c c0
    mov    %es,%cs:-0x6f70              # gen=2e 8c 06  dis=2e 8c 06 90 90
    cs mov %cs,%ax                      # gen=2e 8c c8  dis=2e 8c c8
    cs mov %ss,%ax                      # gen=2e 8c d0  dis=2e 8c d0
    cs mov %ds,%ax                      # gen=2e 8c d8  dis=2e 8c d8
    .byte 0x2e,0x8c,0xe0            # fallback; gen=2e 8c e0
    .byte 0x2e,0x8c,0xe8            # fallback; gen=2e 8c e8
    .byte 0x2e,0x8c,0xf0            # fallback; gen=2e 8c f0
    .byte 0x2e,0x8c,0xf8            # fallback; gen=2e 8c f8
    mov    %cs,%cs:-0x6f70              # gen=2e 8c 0e  dis=2e 8c 0e 90 90
    mov    %ss,%cs:-0x6f70              # gen=2e 8c 16  dis=2e 8c 16 90 90
    mov    %ds,%cs:-0x6f70              # gen=2e 8c 1e  dis=2e 8c 1e 90 90
    .byte 0x2e,0x8c,0x26            # fallback; gen=2e 8c 26
    .byte 0x2e,0x8c,0x2e            # fallback; gen=2e 8c 2e
    .byte 0x2e,0x8c,0x36            # fallback; gen=2e 8c 36
    .byte 0x2e,0x8c,0x3e            # fallback; gen=2e 8c 3e
    .byte 0x2e,0x8d,0xc0            # fallback; gen=2e 8d c0
    .byte 0x2e,0x8d,0x06            # fallback; gen=2e 8d 06
    .byte 0x2e,0x8d,0xc8            # fallback; gen=2e 8d c8
    .byte 0x2e,0x8d,0xd0            # fallback; gen=2e 8d d0
    .byte 0x2e,0x8d,0xd8            # fallback; gen=2e 8d d8
    .byte 0x2e,0x8d,0xe0            # fallback; gen=2e 8d e0
    .byte 0x2e,0x8d,0xe8            # fallback; gen=2e 8d e8
    .byte 0x2e,0x8d,0xf0            # fallback; gen=2e 8d f0
    .byte 0x2e,0x8d,0xf8            # fallback; gen=2e 8d f8
    .byte 0x2e,0x8d,0x0e            # fallback; gen=2e 8d 0e
    .byte 0x2e,0x8d,0x16            # fallback; gen=2e 8d 16
    .byte 0x2e,0x8d,0x1e            # fallback; gen=2e 8d 1e
    .byte 0x2e,0x8d,0x26            # fallback; gen=2e 8d 26
    .byte 0x2e,0x8d,0x2e            # fallback; gen=2e 8d 2e
    .byte 0x2e,0x8d,0x36            # fallback; gen=2e 8d 36
    .byte 0x2e,0x8d,0x3e            # fallback; gen=2e 8d 3e
    cs mov %ax,%es                      # gen=2e 8e c0  dis=2e 8e c0
    mov    %cs:-0x6f70,%es              # gen=2e 8e 06  dis=2e 8e 06 90 90
    cs mov %ax,%cs                      # gen=2e 8e c8  dis=2e 8e c8
    cs mov %ax,%ss                      # gen=2e 8e d0  dis=2e 8e d0
    cs mov %ax,%ds                      # gen=2e 8e d8  dis=2e 8e d8
    .byte 0x2e,0x8e,0xe0            # fallback; gen=2e 8e e0
    .byte 0x2e,0x8e,0xe8            # fallback; gen=2e 8e e8
    .byte 0x2e,0x8e,0xf0            # fallback; gen=2e 8e f0
    .byte 0x2e,0x8e,0xf8            # fallback; gen=2e 8e f8
    mov    %cs:-0x6f70,%cs              # gen=2e 8e 0e  dis=2e 8e 0e 90 90
    mov    %cs:-0x6f70,%ss              # gen=2e 8e 16  dis=2e 8e 16 90 90
    mov    %cs:-0x6f70,%ds              # gen=2e 8e 1e  dis=2e 8e 1e 90 90
    .byte 0x2e,0x8e,0x26            # fallback; gen=2e 8e 26
    .byte 0x2e,0x8e,0x2e            # fallback; gen=2e 8e 2e
    .byte 0x2e,0x8e,0x36            # fallback; gen=2e 8e 36
    .byte 0x2e,0x8e,0x3e            # fallback; gen=2e 8e 3e
    cs pop %ax                          # gen=2e 8f c0  dis=2e 8f c0
    pop    %cs:-0x6f70                  # gen=2e 8f 06  dis=2e 8f 06 90 90
    .byte 0x2e,0x8f,0xc8            # fallback; gen=2e 8f c8
    .byte 0x2e,0x8f,0xd0            # fallback; gen=2e 8f d0
    .byte 0x2e,0x8f,0xd8            # fallback; gen=2e 8f d8
    .byte 0x2e,0x8f,0xe0            # fallback; gen=2e 8f e0
    .byte 0x2e,0x8f,0xe8            # fallback; gen=2e 8f e8
    .byte 0x2e,0x8f,0xf0            # fallback; gen=2e 8f f0
    .byte 0x2e,0x8f,0xf8            # fallback; gen=2e 8f f8
    .byte 0x2e,0x8f,0x0e            # fallback; gen=2e 8f 0e
    .byte 0x2e,0x8f,0x16            # fallback; gen=2e 8f 16
    .byte 0x2e,0x8f,0x1e            # fallback; gen=2e 8f 1e
    .byte 0x2e,0x8f,0x26            # fallback; gen=2e 8f 26
    .byte 0x2e,0x8f,0x2e            # fallback; gen=2e 8f 2e
    .byte 0x2e,0x8f,0x36            # fallback; gen=2e 8f 36
    .byte 0x2e,0x8f,0x3e            # fallback; gen=2e 8f 3e
    cs nop                              # gen=2e 90 c0  dis=2e 90
    cs nop                              # gen=2e 90 06  dis=2e 90
    cs xchg %ax,%cx                     # gen=2e 91 c0  dis=2e 91
    cs xchg %ax,%cx                     # gen=2e 91 06  dis=2e 91
    cs xchg %ax,%dx                     # gen=2e 92 c0  dis=2e 92
    cs xchg %ax,%dx                     # gen=2e 92 06  dis=2e 92
    cs xchg %ax,%bx                     # gen=2e 93 c0  dis=2e 93
    cs xchg %ax,%bx                     # gen=2e 93 06  dis=2e 93
    cs xchg %ax,%sp                     # gen=2e 94 c0  dis=2e 94
    cs xchg %ax,%sp                     # gen=2e 94 06  dis=2e 94
    cs xchg %ax,%bp                     # gen=2e 95 c0  dis=2e 95
    cs xchg %ax,%bp                     # gen=2e 95 06  dis=2e 95
    cs xchg %ax,%si                     # gen=2e 96 c0  dis=2e 96
    cs xchg %ax,%si                     # gen=2e 96 06  dis=2e 96
    cs xchg %ax,%di                     # gen=2e 97 c0  dis=2e 97
    cs xchg %ax,%di                     # gen=2e 97 06  dis=2e 97
    cs cbtw                             # gen=2e 98 c0  dis=2e 98
    cs cbtw                             # gen=2e 98 06  dis=2e 98
    cs cwtd                             # gen=2e 99 c0  dis=2e 99
    cs cwtd                             # gen=2e 99 06  dis=2e 99
    .byte 0x2e,0x9a,0xc0            # fallback; gen=2e 9a c0
    .byte 0x2e,0x9a,0x06            # fallback; gen=2e 9a 06
    .byte 0x2e,0x9b,0xc0            # fallback; gen=2e 9b c0
    .byte 0x2e,0x9b,0x06            # fallback; gen=2e 9b 06
    cs pushf                            # gen=2e 9c c0  dis=2e 9c
    cs pushf                            # gen=2e 9c 06  dis=2e 9c
    cs popf                             # gen=2e 9d c0  dis=2e 9d
    cs popf                             # gen=2e 9d 06  dis=2e 9d
    cs sahf                             # gen=2e 9e c0  dis=2e 9e
    cs sahf                             # gen=2e 9e 06  dis=2e 9e
    cs lahf                             # gen=2e 9f c0  dis=2e 9f
    cs lahf                             # gen=2e 9f 06  dis=2e 9f
    mov    %cs:0x90c0,%al               # gen=2e a0 c0  dis=2e a0 c0 90
    mov    %cs:0x9006,%al               # gen=2e a0 06  dis=2e a0 06 90
    mov    %cs:0x90c0,%ax               # gen=2e a1 c0  dis=2e a1 c0 90
    mov    %cs:0x9006,%ax               # gen=2e a1 06  dis=2e a1 06 90
    mov    %al,%cs:0x90c0               # gen=2e a2 c0  dis=2e a2 c0 90
    mov    %al,%cs:0x9006               # gen=2e a2 06  dis=2e a2 06 90
    mov    %ax,%cs:0x90c0               # gen=2e a3 c0  dis=2e a3 c0 90
    mov    %ax,%cs:0x9006               # gen=2e a3 06  dis=2e a3 06 90
    movsb  %cs:(%si),%es:(%di)          # gen=2e a4 c0  dis=2e a4
    movsb  %cs:(%si),%es:(%di)          # gen=2e a4 06  dis=2e a4
    movsw  %cs:(%si),%es:(%di)          # gen=2e a5 c0  dis=2e a5
    movsw  %cs:(%si),%es:(%di)          # gen=2e a5 06  dis=2e a5
    cmpsb  %es:(%di),%cs:(%si)          # gen=2e a6 c0  dis=2e a6
    cmpsb  %es:(%di),%cs:(%si)          # gen=2e a6 06  dis=2e a6
    cmpsw  %es:(%di),%cs:(%si)          # gen=2e a7 c0  dis=2e a7
    cmpsw  %es:(%di),%cs:(%si)          # gen=2e a7 06  dis=2e a7
    cs test $0xc0,%al                   # gen=2e a8 c0  dis=2e a8 c0
    cs test $0x6,%al                    # gen=2e a8 06  dis=2e a8 06
    cs test $0x90c0,%ax                 # gen=2e a9 c0  dis=2e a9 c0 90
    cs test $0x9006,%ax                 # gen=2e a9 06  dis=2e a9 06 90
    cs stos %al,%es:(%di)               # gen=2e aa c0  dis=2e aa
    cs stos %al,%es:(%di)               # gen=2e aa 06  dis=2e aa
    cs stos %ax,%es:(%di)               # gen=2e ab c0  dis=2e ab
    cs stos %ax,%es:(%di)               # gen=2e ab 06  dis=2e ab
    lods   %cs:(%si),%al                # gen=2e ac c0  dis=2e ac
    lods   %cs:(%si),%al                # gen=2e ac 06  dis=2e ac
    lods   %cs:(%si),%ax                # gen=2e ad c0  dis=2e ad
    lods   %cs:(%si),%ax                # gen=2e ad 06  dis=2e ad
    cs scas %es:(%di),%al               # gen=2e ae c0  dis=2e ae
    cs scas %es:(%di),%al               # gen=2e ae 06  dis=2e ae
    cs scas %es:(%di),%ax               # gen=2e af c0  dis=2e af
    cs scas %es:(%di),%ax               # gen=2e af 06  dis=2e af
    cs mov $0xc0,%al                    # gen=2e b0 c0  dis=2e b0 c0
    cs mov $0x6,%al                     # gen=2e b0 06  dis=2e b0 06
    cs mov $0xc0,%cl                    # gen=2e b1 c0  dis=2e b1 c0
    cs mov $0x6,%cl                     # gen=2e b1 06  dis=2e b1 06
    cs mov $0xc0,%dl                    # gen=2e b2 c0  dis=2e b2 c0
    cs mov $0x6,%dl                     # gen=2e b2 06  dis=2e b2 06
    cs mov $0xc0,%bl                    # gen=2e b3 c0  dis=2e b3 c0
    cs mov $0x6,%bl                     # gen=2e b3 06  dis=2e b3 06
    cs mov $0xc0,%ah                    # gen=2e b4 c0  dis=2e b4 c0
    cs mov $0x6,%ah                     # gen=2e b4 06  dis=2e b4 06
    cs mov $0xc0,%ch                    # gen=2e b5 c0  dis=2e b5 c0
    cs mov $0x6,%ch                     # gen=2e b5 06  dis=2e b5 06
    cs mov $0xc0,%dh                    # gen=2e b6 c0  dis=2e b6 c0
    cs mov $0x6,%dh                     # gen=2e b6 06  dis=2e b6 06
    cs mov $0xc0,%bh                    # gen=2e b7 c0  dis=2e b7 c0
    cs mov $0x6,%bh                     # gen=2e b7 06  dis=2e b7 06
    cs mov $0x90c0,%ax                  # gen=2e b8 c0  dis=2e b8 c0 90
    cs mov $0x9006,%ax                  # gen=2e b8 06  dis=2e b8 06 90
    cs mov $0x90c0,%cx                  # gen=2e b9 c0  dis=2e b9 c0 90
    cs mov $0x9006,%cx                  # gen=2e b9 06  dis=2e b9 06 90
    cs mov $0x90c0,%dx                  # gen=2e ba c0  dis=2e ba c0 90
    cs mov $0x9006,%dx                  # gen=2e ba 06  dis=2e ba 06 90
    cs mov $0x90c0,%bx                  # gen=2e bb c0  dis=2e bb c0 90
    cs mov $0x9006,%bx                  # gen=2e bb 06  dis=2e bb 06 90
    cs mov $0x90c0,%sp                  # gen=2e bc c0  dis=2e bc c0 90
    cs mov $0x9006,%sp                  # gen=2e bc 06  dis=2e bc 06 90
    cs mov $0x90c0,%bp                  # gen=2e bd c0  dis=2e bd c0 90
    cs mov $0x9006,%bp                  # gen=2e bd 06  dis=2e bd 06 90
    cs mov $0x90c0,%si                  # gen=2e be c0  dis=2e be c0 90
    cs mov $0x9006,%si                  # gen=2e be 06  dis=2e be 06 90
    cs mov $0x90c0,%di                  # gen=2e bf c0  dis=2e bf c0 90
    cs mov $0x9006,%di                  # gen=2e bf 06  dis=2e bf 06 90
    .byte 0x2e,0xc0,0xc0            # fallback; gen=2e c0 c0
    .byte 0x2e,0xc0,0x06            # fallback; gen=2e c0 06
    .byte 0x2e,0xc0,0xc8            # fallback; gen=2e c0 c8
    .byte 0x2e,0xc0,0xd0            # fallback; gen=2e c0 d0
    .byte 0x2e,0xc0,0xd8            # fallback; gen=2e c0 d8
    .byte 0x2e,0xc0,0xe0            # fallback; gen=2e c0 e0
    .byte 0x2e,0xc0,0xe8            # fallback; gen=2e c0 e8
    .byte 0x2e,0xc0,0xf0            # fallback; gen=2e c0 f0
    .byte 0x2e,0xc0,0xf8            # fallback; gen=2e c0 f8
    .byte 0x2e,0xc0,0x0e            # fallback; gen=2e c0 0e
    .byte 0x2e,0xc0,0x16            # fallback; gen=2e c0 16
    .byte 0x2e,0xc0,0x1e            # fallback; gen=2e c0 1e
    .byte 0x2e,0xc0,0x26            # fallback; gen=2e c0 26
    .byte 0x2e,0xc0,0x2e            # fallback; gen=2e c0 2e
    .byte 0x2e,0xc0,0x36            # fallback; gen=2e c0 36
    .byte 0x2e,0xc0,0x3e            # fallback; gen=2e c0 3e
    .byte 0x2e,0xc1,0xc0            # fallback; gen=2e c1 c0
    .byte 0x2e,0xc1,0x06            # fallback; gen=2e c1 06
    .byte 0x2e,0xc1,0xc8            # fallback; gen=2e c1 c8
    .byte 0x2e,0xc1,0xd0            # fallback; gen=2e c1 d0
    .byte 0x2e,0xc1,0xd8            # fallback; gen=2e c1 d8
    .byte 0x2e,0xc1,0xe0            # fallback; gen=2e c1 e0
    .byte 0x2e,0xc1,0xe8            # fallback; gen=2e c1 e8
    .byte 0x2e,0xc1,0xf0            # fallback; gen=2e c1 f0
    .byte 0x2e,0xc1,0xf8            # fallback; gen=2e c1 f8
    .byte 0x2e,0xc1,0x0e            # fallback; gen=2e c1 0e
    .byte 0x2e,0xc1,0x16            # fallback; gen=2e c1 16
    .byte 0x2e,0xc1,0x1e            # fallback; gen=2e c1 1e
    .byte 0x2e,0xc1,0x26            # fallback; gen=2e c1 26
    .byte 0x2e,0xc1,0x2e            # fallback; gen=2e c1 2e
    .byte 0x2e,0xc1,0x36            # fallback; gen=2e c1 36
    .byte 0x2e,0xc1,0x3e            # fallback; gen=2e c1 3e
    cs ret $0x90c0                      # gen=2e c2 c0  dis=2e c2 c0 90
    cs ret $0x9006                      # gen=2e c2 06  dis=2e c2 06 90
    cs ret                              # gen=2e c3 c0  dis=2e c3
    cs ret                              # gen=2e c3 06  dis=2e c3
    .byte 0x2e,0xc4,0xc0            # fallback; gen=2e c4 c0
    les    %cs:-0x6f70,%ax              # gen=2e c4 06  dis=2e c4 06 90 90
    .byte 0x2e,0xc4,0xc8            # fallback; gen=2e c4 c8
    .byte 0x2e,0xc4,0xd0            # fallback; gen=2e c4 d0
    .byte 0x2e,0xc4,0xd8            # fallback; gen=2e c4 d8
    .byte 0x2e,0xc4,0xe0            # fallback; gen=2e c4 e0
    .byte 0x2e,0xc4,0xe8            # fallback; gen=2e c4 e8
    .byte 0x2e,0xc4,0xf0            # fallback; gen=2e c4 f0
    .byte 0x2e,0xc4,0xf8            # fallback; gen=2e c4 f8
    les    %cs:-0x6f70,%cx              # gen=2e c4 0e  dis=2e c4 0e 90 90
    les    %cs:-0x6f70,%dx              # gen=2e c4 16  dis=2e c4 16 90 90
    les    %cs:-0x6f70,%bx              # gen=2e c4 1e  dis=2e c4 1e 90 90
    les    %cs:-0x6f70,%sp              # gen=2e c4 26  dis=2e c4 26 90 90
    les    %cs:-0x6f70,%bp              # gen=2e c4 2e  dis=2e c4 2e 90 90
    les    %cs:-0x6f70,%si              # gen=2e c4 36  dis=2e c4 36 90 90
    les    %cs:-0x6f70,%di              # gen=2e c4 3e  dis=2e c4 3e 90 90
    .byte 0x2e,0xc5,0xc0            # fallback; gen=2e c5 c0
    lds    %cs:-0x6f70,%ax              # gen=2e c5 06  dis=2e c5 06 90 90
    .byte 0x2e,0xc5,0xc8            # fallback; gen=2e c5 c8
    .byte 0x2e,0xc5,0xd0            # fallback; gen=2e c5 d0
    .byte 0x2e,0xc5,0xd8            # fallback; gen=2e c5 d8
    .byte 0x2e,0xc5,0xe0            # fallback; gen=2e c5 e0
    .byte 0x2e,0xc5,0xe8            # fallback; gen=2e c5 e8
    .byte 0x2e,0xc5,0xf0            # fallback; gen=2e c5 f0
    .byte 0x2e,0xc5,0xf8            # fallback; gen=2e c5 f8
    lds    %cs:-0x6f70,%cx              # gen=2e c5 0e  dis=2e c5 0e 90 90
    lds    %cs:-0x6f70,%dx              # gen=2e c5 16  dis=2e c5 16 90 90
    lds    %cs:-0x6f70,%bx              # gen=2e c5 1e  dis=2e c5 1e 90 90
    lds    %cs:-0x6f70,%sp              # gen=2e c5 26  dis=2e c5 26 90 90
    lds    %cs:-0x6f70,%bp              # gen=2e c5 2e  dis=2e c5 2e 90 90
    lds    %cs:-0x6f70,%si              # gen=2e c5 36  dis=2e c5 36 90 90
    lds    %cs:-0x6f70,%di              # gen=2e c5 3e  dis=2e c5 3e 90 90
    cs mov $0x90,%al                    # gen=2e c6 c0  dis=2e c6 c0 90
    movb   $0x90,%cs:-0x6f70            # gen=2e c6 06  dis=2e c6 06 90 90 90
    .byte 0x2e,0xc6,0xc8            # fallback; gen=2e c6 c8
    .byte 0x2e,0xc6,0xd0            # fallback; gen=2e c6 d0
    .byte 0x2e,0xc6,0xd8            # fallback; gen=2e c6 d8
    .byte 0x2e,0xc6,0xe0            # fallback; gen=2e c6 e0
    .byte 0x2e,0xc6,0xe8            # fallback; gen=2e c6 e8
    .byte 0x2e,0xc6,0xf0            # fallback; gen=2e c6 f0
    .byte 0x2e,0xc6,0xf8            # fallback; gen=2e c6 f8
    .byte 0x2e,0xc6,0x0e            # fallback; gen=2e c6 0e
    .byte 0x2e,0xc6,0x16            # fallback; gen=2e c6 16
    .byte 0x2e,0xc6,0x1e            # fallback; gen=2e c6 1e
    .byte 0x2e,0xc6,0x26            # fallback; gen=2e c6 26
    .byte 0x2e,0xc6,0x2e            # fallback; gen=2e c6 2e
    .byte 0x2e,0xc6,0x36            # fallback; gen=2e c6 36
    .byte 0x2e,0xc6,0x3e            # fallback; gen=2e c6 3e
    cs mov $0x9090,%ax                  # gen=2e c7 c0  dis=2e c7 c0 90 90
    movw   $0x9090,%cs:-0x6f70          # gen=2e c7 06  dis=2e c7 06 90 90 90 90
    .byte 0x2e,0xc7,0xc8            # fallback; gen=2e c7 c8
    .byte 0x2e,0xc7,0xd0            # fallback; gen=2e c7 d0
    .byte 0x2e,0xc7,0xd8            # fallback; gen=2e c7 d8
    .byte 0x2e,0xc7,0xe0            # fallback; gen=2e c7 e0
    .byte 0x2e,0xc7,0xe8            # fallback; gen=2e c7 e8
    .byte 0x2e,0xc7,0xf0            # fallback; gen=2e c7 f0
    .byte 0x2e,0xc7,0xf8            # fallback; gen=2e c7 f8
    .byte 0x2e,0xc7,0x0e            # fallback; gen=2e c7 0e
    .byte 0x2e,0xc7,0x16            # fallback; gen=2e c7 16
    .byte 0x2e,0xc7,0x1e            # fallback; gen=2e c7 1e
    .byte 0x2e,0xc7,0x26            # fallback; gen=2e c7 26
    .byte 0x2e,0xc7,0x2e            # fallback; gen=2e c7 2e
    .byte 0x2e,0xc7,0x36            # fallback; gen=2e c7 36
    .byte 0x2e,0xc7,0x3e            # fallback; gen=2e c7 3e
    .byte 0x2e,0xc8,0xc0            # fallback; gen=2e c8 c0
    .byte 0x2e,0xc8,0x06            # fallback; gen=2e c8 06
    .byte 0x2e,0xc9,0xc0            # fallback; gen=2e c9 c0
    .byte 0x2e,0xc9,0x06            # fallback; gen=2e c9 06
    cs lret $0x90c0                     # gen=2e ca c0  dis=2e ca c0 90
    cs lret $0x9006                     # gen=2e ca 06  dis=2e ca 06 90
    cs lret                             # gen=2e cb c0  dis=2e cb
    cs lret                             # gen=2e cb 06  dis=2e cb
    cs int3                             # gen=2e cc c0  dis=2e cc
    cs int3                             # gen=2e cc 06  dis=2e cc
    cs int $0xc0                        # gen=2e cd c0  dis=2e cd c0
    cs int $0x6                         # gen=2e cd 06  dis=2e cd 06
    cs into                             # gen=2e ce c0  dis=2e ce
    cs into                             # gen=2e ce 06  dis=2e ce
    cs iret                             # gen=2e cf c0  dis=2e cf
    cs iret                             # gen=2e cf 06  dis=2e cf
    cs rol $1,%al                       # gen=2e d0 c0  dis=2e d0 c0
    rolb   $1,%cs:-0x6f70               # gen=2e d0 06  dis=2e d0 06 90 90
    cs ror $1,%al                       # gen=2e d0 c8  dis=2e d0 c8
    cs rcl $1,%al                       # gen=2e d0 d0  dis=2e d0 d0
    cs rcr $1,%al                       # gen=2e d0 d8  dis=2e d0 d8
    cs shl $1,%al                       # gen=2e d0 e0  dis=2e d0 e0
    cs shr $1,%al                       # gen=2e d0 e8  dis=2e d0 e8
    cs shl $1,%al                       # gen=2e d0 f0  dis=2e d0 f0
    cs sar $1,%al                       # gen=2e d0 f8  dis=2e d0 f8
    rorb   $1,%cs:-0x6f70               # gen=2e d0 0e  dis=2e d0 0e 90 90
    rclb   $1,%cs:-0x6f70               # gen=2e d0 16  dis=2e d0 16 90 90
    rcrb   $1,%cs:-0x6f70               # gen=2e d0 1e  dis=2e d0 1e 90 90
    shlb   $1,%cs:-0x6f70               # gen=2e d0 26  dis=2e d0 26 90 90
    shrb   $1,%cs:-0x6f70               # gen=2e d0 2e  dis=2e d0 2e 90 90
    shlb   $1,%cs:-0x6f70               # gen=2e d0 36  dis=2e d0 36 90 90
    sarb   $1,%cs:-0x6f70               # gen=2e d0 3e  dis=2e d0 3e 90 90
    cs rol $1,%ax                       # gen=2e d1 c0  dis=2e d1 c0
    rolw   $1,%cs:-0x6f70               # gen=2e d1 06  dis=2e d1 06 90 90
    cs ror $1,%ax                       # gen=2e d1 c8  dis=2e d1 c8
    cs rcl $1,%ax                       # gen=2e d1 d0  dis=2e d1 d0
    cs rcr $1,%ax                       # gen=2e d1 d8  dis=2e d1 d8
    cs shl $1,%ax                       # gen=2e d1 e0  dis=2e d1 e0
    cs shr $1,%ax                       # gen=2e d1 e8  dis=2e d1 e8
    cs shl $1,%ax                       # gen=2e d1 f0  dis=2e d1 f0
    cs sar $1,%ax                       # gen=2e d1 f8  dis=2e d1 f8
    rorw   $1,%cs:-0x6f70               # gen=2e d1 0e  dis=2e d1 0e 90 90
    rclw   $1,%cs:-0x6f70               # gen=2e d1 16  dis=2e d1 16 90 90
    rcrw   $1,%cs:-0x6f70               # gen=2e d1 1e  dis=2e d1 1e 90 90
    shlw   $1,%cs:-0x6f70               # gen=2e d1 26  dis=2e d1 26 90 90
    shrw   $1,%cs:-0x6f70               # gen=2e d1 2e  dis=2e d1 2e 90 90
    shlw   $1,%cs:-0x6f70               # gen=2e d1 36  dis=2e d1 36 90 90
    sarw   $1,%cs:-0x6f70               # gen=2e d1 3e  dis=2e d1 3e 90 90
    cs rol %cl,%al                      # gen=2e d2 c0  dis=2e d2 c0
    rolb   %cl,%cs:-0x6f70              # gen=2e d2 06  dis=2e d2 06 90 90
    cs ror %cl,%al                      # gen=2e d2 c8  dis=2e d2 c8
    cs rcl %cl,%al                      # gen=2e d2 d0  dis=2e d2 d0
    cs rcr %cl,%al                      # gen=2e d2 d8  dis=2e d2 d8
    cs shl %cl,%al                      # gen=2e d2 e0  dis=2e d2 e0
    cs shr %cl,%al                      # gen=2e d2 e8  dis=2e d2 e8
    cs shl %cl,%al                      # gen=2e d2 f0  dis=2e d2 f0
    cs sar %cl,%al                      # gen=2e d2 f8  dis=2e d2 f8
    rorb   %cl,%cs:-0x6f70              # gen=2e d2 0e  dis=2e d2 0e 90 90
    rclb   %cl,%cs:-0x6f70              # gen=2e d2 16  dis=2e d2 16 90 90
    rcrb   %cl,%cs:-0x6f70              # gen=2e d2 1e  dis=2e d2 1e 90 90
    shlb   %cl,%cs:-0x6f70              # gen=2e d2 26  dis=2e d2 26 90 90
    shrb   %cl,%cs:-0x6f70              # gen=2e d2 2e  dis=2e d2 2e 90 90
    shlb   %cl,%cs:-0x6f70              # gen=2e d2 36  dis=2e d2 36 90 90
    sarb   %cl,%cs:-0x6f70              # gen=2e d2 3e  dis=2e d2 3e 90 90
    cs rol %cl,%ax                      # gen=2e d3 c0  dis=2e d3 c0
    rolw   %cl,%cs:-0x6f70              # gen=2e d3 06  dis=2e d3 06 90 90
    cs ror %cl,%ax                      # gen=2e d3 c8  dis=2e d3 c8
    cs rcl %cl,%ax                      # gen=2e d3 d0  dis=2e d3 d0
    cs rcr %cl,%ax                      # gen=2e d3 d8  dis=2e d3 d8
    cs shl %cl,%ax                      # gen=2e d3 e0  dis=2e d3 e0
    cs shr %cl,%ax                      # gen=2e d3 e8  dis=2e d3 e8
    cs shl %cl,%ax                      # gen=2e d3 f0  dis=2e d3 f0
    cs sar %cl,%ax                      # gen=2e d3 f8  dis=2e d3 f8
    rorw   %cl,%cs:-0x6f70              # gen=2e d3 0e  dis=2e d3 0e 90 90
    rclw   %cl,%cs:-0x6f70              # gen=2e d3 16  dis=2e d3 16 90 90
    rcrw   %cl,%cs:-0x6f70              # gen=2e d3 1e  dis=2e d3 1e 90 90
    shlw   %cl,%cs:-0x6f70              # gen=2e d3 26  dis=2e d3 26 90 90
    shrw   %cl,%cs:-0x6f70              # gen=2e d3 2e  dis=2e d3 2e 90 90
    shlw   %cl,%cs:-0x6f70              # gen=2e d3 36  dis=2e d3 36 90 90
    sarw   %cl,%cs:-0x6f70              # gen=2e d3 3e  dis=2e d3 3e 90 90
    cs aam $0xc0                        # gen=2e d4 c0  dis=2e d4 c0
    cs aam $0x6                         # gen=2e d4 06  dis=2e d4 06
    cs aad $0xc0                        # gen=2e d5 c0  dis=2e d5 c0
    cs aad $0x6                         # gen=2e d5 06  dis=2e d5 06
    cs salc                             # gen=2e d6 c0  dis=2e d6
    cs salc                             # gen=2e d6 06  dis=2e d6
    xlat   %cs:(%bx)                    # gen=2e d7 c0  dis=2e d7
    xlat   %cs:(%bx)                    # gen=2e d7 06  dis=2e d7
    .byte 0x2e,0xd8,0xc0            # fallback; gen=2e d8 c0
    .byte 0x2e,0xd8,0x06            # fallback; gen=2e d8 06
    .byte 0x2e,0xd8,0xc8            # fallback; gen=2e d8 c8
    .byte 0x2e,0xd8,0xd0            # fallback; gen=2e d8 d0
    .byte 0x2e,0xd8,0xd8            # fallback; gen=2e d8 d8
    .byte 0x2e,0xd8,0xe0            # fallback; gen=2e d8 e0
    .byte 0x2e,0xd8,0xe8            # fallback; gen=2e d8 e8
    .byte 0x2e,0xd8,0xf0            # fallback; gen=2e d8 f0
    .byte 0x2e,0xd8,0xf8            # fallback; gen=2e d8 f8
    .byte 0x2e,0xd8,0x0e            # fallback; gen=2e d8 0e
    .byte 0x2e,0xd8,0x16            # fallback; gen=2e d8 16
    .byte 0x2e,0xd8,0x1e            # fallback; gen=2e d8 1e
    .byte 0x2e,0xd8,0x26            # fallback; gen=2e d8 26
    .byte 0x2e,0xd8,0x2e            # fallback; gen=2e d8 2e
    .byte 0x2e,0xd8,0x36            # fallback; gen=2e d8 36
    .byte 0x2e,0xd8,0x3e            # fallback; gen=2e d8 3e
    .byte 0x2e,0xd9,0xc0            # fallback; gen=2e d9 c0
    .byte 0x2e,0xd9,0x06            # fallback; gen=2e d9 06
    .byte 0x2e,0xd9,0xc8            # fallback; gen=2e d9 c8
    .byte 0x2e,0xd9,0xd0            # fallback; gen=2e d9 d0
    .byte 0x2e,0xd9,0xd8            # fallback; gen=2e d9 d8
    .byte 0x2e,0xd9,0xe0            # fallback; gen=2e d9 e0
    .byte 0x2e,0xd9,0xe8            # fallback; gen=2e d9 e8
    .byte 0x2e,0xd9,0xf0            # fallback; gen=2e d9 f0
    .byte 0x2e,0xd9,0xf8            # fallback; gen=2e d9 f8
    .byte 0x2e,0xd9,0x0e            # fallback; gen=2e d9 0e
    .byte 0x2e,0xd9,0x16            # fallback; gen=2e d9 16
    .byte 0x2e,0xd9,0x1e            # fallback; gen=2e d9 1e
    .byte 0x2e,0xd9,0x26            # fallback; gen=2e d9 26
    .byte 0x2e,0xd9,0x2e            # fallback; gen=2e d9 2e
    .byte 0x2e,0xd9,0x36            # fallback; gen=2e d9 36
    .byte 0x2e,0xd9,0x3e            # fallback; gen=2e d9 3e
    .byte 0x2e,0xda,0xc0            # fallback; gen=2e da c0
    .byte 0x2e,0xda,0x06            # fallback; gen=2e da 06
    .byte 0x2e,0xda,0xc8            # fallback; gen=2e da c8
    .byte 0x2e,0xda,0xd0            # fallback; gen=2e da d0
    .byte 0x2e,0xda,0xd8            # fallback; gen=2e da d8
    .byte 0x2e,0xda,0xe0            # fallback; gen=2e da e0
    .byte 0x2e,0xda,0xe8            # fallback; gen=2e da e8
    .byte 0x2e,0xda,0xf0            # fallback; gen=2e da f0
    .byte 0x2e,0xda,0xf8            # fallback; gen=2e da f8
    .byte 0x2e,0xda,0x0e            # fallback; gen=2e da 0e
    .byte 0x2e,0xda,0x16            # fallback; gen=2e da 16
    .byte 0x2e,0xda,0x1e            # fallback; gen=2e da 1e
    .byte 0x2e,0xda,0x26            # fallback; gen=2e da 26
    .byte 0x2e,0xda,0x2e            # fallback; gen=2e da 2e
    .byte 0x2e,0xda,0x36            # fallback; gen=2e da 36
    .byte 0x2e,0xda,0x3e            # fallback; gen=2e da 3e
    .byte 0x2e,0xdb,0xc0            # fallback; gen=2e db c0
    .byte 0x2e,0xdb,0x06            # fallback; gen=2e db 06
    .byte 0x2e,0xdb,0xc8            # fallback; gen=2e db c8
    .byte 0x2e,0xdb,0xd0            # fallback; gen=2e db d0
    .byte 0x2e,0xdb,0xd8            # fallback; gen=2e db d8
    .byte 0x2e,0xdb,0xe0            # fallback; gen=2e db e0
    .byte 0x2e,0xdb,0xe8            # fallback; gen=2e db e8
    .byte 0x2e,0xdb,0xf0            # fallback; gen=2e db f0
    .byte 0x2e,0xdb,0xf8            # fallback; gen=2e db f8
    .byte 0x2e,0xdb,0x0e            # fallback; gen=2e db 0e
    .byte 0x2e,0xdb,0x16            # fallback; gen=2e db 16
    .byte 0x2e,0xdb,0x1e            # fallback; gen=2e db 1e
    .byte 0x2e,0xdb,0x26            # fallback; gen=2e db 26
    .byte 0x2e,0xdb,0x2e            # fallback; gen=2e db 2e
    .byte 0x2e,0xdb,0x36            # fallback; gen=2e db 36
    .byte 0x2e,0xdb,0x3e            # fallback; gen=2e db 3e
    .byte 0x2e,0xdc,0xc0            # fallback; gen=2e dc c0
    .byte 0x2e,0xdc,0x06            # fallback; gen=2e dc 06
    .byte 0x2e,0xdc,0xc8            # fallback; gen=2e dc c8
    .byte 0x2e,0xdc,0xd0            # fallback; gen=2e dc d0
    .byte 0x2e,0xdc,0xd8            # fallback; gen=2e dc d8
    .byte 0x2e,0xdc,0xe0            # fallback; gen=2e dc e0
    .byte 0x2e,0xdc,0xe8            # fallback; gen=2e dc e8
    .byte 0x2e,0xdc,0xf0            # fallback; gen=2e dc f0
    .byte 0x2e,0xdc,0xf8            # fallback; gen=2e dc f8
    .byte 0x2e,0xdc,0x0e            # fallback; gen=2e dc 0e
    .byte 0x2e,0xdc,0x16            # fallback; gen=2e dc 16
    .byte 0x2e,0xdc,0x1e            # fallback; gen=2e dc 1e
    .byte 0x2e,0xdc,0x26            # fallback; gen=2e dc 26
    .byte 0x2e,0xdc,0x2e            # fallback; gen=2e dc 2e
    .byte 0x2e,0xdc,0x36            # fallback; gen=2e dc 36
    .byte 0x2e,0xdc,0x3e            # fallback; gen=2e dc 3e
    .byte 0x2e,0xdd,0xc0            # fallback; gen=2e dd c0
    .byte 0x2e,0xdd,0x06            # fallback; gen=2e dd 06
    .byte 0x2e,0xdd,0xc8            # fallback; gen=2e dd c8
    .byte 0x2e,0xdd,0xd0            # fallback; gen=2e dd d0
    .byte 0x2e,0xdd,0xd8            # fallback; gen=2e dd d8
    .byte 0x2e,0xdd,0xe0            # fallback; gen=2e dd e0
    .byte 0x2e,0xdd,0xe8            # fallback; gen=2e dd e8
    .byte 0x2e,0xdd,0xf0            # fallback; gen=2e dd f0
    .byte 0x2e,0xdd,0xf8            # fallback; gen=2e dd f8
    .byte 0x2e,0xdd,0x0e            # fallback; gen=2e dd 0e
    .byte 0x2e,0xdd,0x16            # fallback; gen=2e dd 16
    .byte 0x2e,0xdd,0x1e            # fallback; gen=2e dd 1e
    .byte 0x2e,0xdd,0x26            # fallback; gen=2e dd 26
    .byte 0x2e,0xdd,0x2e            # fallback; gen=2e dd 2e
    .byte 0x2e,0xdd,0x36            # fallback; gen=2e dd 36
    .byte 0x2e,0xdd,0x3e            # fallback; gen=2e dd 3e
    .byte 0x2e,0xde,0xc0            # fallback; gen=2e de c0
    .byte 0x2e,0xde,0x06            # fallback; gen=2e de 06
    .byte 0x2e,0xde,0xc8            # fallback; gen=2e de c8
    .byte 0x2e,0xde,0xd0            # fallback; gen=2e de d0
    .byte 0x2e,0xde,0xd8            # fallback; gen=2e de d8
    .byte 0x2e,0xde,0xe0            # fallback; gen=2e de e0
    .byte 0x2e,0xde,0xe8            # fallback; gen=2e de e8
    .byte 0x2e,0xde,0xf0            # fallback; gen=2e de f0
    .byte 0x2e,0xde,0xf8            # fallback; gen=2e de f8
    .byte 0x2e,0xde,0x0e            # fallback; gen=2e de 0e
    .byte 0x2e,0xde,0x16            # fallback; gen=2e de 16
    .byte 0x2e,0xde,0x1e            # fallback; gen=2e de 1e
    .byte 0x2e,0xde,0x26            # fallback; gen=2e de 26
    .byte 0x2e,0xde,0x2e            # fallback; gen=2e de 2e
    .byte 0x2e,0xde,0x36            # fallback; gen=2e de 36
    .byte 0x2e,0xde,0x3e            # fallback; gen=2e de 3e
    .byte 0x2e,0xdf,0xc0            # fallback; gen=2e df c0
    .byte 0x2e,0xdf,0x06            # fallback; gen=2e df 06
    .byte 0x2e,0xdf,0xc8            # fallback; gen=2e df c8
    .byte 0x2e,0xdf,0xd0            # fallback; gen=2e df d0
    .byte 0x2e,0xdf,0xd8            # fallback; gen=2e df d8
    .byte 0x2e,0xdf,0xe0            # fallback; gen=2e df e0
    .byte 0x2e,0xdf,0xe8            # fallback; gen=2e df e8
    .byte 0x2e,0xdf,0xf0            # fallback; gen=2e df f0
    .byte 0x2e,0xdf,0xf8            # fallback; gen=2e df f8
    .byte 0x2e,0xdf,0x0e            # fallback; gen=2e df 0e
    .byte 0x2e,0xdf,0x16            # fallback; gen=2e df 16
    .byte 0x2e,0xdf,0x1e            # fallback; gen=2e df 1e
    .byte 0x2e,0xdf,0x26            # fallback; gen=2e df 26
    .byte 0x2e,0xdf,0x2e            # fallback; gen=2e df 2e
    .byte 0x2e,0xdf,0x36            # fallback; gen=2e df 36
    .byte 0x2e,0xdf,0x3e            # fallback; gen=2e df 3e
    .byte 0x2e,0xe0,0xc0            # fallback; gen=2e e0 c0
    .byte 0x2e,0xe0,0x06            # fallback; gen=2e e0 06
    .byte 0x2e,0xe1,0xc0            # fallback; gen=2e e1 c0
    .byte 0x2e,0xe1,0x06            # fallback; gen=2e e1 06
    .byte 0x2e,0xe2,0xc0            # fallback; gen=2e e2 c0
    .byte 0x2e,0xe2,0x06            # fallback; gen=2e e2 06
    .byte 0x2e,0xe3,0xc0            # fallback; gen=2e e3 c0
    .byte 0x2e,0xe3,0x06            # fallback; gen=2e e3 06
    cs in  $0xc0,%al                    # gen=2e e4 c0  dis=2e e4 c0
    cs in  $0x6,%al                     # gen=2e e4 06  dis=2e e4 06
    cs in  $0xc0,%ax                    # gen=2e e5 c0  dis=2e e5 c0
    cs in  $0x6,%ax                     # gen=2e e5 06  dis=2e e5 06
    cs out %al,$0xc0                    # gen=2e e6 c0  dis=2e e6 c0
    cs out %al,$0x6                     # gen=2e e6 06  dis=2e e6 06
    cs out %ax,$0xc0                    # gen=2e e7 c0  dis=2e e7 c0
    cs out %ax,$0x6                     # gen=2e e7 06  dis=2e e7 06
    .byte 0x2e,0xe8,0xc0            # fallback; gen=2e e8 c0
    .byte 0x2e,0xe8,0x06            # fallback; gen=2e e8 06
    .byte 0x2e,0xe9,0xc0            # fallback; gen=2e e9 c0
    .byte 0x2e,0xe9,0x06            # fallback; gen=2e e9 06
    .byte 0x2e,0xea,0xc0            # fallback; gen=2e ea c0
    .byte 0x2e,0xea,0x06            # fallback; gen=2e ea 06
    .byte 0x2e,0xeb,0xc0            # fallback; gen=2e eb c0
    .byte 0x2e,0xeb,0x06            # fallback; gen=2e eb 06
    cs in  (%dx),%al                    # gen=2e ec c0  dis=2e ec
    cs in  (%dx),%al                    # gen=2e ec 06  dis=2e ec
    cs in  (%dx),%ax                    # gen=2e ed c0  dis=2e ed
    cs in  (%dx),%ax                    # gen=2e ed 06  dis=2e ed
    cs out %al,(%dx)                    # gen=2e ee c0  dis=2e ee
    cs out %al,(%dx)                    # gen=2e ee 06  dis=2e ee
    cs out %ax,(%dx)                    # gen=2e ef c0  dis=2e ef
    cs out %ax,(%dx)                    # gen=2e ef 06  dis=2e ef
    .byte 0x2e,0xf0,0xc0            # fallback; gen=2e f0 c0
    .byte 0x2e,0xf0,0x06            # fallback; gen=2e f0 06
    .byte 0x2e,0xf0,0xc8            # fallback; gen=2e f0 c8
    .byte 0x2e,0xf0,0xd0            # fallback; gen=2e f0 d0
    .byte 0x2e,0xf0,0xd8            # fallback; gen=2e f0 d8
    .byte 0x2e,0xf0,0xe0            # fallback; gen=2e f0 e0
    .byte 0x2e,0xf0,0xe8            # fallback; gen=2e f0 e8
    .byte 0x2e,0xf0,0xf0            # fallback; gen=2e f0 f0
    .byte 0x2e,0xf0,0xf8            # fallback; gen=2e f0 f8
    .byte 0x2e,0xf0,0x0e            # fallback; gen=2e f0 0e
    .byte 0x2e,0xf0,0x16            # fallback; gen=2e f0 16
    .byte 0x2e,0xf0,0x1e            # fallback; gen=2e f0 1e
    .byte 0x2e,0xf0,0x26            # fallback; gen=2e f0 26
    .byte 0x2e,0xf0,0x2e            # fallback; gen=2e f0 2e
    .byte 0x2e,0xf0,0x36            # fallback; gen=2e f0 36
    .byte 0x2e,0xf0,0x3e            # fallback; gen=2e f0 3e
    cs int1                             # gen=2e f1 c0  dis=2e f1
    cs int1                             # gen=2e f1 06  dis=2e f1
    .byte 0x2e,0xf2,0xc0            # fallback; gen=2e f2 c0
    .byte 0x2e,0xf2,0x06            # fallback; gen=2e f2 06
    .byte 0x2e,0xf2,0xc8            # fallback; gen=2e f2 c8
    .byte 0x2e,0xf2,0xd0            # fallback; gen=2e f2 d0
    .byte 0x2e,0xf2,0xd8            # fallback; gen=2e f2 d8
    .byte 0x2e,0xf2,0xe0            # fallback; gen=2e f2 e0
    .byte 0x2e,0xf2,0xe8            # fallback; gen=2e f2 e8
    .byte 0x2e,0xf2,0xf0            # fallback; gen=2e f2 f0
    .byte 0x2e,0xf2,0xf8            # fallback; gen=2e f2 f8
    .byte 0x2e,0xf2,0x0e            # fallback; gen=2e f2 0e
    .byte 0x2e,0xf2,0x16            # fallback; gen=2e f2 16
    .byte 0x2e,0xf2,0x1e            # fallback; gen=2e f2 1e
    .byte 0x2e,0xf2,0x26            # fallback; gen=2e f2 26
    .byte 0x2e,0xf2,0x2e            # fallback; gen=2e f2 2e
    .byte 0x2e,0xf2,0x36            # fallback; gen=2e f2 36
    .byte 0x2e,0xf2,0x3e            # fallback; gen=2e f2 3e
    .byte 0x2e,0xf3,0xc0            # fallback; gen=2e f3 c0
    .byte 0x2e,0xf3,0x06            # fallback; gen=2e f3 06
    .byte 0x2e,0xf3,0xc8            # fallback; gen=2e f3 c8
    .byte 0x2e,0xf3,0xd0            # fallback; gen=2e f3 d0
    .byte 0x2e,0xf3,0xd8            # fallback; gen=2e f3 d8
    .byte 0x2e,0xf3,0xe0            # fallback; gen=2e f3 e0
    .byte 0x2e,0xf3,0xe8            # fallback; gen=2e f3 e8
    .byte 0x2e,0xf3,0xf0            # fallback; gen=2e f3 f0
    .byte 0x2e,0xf3,0xf8            # fallback; gen=2e f3 f8
    .byte 0x2e,0xf3,0x0e            # fallback; gen=2e f3 0e
    .byte 0x2e,0xf3,0x16            # fallback; gen=2e f3 16
    .byte 0x2e,0xf3,0x1e            # fallback; gen=2e f3 1e
    .byte 0x2e,0xf3,0x26            # fallback; gen=2e f3 26
    .byte 0x2e,0xf3,0x2e            # fallback; gen=2e f3 2e
    .byte 0x2e,0xf3,0x36            # fallback; gen=2e f3 36
    .byte 0x2e,0xf3,0x3e            # fallback; gen=2e f3 3e
    cs hlt                              # gen=2e f4 c0  dis=2e f4
    cs hlt                              # gen=2e f4 06  dis=2e f4
    cs cmc                              # gen=2e f5 c0  dis=2e f5
    cs cmc                              # gen=2e f5 06  dis=2e f5
    cs test $0x90,%al                   # gen=2e f6 c0  dis=2e f6 c0 90
    testb  $0x90,%cs:-0x6f70            # gen=2e f6 06  dis=2e f6 06 90 90 90
    cs test $0x90,%al                   # gen=2e f6 c8  dis=2e f6 c8 90
    cs not %al                          # gen=2e f6 d0  dis=2e f6 d0
    cs neg %al                          # gen=2e f6 d8  dis=2e f6 d8
    cs mul %al                          # gen=2e f6 e0  dis=2e f6 e0
    cs imul %al                         # gen=2e f6 e8  dis=2e f6 e8
    cs div %al                          # gen=2e f6 f0  dis=2e f6 f0
    cs idiv %al                         # gen=2e f6 f8  dis=2e f6 f8
    testb  $0x90,%cs:-0x6f70            # gen=2e f6 0e  dis=2e f6 0e 90 90 90
    notb   %cs:-0x6f70                  # gen=2e f6 16  dis=2e f6 16 90 90
    negb   %cs:-0x6f70                  # gen=2e f6 1e  dis=2e f6 1e 90 90
    mulb   %cs:-0x6f70                  # gen=2e f6 26  dis=2e f6 26 90 90
    imulb  %cs:-0x6f70                  # gen=2e f6 2e  dis=2e f6 2e 90 90
    divb   %cs:-0x6f70                  # gen=2e f6 36  dis=2e f6 36 90 90
    idivb  %cs:-0x6f70                  # gen=2e f6 3e  dis=2e f6 3e 90 90
    cs test $0x9090,%ax                 # gen=2e f7 c0  dis=2e f7 c0 90 90
    testw  $0x9090,%cs:-0x6f70          # gen=2e f7 06  dis=2e f7 06 90 90 90 90
    cs test $0x9090,%ax                 # gen=2e f7 c8  dis=2e f7 c8 90 90
    cs not %ax                          # gen=2e f7 d0  dis=2e f7 d0
    cs neg %ax                          # gen=2e f7 d8  dis=2e f7 d8
    cs mul %ax                          # gen=2e f7 e0  dis=2e f7 e0
    cs imul %ax                         # gen=2e f7 e8  dis=2e f7 e8
    cs div %ax                          # gen=2e f7 f0  dis=2e f7 f0
    cs idiv %ax                         # gen=2e f7 f8  dis=2e f7 f8
    testw  $0x9090,%cs:-0x6f70          # gen=2e f7 0e  dis=2e f7 0e 90 90 90 90
    notw   %cs:-0x6f70                  # gen=2e f7 16  dis=2e f7 16 90 90
    negw   %cs:-0x6f70                  # gen=2e f7 1e  dis=2e f7 1e 90 90
    mulw   %cs:-0x6f70                  # gen=2e f7 26  dis=2e f7 26 90 90
    imulw  %cs:-0x6f70                  # gen=2e f7 2e  dis=2e f7 2e 90 90
    divw   %cs:-0x6f70                  # gen=2e f7 36  dis=2e f7 36 90 90
    idivw  %cs:-0x6f70                  # gen=2e f7 3e  dis=2e f7 3e 90 90
    cs clc                              # gen=2e f8 c0  dis=2e f8
    cs clc                              # gen=2e f8 06  dis=2e f8
    cs stc                              # gen=2e f9 c0  dis=2e f9
    cs stc                              # gen=2e f9 06  dis=2e f9
    cs cli                              # gen=2e fa c0  dis=2e fa
    cs cli                              # gen=2e fa 06  dis=2e fa
    cs sti                              # gen=2e fb c0  dis=2e fb
    cs sti                              # gen=2e fb 06  dis=2e fb
    cs cld                              # gen=2e fc c0  dis=2e fc
    cs cld                              # gen=2e fc 06  dis=2e fc
    cs std                              # gen=2e fd c0  dis=2e fd
    cs std                              # gen=2e fd 06  dis=2e fd
    cs inc %al                          # gen=2e fe c0  dis=2e fe c0
    incb   %cs:-0x6f70                  # gen=2e fe 06  dis=2e fe 06 90 90
    cs dec %al                          # gen=2e fe c8  dis=2e fe c8
    .byte 0x2e,0xfe,0xd0            # fallback; gen=2e fe d0
    .byte 0x2e,0xfe,0xd8            # fallback; gen=2e fe d8
    .byte 0x2e,0xfe,0xe0            # fallback; gen=2e fe e0
    .byte 0x2e,0xfe,0xe8            # fallback; gen=2e fe e8
    .byte 0x2e,0xfe,0xf0            # fallback; gen=2e fe f0
    .byte 0x2e,0xfe,0xf8            # fallback; gen=2e fe f8
    decb   %cs:-0x6f70                  # gen=2e fe 0e  dis=2e fe 0e 90 90
    .byte 0x2e,0xfe,0x16            # fallback; gen=2e fe 16
    .byte 0x2e,0xfe,0x1e            # fallback; gen=2e fe 1e
    .byte 0x2e,0xfe,0x26            # fallback; gen=2e fe 26
    .byte 0x2e,0xfe,0x2e            # fallback; gen=2e fe 2e
    .byte 0x2e,0xfe,0x36            # fallback; gen=2e fe 36
    .byte 0x2e,0xfe,0x3e            # fallback; gen=2e fe 3e
    cs inc %ax                          # gen=2e ff c0  dis=2e ff c0
    incw   %cs:-0x6f70                  # gen=2e ff 06  dis=2e ff 06 90 90
    cs dec %ax                          # gen=2e ff c8  dis=2e ff c8
    cs call *%ax                        # gen=2e ff d0  dis=2e ff d0
    .byte 0x2e,0xff,0xd8            # fallback; gen=2e ff d8
    cs jmp *%ax                         # gen=2e ff e0  dis=2e ff e0
    .byte 0x2e,0xff,0xe8            # fallback; gen=2e ff e8
    cs push %ax                         # gen=2e ff f0  dis=2e ff f0
    .byte 0x2e,0xff,0xf8            # fallback; gen=2e ff f8
    decw   %cs:-0x6f70                  # gen=2e ff 0e  dis=2e ff 0e 90 90
    call   *%cs:-0x6f70                 # gen=2e ff 16  dis=2e ff 16 90 90
    lcall  *%cs:-0x6f70                 # gen=2e ff 1e  dis=2e ff 1e 90 90
    jmp    *%cs:-0x6f70                 # gen=2e ff 26  dis=2e ff 26 90 90
    ljmp   *%cs:-0x6f70                 # gen=2e ff 2e  dis=2e ff 2e 90 90
    push   %cs:-0x6f70                  # gen=2e ff 36  dis=2e ff 36 90 90
    .byte 0x2e,0xff,0x3e            # fallback; gen=2e ff 3e
    ss add %al,%al                      # gen=36 00 c0  dis=36 00 c0
    add    %al,%ss:-0x6f70              # gen=36 00 06  dis=36 00 06 90 90
    ss add %cl,%al                      # gen=36 00 c8  dis=36 00 c8
    ss add %dl,%al                      # gen=36 00 d0  dis=36 00 d0
    ss add %bl,%al                      # gen=36 00 d8  dis=36 00 d8
    ss add %ah,%al                      # gen=36 00 e0  dis=36 00 e0
    ss add %ch,%al                      # gen=36 00 e8  dis=36 00 e8
    ss add %dh,%al                      # gen=36 00 f0  dis=36 00 f0
    ss add %bh,%al                      # gen=36 00 f8  dis=36 00 f8
    add    %cl,%ss:-0x6f70              # gen=36 00 0e  dis=36 00 0e 90 90
    add    %dl,%ss:-0x6f70              # gen=36 00 16  dis=36 00 16 90 90
    add    %bl,%ss:-0x6f70              # gen=36 00 1e  dis=36 00 1e 90 90
    add    %ah,%ss:-0x6f70              # gen=36 00 26  dis=36 00 26 90 90
    add    %ch,%ss:-0x6f70              # gen=36 00 2e  dis=36 00 2e 90 90
    add    %dh,%ss:-0x6f70              # gen=36 00 36  dis=36 00 36 90 90
    add    %bh,%ss:-0x6f70              # gen=36 00 3e  dis=36 00 3e 90 90
    ss add %ax,%ax                      # gen=36 01 c0  dis=36 01 c0
    add    %ax,%ss:-0x6f70              # gen=36 01 06  dis=36 01 06 90 90
    ss add %cx,%ax                      # gen=36 01 c8  dis=36 01 c8
    ss add %dx,%ax                      # gen=36 01 d0  dis=36 01 d0
    ss add %bx,%ax                      # gen=36 01 d8  dis=36 01 d8
    ss add %sp,%ax                      # gen=36 01 e0  dis=36 01 e0
    ss add %bp,%ax                      # gen=36 01 e8  dis=36 01 e8
    ss add %si,%ax                      # gen=36 01 f0  dis=36 01 f0
    ss add %di,%ax                      # gen=36 01 f8  dis=36 01 f8
    add    %cx,%ss:-0x6f70              # gen=36 01 0e  dis=36 01 0e 90 90
    add    %dx,%ss:-0x6f70              # gen=36 01 16  dis=36 01 16 90 90
    add    %bx,%ss:-0x6f70              # gen=36 01 1e  dis=36 01 1e 90 90
    add    %sp,%ss:-0x6f70              # gen=36 01 26  dis=36 01 26 90 90
    add    %bp,%ss:-0x6f70              # gen=36 01 2e  dis=36 01 2e 90 90
    add    %si,%ss:-0x6f70              # gen=36 01 36  dis=36 01 36 90 90
    add    %di,%ss:-0x6f70              # gen=36 01 3e  dis=36 01 3e 90 90
    ss add %al,%al                      # gen=36 02 c0  dis=36 02 c0
    add    %ss:-0x6f70,%al              # gen=36 02 06  dis=36 02 06 90 90
    ss add %al,%cl                      # gen=36 02 c8  dis=36 02 c8
    ss add %al,%dl                      # gen=36 02 d0  dis=36 02 d0
    ss add %al,%bl                      # gen=36 02 d8  dis=36 02 d8
    ss add %al,%ah                      # gen=36 02 e0  dis=36 02 e0
    ss add %al,%ch                      # gen=36 02 e8  dis=36 02 e8
    ss add %al,%dh                      # gen=36 02 f0  dis=36 02 f0
    ss add %al,%bh                      # gen=36 02 f8  dis=36 02 f8
    add    %ss:-0x6f70,%cl              # gen=36 02 0e  dis=36 02 0e 90 90
    add    %ss:-0x6f70,%dl              # gen=36 02 16  dis=36 02 16 90 90
    add    %ss:-0x6f70,%bl              # gen=36 02 1e  dis=36 02 1e 90 90
    add    %ss:-0x6f70,%ah              # gen=36 02 26  dis=36 02 26 90 90
    add    %ss:-0x6f70,%ch              # gen=36 02 2e  dis=36 02 2e 90 90
    add    %ss:-0x6f70,%dh              # gen=36 02 36  dis=36 02 36 90 90
    add    %ss:-0x6f70,%bh              # gen=36 02 3e  dis=36 02 3e 90 90
    ss add %ax,%ax                      # gen=36 03 c0  dis=36 03 c0
    add    %ss:-0x6f70,%ax              # gen=36 03 06  dis=36 03 06 90 90
    ss add %ax,%cx                      # gen=36 03 c8  dis=36 03 c8
    ss add %ax,%dx                      # gen=36 03 d0  dis=36 03 d0
    ss add %ax,%bx                      # gen=36 03 d8  dis=36 03 d8
    ss add %ax,%sp                      # gen=36 03 e0  dis=36 03 e0
    ss add %ax,%bp                      # gen=36 03 e8  dis=36 03 e8
    ss add %ax,%si                      # gen=36 03 f0  dis=36 03 f0
    ss add %ax,%di                      # gen=36 03 f8  dis=36 03 f8
    add    %ss:-0x6f70,%cx              # gen=36 03 0e  dis=36 03 0e 90 90
    add    %ss:-0x6f70,%dx              # gen=36 03 16  dis=36 03 16 90 90
    add    %ss:-0x6f70,%bx              # gen=36 03 1e  dis=36 03 1e 90 90
    add    %ss:-0x6f70,%sp              # gen=36 03 26  dis=36 03 26 90 90
    add    %ss:-0x6f70,%bp              # gen=36 03 2e  dis=36 03 2e 90 90
    add    %ss:-0x6f70,%si              # gen=36 03 36  dis=36 03 36 90 90
    add    %ss:-0x6f70,%di              # gen=36 03 3e  dis=36 03 3e 90 90
    ss add $0xc0,%al                    # gen=36 04 c0  dis=36 04 c0
    ss add $0x6,%al                     # gen=36 04 06  dis=36 04 06
    ss add $0x90c0,%ax                  # gen=36 05 c0  dis=36 05 c0 90
    ss add $0x9006,%ax                  # gen=36 05 06  dis=36 05 06 90
    ss push %es                         # gen=36 06 c0  dis=36 06
    ss push %es                         # gen=36 06 06  dis=36 06
    ss pop %es                          # gen=36 07 c0  dis=36 07
    ss pop %es                          # gen=36 07 06  dis=36 07
    ss or  %al,%al                      # gen=36 08 c0  dis=36 08 c0
    or     %al,%ss:-0x6f70              # gen=36 08 06  dis=36 08 06 90 90
    ss or  %cl,%al                      # gen=36 08 c8  dis=36 08 c8
    ss or  %dl,%al                      # gen=36 08 d0  dis=36 08 d0
    ss or  %bl,%al                      # gen=36 08 d8  dis=36 08 d8
    ss or  %ah,%al                      # gen=36 08 e0  dis=36 08 e0
    ss or  %ch,%al                      # gen=36 08 e8  dis=36 08 e8
    ss or  %dh,%al                      # gen=36 08 f0  dis=36 08 f0
    ss or  %bh,%al                      # gen=36 08 f8  dis=36 08 f8
    or     %cl,%ss:-0x6f70              # gen=36 08 0e  dis=36 08 0e 90 90
    or     %dl,%ss:-0x6f70              # gen=36 08 16  dis=36 08 16 90 90
    or     %bl,%ss:-0x6f70              # gen=36 08 1e  dis=36 08 1e 90 90
    or     %ah,%ss:-0x6f70              # gen=36 08 26  dis=36 08 26 90 90
    or     %ch,%ss:-0x6f70              # gen=36 08 2e  dis=36 08 2e 90 90
    or     %dh,%ss:-0x6f70              # gen=36 08 36  dis=36 08 36 90 90
    or     %bh,%ss:-0x6f70              # gen=36 08 3e  dis=36 08 3e 90 90
    ss or  %ax,%ax                      # gen=36 09 c0  dis=36 09 c0
    or     %ax,%ss:-0x6f70              # gen=36 09 06  dis=36 09 06 90 90
    ss or  %cx,%ax                      # gen=36 09 c8  dis=36 09 c8
    ss or  %dx,%ax                      # gen=36 09 d0  dis=36 09 d0
    ss or  %bx,%ax                      # gen=36 09 d8  dis=36 09 d8
    ss or  %sp,%ax                      # gen=36 09 e0  dis=36 09 e0
    ss or  %bp,%ax                      # gen=36 09 e8  dis=36 09 e8
    ss or  %si,%ax                      # gen=36 09 f0  dis=36 09 f0
    ss or  %di,%ax                      # gen=36 09 f8  dis=36 09 f8
    or     %cx,%ss:-0x6f70              # gen=36 09 0e  dis=36 09 0e 90 90
    or     %dx,%ss:-0x6f70              # gen=36 09 16  dis=36 09 16 90 90
    or     %bx,%ss:-0x6f70              # gen=36 09 1e  dis=36 09 1e 90 90
    or     %sp,%ss:-0x6f70              # gen=36 09 26  dis=36 09 26 90 90
    or     %bp,%ss:-0x6f70              # gen=36 09 2e  dis=36 09 2e 90 90
    or     %si,%ss:-0x6f70              # gen=36 09 36  dis=36 09 36 90 90
    or     %di,%ss:-0x6f70              # gen=36 09 3e  dis=36 09 3e 90 90
    ss or  %al,%al                      # gen=36 0a c0  dis=36 0a c0
    or     %ss:-0x6f70,%al              # gen=36 0a 06  dis=36 0a 06 90 90
    ss or  %al,%cl                      # gen=36 0a c8  dis=36 0a c8
    ss or  %al,%dl                      # gen=36 0a d0  dis=36 0a d0
    ss or  %al,%bl                      # gen=36 0a d8  dis=36 0a d8
    ss or  %al,%ah                      # gen=36 0a e0  dis=36 0a e0
    ss or  %al,%ch                      # gen=36 0a e8  dis=36 0a e8
    ss or  %al,%dh                      # gen=36 0a f0  dis=36 0a f0
    ss or  %al,%bh                      # gen=36 0a f8  dis=36 0a f8
    or     %ss:-0x6f70,%cl              # gen=36 0a 0e  dis=36 0a 0e 90 90
    or     %ss:-0x6f70,%dl              # gen=36 0a 16  dis=36 0a 16 90 90
    or     %ss:-0x6f70,%bl              # gen=36 0a 1e  dis=36 0a 1e 90 90
    or     %ss:-0x6f70,%ah              # gen=36 0a 26  dis=36 0a 26 90 90
    or     %ss:-0x6f70,%ch              # gen=36 0a 2e  dis=36 0a 2e 90 90
    or     %ss:-0x6f70,%dh              # gen=36 0a 36  dis=36 0a 36 90 90
    or     %ss:-0x6f70,%bh              # gen=36 0a 3e  dis=36 0a 3e 90 90
    ss or  %ax,%ax                      # gen=36 0b c0  dis=36 0b c0
    or     %ss:-0x6f70,%ax              # gen=36 0b 06  dis=36 0b 06 90 90
    ss or  %ax,%cx                      # gen=36 0b c8  dis=36 0b c8
    ss or  %ax,%dx                      # gen=36 0b d0  dis=36 0b d0
    ss or  %ax,%bx                      # gen=36 0b d8  dis=36 0b d8
    ss or  %ax,%sp                      # gen=36 0b e0  dis=36 0b e0
    ss or  %ax,%bp                      # gen=36 0b e8  dis=36 0b e8
    ss or  %ax,%si                      # gen=36 0b f0  dis=36 0b f0
    ss or  %ax,%di                      # gen=36 0b f8  dis=36 0b f8
    or     %ss:-0x6f70,%cx              # gen=36 0b 0e  dis=36 0b 0e 90 90
    or     %ss:-0x6f70,%dx              # gen=36 0b 16  dis=36 0b 16 90 90
    or     %ss:-0x6f70,%bx              # gen=36 0b 1e  dis=36 0b 1e 90 90
    or     %ss:-0x6f70,%sp              # gen=36 0b 26  dis=36 0b 26 90 90
    or     %ss:-0x6f70,%bp              # gen=36 0b 2e  dis=36 0b 2e 90 90
    or     %ss:-0x6f70,%si              # gen=36 0b 36  dis=36 0b 36 90 90
    or     %ss:-0x6f70,%di              # gen=36 0b 3e  dis=36 0b 3e 90 90
    ss or  $0xc0,%al                    # gen=36 0c c0  dis=36 0c c0
    ss or  $0x6,%al                     # gen=36 0c 06  dis=36 0c 06
    ss or  $0x90c0,%ax                  # gen=36 0d c0  dis=36 0d c0 90
    ss or  $0x9006,%ax                  # gen=36 0d 06  dis=36 0d 06 90
    ss push %cs                         # gen=36 0e c0  dis=36 0e
    ss push %cs                         # gen=36 0e 06  dis=36 0e
    .byte 0x36,0x0f,0xc0            # fallback; gen=36 0f c0
    .byte 0x36,0x0f,0x06            # fallback; gen=36 0f 06
    .byte 0x36,0x0f,0xc8            # fallback; gen=36 0f c8
    .byte 0x36,0x0f,0xd0            # fallback; gen=36 0f d0
    .byte 0x36,0x0f,0xd8            # fallback; gen=36 0f d8
    .byte 0x36,0x0f,0xe0            # fallback; gen=36 0f e0
    .byte 0x36,0x0f,0xe8            # fallback; gen=36 0f e8
    .byte 0x36,0x0f,0xf0            # fallback; gen=36 0f f0
    .byte 0x36,0x0f,0xf8            # fallback; gen=36 0f f8
    .byte 0x36,0x0f,0x0e            # fallback; gen=36 0f 0e
    .byte 0x36,0x0f,0x16            # fallback; gen=36 0f 16
    .byte 0x36,0x0f,0x1e            # fallback; gen=36 0f 1e
    .byte 0x36,0x0f,0x26            # fallback; gen=36 0f 26
    .byte 0x36,0x0f,0x2e            # fallback; gen=36 0f 2e
    .byte 0x36,0x0f,0x36            # fallback; gen=36 0f 36
    .byte 0x36,0x0f,0x3e            # fallback; gen=36 0f 3e
    ss adc %al,%al                      # gen=36 10 c0  dis=36 10 c0
    adc    %al,%ss:-0x6f70              # gen=36 10 06  dis=36 10 06 90 90
    ss adc %cl,%al                      # gen=36 10 c8  dis=36 10 c8
    ss adc %dl,%al                      # gen=36 10 d0  dis=36 10 d0
    ss adc %bl,%al                      # gen=36 10 d8  dis=36 10 d8
    ss adc %ah,%al                      # gen=36 10 e0  dis=36 10 e0
    ss adc %ch,%al                      # gen=36 10 e8  dis=36 10 e8
    ss adc %dh,%al                      # gen=36 10 f0  dis=36 10 f0
    ss adc %bh,%al                      # gen=36 10 f8  dis=36 10 f8
    adc    %cl,%ss:-0x6f70              # gen=36 10 0e  dis=36 10 0e 90 90
    adc    %dl,%ss:-0x6f70              # gen=36 10 16  dis=36 10 16 90 90
    adc    %bl,%ss:-0x6f70              # gen=36 10 1e  dis=36 10 1e 90 90
    adc    %ah,%ss:-0x6f70              # gen=36 10 26  dis=36 10 26 90 90
    adc    %ch,%ss:-0x6f70              # gen=36 10 2e  dis=36 10 2e 90 90
    adc    %dh,%ss:-0x6f70              # gen=36 10 36  dis=36 10 36 90 90
    adc    %bh,%ss:-0x6f70              # gen=36 10 3e  dis=36 10 3e 90 90
    ss adc %ax,%ax                      # gen=36 11 c0  dis=36 11 c0
    adc    %ax,%ss:-0x6f70              # gen=36 11 06  dis=36 11 06 90 90
    ss adc %cx,%ax                      # gen=36 11 c8  dis=36 11 c8
    ss adc %dx,%ax                      # gen=36 11 d0  dis=36 11 d0
    ss adc %bx,%ax                      # gen=36 11 d8  dis=36 11 d8
    ss adc %sp,%ax                      # gen=36 11 e0  dis=36 11 e0
    ss adc %bp,%ax                      # gen=36 11 e8  dis=36 11 e8
    ss adc %si,%ax                      # gen=36 11 f0  dis=36 11 f0
    ss adc %di,%ax                      # gen=36 11 f8  dis=36 11 f8
    adc    %cx,%ss:-0x6f70              # gen=36 11 0e  dis=36 11 0e 90 90
    adc    %dx,%ss:-0x6f70              # gen=36 11 16  dis=36 11 16 90 90
    adc    %bx,%ss:-0x6f70              # gen=36 11 1e  dis=36 11 1e 90 90
    adc    %sp,%ss:-0x6f70              # gen=36 11 26  dis=36 11 26 90 90
    adc    %bp,%ss:-0x6f70              # gen=36 11 2e  dis=36 11 2e 90 90
    adc    %si,%ss:-0x6f70              # gen=36 11 36  dis=36 11 36 90 90
    adc    %di,%ss:-0x6f70              # gen=36 11 3e  dis=36 11 3e 90 90
    ss adc %al,%al                      # gen=36 12 c0  dis=36 12 c0
    adc    %ss:-0x6f70,%al              # gen=36 12 06  dis=36 12 06 90 90
    ss adc %al,%cl                      # gen=36 12 c8  dis=36 12 c8
    ss adc %al,%dl                      # gen=36 12 d0  dis=36 12 d0
    ss adc %al,%bl                      # gen=36 12 d8  dis=36 12 d8
    ss adc %al,%ah                      # gen=36 12 e0  dis=36 12 e0
    ss adc %al,%ch                      # gen=36 12 e8  dis=36 12 e8
    ss adc %al,%dh                      # gen=36 12 f0  dis=36 12 f0
    ss adc %al,%bh                      # gen=36 12 f8  dis=36 12 f8
    adc    %ss:-0x6f70,%cl              # gen=36 12 0e  dis=36 12 0e 90 90
    adc    %ss:-0x6f70,%dl              # gen=36 12 16  dis=36 12 16 90 90
    adc    %ss:-0x6f70,%bl              # gen=36 12 1e  dis=36 12 1e 90 90
    adc    %ss:-0x6f70,%ah              # gen=36 12 26  dis=36 12 26 90 90
    adc    %ss:-0x6f70,%ch              # gen=36 12 2e  dis=36 12 2e 90 90
    adc    %ss:-0x6f70,%dh              # gen=36 12 36  dis=36 12 36 90 90
    adc    %ss:-0x6f70,%bh              # gen=36 12 3e  dis=36 12 3e 90 90
    ss adc %ax,%ax                      # gen=36 13 c0  dis=36 13 c0
    adc    %ss:-0x6f70,%ax              # gen=36 13 06  dis=36 13 06 90 90
    ss adc %ax,%cx                      # gen=36 13 c8  dis=36 13 c8
    ss adc %ax,%dx                      # gen=36 13 d0  dis=36 13 d0
    ss adc %ax,%bx                      # gen=36 13 d8  dis=36 13 d8
    ss adc %ax,%sp                      # gen=36 13 e0  dis=36 13 e0
    ss adc %ax,%bp                      # gen=36 13 e8  dis=36 13 e8
    ss adc %ax,%si                      # gen=36 13 f0  dis=36 13 f0
    ss adc %ax,%di                      # gen=36 13 f8  dis=36 13 f8
    adc    %ss:-0x6f70,%cx              # gen=36 13 0e  dis=36 13 0e 90 90
    adc    %ss:-0x6f70,%dx              # gen=36 13 16  dis=36 13 16 90 90
    adc    %ss:-0x6f70,%bx              # gen=36 13 1e  dis=36 13 1e 90 90
    adc    %ss:-0x6f70,%sp              # gen=36 13 26  dis=36 13 26 90 90
    adc    %ss:-0x6f70,%bp              # gen=36 13 2e  dis=36 13 2e 90 90
    adc    %ss:-0x6f70,%si              # gen=36 13 36  dis=36 13 36 90 90
    adc    %ss:-0x6f70,%di              # gen=36 13 3e  dis=36 13 3e 90 90
    ss adc $0xc0,%al                    # gen=36 14 c0  dis=36 14 c0
    ss adc $0x6,%al                     # gen=36 14 06  dis=36 14 06
    ss adc $0x90c0,%ax                  # gen=36 15 c0  dis=36 15 c0 90
    ss adc $0x9006,%ax                  # gen=36 15 06  dis=36 15 06 90
    ss push %ss                         # gen=36 16 c0  dis=36 16
    ss push %ss                         # gen=36 16 06  dis=36 16
    ss pop %ss                          # gen=36 17 c0  dis=36 17
    ss pop %ss                          # gen=36 17 06  dis=36 17
    ss sbb %al,%al                      # gen=36 18 c0  dis=36 18 c0
    sbb    %al,%ss:-0x6f70              # gen=36 18 06  dis=36 18 06 90 90
    ss sbb %cl,%al                      # gen=36 18 c8  dis=36 18 c8
    ss sbb %dl,%al                      # gen=36 18 d0  dis=36 18 d0
    ss sbb %bl,%al                      # gen=36 18 d8  dis=36 18 d8
    ss sbb %ah,%al                      # gen=36 18 e0  dis=36 18 e0
    ss sbb %ch,%al                      # gen=36 18 e8  dis=36 18 e8
    ss sbb %dh,%al                      # gen=36 18 f0  dis=36 18 f0
    ss sbb %bh,%al                      # gen=36 18 f8  dis=36 18 f8
    sbb    %cl,%ss:-0x6f70              # gen=36 18 0e  dis=36 18 0e 90 90
    sbb    %dl,%ss:-0x6f70              # gen=36 18 16  dis=36 18 16 90 90
    sbb    %bl,%ss:-0x6f70              # gen=36 18 1e  dis=36 18 1e 90 90
    sbb    %ah,%ss:-0x6f70              # gen=36 18 26  dis=36 18 26 90 90
    sbb    %ch,%ss:-0x6f70              # gen=36 18 2e  dis=36 18 2e 90 90
    sbb    %dh,%ss:-0x6f70              # gen=36 18 36  dis=36 18 36 90 90
    sbb    %bh,%ss:-0x6f70              # gen=36 18 3e  dis=36 18 3e 90 90
    ss sbb %ax,%ax                      # gen=36 19 c0  dis=36 19 c0
    sbb    %ax,%ss:-0x6f70              # gen=36 19 06  dis=36 19 06 90 90
    ss sbb %cx,%ax                      # gen=36 19 c8  dis=36 19 c8
    ss sbb %dx,%ax                      # gen=36 19 d0  dis=36 19 d0
    ss sbb %bx,%ax                      # gen=36 19 d8  dis=36 19 d8
    ss sbb %sp,%ax                      # gen=36 19 e0  dis=36 19 e0
    ss sbb %bp,%ax                      # gen=36 19 e8  dis=36 19 e8
    ss sbb %si,%ax                      # gen=36 19 f0  dis=36 19 f0
    ss sbb %di,%ax                      # gen=36 19 f8  dis=36 19 f8
    sbb    %cx,%ss:-0x6f70              # gen=36 19 0e  dis=36 19 0e 90 90
    sbb    %dx,%ss:-0x6f70              # gen=36 19 16  dis=36 19 16 90 90
    sbb    %bx,%ss:-0x6f70              # gen=36 19 1e  dis=36 19 1e 90 90
    sbb    %sp,%ss:-0x6f70              # gen=36 19 26  dis=36 19 26 90 90
    sbb    %bp,%ss:-0x6f70              # gen=36 19 2e  dis=36 19 2e 90 90
    sbb    %si,%ss:-0x6f70              # gen=36 19 36  dis=36 19 36 90 90
    sbb    %di,%ss:-0x6f70              # gen=36 19 3e  dis=36 19 3e 90 90
    ss sbb %al,%al                      # gen=36 1a c0  dis=36 1a c0
    sbb    %ss:-0x6f70,%al              # gen=36 1a 06  dis=36 1a 06 90 90
    ss sbb %al,%cl                      # gen=36 1a c8  dis=36 1a c8
    ss sbb %al,%dl                      # gen=36 1a d0  dis=36 1a d0
    ss sbb %al,%bl                      # gen=36 1a d8  dis=36 1a d8
    ss sbb %al,%ah                      # gen=36 1a e0  dis=36 1a e0
    ss sbb %al,%ch                      # gen=36 1a e8  dis=36 1a e8
    ss sbb %al,%dh                      # gen=36 1a f0  dis=36 1a f0
    ss sbb %al,%bh                      # gen=36 1a f8  dis=36 1a f8
    sbb    %ss:-0x6f70,%cl              # gen=36 1a 0e  dis=36 1a 0e 90 90
    sbb    %ss:-0x6f70,%dl              # gen=36 1a 16  dis=36 1a 16 90 90
    sbb    %ss:-0x6f70,%bl              # gen=36 1a 1e  dis=36 1a 1e 90 90
    sbb    %ss:-0x6f70,%ah              # gen=36 1a 26  dis=36 1a 26 90 90
    sbb    %ss:-0x6f70,%ch              # gen=36 1a 2e  dis=36 1a 2e 90 90
    sbb    %ss:-0x6f70,%dh              # gen=36 1a 36  dis=36 1a 36 90 90
    sbb    %ss:-0x6f70,%bh              # gen=36 1a 3e  dis=36 1a 3e 90 90
    ss sbb %ax,%ax                      # gen=36 1b c0  dis=36 1b c0
    sbb    %ss:-0x6f70,%ax              # gen=36 1b 06  dis=36 1b 06 90 90
    ss sbb %ax,%cx                      # gen=36 1b c8  dis=36 1b c8
    ss sbb %ax,%dx                      # gen=36 1b d0  dis=36 1b d0
    ss sbb %ax,%bx                      # gen=36 1b d8  dis=36 1b d8
    ss sbb %ax,%sp                      # gen=36 1b e0  dis=36 1b e0
    ss sbb %ax,%bp                      # gen=36 1b e8  dis=36 1b e8
    ss sbb %ax,%si                      # gen=36 1b f0  dis=36 1b f0
    ss sbb %ax,%di                      # gen=36 1b f8  dis=36 1b f8
    sbb    %ss:-0x6f70,%cx              # gen=36 1b 0e  dis=36 1b 0e 90 90
    sbb    %ss:-0x6f70,%dx              # gen=36 1b 16  dis=36 1b 16 90 90
    sbb    %ss:-0x6f70,%bx              # gen=36 1b 1e  dis=36 1b 1e 90 90
    sbb    %ss:-0x6f70,%sp              # gen=36 1b 26  dis=36 1b 26 90 90
    sbb    %ss:-0x6f70,%bp              # gen=36 1b 2e  dis=36 1b 2e 90 90
    sbb    %ss:-0x6f70,%si              # gen=36 1b 36  dis=36 1b 36 90 90
    sbb    %ss:-0x6f70,%di              # gen=36 1b 3e  dis=36 1b 3e 90 90
    ss sbb $0xc0,%al                    # gen=36 1c c0  dis=36 1c c0
    ss sbb $0x6,%al                     # gen=36 1c 06  dis=36 1c 06
    ss sbb $0x90c0,%ax                  # gen=36 1d c0  dis=36 1d c0 90
    ss sbb $0x9006,%ax                  # gen=36 1d 06  dis=36 1d 06 90
    ss push %ds                         # gen=36 1e c0  dis=36 1e
    ss push %ds                         # gen=36 1e 06  dis=36 1e
    ss pop %ds                          # gen=36 1f c0  dis=36 1f
    ss pop %ds                          # gen=36 1f 06  dis=36 1f
    ss and %al,%al                      # gen=36 20 c0  dis=36 20 c0
    and    %al,%ss:-0x6f70              # gen=36 20 06  dis=36 20 06 90 90
    ss and %cl,%al                      # gen=36 20 c8  dis=36 20 c8
    ss and %dl,%al                      # gen=36 20 d0  dis=36 20 d0
    ss and %bl,%al                      # gen=36 20 d8  dis=36 20 d8
    ss and %ah,%al                      # gen=36 20 e0  dis=36 20 e0
    ss and %ch,%al                      # gen=36 20 e8  dis=36 20 e8
    ss and %dh,%al                      # gen=36 20 f0  dis=36 20 f0
    ss and %bh,%al                      # gen=36 20 f8  dis=36 20 f8
    and    %cl,%ss:-0x6f70              # gen=36 20 0e  dis=36 20 0e 90 90
    and    %dl,%ss:-0x6f70              # gen=36 20 16  dis=36 20 16 90 90
    and    %bl,%ss:-0x6f70              # gen=36 20 1e  dis=36 20 1e 90 90
    and    %ah,%ss:-0x6f70              # gen=36 20 26  dis=36 20 26 90 90
    and    %ch,%ss:-0x6f70              # gen=36 20 2e  dis=36 20 2e 90 90
    and    %dh,%ss:-0x6f70              # gen=36 20 36  dis=36 20 36 90 90
    and    %bh,%ss:-0x6f70              # gen=36 20 3e  dis=36 20 3e 90 90
    ss and %ax,%ax                      # gen=36 21 c0  dis=36 21 c0
    and    %ax,%ss:-0x6f70              # gen=36 21 06  dis=36 21 06 90 90
    ss and %cx,%ax                      # gen=36 21 c8  dis=36 21 c8
    ss and %dx,%ax                      # gen=36 21 d0  dis=36 21 d0
    ss and %bx,%ax                      # gen=36 21 d8  dis=36 21 d8
    ss and %sp,%ax                      # gen=36 21 e0  dis=36 21 e0
    ss and %bp,%ax                      # gen=36 21 e8  dis=36 21 e8
    ss and %si,%ax                      # gen=36 21 f0  dis=36 21 f0
    ss and %di,%ax                      # gen=36 21 f8  dis=36 21 f8
    and    %cx,%ss:-0x6f70              # gen=36 21 0e  dis=36 21 0e 90 90
    and    %dx,%ss:-0x6f70              # gen=36 21 16  dis=36 21 16 90 90
    and    %bx,%ss:-0x6f70              # gen=36 21 1e  dis=36 21 1e 90 90
    and    %sp,%ss:-0x6f70              # gen=36 21 26  dis=36 21 26 90 90
    and    %bp,%ss:-0x6f70              # gen=36 21 2e  dis=36 21 2e 90 90
    and    %si,%ss:-0x6f70              # gen=36 21 36  dis=36 21 36 90 90
    and    %di,%ss:-0x6f70              # gen=36 21 3e  dis=36 21 3e 90 90
    ss and %al,%al                      # gen=36 22 c0  dis=36 22 c0
    and    %ss:-0x6f70,%al              # gen=36 22 06  dis=36 22 06 90 90
    ss and %al,%cl                      # gen=36 22 c8  dis=36 22 c8
    ss and %al,%dl                      # gen=36 22 d0  dis=36 22 d0
    ss and %al,%bl                      # gen=36 22 d8  dis=36 22 d8
    ss and %al,%ah                      # gen=36 22 e0  dis=36 22 e0
    ss and %al,%ch                      # gen=36 22 e8  dis=36 22 e8
    ss and %al,%dh                      # gen=36 22 f0  dis=36 22 f0
    ss and %al,%bh                      # gen=36 22 f8  dis=36 22 f8
    and    %ss:-0x6f70,%cl              # gen=36 22 0e  dis=36 22 0e 90 90
    and    %ss:-0x6f70,%dl              # gen=36 22 16  dis=36 22 16 90 90
    and    %ss:-0x6f70,%bl              # gen=36 22 1e  dis=36 22 1e 90 90
    and    %ss:-0x6f70,%ah              # gen=36 22 26  dis=36 22 26 90 90
    and    %ss:-0x6f70,%ch              # gen=36 22 2e  dis=36 22 2e 90 90
    and    %ss:-0x6f70,%dh              # gen=36 22 36  dis=36 22 36 90 90
    and    %ss:-0x6f70,%bh              # gen=36 22 3e  dis=36 22 3e 90 90
    ss and %ax,%ax                      # gen=36 23 c0  dis=36 23 c0
    and    %ss:-0x6f70,%ax              # gen=36 23 06  dis=36 23 06 90 90
    ss and %ax,%cx                      # gen=36 23 c8  dis=36 23 c8
    ss and %ax,%dx                      # gen=36 23 d0  dis=36 23 d0
    ss and %ax,%bx                      # gen=36 23 d8  dis=36 23 d8
    ss and %ax,%sp                      # gen=36 23 e0  dis=36 23 e0
    ss and %ax,%bp                      # gen=36 23 e8  dis=36 23 e8
    ss and %ax,%si                      # gen=36 23 f0  dis=36 23 f0
    ss and %ax,%di                      # gen=36 23 f8  dis=36 23 f8
    and    %ss:-0x6f70,%cx              # gen=36 23 0e  dis=36 23 0e 90 90
    and    %ss:-0x6f70,%dx              # gen=36 23 16  dis=36 23 16 90 90
    and    %ss:-0x6f70,%bx              # gen=36 23 1e  dis=36 23 1e 90 90
    and    %ss:-0x6f70,%sp              # gen=36 23 26  dis=36 23 26 90 90
    and    %ss:-0x6f70,%bp              # gen=36 23 2e  dis=36 23 2e 90 90
    and    %ss:-0x6f70,%si              # gen=36 23 36  dis=36 23 36 90 90
    and    %ss:-0x6f70,%di              # gen=36 23 3e  dis=36 23 3e 90 90
    ss and $0xc0,%al                    # gen=36 24 c0  dis=36 24 c0
    ss and $0x6,%al                     # gen=36 24 06  dis=36 24 06
    ss and $0x90c0,%ax                  # gen=36 25 c0  dis=36 25 c0 90
    ss and $0x9006,%ax                  # gen=36 25 06  dis=36 25 06 90
    .byte 0x36,0x26,0xc0            # fallback; gen=36 26 c0
    .byte 0x36,0x26,0x06            # fallback; gen=36 26 06
    ss daa                              # gen=36 27 c0  dis=36 27
    ss daa                              # gen=36 27 06  dis=36 27
    ss sub %al,%al                      # gen=36 28 c0  dis=36 28 c0
    sub    %al,%ss:-0x6f70              # gen=36 28 06  dis=36 28 06 90 90
    ss sub %cl,%al                      # gen=36 28 c8  dis=36 28 c8
    ss sub %dl,%al                      # gen=36 28 d0  dis=36 28 d0
    ss sub %bl,%al                      # gen=36 28 d8  dis=36 28 d8
    ss sub %ah,%al                      # gen=36 28 e0  dis=36 28 e0
    ss sub %ch,%al                      # gen=36 28 e8  dis=36 28 e8
    ss sub %dh,%al                      # gen=36 28 f0  dis=36 28 f0
    ss sub %bh,%al                      # gen=36 28 f8  dis=36 28 f8
    sub    %cl,%ss:-0x6f70              # gen=36 28 0e  dis=36 28 0e 90 90
    sub    %dl,%ss:-0x6f70              # gen=36 28 16  dis=36 28 16 90 90
    sub    %bl,%ss:-0x6f70              # gen=36 28 1e  dis=36 28 1e 90 90
    sub    %ah,%ss:-0x6f70              # gen=36 28 26  dis=36 28 26 90 90
    sub    %ch,%ss:-0x6f70              # gen=36 28 2e  dis=36 28 2e 90 90
    sub    %dh,%ss:-0x6f70              # gen=36 28 36  dis=36 28 36 90 90
    sub    %bh,%ss:-0x6f70              # gen=36 28 3e  dis=36 28 3e 90 90
    ss sub %ax,%ax                      # gen=36 29 c0  dis=36 29 c0
    sub    %ax,%ss:-0x6f70              # gen=36 29 06  dis=36 29 06 90 90
    ss sub %cx,%ax                      # gen=36 29 c8  dis=36 29 c8
    ss sub %dx,%ax                      # gen=36 29 d0  dis=36 29 d0
    ss sub %bx,%ax                      # gen=36 29 d8  dis=36 29 d8
    ss sub %sp,%ax                      # gen=36 29 e0  dis=36 29 e0
    ss sub %bp,%ax                      # gen=36 29 e8  dis=36 29 e8
    ss sub %si,%ax                      # gen=36 29 f0  dis=36 29 f0
    ss sub %di,%ax                      # gen=36 29 f8  dis=36 29 f8
    sub    %cx,%ss:-0x6f70              # gen=36 29 0e  dis=36 29 0e 90 90
    sub    %dx,%ss:-0x6f70              # gen=36 29 16  dis=36 29 16 90 90
    sub    %bx,%ss:-0x6f70              # gen=36 29 1e  dis=36 29 1e 90 90
    sub    %sp,%ss:-0x6f70              # gen=36 29 26  dis=36 29 26 90 90
    sub    %bp,%ss:-0x6f70              # gen=36 29 2e  dis=36 29 2e 90 90
    sub    %si,%ss:-0x6f70              # gen=36 29 36  dis=36 29 36 90 90
    sub    %di,%ss:-0x6f70              # gen=36 29 3e  dis=36 29 3e 90 90
    ss sub %al,%al                      # gen=36 2a c0  dis=36 2a c0
    sub    %ss:-0x6f70,%al              # gen=36 2a 06  dis=36 2a 06 90 90
    ss sub %al,%cl                      # gen=36 2a c8  dis=36 2a c8
    ss sub %al,%dl                      # gen=36 2a d0  dis=36 2a d0
    ss sub %al,%bl                      # gen=36 2a d8  dis=36 2a d8
    ss sub %al,%ah                      # gen=36 2a e0  dis=36 2a e0
    ss sub %al,%ch                      # gen=36 2a e8  dis=36 2a e8
    ss sub %al,%dh                      # gen=36 2a f0  dis=36 2a f0
    ss sub %al,%bh                      # gen=36 2a f8  dis=36 2a f8
    sub    %ss:-0x6f70,%cl              # gen=36 2a 0e  dis=36 2a 0e 90 90
    sub    %ss:-0x6f70,%dl              # gen=36 2a 16  dis=36 2a 16 90 90
    sub    %ss:-0x6f70,%bl              # gen=36 2a 1e  dis=36 2a 1e 90 90
    sub    %ss:-0x6f70,%ah              # gen=36 2a 26  dis=36 2a 26 90 90
    sub    %ss:-0x6f70,%ch              # gen=36 2a 2e  dis=36 2a 2e 90 90
    sub    %ss:-0x6f70,%dh              # gen=36 2a 36  dis=36 2a 36 90 90
    sub    %ss:-0x6f70,%bh              # gen=36 2a 3e  dis=36 2a 3e 90 90
    ss sub %ax,%ax                      # gen=36 2b c0  dis=36 2b c0
    sub    %ss:-0x6f70,%ax              # gen=36 2b 06  dis=36 2b 06 90 90
    ss sub %ax,%cx                      # gen=36 2b c8  dis=36 2b c8
    ss sub %ax,%dx                      # gen=36 2b d0  dis=36 2b d0
    ss sub %ax,%bx                      # gen=36 2b d8  dis=36 2b d8
    ss sub %ax,%sp                      # gen=36 2b e0  dis=36 2b e0
    ss sub %ax,%bp                      # gen=36 2b e8  dis=36 2b e8
    ss sub %ax,%si                      # gen=36 2b f0  dis=36 2b f0
    ss sub %ax,%di                      # gen=36 2b f8  dis=36 2b f8
    sub    %ss:-0x6f70,%cx              # gen=36 2b 0e  dis=36 2b 0e 90 90
    sub    %ss:-0x6f70,%dx              # gen=36 2b 16  dis=36 2b 16 90 90
    sub    %ss:-0x6f70,%bx              # gen=36 2b 1e  dis=36 2b 1e 90 90
    sub    %ss:-0x6f70,%sp              # gen=36 2b 26  dis=36 2b 26 90 90
    sub    %ss:-0x6f70,%bp              # gen=36 2b 2e  dis=36 2b 2e 90 90
    sub    %ss:-0x6f70,%si              # gen=36 2b 36  dis=36 2b 36 90 90
    sub    %ss:-0x6f70,%di              # gen=36 2b 3e  dis=36 2b 3e 90 90
    ss sub $0xc0,%al                    # gen=36 2c c0  dis=36 2c c0
    ss sub $0x6,%al                     # gen=36 2c 06  dis=36 2c 06
    ss sub $0x90c0,%ax                  # gen=36 2d c0  dis=36 2d c0 90
    ss sub $0x9006,%ax                  # gen=36 2d 06  dis=36 2d 06 90
    .byte 0x36,0x2e,0xc0            # fallback; gen=36 2e c0
    .byte 0x36,0x2e,0x06            # fallback; gen=36 2e 06
    ss das                              # gen=36 2f c0  dis=36 2f
    ss das                              # gen=36 2f 06  dis=36 2f
    ss xor %al,%al                      # gen=36 30 c0  dis=36 30 c0
    xor    %al,%ss:-0x6f70              # gen=36 30 06  dis=36 30 06 90 90
    ss xor %cl,%al                      # gen=36 30 c8  dis=36 30 c8
    ss xor %dl,%al                      # gen=36 30 d0  dis=36 30 d0
    ss xor %bl,%al                      # gen=36 30 d8  dis=36 30 d8
    ss xor %ah,%al                      # gen=36 30 e0  dis=36 30 e0
    ss xor %ch,%al                      # gen=36 30 e8  dis=36 30 e8
    ss xor %dh,%al                      # gen=36 30 f0  dis=36 30 f0
    ss xor %bh,%al                      # gen=36 30 f8  dis=36 30 f8
    xor    %cl,%ss:-0x6f70              # gen=36 30 0e  dis=36 30 0e 90 90
    xor    %dl,%ss:-0x6f70              # gen=36 30 16  dis=36 30 16 90 90
    xor    %bl,%ss:-0x6f70              # gen=36 30 1e  dis=36 30 1e 90 90
    xor    %ah,%ss:-0x6f70              # gen=36 30 26  dis=36 30 26 90 90
    xor    %ch,%ss:-0x6f70              # gen=36 30 2e  dis=36 30 2e 90 90
    xor    %dh,%ss:-0x6f70              # gen=36 30 36  dis=36 30 36 90 90
    xor    %bh,%ss:-0x6f70              # gen=36 30 3e  dis=36 30 3e 90 90
    ss xor %ax,%ax                      # gen=36 31 c0  dis=36 31 c0
    xor    %ax,%ss:-0x6f70              # gen=36 31 06  dis=36 31 06 90 90
    ss xor %cx,%ax                      # gen=36 31 c8  dis=36 31 c8
    ss xor %dx,%ax                      # gen=36 31 d0  dis=36 31 d0
    ss xor %bx,%ax                      # gen=36 31 d8  dis=36 31 d8
    ss xor %sp,%ax                      # gen=36 31 e0  dis=36 31 e0
    ss xor %bp,%ax                      # gen=36 31 e8  dis=36 31 e8
    ss xor %si,%ax                      # gen=36 31 f0  dis=36 31 f0
    ss xor %di,%ax                      # gen=36 31 f8  dis=36 31 f8
    xor    %cx,%ss:-0x6f70              # gen=36 31 0e  dis=36 31 0e 90 90
    xor    %dx,%ss:-0x6f70              # gen=36 31 16  dis=36 31 16 90 90
    xor    %bx,%ss:-0x6f70              # gen=36 31 1e  dis=36 31 1e 90 90
    xor    %sp,%ss:-0x6f70              # gen=36 31 26  dis=36 31 26 90 90
    xor    %bp,%ss:-0x6f70              # gen=36 31 2e  dis=36 31 2e 90 90
    xor    %si,%ss:-0x6f70              # gen=36 31 36  dis=36 31 36 90 90
    xor    %di,%ss:-0x6f70              # gen=36 31 3e  dis=36 31 3e 90 90
    ss xor %al,%al                      # gen=36 32 c0  dis=36 32 c0
    xor    %ss:-0x6f70,%al              # gen=36 32 06  dis=36 32 06 90 90
    ss xor %al,%cl                      # gen=36 32 c8  dis=36 32 c8
    ss xor %al,%dl                      # gen=36 32 d0  dis=36 32 d0
    ss xor %al,%bl                      # gen=36 32 d8  dis=36 32 d8
    ss xor %al,%ah                      # gen=36 32 e0  dis=36 32 e0
    ss xor %al,%ch                      # gen=36 32 e8  dis=36 32 e8
    ss xor %al,%dh                      # gen=36 32 f0  dis=36 32 f0
    ss xor %al,%bh                      # gen=36 32 f8  dis=36 32 f8
    xor    %ss:-0x6f70,%cl              # gen=36 32 0e  dis=36 32 0e 90 90
    xor    %ss:-0x6f70,%dl              # gen=36 32 16  dis=36 32 16 90 90
    xor    %ss:-0x6f70,%bl              # gen=36 32 1e  dis=36 32 1e 90 90
    xor    %ss:-0x6f70,%ah              # gen=36 32 26  dis=36 32 26 90 90
    xor    %ss:-0x6f70,%ch              # gen=36 32 2e  dis=36 32 2e 90 90
    xor    %ss:-0x6f70,%dh              # gen=36 32 36  dis=36 32 36 90 90
    xor    %ss:-0x6f70,%bh              # gen=36 32 3e  dis=36 32 3e 90 90
    ss xor %ax,%ax                      # gen=36 33 c0  dis=36 33 c0
    xor    %ss:-0x6f70,%ax              # gen=36 33 06  dis=36 33 06 90 90
    ss xor %ax,%cx                      # gen=36 33 c8  dis=36 33 c8
    ss xor %ax,%dx                      # gen=36 33 d0  dis=36 33 d0
    ss xor %ax,%bx                      # gen=36 33 d8  dis=36 33 d8
    ss xor %ax,%sp                      # gen=36 33 e0  dis=36 33 e0
    ss xor %ax,%bp                      # gen=36 33 e8  dis=36 33 e8
    ss xor %ax,%si                      # gen=36 33 f0  dis=36 33 f0
    ss xor %ax,%di                      # gen=36 33 f8  dis=36 33 f8
    xor    %ss:-0x6f70,%cx              # gen=36 33 0e  dis=36 33 0e 90 90
    xor    %ss:-0x6f70,%dx              # gen=36 33 16  dis=36 33 16 90 90
    xor    %ss:-0x6f70,%bx              # gen=36 33 1e  dis=36 33 1e 90 90
    xor    %ss:-0x6f70,%sp              # gen=36 33 26  dis=36 33 26 90 90
    xor    %ss:-0x6f70,%bp              # gen=36 33 2e  dis=36 33 2e 90 90
    xor    %ss:-0x6f70,%si              # gen=36 33 36  dis=36 33 36 90 90
    xor    %ss:-0x6f70,%di              # gen=36 33 3e  dis=36 33 3e 90 90
    ss xor $0xc0,%al                    # gen=36 34 c0  dis=36 34 c0
    ss xor $0x6,%al                     # gen=36 34 06  dis=36 34 06
    ss xor $0x90c0,%ax                  # gen=36 35 c0  dis=36 35 c0 90
    ss xor $0x9006,%ax                  # gen=36 35 06  dis=36 35 06 90
    .byte 0x36,0x36,0xc0            # fallback; gen=36 36 c0
    .byte 0x36,0x36,0x06            # fallback; gen=36 36 06
    ss aaa                              # gen=36 37 c0  dis=36 37
    ss aaa                              # gen=36 37 06  dis=36 37
    ss cmp %al,%al                      # gen=36 38 c0  dis=36 38 c0
    cmp    %al,%ss:-0x6f70              # gen=36 38 06  dis=36 38 06 90 90
    ss cmp %cl,%al                      # gen=36 38 c8  dis=36 38 c8
    ss cmp %dl,%al                      # gen=36 38 d0  dis=36 38 d0
    ss cmp %bl,%al                      # gen=36 38 d8  dis=36 38 d8
    ss cmp %ah,%al                      # gen=36 38 e0  dis=36 38 e0
    ss cmp %ch,%al                      # gen=36 38 e8  dis=36 38 e8
    ss cmp %dh,%al                      # gen=36 38 f0  dis=36 38 f0
    ss cmp %bh,%al                      # gen=36 38 f8  dis=36 38 f8
    cmp    %cl,%ss:-0x6f70              # gen=36 38 0e  dis=36 38 0e 90 90
    cmp    %dl,%ss:-0x6f70              # gen=36 38 16  dis=36 38 16 90 90
    cmp    %bl,%ss:-0x6f70              # gen=36 38 1e  dis=36 38 1e 90 90
    cmp    %ah,%ss:-0x6f70              # gen=36 38 26  dis=36 38 26 90 90
    cmp    %ch,%ss:-0x6f70              # gen=36 38 2e  dis=36 38 2e 90 90
    cmp    %dh,%ss:-0x6f70              # gen=36 38 36  dis=36 38 36 90 90
    cmp    %bh,%ss:-0x6f70              # gen=36 38 3e  dis=36 38 3e 90 90
    ss cmp %ax,%ax                      # gen=36 39 c0  dis=36 39 c0
    cmp    %ax,%ss:-0x6f70              # gen=36 39 06  dis=36 39 06 90 90
    ss cmp %cx,%ax                      # gen=36 39 c8  dis=36 39 c8
    ss cmp %dx,%ax                      # gen=36 39 d0  dis=36 39 d0
    ss cmp %bx,%ax                      # gen=36 39 d8  dis=36 39 d8
    ss cmp %sp,%ax                      # gen=36 39 e0  dis=36 39 e0
    ss cmp %bp,%ax                      # gen=36 39 e8  dis=36 39 e8
    ss cmp %si,%ax                      # gen=36 39 f0  dis=36 39 f0
    ss cmp %di,%ax                      # gen=36 39 f8  dis=36 39 f8
    cmp    %cx,%ss:-0x6f70              # gen=36 39 0e  dis=36 39 0e 90 90
    cmp    %dx,%ss:-0x6f70              # gen=36 39 16  dis=36 39 16 90 90
    cmp    %bx,%ss:-0x6f70              # gen=36 39 1e  dis=36 39 1e 90 90
    cmp    %sp,%ss:-0x6f70              # gen=36 39 26  dis=36 39 26 90 90
    cmp    %bp,%ss:-0x6f70              # gen=36 39 2e  dis=36 39 2e 90 90
    cmp    %si,%ss:-0x6f70              # gen=36 39 36  dis=36 39 36 90 90
    cmp    %di,%ss:-0x6f70              # gen=36 39 3e  dis=36 39 3e 90 90
    ss cmp %al,%al                      # gen=36 3a c0  dis=36 3a c0
    cmp    %ss:-0x6f70,%al              # gen=36 3a 06  dis=36 3a 06 90 90
    ss cmp %al,%cl                      # gen=36 3a c8  dis=36 3a c8
    ss cmp %al,%dl                      # gen=36 3a d0  dis=36 3a d0
    ss cmp %al,%bl                      # gen=36 3a d8  dis=36 3a d8
    ss cmp %al,%ah                      # gen=36 3a e0  dis=36 3a e0
    ss cmp %al,%ch                      # gen=36 3a e8  dis=36 3a e8
    ss cmp %al,%dh                      # gen=36 3a f0  dis=36 3a f0
    ss cmp %al,%bh                      # gen=36 3a f8  dis=36 3a f8
    cmp    %ss:-0x6f70,%cl              # gen=36 3a 0e  dis=36 3a 0e 90 90
    cmp    %ss:-0x6f70,%dl              # gen=36 3a 16  dis=36 3a 16 90 90
    cmp    %ss:-0x6f70,%bl              # gen=36 3a 1e  dis=36 3a 1e 90 90
    cmp    %ss:-0x6f70,%ah              # gen=36 3a 26  dis=36 3a 26 90 90
    cmp    %ss:-0x6f70,%ch              # gen=36 3a 2e  dis=36 3a 2e 90 90
    cmp    %ss:-0x6f70,%dh              # gen=36 3a 36  dis=36 3a 36 90 90
    cmp    %ss:-0x6f70,%bh              # gen=36 3a 3e  dis=36 3a 3e 90 90
    ss cmp %ax,%ax                      # gen=36 3b c0  dis=36 3b c0
    cmp    %ss:-0x6f70,%ax              # gen=36 3b 06  dis=36 3b 06 90 90
    ss cmp %ax,%cx                      # gen=36 3b c8  dis=36 3b c8
    ss cmp %ax,%dx                      # gen=36 3b d0  dis=36 3b d0
    ss cmp %ax,%bx                      # gen=36 3b d8  dis=36 3b d8
    ss cmp %ax,%sp                      # gen=36 3b e0  dis=36 3b e0
    ss cmp %ax,%bp                      # gen=36 3b e8  dis=36 3b e8
    ss cmp %ax,%si                      # gen=36 3b f0  dis=36 3b f0
    ss cmp %ax,%di                      # gen=36 3b f8  dis=36 3b f8
    cmp    %ss:-0x6f70,%cx              # gen=36 3b 0e  dis=36 3b 0e 90 90
    cmp    %ss:-0x6f70,%dx              # gen=36 3b 16  dis=36 3b 16 90 90
    cmp    %ss:-0x6f70,%bx              # gen=36 3b 1e  dis=36 3b 1e 90 90
    cmp    %ss:-0x6f70,%sp              # gen=36 3b 26  dis=36 3b 26 90 90
    cmp    %ss:-0x6f70,%bp              # gen=36 3b 2e  dis=36 3b 2e 90 90
    cmp    %ss:-0x6f70,%si              # gen=36 3b 36  dis=36 3b 36 90 90
    cmp    %ss:-0x6f70,%di              # gen=36 3b 3e  dis=36 3b 3e 90 90
    ss cmp $0xc0,%al                    # gen=36 3c c0  dis=36 3c c0
    ss cmp $0x6,%al                     # gen=36 3c 06  dis=36 3c 06
    ss cmp $0x90c0,%ax                  # gen=36 3d c0  dis=36 3d c0 90
    ss cmp $0x9006,%ax                  # gen=36 3d 06  dis=36 3d 06 90
    .byte 0x36,0x3e,0xc0            # fallback; gen=36 3e c0
    .byte 0x36,0x3e,0x06            # fallback; gen=36 3e 06
    ss aas                              # gen=36 3f c0  dis=36 3f
    ss aas                              # gen=36 3f 06  dis=36 3f
    ss inc %ax                          # gen=36 40 c0  dis=36 40
    ss inc %ax                          # gen=36 40 06  dis=36 40
    ss inc %cx                          # gen=36 41 c0  dis=36 41
    ss inc %cx                          # gen=36 41 06  dis=36 41
    ss inc %dx                          # gen=36 42 c0  dis=36 42
    ss inc %dx                          # gen=36 42 06  dis=36 42
    ss inc %bx                          # gen=36 43 c0  dis=36 43
    ss inc %bx                          # gen=36 43 06  dis=36 43
    ss inc %sp                          # gen=36 44 c0  dis=36 44
    ss inc %sp                          # gen=36 44 06  dis=36 44
    ss inc %bp                          # gen=36 45 c0  dis=36 45
    ss inc %bp                          # gen=36 45 06  dis=36 45
    ss inc %si                          # gen=36 46 c0  dis=36 46
    ss inc %si                          # gen=36 46 06  dis=36 46
    ss inc %di                          # gen=36 47 c0  dis=36 47
    ss inc %di                          # gen=36 47 06  dis=36 47
    ss dec %ax                          # gen=36 48 c0  dis=36 48
    ss dec %ax                          # gen=36 48 06  dis=36 48
    ss dec %cx                          # gen=36 49 c0  dis=36 49
    ss dec %cx                          # gen=36 49 06  dis=36 49
    ss dec %dx                          # gen=36 4a c0  dis=36 4a
    ss dec %dx                          # gen=36 4a 06  dis=36 4a
    ss dec %bx                          # gen=36 4b c0  dis=36 4b
    ss dec %bx                          # gen=36 4b 06  dis=36 4b
    ss dec %sp                          # gen=36 4c c0  dis=36 4c
    ss dec %sp                          # gen=36 4c 06  dis=36 4c
    ss dec %bp                          # gen=36 4d c0  dis=36 4d
    ss dec %bp                          # gen=36 4d 06  dis=36 4d
    ss dec %si                          # gen=36 4e c0  dis=36 4e
    ss dec %si                          # gen=36 4e 06  dis=36 4e
    ss dec %di                          # gen=36 4f c0  dis=36 4f
    ss dec %di                          # gen=36 4f 06  dis=36 4f
    ss push %ax                         # gen=36 50 c0  dis=36 50
    ss push %ax                         # gen=36 50 06  dis=36 50
    ss push %cx                         # gen=36 51 c0  dis=36 51
    ss push %cx                         # gen=36 51 06  dis=36 51
    ss push %dx                         # gen=36 52 c0  dis=36 52
    ss push %dx                         # gen=36 52 06  dis=36 52
    ss push %bx                         # gen=36 53 c0  dis=36 53
    ss push %bx                         # gen=36 53 06  dis=36 53
    ss push %sp                         # gen=36 54 c0  dis=36 54
    ss push %sp                         # gen=36 54 06  dis=36 54
    ss push %bp                         # gen=36 55 c0  dis=36 55
    ss push %bp                         # gen=36 55 06  dis=36 55
    ss push %si                         # gen=36 56 c0  dis=36 56
    ss push %si                         # gen=36 56 06  dis=36 56
    ss push %di                         # gen=36 57 c0  dis=36 57
    ss push %di                         # gen=36 57 06  dis=36 57
    ss pop %ax                          # gen=36 58 c0  dis=36 58
    ss pop %ax                          # gen=36 58 06  dis=36 58
    ss pop %cx                          # gen=36 59 c0  dis=36 59
    ss pop %cx                          # gen=36 59 06  dis=36 59
    ss pop %dx                          # gen=36 5a c0  dis=36 5a
    ss pop %dx                          # gen=36 5a 06  dis=36 5a
    ss pop %bx                          # gen=36 5b c0  dis=36 5b
    ss pop %bx                          # gen=36 5b 06  dis=36 5b
    ss pop %sp                          # gen=36 5c c0  dis=36 5c
    ss pop %sp                          # gen=36 5c 06  dis=36 5c
    ss pop %bp                          # gen=36 5d c0  dis=36 5d
    ss pop %bp                          # gen=36 5d 06  dis=36 5d
    ss pop %si                          # gen=36 5e c0  dis=36 5e
    ss pop %si                          # gen=36 5e 06  dis=36 5e
    ss pop %di                          # gen=36 5f c0  dis=36 5f
    ss pop %di                          # gen=36 5f 06  dis=36 5f
    .byte 0x36,0x60,0xc0            # fallback; gen=36 60 c0
    .byte 0x36,0x60,0x06            # fallback; gen=36 60 06
    .byte 0x36,0x61,0xc0            # fallback; gen=36 61 c0
    .byte 0x36,0x61,0x06            # fallback; gen=36 61 06
    .byte 0x36,0x62,0xc0            # fallback; gen=36 62 c0
    .byte 0x36,0x62,0x06            # fallback; gen=36 62 06
    .byte 0x36,0x62,0xc8            # fallback; gen=36 62 c8
    .byte 0x36,0x62,0xd0            # fallback; gen=36 62 d0
    .byte 0x36,0x62,0xd8            # fallback; gen=36 62 d8
    .byte 0x36,0x62,0xe0            # fallback; gen=36 62 e0
    .byte 0x36,0x62,0xe8            # fallback; gen=36 62 e8
    .byte 0x36,0x62,0xf0            # fallback; gen=36 62 f0
    .byte 0x36,0x62,0xf8            # fallback; gen=36 62 f8
    .byte 0x36,0x62,0x0e            # fallback; gen=36 62 0e
    .byte 0x36,0x62,0x16            # fallback; gen=36 62 16
    .byte 0x36,0x62,0x1e            # fallback; gen=36 62 1e
    .byte 0x36,0x62,0x26            # fallback; gen=36 62 26
    .byte 0x36,0x62,0x2e            # fallback; gen=36 62 2e
    .byte 0x36,0x62,0x36            # fallback; gen=36 62 36
    .byte 0x36,0x62,0x3e            # fallback; gen=36 62 3e
    .byte 0x36,0x63,0xc0            # fallback; gen=36 63 c0
    .byte 0x36,0x63,0x06            # fallback; gen=36 63 06
    .byte 0x36,0x63,0xc8            # fallback; gen=36 63 c8
    .byte 0x36,0x63,0xd0            # fallback; gen=36 63 d0
    .byte 0x36,0x63,0xd8            # fallback; gen=36 63 d8
    .byte 0x36,0x63,0xe0            # fallback; gen=36 63 e0
    .byte 0x36,0x63,0xe8            # fallback; gen=36 63 e8
    .byte 0x36,0x63,0xf0            # fallback; gen=36 63 f0
    .byte 0x36,0x63,0xf8            # fallback; gen=36 63 f8
    .byte 0x36,0x63,0x0e            # fallback; gen=36 63 0e
    .byte 0x36,0x63,0x16            # fallback; gen=36 63 16
    .byte 0x36,0x63,0x1e            # fallback; gen=36 63 1e
    .byte 0x36,0x63,0x26            # fallback; gen=36 63 26
    .byte 0x36,0x63,0x2e            # fallback; gen=36 63 2e
    .byte 0x36,0x63,0x36            # fallback; gen=36 63 36
    .byte 0x36,0x63,0x3e            # fallback; gen=36 63 3e
    .byte 0x36,0x64,0xc0            # fallback; gen=36 64 c0
    .byte 0x36,0x64,0x06            # fallback; gen=36 64 06
    .byte 0x36,0x65,0xc0            # fallback; gen=36 65 c0
    .byte 0x36,0x65,0x06            # fallback; gen=36 65 06
    .byte 0x36,0x66,0xc0            # fallback; gen=36 66 c0
    .byte 0x36,0x66,0x06            # fallback; gen=36 66 06
    .byte 0x36,0x66,0xc8            # fallback; gen=36 66 c8
    .byte 0x36,0x66,0xd0            # fallback; gen=36 66 d0
    .byte 0x36,0x66,0xd8            # fallback; gen=36 66 d8
    .byte 0x36,0x66,0xe0            # fallback; gen=36 66 e0
    .byte 0x36,0x66,0xe8            # fallback; gen=36 66 e8
    .byte 0x36,0x66,0xf0            # fallback; gen=36 66 f0
    .byte 0x36,0x66,0xf8            # fallback; gen=36 66 f8
    .byte 0x36,0x66,0x0e            # fallback; gen=36 66 0e
    .byte 0x36,0x66,0x16            # fallback; gen=36 66 16
    .byte 0x36,0x66,0x1e            # fallback; gen=36 66 1e
    .byte 0x36,0x66,0x26            # fallback; gen=36 66 26
    .byte 0x36,0x66,0x2e            # fallback; gen=36 66 2e
    .byte 0x36,0x66,0x36            # fallback; gen=36 66 36
    .byte 0x36,0x66,0x3e            # fallback; gen=36 66 3e
    .byte 0x36,0x67,0xc0            # fallback; gen=36 67 c0
    .byte 0x36,0x67,0x06            # fallback; gen=36 67 06
    .byte 0x36,0x67,0xc8            # fallback; gen=36 67 c8
    .byte 0x36,0x67,0xd0            # fallback; gen=36 67 d0
    .byte 0x36,0x67,0xd8            # fallback; gen=36 67 d8
    .byte 0x36,0x67,0xe0            # fallback; gen=36 67 e0
    .byte 0x36,0x67,0xe8            # fallback; gen=36 67 e8
    .byte 0x36,0x67,0xf0            # fallback; gen=36 67 f0
    .byte 0x36,0x67,0xf8            # fallback; gen=36 67 f8
    .byte 0x36,0x67,0x0e            # fallback; gen=36 67 0e
    .byte 0x36,0x67,0x16            # fallback; gen=36 67 16
    .byte 0x36,0x67,0x1e            # fallback; gen=36 67 1e
    .byte 0x36,0x67,0x26            # fallback; gen=36 67 26
    .byte 0x36,0x67,0x2e            # fallback; gen=36 67 2e
    .byte 0x36,0x67,0x36            # fallback; gen=36 67 36
    .byte 0x36,0x67,0x3e            # fallback; gen=36 67 3e
    .byte 0x36,0x68,0xc0            # fallback; gen=36 68 c0
    .byte 0x36,0x68,0x06            # fallback; gen=36 68 06
    .byte 0x36,0x69,0xc0            # fallback; gen=36 69 c0
    .byte 0x36,0x69,0x06            # fallback; gen=36 69 06
    .byte 0x36,0x69,0xc8            # fallback; gen=36 69 c8
    .byte 0x36,0x69,0xd0            # fallback; gen=36 69 d0
    .byte 0x36,0x69,0xd8            # fallback; gen=36 69 d8
    .byte 0x36,0x69,0xe0            # fallback; gen=36 69 e0
    .byte 0x36,0x69,0xe8            # fallback; gen=36 69 e8
    .byte 0x36,0x69,0xf0            # fallback; gen=36 69 f0
    .byte 0x36,0x69,0xf8            # fallback; gen=36 69 f8
    .byte 0x36,0x69,0x0e            # fallback; gen=36 69 0e
    .byte 0x36,0x69,0x16            # fallback; gen=36 69 16
    .byte 0x36,0x69,0x1e            # fallback; gen=36 69 1e
    .byte 0x36,0x69,0x26            # fallback; gen=36 69 26
    .byte 0x36,0x69,0x2e            # fallback; gen=36 69 2e
    .byte 0x36,0x69,0x36            # fallback; gen=36 69 36
    .byte 0x36,0x69,0x3e            # fallback; gen=36 69 3e
    .byte 0x36,0x6a,0xc0            # fallback; gen=36 6a c0
    .byte 0x36,0x6a,0x06            # fallback; gen=36 6a 06
    .byte 0x36,0x6b,0xc0            # fallback; gen=36 6b c0
    .byte 0x36,0x6b,0x06            # fallback; gen=36 6b 06
    .byte 0x36,0x6b,0xc8            # fallback; gen=36 6b c8
    .byte 0x36,0x6b,0xd0            # fallback; gen=36 6b d0
    .byte 0x36,0x6b,0xd8            # fallback; gen=36 6b d8
    .byte 0x36,0x6b,0xe0            # fallback; gen=36 6b e0
    .byte 0x36,0x6b,0xe8            # fallback; gen=36 6b e8
    .byte 0x36,0x6b,0xf0            # fallback; gen=36 6b f0
    .byte 0x36,0x6b,0xf8            # fallback; gen=36 6b f8
    .byte 0x36,0x6b,0x0e            # fallback; gen=36 6b 0e
    .byte 0x36,0x6b,0x16            # fallback; gen=36 6b 16
    .byte 0x36,0x6b,0x1e            # fallback; gen=36 6b 1e
    .byte 0x36,0x6b,0x26            # fallback; gen=36 6b 26
    .byte 0x36,0x6b,0x2e            # fallback; gen=36 6b 2e
    .byte 0x36,0x6b,0x36            # fallback; gen=36 6b 36
    .byte 0x36,0x6b,0x3e            # fallback; gen=36 6b 3e
    .byte 0x36,0x6c,0xc0            # fallback; gen=36 6c c0
    .byte 0x36,0x6c,0x06            # fallback; gen=36 6c 06
    .byte 0x36,0x6d,0xc0            # fallback; gen=36 6d c0
    .byte 0x36,0x6d,0x06            # fallback; gen=36 6d 06
    .byte 0x36,0x6e,0xc0            # fallback; gen=36 6e c0
    .byte 0x36,0x6e,0x06            # fallback; gen=36 6e 06
    .byte 0x36,0x6f,0xc0            # fallback; gen=36 6f c0
    .byte 0x36,0x6f,0x06            # fallback; gen=36 6f 06
    .byte 0x36,0x70,0xc0            # fallback; gen=36 70 c0
    .byte 0x36,0x70,0x06            # fallback; gen=36 70 06
    .byte 0x36,0x71,0xc0            # fallback; gen=36 71 c0
    .byte 0x36,0x71,0x06            # fallback; gen=36 71 06
    .byte 0x36,0x72,0xc0            # fallback; gen=36 72 c0
    .byte 0x36,0x72,0x06            # fallback; gen=36 72 06
    .byte 0x36,0x73,0xc0            # fallback; gen=36 73 c0
    .byte 0x36,0x73,0x06            # fallback; gen=36 73 06
    .byte 0x36,0x74,0xc0            # fallback; gen=36 74 c0
    .byte 0x36,0x74,0x06            # fallback; gen=36 74 06
    .byte 0x36,0x75,0xc0            # fallback; gen=36 75 c0
    .byte 0x36,0x75,0x06            # fallback; gen=36 75 06
    .byte 0x36,0x76,0xc0            # fallback; gen=36 76 c0
    .byte 0x36,0x76,0x06            # fallback; gen=36 76 06
    .byte 0x36,0x77,0xc0            # fallback; gen=36 77 c0
    .byte 0x36,0x77,0x06            # fallback; gen=36 77 06
    .byte 0x36,0x78,0xc0            # fallback; gen=36 78 c0
    .byte 0x36,0x78,0x06            # fallback; gen=36 78 06
    .byte 0x36,0x79,0xc0            # fallback; gen=36 79 c0
    .byte 0x36,0x79,0x06            # fallback; gen=36 79 06
    .byte 0x36,0x7a,0xc0            # fallback; gen=36 7a c0
    .byte 0x36,0x7a,0x06            # fallback; gen=36 7a 06
    .byte 0x36,0x7b,0xc0            # fallback; gen=36 7b c0
    .byte 0x36,0x7b,0x06            # fallback; gen=36 7b 06
    .byte 0x36,0x7c,0xc0            # fallback; gen=36 7c c0
    .byte 0x36,0x7c,0x06            # fallback; gen=36 7c 06
    .byte 0x36,0x7d,0xc0            # fallback; gen=36 7d c0
    .byte 0x36,0x7d,0x06            # fallback; gen=36 7d 06
    .byte 0x36,0x7e,0xc0            # fallback; gen=36 7e c0
    .byte 0x36,0x7e,0x06            # fallback; gen=36 7e 06
    .byte 0x36,0x7f,0xc0            # fallback; gen=36 7f c0
    .byte 0x36,0x7f,0x06            # fallback; gen=36 7f 06
    ss add $0x90,%al                    # gen=36 80 c0  dis=36 80 c0 90
    addb   $0x90,%ss:-0x6f70            # gen=36 80 06  dis=36 80 06 90 90 90
    ss or  $0x90,%al                    # gen=36 80 c8  dis=36 80 c8 90
    ss adc $0x90,%al                    # gen=36 80 d0  dis=36 80 d0 90
    ss sbb $0x90,%al                    # gen=36 80 d8  dis=36 80 d8 90
    ss and $0x90,%al                    # gen=36 80 e0  dis=36 80 e0 90
    ss sub $0x90,%al                    # gen=36 80 e8  dis=36 80 e8 90
    ss xor $0x90,%al                    # gen=36 80 f0  dis=36 80 f0 90
    ss cmp $0x90,%al                    # gen=36 80 f8  dis=36 80 f8 90
    orb    $0x90,%ss:-0x6f70            # gen=36 80 0e  dis=36 80 0e 90 90 90
    adcb   $0x90,%ss:-0x6f70            # gen=36 80 16  dis=36 80 16 90 90 90
    sbbb   $0x90,%ss:-0x6f70            # gen=36 80 1e  dis=36 80 1e 90 90 90
    andb   $0x90,%ss:-0x6f70            # gen=36 80 26  dis=36 80 26 90 90 90
    subb   $0x90,%ss:-0x6f70            # gen=36 80 2e  dis=36 80 2e 90 90 90
    xorb   $0x90,%ss:-0x6f70            # gen=36 80 36  dis=36 80 36 90 90 90
    cmpb   $0x90,%ss:-0x6f70            # gen=36 80 3e  dis=36 80 3e 90 90 90
    ss add $0x9090,%ax                  # gen=36 81 c0  dis=36 81 c0 90 90
    addw   $0x9090,%ss:-0x6f70          # gen=36 81 06  dis=36 81 06 90 90 90 90
    ss or  $0x9090,%ax                  # gen=36 81 c8  dis=36 81 c8 90 90
    ss adc $0x9090,%ax                  # gen=36 81 d0  dis=36 81 d0 90 90
    ss sbb $0x9090,%ax                  # gen=36 81 d8  dis=36 81 d8 90 90
    ss and $0x9090,%ax                  # gen=36 81 e0  dis=36 81 e0 90 90
    ss sub $0x9090,%ax                  # gen=36 81 e8  dis=36 81 e8 90 90
    ss xor $0x9090,%ax                  # gen=36 81 f0  dis=36 81 f0 90 90
    ss cmp $0x9090,%ax                  # gen=36 81 f8  dis=36 81 f8 90 90
    orw    $0x9090,%ss:-0x6f70          # gen=36 81 0e  dis=36 81 0e 90 90 90 90
    adcw   $0x9090,%ss:-0x6f70          # gen=36 81 16  dis=36 81 16 90 90 90 90
    sbbw   $0x9090,%ss:-0x6f70          # gen=36 81 1e  dis=36 81 1e 90 90 90 90
    andw   $0x9090,%ss:-0x6f70          # gen=36 81 26  dis=36 81 26 90 90 90 90
    subw   $0x9090,%ss:-0x6f70          # gen=36 81 2e  dis=36 81 2e 90 90 90 90
    xorw   $0x9090,%ss:-0x6f70          # gen=36 81 36  dis=36 81 36 90 90 90 90
    cmpw   $0x9090,%ss:-0x6f70          # gen=36 81 3e  dis=36 81 3e 90 90 90 90
    ss add $0x90,%al                    # gen=36 82 c0  dis=36 82 c0 90
    addb   $0x90,%ss:-0x6f70            # gen=36 82 06  dis=36 82 06 90 90 90
    ss or  $0x90,%al                    # gen=36 82 c8  dis=36 82 c8 90
    ss adc $0x90,%al                    # gen=36 82 d0  dis=36 82 d0 90
    ss sbb $0x90,%al                    # gen=36 82 d8  dis=36 82 d8 90
    ss and $0x90,%al                    # gen=36 82 e0  dis=36 82 e0 90
    ss sub $0x90,%al                    # gen=36 82 e8  dis=36 82 e8 90
    ss xor $0x90,%al                    # gen=36 82 f0  dis=36 82 f0 90
    ss cmp $0x90,%al                    # gen=36 82 f8  dis=36 82 f8 90
    orb    $0x90,%ss:-0x6f70            # gen=36 82 0e  dis=36 82 0e 90 90 90
    adcb   $0x90,%ss:-0x6f70            # gen=36 82 16  dis=36 82 16 90 90 90
    sbbb   $0x90,%ss:-0x6f70            # gen=36 82 1e  dis=36 82 1e 90 90 90
    andb   $0x90,%ss:-0x6f70            # gen=36 82 26  dis=36 82 26 90 90 90
    subb   $0x90,%ss:-0x6f70            # gen=36 82 2e  dis=36 82 2e 90 90 90
    xorb   $0x90,%ss:-0x6f70            # gen=36 82 36  dis=36 82 36 90 90 90
    cmpb   $0x90,%ss:-0x6f70            # gen=36 82 3e  dis=36 82 3e 90 90 90
    ss add $0xff90,%ax                  # gen=36 83 c0  dis=36 83 c0 90
    addw   $0xff90,%ss:-0x6f70          # gen=36 83 06  dis=36 83 06 90 90 90
    ss or  $0xff90,%ax                  # gen=36 83 c8  dis=36 83 c8 90
    ss adc $0xff90,%ax                  # gen=36 83 d0  dis=36 83 d0 90
    ss sbb $0xff90,%ax                  # gen=36 83 d8  dis=36 83 d8 90
    ss and $0xff90,%ax                  # gen=36 83 e0  dis=36 83 e0 90
    ss sub $0xff90,%ax                  # gen=36 83 e8  dis=36 83 e8 90
    ss xor $0xff90,%ax                  # gen=36 83 f0  dis=36 83 f0 90
    ss cmp $0xff90,%ax                  # gen=36 83 f8  dis=36 83 f8 90
    orw    $0xff90,%ss:-0x6f70          # gen=36 83 0e  dis=36 83 0e 90 90 90
    adcw   $0xff90,%ss:-0x6f70          # gen=36 83 16  dis=36 83 16 90 90 90
    sbbw   $0xff90,%ss:-0x6f70          # gen=36 83 1e  dis=36 83 1e 90 90 90
    andw   $0xff90,%ss:-0x6f70          # gen=36 83 26  dis=36 83 26 90 90 90
    subw   $0xff90,%ss:-0x6f70          # gen=36 83 2e  dis=36 83 2e 90 90 90
    xorw   $0xff90,%ss:-0x6f70          # gen=36 83 36  dis=36 83 36 90 90 90
    cmpw   $0xff90,%ss:-0x6f70          # gen=36 83 3e  dis=36 83 3e 90 90 90
    ss test %al,%al                     # gen=36 84 c0  dis=36 84 c0
    test   %al,%ss:-0x6f70              # gen=36 84 06  dis=36 84 06 90 90
    ss test %cl,%al                     # gen=36 84 c8  dis=36 84 c8
    ss test %dl,%al                     # gen=36 84 d0  dis=36 84 d0
    ss test %bl,%al                     # gen=36 84 d8  dis=36 84 d8
    ss test %ah,%al                     # gen=36 84 e0  dis=36 84 e0
    ss test %ch,%al                     # gen=36 84 e8  dis=36 84 e8
    ss test %dh,%al                     # gen=36 84 f0  dis=36 84 f0
    ss test %bh,%al                     # gen=36 84 f8  dis=36 84 f8
    test   %cl,%ss:-0x6f70              # gen=36 84 0e  dis=36 84 0e 90 90
    test   %dl,%ss:-0x6f70              # gen=36 84 16  dis=36 84 16 90 90
    test   %bl,%ss:-0x6f70              # gen=36 84 1e  dis=36 84 1e 90 90
    test   %ah,%ss:-0x6f70              # gen=36 84 26  dis=36 84 26 90 90
    test   %ch,%ss:-0x6f70              # gen=36 84 2e  dis=36 84 2e 90 90
    test   %dh,%ss:-0x6f70              # gen=36 84 36  dis=36 84 36 90 90
    test   %bh,%ss:-0x6f70              # gen=36 84 3e  dis=36 84 3e 90 90
    ss test %ax,%ax                     # gen=36 85 c0  dis=36 85 c0
    test   %ax,%ss:-0x6f70              # gen=36 85 06  dis=36 85 06 90 90
    ss test %cx,%ax                     # gen=36 85 c8  dis=36 85 c8
    ss test %dx,%ax                     # gen=36 85 d0  dis=36 85 d0
    ss test %bx,%ax                     # gen=36 85 d8  dis=36 85 d8
    ss test %sp,%ax                     # gen=36 85 e0  dis=36 85 e0
    ss test %bp,%ax                     # gen=36 85 e8  dis=36 85 e8
    ss test %si,%ax                     # gen=36 85 f0  dis=36 85 f0
    ss test %di,%ax                     # gen=36 85 f8  dis=36 85 f8
    test   %cx,%ss:-0x6f70              # gen=36 85 0e  dis=36 85 0e 90 90
    test   %dx,%ss:-0x6f70              # gen=36 85 16  dis=36 85 16 90 90
    test   %bx,%ss:-0x6f70              # gen=36 85 1e  dis=36 85 1e 90 90
    test   %sp,%ss:-0x6f70              # gen=36 85 26  dis=36 85 26 90 90
    test   %bp,%ss:-0x6f70              # gen=36 85 2e  dis=36 85 2e 90 90
    test   %si,%ss:-0x6f70              # gen=36 85 36  dis=36 85 36 90 90
    test   %di,%ss:-0x6f70              # gen=36 85 3e  dis=36 85 3e 90 90
    ss xchg %al,%al                     # gen=36 86 c0  dis=36 86 c0
    xchg   %al,%ss:-0x6f70              # gen=36 86 06  dis=36 86 06 90 90
    ss xchg %cl,%al                     # gen=36 86 c8  dis=36 86 c8
    ss xchg %dl,%al                     # gen=36 86 d0  dis=36 86 d0
    ss xchg %bl,%al                     # gen=36 86 d8  dis=36 86 d8
    ss xchg %ah,%al                     # gen=36 86 e0  dis=36 86 e0
    ss xchg %ch,%al                     # gen=36 86 e8  dis=36 86 e8
    ss xchg %dh,%al                     # gen=36 86 f0  dis=36 86 f0
    ss xchg %bh,%al                     # gen=36 86 f8  dis=36 86 f8
    xchg   %cl,%ss:-0x6f70              # gen=36 86 0e  dis=36 86 0e 90 90
    xchg   %dl,%ss:-0x6f70              # gen=36 86 16  dis=36 86 16 90 90
    xchg   %bl,%ss:-0x6f70              # gen=36 86 1e  dis=36 86 1e 90 90
    xchg   %ah,%ss:-0x6f70              # gen=36 86 26  dis=36 86 26 90 90
    xchg   %ch,%ss:-0x6f70              # gen=36 86 2e  dis=36 86 2e 90 90
    xchg   %dh,%ss:-0x6f70              # gen=36 86 36  dis=36 86 36 90 90
    xchg   %bh,%ss:-0x6f70              # gen=36 86 3e  dis=36 86 3e 90 90
    ss xchg %ax,%ax                     # gen=36 87 c0  dis=36 87 c0
    xchg   %ax,%ss:-0x6f70              # gen=36 87 06  dis=36 87 06 90 90
    ss xchg %cx,%ax                     # gen=36 87 c8  dis=36 87 c8
    ss xchg %dx,%ax                     # gen=36 87 d0  dis=36 87 d0
    ss xchg %bx,%ax                     # gen=36 87 d8  dis=36 87 d8
    ss xchg %sp,%ax                     # gen=36 87 e0  dis=36 87 e0
    ss xchg %bp,%ax                     # gen=36 87 e8  dis=36 87 e8
    ss xchg %si,%ax                     # gen=36 87 f0  dis=36 87 f0
    ss xchg %di,%ax                     # gen=36 87 f8  dis=36 87 f8
    xchg   %cx,%ss:-0x6f70              # gen=36 87 0e  dis=36 87 0e 90 90
    xchg   %dx,%ss:-0x6f70              # gen=36 87 16  dis=36 87 16 90 90
    xchg   %bx,%ss:-0x6f70              # gen=36 87 1e  dis=36 87 1e 90 90
    xchg   %sp,%ss:-0x6f70              # gen=36 87 26  dis=36 87 26 90 90
    xchg   %bp,%ss:-0x6f70              # gen=36 87 2e  dis=36 87 2e 90 90
    xchg   %si,%ss:-0x6f70              # gen=36 87 36  dis=36 87 36 90 90
    xchg   %di,%ss:-0x6f70              # gen=36 87 3e  dis=36 87 3e 90 90
    ss mov %al,%al                      # gen=36 88 c0  dis=36 88 c0
    mov    %al,%ss:-0x6f70              # gen=36 88 06  dis=36 88 06 90 90
    ss mov %cl,%al                      # gen=36 88 c8  dis=36 88 c8
    ss mov %dl,%al                      # gen=36 88 d0  dis=36 88 d0
    ss mov %bl,%al                      # gen=36 88 d8  dis=36 88 d8
    ss mov %ah,%al                      # gen=36 88 e0  dis=36 88 e0
    ss mov %ch,%al                      # gen=36 88 e8  dis=36 88 e8
    ss mov %dh,%al                      # gen=36 88 f0  dis=36 88 f0
    ss mov %bh,%al                      # gen=36 88 f8  dis=36 88 f8
    mov    %cl,%ss:-0x6f70              # gen=36 88 0e  dis=36 88 0e 90 90
    mov    %dl,%ss:-0x6f70              # gen=36 88 16  dis=36 88 16 90 90
    mov    %bl,%ss:-0x6f70              # gen=36 88 1e  dis=36 88 1e 90 90
    mov    %ah,%ss:-0x6f70              # gen=36 88 26  dis=36 88 26 90 90
    mov    %ch,%ss:-0x6f70              # gen=36 88 2e  dis=36 88 2e 90 90
    mov    %dh,%ss:-0x6f70              # gen=36 88 36  dis=36 88 36 90 90
    mov    %bh,%ss:-0x6f70              # gen=36 88 3e  dis=36 88 3e 90 90
    ss mov %ax,%ax                      # gen=36 89 c0  dis=36 89 c0
    mov    %ax,%ss:-0x6f70              # gen=36 89 06  dis=36 89 06 90 90
    ss mov %cx,%ax                      # gen=36 89 c8  dis=36 89 c8
    ss mov %dx,%ax                      # gen=36 89 d0  dis=36 89 d0
    ss mov %bx,%ax                      # gen=36 89 d8  dis=36 89 d8
    ss mov %sp,%ax                      # gen=36 89 e0  dis=36 89 e0
    ss mov %bp,%ax                      # gen=36 89 e8  dis=36 89 e8
    ss mov %si,%ax                      # gen=36 89 f0  dis=36 89 f0
    ss mov %di,%ax                      # gen=36 89 f8  dis=36 89 f8
    mov    %cx,%ss:-0x6f70              # gen=36 89 0e  dis=36 89 0e 90 90
    mov    %dx,%ss:-0x6f70              # gen=36 89 16  dis=36 89 16 90 90
    mov    %bx,%ss:-0x6f70              # gen=36 89 1e  dis=36 89 1e 90 90
    mov    %sp,%ss:-0x6f70              # gen=36 89 26  dis=36 89 26 90 90
    mov    %bp,%ss:-0x6f70              # gen=36 89 2e  dis=36 89 2e 90 90
    mov    %si,%ss:-0x6f70              # gen=36 89 36  dis=36 89 36 90 90
    mov    %di,%ss:-0x6f70              # gen=36 89 3e  dis=36 89 3e 90 90
    ss mov %al,%al                      # gen=36 8a c0  dis=36 8a c0
    mov    %ss:-0x6f70,%al              # gen=36 8a 06  dis=36 8a 06 90 90
    ss mov %al,%cl                      # gen=36 8a c8  dis=36 8a c8
    ss mov %al,%dl                      # gen=36 8a d0  dis=36 8a d0
    ss mov %al,%bl                      # gen=36 8a d8  dis=36 8a d8
    ss mov %al,%ah                      # gen=36 8a e0  dis=36 8a e0
    ss mov %al,%ch                      # gen=36 8a e8  dis=36 8a e8
    ss mov %al,%dh                      # gen=36 8a f0  dis=36 8a f0
    ss mov %al,%bh                      # gen=36 8a f8  dis=36 8a f8
    mov    %ss:-0x6f70,%cl              # gen=36 8a 0e  dis=36 8a 0e 90 90
    mov    %ss:-0x6f70,%dl              # gen=36 8a 16  dis=36 8a 16 90 90
    mov    %ss:-0x6f70,%bl              # gen=36 8a 1e  dis=36 8a 1e 90 90
    mov    %ss:-0x6f70,%ah              # gen=36 8a 26  dis=36 8a 26 90 90
    mov    %ss:-0x6f70,%ch              # gen=36 8a 2e  dis=36 8a 2e 90 90
    mov    %ss:-0x6f70,%dh              # gen=36 8a 36  dis=36 8a 36 90 90
    mov    %ss:-0x6f70,%bh              # gen=36 8a 3e  dis=36 8a 3e 90 90
    ss mov %ax,%ax                      # gen=36 8b c0  dis=36 8b c0
    mov    %ss:-0x6f70,%ax              # gen=36 8b 06  dis=36 8b 06 90 90
    ss mov %ax,%cx                      # gen=36 8b c8  dis=36 8b c8
    ss mov %ax,%dx                      # gen=36 8b d0  dis=36 8b d0
    ss mov %ax,%bx                      # gen=36 8b d8  dis=36 8b d8
    ss mov %ax,%sp                      # gen=36 8b e0  dis=36 8b e0
    ss mov %ax,%bp                      # gen=36 8b e8  dis=36 8b e8
    ss mov %ax,%si                      # gen=36 8b f0  dis=36 8b f0
    ss mov %ax,%di                      # gen=36 8b f8  dis=36 8b f8
    mov    %ss:-0x6f70,%cx              # gen=36 8b 0e  dis=36 8b 0e 90 90
    mov    %ss:-0x6f70,%dx              # gen=36 8b 16  dis=36 8b 16 90 90
    mov    %ss:-0x6f70,%bx              # gen=36 8b 1e  dis=36 8b 1e 90 90
    mov    %ss:-0x6f70,%sp              # gen=36 8b 26  dis=36 8b 26 90 90
    mov    %ss:-0x6f70,%bp              # gen=36 8b 2e  dis=36 8b 2e 90 90
    mov    %ss:-0x6f70,%si              # gen=36 8b 36  dis=36 8b 36 90 90
    mov    %ss:-0x6f70,%di              # gen=36 8b 3e  dis=36 8b 3e 90 90
    ss mov %es,%ax                      # gen=36 8c c0  dis=36 8c c0
    mov    %es,%ss:-0x6f70              # gen=36 8c 06  dis=36 8c 06 90 90
    ss mov %cs,%ax                      # gen=36 8c c8  dis=36 8c c8
    ss mov %ss,%ax                      # gen=36 8c d0  dis=36 8c d0
    ss mov %ds,%ax                      # gen=36 8c d8  dis=36 8c d8
    .byte 0x36,0x8c,0xe0            # fallback; gen=36 8c e0
    .byte 0x36,0x8c,0xe8            # fallback; gen=36 8c e8
    .byte 0x36,0x8c,0xf0            # fallback; gen=36 8c f0
    .byte 0x36,0x8c,0xf8            # fallback; gen=36 8c f8
    mov    %cs,%ss:-0x6f70              # gen=36 8c 0e  dis=36 8c 0e 90 90
    mov    %ss,%ss:-0x6f70              # gen=36 8c 16  dis=36 8c 16 90 90
    mov    %ds,%ss:-0x6f70              # gen=36 8c 1e  dis=36 8c 1e 90 90
    .byte 0x36,0x8c,0x26            # fallback; gen=36 8c 26
    .byte 0x36,0x8c,0x2e            # fallback; gen=36 8c 2e
    .byte 0x36,0x8c,0x36            # fallback; gen=36 8c 36
    .byte 0x36,0x8c,0x3e            # fallback; gen=36 8c 3e
    .byte 0x36,0x8d,0xc0            # fallback; gen=36 8d c0
    .byte 0x36,0x8d,0x06            # fallback; gen=36 8d 06
    .byte 0x36,0x8d,0xc8            # fallback; gen=36 8d c8
    .byte 0x36,0x8d,0xd0            # fallback; gen=36 8d d0
    .byte 0x36,0x8d,0xd8            # fallback; gen=36 8d d8
    .byte 0x36,0x8d,0xe0            # fallback; gen=36 8d e0
    .byte 0x36,0x8d,0xe8            # fallback; gen=36 8d e8
    .byte 0x36,0x8d,0xf0            # fallback; gen=36 8d f0
    .byte 0x36,0x8d,0xf8            # fallback; gen=36 8d f8
    .byte 0x36,0x8d,0x0e            # fallback; gen=36 8d 0e
    .byte 0x36,0x8d,0x16            # fallback; gen=36 8d 16
    .byte 0x36,0x8d,0x1e            # fallback; gen=36 8d 1e
    .byte 0x36,0x8d,0x26            # fallback; gen=36 8d 26
    .byte 0x36,0x8d,0x2e            # fallback; gen=36 8d 2e
    .byte 0x36,0x8d,0x36            # fallback; gen=36 8d 36
    .byte 0x36,0x8d,0x3e            # fallback; gen=36 8d 3e
    ss mov %ax,%es                      # gen=36 8e c0  dis=36 8e c0
    mov    %ss:-0x6f70,%es              # gen=36 8e 06  dis=36 8e 06 90 90
    ss mov %ax,%cs                      # gen=36 8e c8  dis=36 8e c8
    ss mov %ax,%ss                      # gen=36 8e d0  dis=36 8e d0
    ss mov %ax,%ds                      # gen=36 8e d8  dis=36 8e d8
    .byte 0x36,0x8e,0xe0            # fallback; gen=36 8e e0
    .byte 0x36,0x8e,0xe8            # fallback; gen=36 8e e8
    .byte 0x36,0x8e,0xf0            # fallback; gen=36 8e f0
    .byte 0x36,0x8e,0xf8            # fallback; gen=36 8e f8
    mov    %ss:-0x6f70,%cs              # gen=36 8e 0e  dis=36 8e 0e 90 90
    mov    %ss:-0x6f70,%ss              # gen=36 8e 16  dis=36 8e 16 90 90
    mov    %ss:-0x6f70,%ds              # gen=36 8e 1e  dis=36 8e 1e 90 90
    .byte 0x36,0x8e,0x26            # fallback; gen=36 8e 26
    .byte 0x36,0x8e,0x2e            # fallback; gen=36 8e 2e
    .byte 0x36,0x8e,0x36            # fallback; gen=36 8e 36
    .byte 0x36,0x8e,0x3e            # fallback; gen=36 8e 3e
    ss pop %ax                          # gen=36 8f c0  dis=36 8f c0
    pop    %ss:-0x6f70                  # gen=36 8f 06  dis=36 8f 06 90 90
    .byte 0x36,0x8f,0xc8            # fallback; gen=36 8f c8
    .byte 0x36,0x8f,0xd0            # fallback; gen=36 8f d0
    .byte 0x36,0x8f,0xd8            # fallback; gen=36 8f d8
    .byte 0x36,0x8f,0xe0            # fallback; gen=36 8f e0
    .byte 0x36,0x8f,0xe8            # fallback; gen=36 8f e8
    .byte 0x36,0x8f,0xf0            # fallback; gen=36 8f f0
    .byte 0x36,0x8f,0xf8            # fallback; gen=36 8f f8
    .byte 0x36,0x8f,0x0e            # fallback; gen=36 8f 0e
    .byte 0x36,0x8f,0x16            # fallback; gen=36 8f 16
    .byte 0x36,0x8f,0x1e            # fallback; gen=36 8f 1e
    .byte 0x36,0x8f,0x26            # fallback; gen=36 8f 26
    .byte 0x36,0x8f,0x2e            # fallback; gen=36 8f 2e
    .byte 0x36,0x8f,0x36            # fallback; gen=36 8f 36
    .byte 0x36,0x8f,0x3e            # fallback; gen=36 8f 3e
    ss nop                              # gen=36 90 c0  dis=36 90
    ss nop                              # gen=36 90 06  dis=36 90
    ss xchg %ax,%cx                     # gen=36 91 c0  dis=36 91
    ss xchg %ax,%cx                     # gen=36 91 06  dis=36 91
    ss xchg %ax,%dx                     # gen=36 92 c0  dis=36 92
    ss xchg %ax,%dx                     # gen=36 92 06  dis=36 92
    ss xchg %ax,%bx                     # gen=36 93 c0  dis=36 93
    ss xchg %ax,%bx                     # gen=36 93 06  dis=36 93
    ss xchg %ax,%sp                     # gen=36 94 c0  dis=36 94
    ss xchg %ax,%sp                     # gen=36 94 06  dis=36 94
    ss xchg %ax,%bp                     # gen=36 95 c0  dis=36 95
    ss xchg %ax,%bp                     # gen=36 95 06  dis=36 95
    ss xchg %ax,%si                     # gen=36 96 c0  dis=36 96
    ss xchg %ax,%si                     # gen=36 96 06  dis=36 96
    ss xchg %ax,%di                     # gen=36 97 c0  dis=36 97
    ss xchg %ax,%di                     # gen=36 97 06  dis=36 97
    ss cbtw                             # gen=36 98 c0  dis=36 98
    ss cbtw                             # gen=36 98 06  dis=36 98
    ss cwtd                             # gen=36 99 c0  dis=36 99
    ss cwtd                             # gen=36 99 06  dis=36 99
    .byte 0x36,0x9a,0xc0            # fallback; gen=36 9a c0
    .byte 0x36,0x9a,0x06            # fallback; gen=36 9a 06
    .byte 0x36,0x9b,0xc0            # fallback; gen=36 9b c0
    .byte 0x36,0x9b,0x06            # fallback; gen=36 9b 06
    ss pushf                            # gen=36 9c c0  dis=36 9c
    ss pushf                            # gen=36 9c 06  dis=36 9c
    ss popf                             # gen=36 9d c0  dis=36 9d
    ss popf                             # gen=36 9d 06  dis=36 9d
    ss sahf                             # gen=36 9e c0  dis=36 9e
    ss sahf                             # gen=36 9e 06  dis=36 9e
    ss lahf                             # gen=36 9f c0  dis=36 9f
    ss lahf                             # gen=36 9f 06  dis=36 9f
    mov    %ss:0x90c0,%al               # gen=36 a0 c0  dis=36 a0 c0 90
    mov    %ss:0x9006,%al               # gen=36 a0 06  dis=36 a0 06 90
    mov    %ss:0x90c0,%ax               # gen=36 a1 c0  dis=36 a1 c0 90
    mov    %ss:0x9006,%ax               # gen=36 a1 06  dis=36 a1 06 90
    mov    %al,%ss:0x90c0               # gen=36 a2 c0  dis=36 a2 c0 90
    mov    %al,%ss:0x9006               # gen=36 a2 06  dis=36 a2 06 90
    mov    %ax,%ss:0x90c0               # gen=36 a3 c0  dis=36 a3 c0 90
    mov    %ax,%ss:0x9006               # gen=36 a3 06  dis=36 a3 06 90
    movsb  %ss:(%si),%es:(%di)          # gen=36 a4 c0  dis=36 a4
    movsb  %ss:(%si),%es:(%di)          # gen=36 a4 06  dis=36 a4
    movsw  %ss:(%si),%es:(%di)          # gen=36 a5 c0  dis=36 a5
    movsw  %ss:(%si),%es:(%di)          # gen=36 a5 06  dis=36 a5
    cmpsb  %es:(%di),%ss:(%si)          # gen=36 a6 c0  dis=36 a6
    cmpsb  %es:(%di),%ss:(%si)          # gen=36 a6 06  dis=36 a6
    cmpsw  %es:(%di),%ss:(%si)          # gen=36 a7 c0  dis=36 a7
    cmpsw  %es:(%di),%ss:(%si)          # gen=36 a7 06  dis=36 a7
    ss test $0xc0,%al                   # gen=36 a8 c0  dis=36 a8 c0
    ss test $0x6,%al                    # gen=36 a8 06  dis=36 a8 06
    ss test $0x90c0,%ax                 # gen=36 a9 c0  dis=36 a9 c0 90
    ss test $0x9006,%ax                 # gen=36 a9 06  dis=36 a9 06 90
    ss stos %al,%es:(%di)               # gen=36 aa c0  dis=36 aa
    ss stos %al,%es:(%di)               # gen=36 aa 06  dis=36 aa
    ss stos %ax,%es:(%di)               # gen=36 ab c0  dis=36 ab
    ss stos %ax,%es:(%di)               # gen=36 ab 06  dis=36 ab
    lods   %ss:(%si),%al                # gen=36 ac c0  dis=36 ac
    lods   %ss:(%si),%al                # gen=36 ac 06  dis=36 ac
    lods   %ss:(%si),%ax                # gen=36 ad c0  dis=36 ad
    lods   %ss:(%si),%ax                # gen=36 ad 06  dis=36 ad
    ss scas %es:(%di),%al               # gen=36 ae c0  dis=36 ae
    ss scas %es:(%di),%al               # gen=36 ae 06  dis=36 ae
    ss scas %es:(%di),%ax               # gen=36 af c0  dis=36 af
    ss scas %es:(%di),%ax               # gen=36 af 06  dis=36 af
    ss mov $0xc0,%al                    # gen=36 b0 c0  dis=36 b0 c0
    ss mov $0x6,%al                     # gen=36 b0 06  dis=36 b0 06
    ss mov $0xc0,%cl                    # gen=36 b1 c0  dis=36 b1 c0
    ss mov $0x6,%cl                     # gen=36 b1 06  dis=36 b1 06
    ss mov $0xc0,%dl                    # gen=36 b2 c0  dis=36 b2 c0
    ss mov $0x6,%dl                     # gen=36 b2 06  dis=36 b2 06
    ss mov $0xc0,%bl                    # gen=36 b3 c0  dis=36 b3 c0
    ss mov $0x6,%bl                     # gen=36 b3 06  dis=36 b3 06
    ss mov $0xc0,%ah                    # gen=36 b4 c0  dis=36 b4 c0
    ss mov $0x6,%ah                     # gen=36 b4 06  dis=36 b4 06
    ss mov $0xc0,%ch                    # gen=36 b5 c0  dis=36 b5 c0
    ss mov $0x6,%ch                     # gen=36 b5 06  dis=36 b5 06
    ss mov $0xc0,%dh                    # gen=36 b6 c0  dis=36 b6 c0
    ss mov $0x6,%dh                     # gen=36 b6 06  dis=36 b6 06
    ss mov $0xc0,%bh                    # gen=36 b7 c0  dis=36 b7 c0
    ss mov $0x6,%bh                     # gen=36 b7 06  dis=36 b7 06
    ss mov $0x90c0,%ax                  # gen=36 b8 c0  dis=36 b8 c0 90
    ss mov $0x9006,%ax                  # gen=36 b8 06  dis=36 b8 06 90
    ss mov $0x90c0,%cx                  # gen=36 b9 c0  dis=36 b9 c0 90
    ss mov $0x9006,%cx                  # gen=36 b9 06  dis=36 b9 06 90
    ss mov $0x90c0,%dx                  # gen=36 ba c0  dis=36 ba c0 90
    ss mov $0x9006,%dx                  # gen=36 ba 06  dis=36 ba 06 90
    ss mov $0x90c0,%bx                  # gen=36 bb c0  dis=36 bb c0 90
    ss mov $0x9006,%bx                  # gen=36 bb 06  dis=36 bb 06 90
    ss mov $0x90c0,%sp                  # gen=36 bc c0  dis=36 bc c0 90
    ss mov $0x9006,%sp                  # gen=36 bc 06  dis=36 bc 06 90
    ss mov $0x90c0,%bp                  # gen=36 bd c0  dis=36 bd c0 90
    ss mov $0x9006,%bp                  # gen=36 bd 06  dis=36 bd 06 90
    ss mov $0x90c0,%si                  # gen=36 be c0  dis=36 be c0 90
    ss mov $0x9006,%si                  # gen=36 be 06  dis=36 be 06 90
    ss mov $0x90c0,%di                  # gen=36 bf c0  dis=36 bf c0 90
    ss mov $0x9006,%di                  # gen=36 bf 06  dis=36 bf 06 90
    .byte 0x36,0xc0,0xc0            # fallback; gen=36 c0 c0
    .byte 0x36,0xc0,0x06            # fallback; gen=36 c0 06
    .byte 0x36,0xc0,0xc8            # fallback; gen=36 c0 c8
    .byte 0x36,0xc0,0xd0            # fallback; gen=36 c0 d0
    .byte 0x36,0xc0,0xd8            # fallback; gen=36 c0 d8
    .byte 0x36,0xc0,0xe0            # fallback; gen=36 c0 e0
    .byte 0x36,0xc0,0xe8            # fallback; gen=36 c0 e8
    .byte 0x36,0xc0,0xf0            # fallback; gen=36 c0 f0
    .byte 0x36,0xc0,0xf8            # fallback; gen=36 c0 f8
    .byte 0x36,0xc0,0x0e            # fallback; gen=36 c0 0e
    .byte 0x36,0xc0,0x16            # fallback; gen=36 c0 16
    .byte 0x36,0xc0,0x1e            # fallback; gen=36 c0 1e
    .byte 0x36,0xc0,0x26            # fallback; gen=36 c0 26
    .byte 0x36,0xc0,0x2e            # fallback; gen=36 c0 2e
    .byte 0x36,0xc0,0x36            # fallback; gen=36 c0 36
    .byte 0x36,0xc0,0x3e            # fallback; gen=36 c0 3e
    .byte 0x36,0xc1,0xc0            # fallback; gen=36 c1 c0
    .byte 0x36,0xc1,0x06            # fallback; gen=36 c1 06
    .byte 0x36,0xc1,0xc8            # fallback; gen=36 c1 c8
    .byte 0x36,0xc1,0xd0            # fallback; gen=36 c1 d0
    .byte 0x36,0xc1,0xd8            # fallback; gen=36 c1 d8
    .byte 0x36,0xc1,0xe0            # fallback; gen=36 c1 e0
    .byte 0x36,0xc1,0xe8            # fallback; gen=36 c1 e8
    .byte 0x36,0xc1,0xf0            # fallback; gen=36 c1 f0
    .byte 0x36,0xc1,0xf8            # fallback; gen=36 c1 f8
    .byte 0x36,0xc1,0x0e            # fallback; gen=36 c1 0e
    .byte 0x36,0xc1,0x16            # fallback; gen=36 c1 16
    .byte 0x36,0xc1,0x1e            # fallback; gen=36 c1 1e
    .byte 0x36,0xc1,0x26            # fallback; gen=36 c1 26
    .byte 0x36,0xc1,0x2e            # fallback; gen=36 c1 2e
    .byte 0x36,0xc1,0x36            # fallback; gen=36 c1 36
    .byte 0x36,0xc1,0x3e            # fallback; gen=36 c1 3e
    ss ret $0x90c0                      # gen=36 c2 c0  dis=36 c2 c0 90
    ss ret $0x9006                      # gen=36 c2 06  dis=36 c2 06 90
    ss ret                              # gen=36 c3 c0  dis=36 c3
    ss ret                              # gen=36 c3 06  dis=36 c3
    .byte 0x36,0xc4,0xc0            # fallback; gen=36 c4 c0
    les    %ss:-0x6f70,%ax              # gen=36 c4 06  dis=36 c4 06 90 90
    .byte 0x36,0xc4,0xc8            # fallback; gen=36 c4 c8
    .byte 0x36,0xc4,0xd0            # fallback; gen=36 c4 d0
    .byte 0x36,0xc4,0xd8            # fallback; gen=36 c4 d8
    .byte 0x36,0xc4,0xe0            # fallback; gen=36 c4 e0
    .byte 0x36,0xc4,0xe8            # fallback; gen=36 c4 e8
    .byte 0x36,0xc4,0xf0            # fallback; gen=36 c4 f0
    .byte 0x36,0xc4,0xf8            # fallback; gen=36 c4 f8
    les    %ss:-0x6f70,%cx              # gen=36 c4 0e  dis=36 c4 0e 90 90
    les    %ss:-0x6f70,%dx              # gen=36 c4 16  dis=36 c4 16 90 90
    les    %ss:-0x6f70,%bx              # gen=36 c4 1e  dis=36 c4 1e 90 90
    les    %ss:-0x6f70,%sp              # gen=36 c4 26  dis=36 c4 26 90 90
    les    %ss:-0x6f70,%bp              # gen=36 c4 2e  dis=36 c4 2e 90 90
    les    %ss:-0x6f70,%si              # gen=36 c4 36  dis=36 c4 36 90 90
    les    %ss:-0x6f70,%di              # gen=36 c4 3e  dis=36 c4 3e 90 90
    .byte 0x36,0xc5,0xc0            # fallback; gen=36 c5 c0
    lds    %ss:-0x6f70,%ax              # gen=36 c5 06  dis=36 c5 06 90 90
    .byte 0x36,0xc5,0xc8            # fallback; gen=36 c5 c8
    .byte 0x36,0xc5,0xd0            # fallback; gen=36 c5 d0
    .byte 0x36,0xc5,0xd8            # fallback; gen=36 c5 d8
    .byte 0x36,0xc5,0xe0            # fallback; gen=36 c5 e0
    .byte 0x36,0xc5,0xe8            # fallback; gen=36 c5 e8
    .byte 0x36,0xc5,0xf0            # fallback; gen=36 c5 f0
    .byte 0x36,0xc5,0xf8            # fallback; gen=36 c5 f8
    lds    %ss:-0x6f70,%cx              # gen=36 c5 0e  dis=36 c5 0e 90 90
    lds    %ss:-0x6f70,%dx              # gen=36 c5 16  dis=36 c5 16 90 90
    lds    %ss:-0x6f70,%bx              # gen=36 c5 1e  dis=36 c5 1e 90 90
    lds    %ss:-0x6f70,%sp              # gen=36 c5 26  dis=36 c5 26 90 90
    lds    %ss:-0x6f70,%bp              # gen=36 c5 2e  dis=36 c5 2e 90 90
    lds    %ss:-0x6f70,%si              # gen=36 c5 36  dis=36 c5 36 90 90
    lds    %ss:-0x6f70,%di              # gen=36 c5 3e  dis=36 c5 3e 90 90
    ss mov $0x90,%al                    # gen=36 c6 c0  dis=36 c6 c0 90
    movb   $0x90,%ss:-0x6f70            # gen=36 c6 06  dis=36 c6 06 90 90 90
    .byte 0x36,0xc6,0xc8            # fallback; gen=36 c6 c8
    .byte 0x36,0xc6,0xd0            # fallback; gen=36 c6 d0
    .byte 0x36,0xc6,0xd8            # fallback; gen=36 c6 d8
    .byte 0x36,0xc6,0xe0            # fallback; gen=36 c6 e0
    .byte 0x36,0xc6,0xe8            # fallback; gen=36 c6 e8
    .byte 0x36,0xc6,0xf0            # fallback; gen=36 c6 f0
    .byte 0x36,0xc6,0xf8            # fallback; gen=36 c6 f8
    .byte 0x36,0xc6,0x0e            # fallback; gen=36 c6 0e
    .byte 0x36,0xc6,0x16            # fallback; gen=36 c6 16
    .byte 0x36,0xc6,0x1e            # fallback; gen=36 c6 1e
    .byte 0x36,0xc6,0x26            # fallback; gen=36 c6 26
    .byte 0x36,0xc6,0x2e            # fallback; gen=36 c6 2e
    .byte 0x36,0xc6,0x36            # fallback; gen=36 c6 36
    .byte 0x36,0xc6,0x3e            # fallback; gen=36 c6 3e
    ss mov $0x9090,%ax                  # gen=36 c7 c0  dis=36 c7 c0 90 90
    movw   $0x9090,%ss:-0x6f70          # gen=36 c7 06  dis=36 c7 06 90 90 90 90
    .byte 0x36,0xc7,0xc8            # fallback; gen=36 c7 c8
    .byte 0x36,0xc7,0xd0            # fallback; gen=36 c7 d0
    .byte 0x36,0xc7,0xd8            # fallback; gen=36 c7 d8
    .byte 0x36,0xc7,0xe0            # fallback; gen=36 c7 e0
    .byte 0x36,0xc7,0xe8            # fallback; gen=36 c7 e8
    .byte 0x36,0xc7,0xf0            # fallback; gen=36 c7 f0
    .byte 0x36,0xc7,0xf8            # fallback; gen=36 c7 f8
    .byte 0x36,0xc7,0x0e            # fallback; gen=36 c7 0e
    .byte 0x36,0xc7,0x16            # fallback; gen=36 c7 16
    .byte 0x36,0xc7,0x1e            # fallback; gen=36 c7 1e
    .byte 0x36,0xc7,0x26            # fallback; gen=36 c7 26
    .byte 0x36,0xc7,0x2e            # fallback; gen=36 c7 2e
    .byte 0x36,0xc7,0x36            # fallback; gen=36 c7 36
    .byte 0x36,0xc7,0x3e            # fallback; gen=36 c7 3e
    .byte 0x36,0xc8,0xc0            # fallback; gen=36 c8 c0
    .byte 0x36,0xc8,0x06            # fallback; gen=36 c8 06
    .byte 0x36,0xc9,0xc0            # fallback; gen=36 c9 c0
    .byte 0x36,0xc9,0x06            # fallback; gen=36 c9 06
    ss lret $0x90c0                     # gen=36 ca c0  dis=36 ca c0 90
    ss lret $0x9006                     # gen=36 ca 06  dis=36 ca 06 90
    ss lret                             # gen=36 cb c0  dis=36 cb
    ss lret                             # gen=36 cb 06  dis=36 cb
    ss int3                             # gen=36 cc c0  dis=36 cc
    ss int3                             # gen=36 cc 06  dis=36 cc
    ss int $0xc0                        # gen=36 cd c0  dis=36 cd c0
    ss int $0x6                         # gen=36 cd 06  dis=36 cd 06
    ss into                             # gen=36 ce c0  dis=36 ce
    ss into                             # gen=36 ce 06  dis=36 ce
    ss iret                             # gen=36 cf c0  dis=36 cf
    ss iret                             # gen=36 cf 06  dis=36 cf
    ss rol $1,%al                       # gen=36 d0 c0  dis=36 d0 c0
    rolb   $1,%ss:-0x6f70               # gen=36 d0 06  dis=36 d0 06 90 90
    ss ror $1,%al                       # gen=36 d0 c8  dis=36 d0 c8
    ss rcl $1,%al                       # gen=36 d0 d0  dis=36 d0 d0
    ss rcr $1,%al                       # gen=36 d0 d8  dis=36 d0 d8
    ss shl $1,%al                       # gen=36 d0 e0  dis=36 d0 e0
    ss shr $1,%al                       # gen=36 d0 e8  dis=36 d0 e8
    ss shl $1,%al                       # gen=36 d0 f0  dis=36 d0 f0
    ss sar $1,%al                       # gen=36 d0 f8  dis=36 d0 f8
    rorb   $1,%ss:-0x6f70               # gen=36 d0 0e  dis=36 d0 0e 90 90
    rclb   $1,%ss:-0x6f70               # gen=36 d0 16  dis=36 d0 16 90 90
    rcrb   $1,%ss:-0x6f70               # gen=36 d0 1e  dis=36 d0 1e 90 90
    shlb   $1,%ss:-0x6f70               # gen=36 d0 26  dis=36 d0 26 90 90
    shrb   $1,%ss:-0x6f70               # gen=36 d0 2e  dis=36 d0 2e 90 90
    shlb   $1,%ss:-0x6f70               # gen=36 d0 36  dis=36 d0 36 90 90
    sarb   $1,%ss:-0x6f70               # gen=36 d0 3e  dis=36 d0 3e 90 90
    ss rol $1,%ax                       # gen=36 d1 c0  dis=36 d1 c0
    rolw   $1,%ss:-0x6f70               # gen=36 d1 06  dis=36 d1 06 90 90
    ss ror $1,%ax                       # gen=36 d1 c8  dis=36 d1 c8
    ss rcl $1,%ax                       # gen=36 d1 d0  dis=36 d1 d0
    ss rcr $1,%ax                       # gen=36 d1 d8  dis=36 d1 d8
    ss shl $1,%ax                       # gen=36 d1 e0  dis=36 d1 e0
    ss shr $1,%ax                       # gen=36 d1 e8  dis=36 d1 e8
    ss shl $1,%ax                       # gen=36 d1 f0  dis=36 d1 f0
    ss sar $1,%ax                       # gen=36 d1 f8  dis=36 d1 f8
    rorw   $1,%ss:-0x6f70               # gen=36 d1 0e  dis=36 d1 0e 90 90
    rclw   $1,%ss:-0x6f70               # gen=36 d1 16  dis=36 d1 16 90 90
    rcrw   $1,%ss:-0x6f70               # gen=36 d1 1e  dis=36 d1 1e 90 90
    shlw   $1,%ss:-0x6f70               # gen=36 d1 26  dis=36 d1 26 90 90
    shrw   $1,%ss:-0x6f70               # gen=36 d1 2e  dis=36 d1 2e 90 90
    shlw   $1,%ss:-0x6f70               # gen=36 d1 36  dis=36 d1 36 90 90
    sarw   $1,%ss:-0x6f70               # gen=36 d1 3e  dis=36 d1 3e 90 90
    ss rol %cl,%al                      # gen=36 d2 c0  dis=36 d2 c0
    rolb   %cl,%ss:-0x6f70              # gen=36 d2 06  dis=36 d2 06 90 90
    ss ror %cl,%al                      # gen=36 d2 c8  dis=36 d2 c8
    ss rcl %cl,%al                      # gen=36 d2 d0  dis=36 d2 d0
    ss rcr %cl,%al                      # gen=36 d2 d8  dis=36 d2 d8
    ss shl %cl,%al                      # gen=36 d2 e0  dis=36 d2 e0
    ss shr %cl,%al                      # gen=36 d2 e8  dis=36 d2 e8
    ss shl %cl,%al                      # gen=36 d2 f0  dis=36 d2 f0
    ss sar %cl,%al                      # gen=36 d2 f8  dis=36 d2 f8
    rorb   %cl,%ss:-0x6f70              # gen=36 d2 0e  dis=36 d2 0e 90 90
    rclb   %cl,%ss:-0x6f70              # gen=36 d2 16  dis=36 d2 16 90 90
    rcrb   %cl,%ss:-0x6f70              # gen=36 d2 1e  dis=36 d2 1e 90 90
    shlb   %cl,%ss:-0x6f70              # gen=36 d2 26  dis=36 d2 26 90 90
    shrb   %cl,%ss:-0x6f70              # gen=36 d2 2e  dis=36 d2 2e 90 90
    shlb   %cl,%ss:-0x6f70              # gen=36 d2 36  dis=36 d2 36 90 90
    sarb   %cl,%ss:-0x6f70              # gen=36 d2 3e  dis=36 d2 3e 90 90
    ss rol %cl,%ax                      # gen=36 d3 c0  dis=36 d3 c0
    rolw   %cl,%ss:-0x6f70              # gen=36 d3 06  dis=36 d3 06 90 90
    ss ror %cl,%ax                      # gen=36 d3 c8  dis=36 d3 c8
    ss rcl %cl,%ax                      # gen=36 d3 d0  dis=36 d3 d0
    ss rcr %cl,%ax                      # gen=36 d3 d8  dis=36 d3 d8
    ss shl %cl,%ax                      # gen=36 d3 e0  dis=36 d3 e0
    ss shr %cl,%ax                      # gen=36 d3 e8  dis=36 d3 e8
    ss shl %cl,%ax                      # gen=36 d3 f0  dis=36 d3 f0
    ss sar %cl,%ax                      # gen=36 d3 f8  dis=36 d3 f8
    rorw   %cl,%ss:-0x6f70              # gen=36 d3 0e  dis=36 d3 0e 90 90
    rclw   %cl,%ss:-0x6f70              # gen=36 d3 16  dis=36 d3 16 90 90
    rcrw   %cl,%ss:-0x6f70              # gen=36 d3 1e  dis=36 d3 1e 90 90
    shlw   %cl,%ss:-0x6f70              # gen=36 d3 26  dis=36 d3 26 90 90
    shrw   %cl,%ss:-0x6f70              # gen=36 d3 2e  dis=36 d3 2e 90 90
    shlw   %cl,%ss:-0x6f70              # gen=36 d3 36  dis=36 d3 36 90 90
    sarw   %cl,%ss:-0x6f70              # gen=36 d3 3e  dis=36 d3 3e 90 90
    ss aam $0xc0                        # gen=36 d4 c0  dis=36 d4 c0
    ss aam $0x6                         # gen=36 d4 06  dis=36 d4 06
    ss aad $0xc0                        # gen=36 d5 c0  dis=36 d5 c0
    ss aad $0x6                         # gen=36 d5 06  dis=36 d5 06
    ss salc                             # gen=36 d6 c0  dis=36 d6
    ss salc                             # gen=36 d6 06  dis=36 d6
    xlat   %ss:(%bx)                    # gen=36 d7 c0  dis=36 d7
    xlat   %ss:(%bx)                    # gen=36 d7 06  dis=36 d7
    .byte 0x36,0xd8,0xc0            # fallback; gen=36 d8 c0
    .byte 0x36,0xd8,0x06            # fallback; gen=36 d8 06
    .byte 0x36,0xd8,0xc8            # fallback; gen=36 d8 c8
    .byte 0x36,0xd8,0xd0            # fallback; gen=36 d8 d0
    .byte 0x36,0xd8,0xd8            # fallback; gen=36 d8 d8
    .byte 0x36,0xd8,0xe0            # fallback; gen=36 d8 e0
    .byte 0x36,0xd8,0xe8            # fallback; gen=36 d8 e8
    .byte 0x36,0xd8,0xf0            # fallback; gen=36 d8 f0
    .byte 0x36,0xd8,0xf8            # fallback; gen=36 d8 f8
    .byte 0x36,0xd8,0x0e            # fallback; gen=36 d8 0e
    .byte 0x36,0xd8,0x16            # fallback; gen=36 d8 16
    .byte 0x36,0xd8,0x1e            # fallback; gen=36 d8 1e
    .byte 0x36,0xd8,0x26            # fallback; gen=36 d8 26
    .byte 0x36,0xd8,0x2e            # fallback; gen=36 d8 2e
    .byte 0x36,0xd8,0x36            # fallback; gen=36 d8 36
    .byte 0x36,0xd8,0x3e            # fallback; gen=36 d8 3e
    .byte 0x36,0xd9,0xc0            # fallback; gen=36 d9 c0
    .byte 0x36,0xd9,0x06            # fallback; gen=36 d9 06
    .byte 0x36,0xd9,0xc8            # fallback; gen=36 d9 c8
    .byte 0x36,0xd9,0xd0            # fallback; gen=36 d9 d0
    .byte 0x36,0xd9,0xd8            # fallback; gen=36 d9 d8
    .byte 0x36,0xd9,0xe0            # fallback; gen=36 d9 e0
    .byte 0x36,0xd9,0xe8            # fallback; gen=36 d9 e8
    .byte 0x36,0xd9,0xf0            # fallback; gen=36 d9 f0
    .byte 0x36,0xd9,0xf8            # fallback; gen=36 d9 f8
    .byte 0x36,0xd9,0x0e            # fallback; gen=36 d9 0e
    .byte 0x36,0xd9,0x16            # fallback; gen=36 d9 16
    .byte 0x36,0xd9,0x1e            # fallback; gen=36 d9 1e
    .byte 0x36,0xd9,0x26            # fallback; gen=36 d9 26
    .byte 0x36,0xd9,0x2e            # fallback; gen=36 d9 2e
    .byte 0x36,0xd9,0x36            # fallback; gen=36 d9 36
    .byte 0x36,0xd9,0x3e            # fallback; gen=36 d9 3e
    .byte 0x36,0xda,0xc0            # fallback; gen=36 da c0
    .byte 0x36,0xda,0x06            # fallback; gen=36 da 06
    .byte 0x36,0xda,0xc8            # fallback; gen=36 da c8
    .byte 0x36,0xda,0xd0            # fallback; gen=36 da d0
    .byte 0x36,0xda,0xd8            # fallback; gen=36 da d8
    .byte 0x36,0xda,0xe0            # fallback; gen=36 da e0
    .byte 0x36,0xda,0xe8            # fallback; gen=36 da e8
    .byte 0x36,0xda,0xf0            # fallback; gen=36 da f0
    .byte 0x36,0xda,0xf8            # fallback; gen=36 da f8
    .byte 0x36,0xda,0x0e            # fallback; gen=36 da 0e
    .byte 0x36,0xda,0x16            # fallback; gen=36 da 16
    .byte 0x36,0xda,0x1e            # fallback; gen=36 da 1e
    .byte 0x36,0xda,0x26            # fallback; gen=36 da 26
    .byte 0x36,0xda,0x2e            # fallback; gen=36 da 2e
    .byte 0x36,0xda,0x36            # fallback; gen=36 da 36
    .byte 0x36,0xda,0x3e            # fallback; gen=36 da 3e
    .byte 0x36,0xdb,0xc0            # fallback; gen=36 db c0
    .byte 0x36,0xdb,0x06            # fallback; gen=36 db 06
    .byte 0x36,0xdb,0xc8            # fallback; gen=36 db c8
    .byte 0x36,0xdb,0xd0            # fallback; gen=36 db d0
    .byte 0x36,0xdb,0xd8            # fallback; gen=36 db d8
    .byte 0x36,0xdb,0xe0            # fallback; gen=36 db e0
    .byte 0x36,0xdb,0xe8            # fallback; gen=36 db e8
    .byte 0x36,0xdb,0xf0            # fallback; gen=36 db f0
    .byte 0x36,0xdb,0xf8            # fallback; gen=36 db f8
    .byte 0x36,0xdb,0x0e            # fallback; gen=36 db 0e
    .byte 0x36,0xdb,0x16            # fallback; gen=36 db 16
    .byte 0x36,0xdb,0x1e            # fallback; gen=36 db 1e
    .byte 0x36,0xdb,0x26            # fallback; gen=36 db 26
    .byte 0x36,0xdb,0x2e            # fallback; gen=36 db 2e
    .byte 0x36,0xdb,0x36            # fallback; gen=36 db 36
    .byte 0x36,0xdb,0x3e            # fallback; gen=36 db 3e
    .byte 0x36,0xdc,0xc0            # fallback; gen=36 dc c0
    .byte 0x36,0xdc,0x06            # fallback; gen=36 dc 06
    .byte 0x36,0xdc,0xc8            # fallback; gen=36 dc c8
    .byte 0x36,0xdc,0xd0            # fallback; gen=36 dc d0
    .byte 0x36,0xdc,0xd8            # fallback; gen=36 dc d8
    .byte 0x36,0xdc,0xe0            # fallback; gen=36 dc e0
    .byte 0x36,0xdc,0xe8            # fallback; gen=36 dc e8
    .byte 0x36,0xdc,0xf0            # fallback; gen=36 dc f0
    .byte 0x36,0xdc,0xf8            # fallback; gen=36 dc f8
    .byte 0x36,0xdc,0x0e            # fallback; gen=36 dc 0e
    .byte 0x36,0xdc,0x16            # fallback; gen=36 dc 16
    .byte 0x36,0xdc,0x1e            # fallback; gen=36 dc 1e
    .byte 0x36,0xdc,0x26            # fallback; gen=36 dc 26
    .byte 0x36,0xdc,0x2e            # fallback; gen=36 dc 2e
    .byte 0x36,0xdc,0x36            # fallback; gen=36 dc 36
    .byte 0x36,0xdc,0x3e            # fallback; gen=36 dc 3e
    .byte 0x36,0xdd,0xc0            # fallback; gen=36 dd c0
    .byte 0x36,0xdd,0x06            # fallback; gen=36 dd 06
    .byte 0x36,0xdd,0xc8            # fallback; gen=36 dd c8
    .byte 0x36,0xdd,0xd0            # fallback; gen=36 dd d0
    .byte 0x36,0xdd,0xd8            # fallback; gen=36 dd d8
    .byte 0x36,0xdd,0xe0            # fallback; gen=36 dd e0
    .byte 0x36,0xdd,0xe8            # fallback; gen=36 dd e8
    .byte 0x36,0xdd,0xf0            # fallback; gen=36 dd f0
    .byte 0x36,0xdd,0xf8            # fallback; gen=36 dd f8
    .byte 0x36,0xdd,0x0e            # fallback; gen=36 dd 0e
    .byte 0x36,0xdd,0x16            # fallback; gen=36 dd 16
    .byte 0x36,0xdd,0x1e            # fallback; gen=36 dd 1e
    .byte 0x36,0xdd,0x26            # fallback; gen=36 dd 26
    .byte 0x36,0xdd,0x2e            # fallback; gen=36 dd 2e
    .byte 0x36,0xdd,0x36            # fallback; gen=36 dd 36
    .byte 0x36,0xdd,0x3e            # fallback; gen=36 dd 3e
    .byte 0x36,0xde,0xc0            # fallback; gen=36 de c0
    .byte 0x36,0xde,0x06            # fallback; gen=36 de 06
    .byte 0x36,0xde,0xc8            # fallback; gen=36 de c8
    .byte 0x36,0xde,0xd0            # fallback; gen=36 de d0
    .byte 0x36,0xde,0xd8            # fallback; gen=36 de d8
    .byte 0x36,0xde,0xe0            # fallback; gen=36 de e0
    .byte 0x36,0xde,0xe8            # fallback; gen=36 de e8
    .byte 0x36,0xde,0xf0            # fallback; gen=36 de f0
    .byte 0x36,0xde,0xf8            # fallback; gen=36 de f8
    .byte 0x36,0xde,0x0e            # fallback; gen=36 de 0e
    .byte 0x36,0xde,0x16            # fallback; gen=36 de 16
    .byte 0x36,0xde,0x1e            # fallback; gen=36 de 1e
    .byte 0x36,0xde,0x26            # fallback; gen=36 de 26
    .byte 0x36,0xde,0x2e            # fallback; gen=36 de 2e
    .byte 0x36,0xde,0x36            # fallback; gen=36 de 36
    .byte 0x36,0xde,0x3e            # fallback; gen=36 de 3e
    .byte 0x36,0xdf,0xc0            # fallback; gen=36 df c0
    .byte 0x36,0xdf,0x06            # fallback; gen=36 df 06
    .byte 0x36,0xdf,0xc8            # fallback; gen=36 df c8
    .byte 0x36,0xdf,0xd0            # fallback; gen=36 df d0
    .byte 0x36,0xdf,0xd8            # fallback; gen=36 df d8
    .byte 0x36,0xdf,0xe0            # fallback; gen=36 df e0
    .byte 0x36,0xdf,0xe8            # fallback; gen=36 df e8
    .byte 0x36,0xdf,0xf0            # fallback; gen=36 df f0
    .byte 0x36,0xdf,0xf8            # fallback; gen=36 df f8
    .byte 0x36,0xdf,0x0e            # fallback; gen=36 df 0e
    .byte 0x36,0xdf,0x16            # fallback; gen=36 df 16
    .byte 0x36,0xdf,0x1e            # fallback; gen=36 df 1e
    .byte 0x36,0xdf,0x26            # fallback; gen=36 df 26
    .byte 0x36,0xdf,0x2e            # fallback; gen=36 df 2e
    .byte 0x36,0xdf,0x36            # fallback; gen=36 df 36
    .byte 0x36,0xdf,0x3e            # fallback; gen=36 df 3e
    .byte 0x36,0xe0,0xc0            # fallback; gen=36 e0 c0
    .byte 0x36,0xe0,0x06            # fallback; gen=36 e0 06
    .byte 0x36,0xe1,0xc0            # fallback; gen=36 e1 c0
    .byte 0x36,0xe1,0x06            # fallback; gen=36 e1 06
    .byte 0x36,0xe2,0xc0            # fallback; gen=36 e2 c0
    .byte 0x36,0xe2,0x06            # fallback; gen=36 e2 06
    .byte 0x36,0xe3,0xc0            # fallback; gen=36 e3 c0
    .byte 0x36,0xe3,0x06            # fallback; gen=36 e3 06
    ss in  $0xc0,%al                    # gen=36 e4 c0  dis=36 e4 c0
    ss in  $0x6,%al                     # gen=36 e4 06  dis=36 e4 06
    ss in  $0xc0,%ax                    # gen=36 e5 c0  dis=36 e5 c0
    ss in  $0x6,%ax                     # gen=36 e5 06  dis=36 e5 06
    ss out %al,$0xc0                    # gen=36 e6 c0  dis=36 e6 c0
    ss out %al,$0x6                     # gen=36 e6 06  dis=36 e6 06
    ss out %ax,$0xc0                    # gen=36 e7 c0  dis=36 e7 c0
    ss out %ax,$0x6                     # gen=36 e7 06  dis=36 e7 06
    .byte 0x36,0xe8,0xc0            # fallback; gen=36 e8 c0
    .byte 0x36,0xe8,0x06            # fallback; gen=36 e8 06
    .byte 0x36,0xe9,0xc0            # fallback; gen=36 e9 c0
    .byte 0x36,0xe9,0x06            # fallback; gen=36 e9 06
    .byte 0x36,0xea,0xc0            # fallback; gen=36 ea c0
    .byte 0x36,0xea,0x06            # fallback; gen=36 ea 06
    .byte 0x36,0xeb,0xc0            # fallback; gen=36 eb c0
    .byte 0x36,0xeb,0x06            # fallback; gen=36 eb 06
    ss in  (%dx),%al                    # gen=36 ec c0  dis=36 ec
    ss in  (%dx),%al                    # gen=36 ec 06  dis=36 ec
    ss in  (%dx),%ax                    # gen=36 ed c0  dis=36 ed
    ss in  (%dx),%ax                    # gen=36 ed 06  dis=36 ed
    ss out %al,(%dx)                    # gen=36 ee c0  dis=36 ee
    ss out %al,(%dx)                    # gen=36 ee 06  dis=36 ee
    ss out %ax,(%dx)                    # gen=36 ef c0  dis=36 ef
    ss out %ax,(%dx)                    # gen=36 ef 06  dis=36 ef
    .byte 0x36,0xf0,0xc0            # fallback; gen=36 f0 c0
    .byte 0x36,0xf0,0x06            # fallback; gen=36 f0 06
    .byte 0x36,0xf0,0xc8            # fallback; gen=36 f0 c8
    .byte 0x36,0xf0,0xd0            # fallback; gen=36 f0 d0
    .byte 0x36,0xf0,0xd8            # fallback; gen=36 f0 d8
    .byte 0x36,0xf0,0xe0            # fallback; gen=36 f0 e0
    .byte 0x36,0xf0,0xe8            # fallback; gen=36 f0 e8
    .byte 0x36,0xf0,0xf0            # fallback; gen=36 f0 f0
    .byte 0x36,0xf0,0xf8            # fallback; gen=36 f0 f8
    .byte 0x36,0xf0,0x0e            # fallback; gen=36 f0 0e
    .byte 0x36,0xf0,0x16            # fallback; gen=36 f0 16
    .byte 0x36,0xf0,0x1e            # fallback; gen=36 f0 1e
    .byte 0x36,0xf0,0x26            # fallback; gen=36 f0 26
    .byte 0x36,0xf0,0x2e            # fallback; gen=36 f0 2e
    .byte 0x36,0xf0,0x36            # fallback; gen=36 f0 36
    .byte 0x36,0xf0,0x3e            # fallback; gen=36 f0 3e
    ss int1                             # gen=36 f1 c0  dis=36 f1
    ss int1                             # gen=36 f1 06  dis=36 f1
    .byte 0x36,0xf2,0xc0            # fallback; gen=36 f2 c0
    .byte 0x36,0xf2,0x06            # fallback; gen=36 f2 06
    .byte 0x36,0xf2,0xc8            # fallback; gen=36 f2 c8
    .byte 0x36,0xf2,0xd0            # fallback; gen=36 f2 d0
    .byte 0x36,0xf2,0xd8            # fallback; gen=36 f2 d8
    .byte 0x36,0xf2,0xe0            # fallback; gen=36 f2 e0
    .byte 0x36,0xf2,0xe8            # fallback; gen=36 f2 e8
    .byte 0x36,0xf2,0xf0            # fallback; gen=36 f2 f0
    .byte 0x36,0xf2,0xf8            # fallback; gen=36 f2 f8
    .byte 0x36,0xf2,0x0e            # fallback; gen=36 f2 0e
    .byte 0x36,0xf2,0x16            # fallback; gen=36 f2 16
    .byte 0x36,0xf2,0x1e            # fallback; gen=36 f2 1e
    .byte 0x36,0xf2,0x26            # fallback; gen=36 f2 26
    .byte 0x36,0xf2,0x2e            # fallback; gen=36 f2 2e
    .byte 0x36,0xf2,0x36            # fallback; gen=36 f2 36
    .byte 0x36,0xf2,0x3e            # fallback; gen=36 f2 3e
    .byte 0x36,0xf3,0xc0            # fallback; gen=36 f3 c0
    .byte 0x36,0xf3,0x06            # fallback; gen=36 f3 06
    .byte 0x36,0xf3,0xc8            # fallback; gen=36 f3 c8
    .byte 0x36,0xf3,0xd0            # fallback; gen=36 f3 d0
    .byte 0x36,0xf3,0xd8            # fallback; gen=36 f3 d8
    .byte 0x36,0xf3,0xe0            # fallback; gen=36 f3 e0
    .byte 0x36,0xf3,0xe8            # fallback; gen=36 f3 e8
    .byte 0x36,0xf3,0xf0            # fallback; gen=36 f3 f0
    .byte 0x36,0xf3,0xf8            # fallback; gen=36 f3 f8
    .byte 0x36,0xf3,0x0e            # fallback; gen=36 f3 0e
    .byte 0x36,0xf3,0x16            # fallback; gen=36 f3 16
    .byte 0x36,0xf3,0x1e            # fallback; gen=36 f3 1e
    .byte 0x36,0xf3,0x26            # fallback; gen=36 f3 26
    .byte 0x36,0xf3,0x2e            # fallback; gen=36 f3 2e
    .byte 0x36,0xf3,0x36            # fallback; gen=36 f3 36
    .byte 0x36,0xf3,0x3e            # fallback; gen=36 f3 3e
    ss hlt                              # gen=36 f4 c0  dis=36 f4
    ss hlt                              # gen=36 f4 06  dis=36 f4
    ss cmc                              # gen=36 f5 c0  dis=36 f5
    ss cmc                              # gen=36 f5 06  dis=36 f5
    ss test $0x90,%al                   # gen=36 f6 c0  dis=36 f6 c0 90
    testb  $0x90,%ss:-0x6f70            # gen=36 f6 06  dis=36 f6 06 90 90 90
    ss test $0x90,%al                   # gen=36 f6 c8  dis=36 f6 c8 90
    ss not %al                          # gen=36 f6 d0  dis=36 f6 d0
    ss neg %al                          # gen=36 f6 d8  dis=36 f6 d8
    ss mul %al                          # gen=36 f6 e0  dis=36 f6 e0
    ss imul %al                         # gen=36 f6 e8  dis=36 f6 e8
    ss div %al                          # gen=36 f6 f0  dis=36 f6 f0
    ss idiv %al                         # gen=36 f6 f8  dis=36 f6 f8
    testb  $0x90,%ss:-0x6f70            # gen=36 f6 0e  dis=36 f6 0e 90 90 90
    notb   %ss:-0x6f70                  # gen=36 f6 16  dis=36 f6 16 90 90
    negb   %ss:-0x6f70                  # gen=36 f6 1e  dis=36 f6 1e 90 90
    mulb   %ss:-0x6f70                  # gen=36 f6 26  dis=36 f6 26 90 90
    imulb  %ss:-0x6f70                  # gen=36 f6 2e  dis=36 f6 2e 90 90
    divb   %ss:-0x6f70                  # gen=36 f6 36  dis=36 f6 36 90 90
    idivb  %ss:-0x6f70                  # gen=36 f6 3e  dis=36 f6 3e 90 90
    ss test $0x9090,%ax                 # gen=36 f7 c0  dis=36 f7 c0 90 90
    testw  $0x9090,%ss:-0x6f70          # gen=36 f7 06  dis=36 f7 06 90 90 90 90
    ss test $0x9090,%ax                 # gen=36 f7 c8  dis=36 f7 c8 90 90
    ss not %ax                          # gen=36 f7 d0  dis=36 f7 d0
    ss neg %ax                          # gen=36 f7 d8  dis=36 f7 d8
    ss mul %ax                          # gen=36 f7 e0  dis=36 f7 e0
    ss imul %ax                         # gen=36 f7 e8  dis=36 f7 e8
    ss div %ax                          # gen=36 f7 f0  dis=36 f7 f0
    ss idiv %ax                         # gen=36 f7 f8  dis=36 f7 f8
    testw  $0x9090,%ss:-0x6f70          # gen=36 f7 0e  dis=36 f7 0e 90 90 90 90
    notw   %ss:-0x6f70                  # gen=36 f7 16  dis=36 f7 16 90 90
    negw   %ss:-0x6f70                  # gen=36 f7 1e  dis=36 f7 1e 90 90
    mulw   %ss:-0x6f70                  # gen=36 f7 26  dis=36 f7 26 90 90
    imulw  %ss:-0x6f70                  # gen=36 f7 2e  dis=36 f7 2e 90 90
    divw   %ss:-0x6f70                  # gen=36 f7 36  dis=36 f7 36 90 90
    idivw  %ss:-0x6f70                  # gen=36 f7 3e  dis=36 f7 3e 90 90
    ss clc                              # gen=36 f8 c0  dis=36 f8
    ss clc                              # gen=36 f8 06  dis=36 f8
    ss stc                              # gen=36 f9 c0  dis=36 f9
    ss stc                              # gen=36 f9 06  dis=36 f9
    ss cli                              # gen=36 fa c0  dis=36 fa
    ss cli                              # gen=36 fa 06  dis=36 fa
    ss sti                              # gen=36 fb c0  dis=36 fb
    ss sti                              # gen=36 fb 06  dis=36 fb
    ss cld                              # gen=36 fc c0  dis=36 fc
    ss cld                              # gen=36 fc 06  dis=36 fc
    ss std                              # gen=36 fd c0  dis=36 fd
    ss std                              # gen=36 fd 06  dis=36 fd
    ss inc %al                          # gen=36 fe c0  dis=36 fe c0
    incb   %ss:-0x6f70                  # gen=36 fe 06  dis=36 fe 06 90 90
    ss dec %al                          # gen=36 fe c8  dis=36 fe c8
    .byte 0x36,0xfe,0xd0            # fallback; gen=36 fe d0
    .byte 0x36,0xfe,0xd8            # fallback; gen=36 fe d8
    .byte 0x36,0xfe,0xe0            # fallback; gen=36 fe e0
    .byte 0x36,0xfe,0xe8            # fallback; gen=36 fe e8
    .byte 0x36,0xfe,0xf0            # fallback; gen=36 fe f0
    .byte 0x36,0xfe,0xf8            # fallback; gen=36 fe f8
    decb   %ss:-0x6f70                  # gen=36 fe 0e  dis=36 fe 0e 90 90
    .byte 0x36,0xfe,0x16            # fallback; gen=36 fe 16
    .byte 0x36,0xfe,0x1e            # fallback; gen=36 fe 1e
    .byte 0x36,0xfe,0x26            # fallback; gen=36 fe 26
    .byte 0x36,0xfe,0x2e            # fallback; gen=36 fe 2e
    .byte 0x36,0xfe,0x36            # fallback; gen=36 fe 36
    .byte 0x36,0xfe,0x3e            # fallback; gen=36 fe 3e
    ss inc %ax                          # gen=36 ff c0  dis=36 ff c0
    incw   %ss:-0x6f70                  # gen=36 ff 06  dis=36 ff 06 90 90
    ss dec %ax                          # gen=36 ff c8  dis=36 ff c8
    ss call *%ax                        # gen=36 ff d0  dis=36 ff d0
    .byte 0x36,0xff,0xd8            # fallback; gen=36 ff d8
    ss jmp *%ax                         # gen=36 ff e0  dis=36 ff e0
    .byte 0x36,0xff,0xe8            # fallback; gen=36 ff e8
    ss push %ax                         # gen=36 ff f0  dis=36 ff f0
    .byte 0x36,0xff,0xf8            # fallback; gen=36 ff f8
    decw   %ss:-0x6f70                  # gen=36 ff 0e  dis=36 ff 0e 90 90
    call   *%ss:-0x6f70                 # gen=36 ff 16  dis=36 ff 16 90 90
    lcall  *%ss:-0x6f70                 # gen=36 ff 1e  dis=36 ff 1e 90 90
    jmp    *%ss:-0x6f70                 # gen=36 ff 26  dis=36 ff 26 90 90
    ljmp   *%ss:-0x6f70                 # gen=36 ff 2e  dis=36 ff 2e 90 90
    push   %ss:-0x6f70                  # gen=36 ff 36  dis=36 ff 36 90 90
    .byte 0x36,0xff,0x3e            # fallback; gen=36 ff 3e
    ds add %al,%al                      # gen=3e 00 c0  dis=3e 00 c0
    add    %al,%ds:-0x6f70              # gen=3e 00 06  dis=3e 00 06 90 90
    ds add %cl,%al                      # gen=3e 00 c8  dis=3e 00 c8
    ds add %dl,%al                      # gen=3e 00 d0  dis=3e 00 d0
    ds add %bl,%al                      # gen=3e 00 d8  dis=3e 00 d8
    ds add %ah,%al                      # gen=3e 00 e0  dis=3e 00 e0
    ds add %ch,%al                      # gen=3e 00 e8  dis=3e 00 e8
    ds add %dh,%al                      # gen=3e 00 f0  dis=3e 00 f0
    ds add %bh,%al                      # gen=3e 00 f8  dis=3e 00 f8
    add    %cl,%ds:-0x6f70              # gen=3e 00 0e  dis=3e 00 0e 90 90
    add    %dl,%ds:-0x6f70              # gen=3e 00 16  dis=3e 00 16 90 90
    add    %bl,%ds:-0x6f70              # gen=3e 00 1e  dis=3e 00 1e 90 90
    add    %ah,%ds:-0x6f70              # gen=3e 00 26  dis=3e 00 26 90 90
    add    %ch,%ds:-0x6f70              # gen=3e 00 2e  dis=3e 00 2e 90 90
    add    %dh,%ds:-0x6f70              # gen=3e 00 36  dis=3e 00 36 90 90
    add    %bh,%ds:-0x6f70              # gen=3e 00 3e  dis=3e 00 3e 90 90
    ds add %ax,%ax                      # gen=3e 01 c0  dis=3e 01 c0
    add    %ax,%ds:-0x6f70              # gen=3e 01 06  dis=3e 01 06 90 90
    ds add %cx,%ax                      # gen=3e 01 c8  dis=3e 01 c8
    ds add %dx,%ax                      # gen=3e 01 d0  dis=3e 01 d0
    ds add %bx,%ax                      # gen=3e 01 d8  dis=3e 01 d8
    ds add %sp,%ax                      # gen=3e 01 e0  dis=3e 01 e0
    ds add %bp,%ax                      # gen=3e 01 e8  dis=3e 01 e8
    ds add %si,%ax                      # gen=3e 01 f0  dis=3e 01 f0
    ds add %di,%ax                      # gen=3e 01 f8  dis=3e 01 f8
    add    %cx,%ds:-0x6f70              # gen=3e 01 0e  dis=3e 01 0e 90 90
    add    %dx,%ds:-0x6f70              # gen=3e 01 16  dis=3e 01 16 90 90
    add    %bx,%ds:-0x6f70              # gen=3e 01 1e  dis=3e 01 1e 90 90
    add    %sp,%ds:-0x6f70              # gen=3e 01 26  dis=3e 01 26 90 90
    add    %bp,%ds:-0x6f70              # gen=3e 01 2e  dis=3e 01 2e 90 90
    add    %si,%ds:-0x6f70              # gen=3e 01 36  dis=3e 01 36 90 90
    add    %di,%ds:-0x6f70              # gen=3e 01 3e  dis=3e 01 3e 90 90
    ds add %al,%al                      # gen=3e 02 c0  dis=3e 02 c0
    add    %ds:-0x6f70,%al              # gen=3e 02 06  dis=3e 02 06 90 90
    ds add %al,%cl                      # gen=3e 02 c8  dis=3e 02 c8
    ds add %al,%dl                      # gen=3e 02 d0  dis=3e 02 d0
    ds add %al,%bl                      # gen=3e 02 d8  dis=3e 02 d8
    ds add %al,%ah                      # gen=3e 02 e0  dis=3e 02 e0
    ds add %al,%ch                      # gen=3e 02 e8  dis=3e 02 e8
    ds add %al,%dh                      # gen=3e 02 f0  dis=3e 02 f0
    ds add %al,%bh                      # gen=3e 02 f8  dis=3e 02 f8
    add    %ds:-0x6f70,%cl              # gen=3e 02 0e  dis=3e 02 0e 90 90
    add    %ds:-0x6f70,%dl              # gen=3e 02 16  dis=3e 02 16 90 90
    add    %ds:-0x6f70,%bl              # gen=3e 02 1e  dis=3e 02 1e 90 90
    add    %ds:-0x6f70,%ah              # gen=3e 02 26  dis=3e 02 26 90 90
    add    %ds:-0x6f70,%ch              # gen=3e 02 2e  dis=3e 02 2e 90 90
    add    %ds:-0x6f70,%dh              # gen=3e 02 36  dis=3e 02 36 90 90
    add    %ds:-0x6f70,%bh              # gen=3e 02 3e  dis=3e 02 3e 90 90
    ds add %ax,%ax                      # gen=3e 03 c0  dis=3e 03 c0
    add    %ds:-0x6f70,%ax              # gen=3e 03 06  dis=3e 03 06 90 90
    ds add %ax,%cx                      # gen=3e 03 c8  dis=3e 03 c8
    ds add %ax,%dx                      # gen=3e 03 d0  dis=3e 03 d0
    ds add %ax,%bx                      # gen=3e 03 d8  dis=3e 03 d8
    ds add %ax,%sp                      # gen=3e 03 e0  dis=3e 03 e0
    ds add %ax,%bp                      # gen=3e 03 e8  dis=3e 03 e8
    ds add %ax,%si                      # gen=3e 03 f0  dis=3e 03 f0
    ds add %ax,%di                      # gen=3e 03 f8  dis=3e 03 f8
    add    %ds:-0x6f70,%cx              # gen=3e 03 0e  dis=3e 03 0e 90 90
    add    %ds:-0x6f70,%dx              # gen=3e 03 16  dis=3e 03 16 90 90
    add    %ds:-0x6f70,%bx              # gen=3e 03 1e  dis=3e 03 1e 90 90
    add    %ds:-0x6f70,%sp              # gen=3e 03 26  dis=3e 03 26 90 90
    add    %ds:-0x6f70,%bp              # gen=3e 03 2e  dis=3e 03 2e 90 90
    add    %ds:-0x6f70,%si              # gen=3e 03 36  dis=3e 03 36 90 90
    add    %ds:-0x6f70,%di              # gen=3e 03 3e  dis=3e 03 3e 90 90
    ds add $0xc0,%al                    # gen=3e 04 c0  dis=3e 04 c0
    ds add $0x6,%al                     # gen=3e 04 06  dis=3e 04 06
    ds add $0x90c0,%ax                  # gen=3e 05 c0  dis=3e 05 c0 90
    ds add $0x9006,%ax                  # gen=3e 05 06  dis=3e 05 06 90
    ds push %es                         # gen=3e 06 c0  dis=3e 06
    ds push %es                         # gen=3e 06 06  dis=3e 06
    ds pop %es                          # gen=3e 07 c0  dis=3e 07
    ds pop %es                          # gen=3e 07 06  dis=3e 07
    ds or  %al,%al                      # gen=3e 08 c0  dis=3e 08 c0
    or     %al,%ds:-0x6f70              # gen=3e 08 06  dis=3e 08 06 90 90
    ds or  %cl,%al                      # gen=3e 08 c8  dis=3e 08 c8
    ds or  %dl,%al                      # gen=3e 08 d0  dis=3e 08 d0
    ds or  %bl,%al                      # gen=3e 08 d8  dis=3e 08 d8
    ds or  %ah,%al                      # gen=3e 08 e0  dis=3e 08 e0
    ds or  %ch,%al                      # gen=3e 08 e8  dis=3e 08 e8
    ds or  %dh,%al                      # gen=3e 08 f0  dis=3e 08 f0
    ds or  %bh,%al                      # gen=3e 08 f8  dis=3e 08 f8
    or     %cl,%ds:-0x6f70              # gen=3e 08 0e  dis=3e 08 0e 90 90
    or     %dl,%ds:-0x6f70              # gen=3e 08 16  dis=3e 08 16 90 90
    or     %bl,%ds:-0x6f70              # gen=3e 08 1e  dis=3e 08 1e 90 90
    or     %ah,%ds:-0x6f70              # gen=3e 08 26  dis=3e 08 26 90 90
    or     %ch,%ds:-0x6f70              # gen=3e 08 2e  dis=3e 08 2e 90 90
    or     %dh,%ds:-0x6f70              # gen=3e 08 36  dis=3e 08 36 90 90
    or     %bh,%ds:-0x6f70              # gen=3e 08 3e  dis=3e 08 3e 90 90
    ds or  %ax,%ax                      # gen=3e 09 c0  dis=3e 09 c0
    or     %ax,%ds:-0x6f70              # gen=3e 09 06  dis=3e 09 06 90 90
    ds or  %cx,%ax                      # gen=3e 09 c8  dis=3e 09 c8
    ds or  %dx,%ax                      # gen=3e 09 d0  dis=3e 09 d0
    ds or  %bx,%ax                      # gen=3e 09 d8  dis=3e 09 d8
    ds or  %sp,%ax                      # gen=3e 09 e0  dis=3e 09 e0
    ds or  %bp,%ax                      # gen=3e 09 e8  dis=3e 09 e8
    ds or  %si,%ax                      # gen=3e 09 f0  dis=3e 09 f0
    ds or  %di,%ax                      # gen=3e 09 f8  dis=3e 09 f8
    or     %cx,%ds:-0x6f70              # gen=3e 09 0e  dis=3e 09 0e 90 90
    or     %dx,%ds:-0x6f70              # gen=3e 09 16  dis=3e 09 16 90 90
    or     %bx,%ds:-0x6f70              # gen=3e 09 1e  dis=3e 09 1e 90 90
    or     %sp,%ds:-0x6f70              # gen=3e 09 26  dis=3e 09 26 90 90
    or     %bp,%ds:-0x6f70              # gen=3e 09 2e  dis=3e 09 2e 90 90
    or     %si,%ds:-0x6f70              # gen=3e 09 36  dis=3e 09 36 90 90
    or     %di,%ds:-0x6f70              # gen=3e 09 3e  dis=3e 09 3e 90 90
    ds or  %al,%al                      # gen=3e 0a c0  dis=3e 0a c0
    or     %ds:-0x6f70,%al              # gen=3e 0a 06  dis=3e 0a 06 90 90
    ds or  %al,%cl                      # gen=3e 0a c8  dis=3e 0a c8
    ds or  %al,%dl                      # gen=3e 0a d0  dis=3e 0a d0
    ds or  %al,%bl                      # gen=3e 0a d8  dis=3e 0a d8
    ds or  %al,%ah                      # gen=3e 0a e0  dis=3e 0a e0
    ds or  %al,%ch                      # gen=3e 0a e8  dis=3e 0a e8
    ds or  %al,%dh                      # gen=3e 0a f0  dis=3e 0a f0
    ds or  %al,%bh                      # gen=3e 0a f8  dis=3e 0a f8
    or     %ds:-0x6f70,%cl              # gen=3e 0a 0e  dis=3e 0a 0e 90 90
    or     %ds:-0x6f70,%dl              # gen=3e 0a 16  dis=3e 0a 16 90 90
    or     %ds:-0x6f70,%bl              # gen=3e 0a 1e  dis=3e 0a 1e 90 90
    or     %ds:-0x6f70,%ah              # gen=3e 0a 26  dis=3e 0a 26 90 90
    or     %ds:-0x6f70,%ch              # gen=3e 0a 2e  dis=3e 0a 2e 90 90
    or     %ds:-0x6f70,%dh              # gen=3e 0a 36  dis=3e 0a 36 90 90
    or     %ds:-0x6f70,%bh              # gen=3e 0a 3e  dis=3e 0a 3e 90 90
    ds or  %ax,%ax                      # gen=3e 0b c0  dis=3e 0b c0
    or     %ds:-0x6f70,%ax              # gen=3e 0b 06  dis=3e 0b 06 90 90
    ds or  %ax,%cx                      # gen=3e 0b c8  dis=3e 0b c8
    ds or  %ax,%dx                      # gen=3e 0b d0  dis=3e 0b d0
    ds or  %ax,%bx                      # gen=3e 0b d8  dis=3e 0b d8
    ds or  %ax,%sp                      # gen=3e 0b e0  dis=3e 0b e0
    ds or  %ax,%bp                      # gen=3e 0b e8  dis=3e 0b e8
    ds or  %ax,%si                      # gen=3e 0b f0  dis=3e 0b f0
    ds or  %ax,%di                      # gen=3e 0b f8  dis=3e 0b f8
    or     %ds:-0x6f70,%cx              # gen=3e 0b 0e  dis=3e 0b 0e 90 90
    or     %ds:-0x6f70,%dx              # gen=3e 0b 16  dis=3e 0b 16 90 90
    or     %ds:-0x6f70,%bx              # gen=3e 0b 1e  dis=3e 0b 1e 90 90
    or     %ds:-0x6f70,%sp              # gen=3e 0b 26  dis=3e 0b 26 90 90
    or     %ds:-0x6f70,%bp              # gen=3e 0b 2e  dis=3e 0b 2e 90 90
    or     %ds:-0x6f70,%si              # gen=3e 0b 36  dis=3e 0b 36 90 90
    or     %ds:-0x6f70,%di              # gen=3e 0b 3e  dis=3e 0b 3e 90 90
    ds or  $0xc0,%al                    # gen=3e 0c c0  dis=3e 0c c0
    ds or  $0x6,%al                     # gen=3e 0c 06  dis=3e 0c 06
    ds or  $0x90c0,%ax                  # gen=3e 0d c0  dis=3e 0d c0 90
    ds or  $0x9006,%ax                  # gen=3e 0d 06  dis=3e 0d 06 90
    ds push %cs                         # gen=3e 0e c0  dis=3e 0e
    ds push %cs                         # gen=3e 0e 06  dis=3e 0e
    .byte 0x3e,0x0f,0xc0            # fallback; gen=3e 0f c0
    .byte 0x3e,0x0f,0x06            # fallback; gen=3e 0f 06
    .byte 0x3e,0x0f,0xc8            # fallback; gen=3e 0f c8
    .byte 0x3e,0x0f,0xd0            # fallback; gen=3e 0f d0
    .byte 0x3e,0x0f,0xd8            # fallback; gen=3e 0f d8
    .byte 0x3e,0x0f,0xe0            # fallback; gen=3e 0f e0
    .byte 0x3e,0x0f,0xe8            # fallback; gen=3e 0f e8
    .byte 0x3e,0x0f,0xf0            # fallback; gen=3e 0f f0
    .byte 0x3e,0x0f,0xf8            # fallback; gen=3e 0f f8
    .byte 0x3e,0x0f,0x0e            # fallback; gen=3e 0f 0e
    .byte 0x3e,0x0f,0x16            # fallback; gen=3e 0f 16
    .byte 0x3e,0x0f,0x1e            # fallback; gen=3e 0f 1e
    .byte 0x3e,0x0f,0x26            # fallback; gen=3e 0f 26
    .byte 0x3e,0x0f,0x2e            # fallback; gen=3e 0f 2e
    .byte 0x3e,0x0f,0x36            # fallback; gen=3e 0f 36
    .byte 0x3e,0x0f,0x3e            # fallback; gen=3e 0f 3e
    ds adc %al,%al                      # gen=3e 10 c0  dis=3e 10 c0
    adc    %al,%ds:-0x6f70              # gen=3e 10 06  dis=3e 10 06 90 90
    ds adc %cl,%al                      # gen=3e 10 c8  dis=3e 10 c8
    ds adc %dl,%al                      # gen=3e 10 d0  dis=3e 10 d0
    ds adc %bl,%al                      # gen=3e 10 d8  dis=3e 10 d8
    ds adc %ah,%al                      # gen=3e 10 e0  dis=3e 10 e0
    ds adc %ch,%al                      # gen=3e 10 e8  dis=3e 10 e8
    ds adc %dh,%al                      # gen=3e 10 f0  dis=3e 10 f0
    ds adc %bh,%al                      # gen=3e 10 f8  dis=3e 10 f8
    adc    %cl,%ds:-0x6f70              # gen=3e 10 0e  dis=3e 10 0e 90 90
    adc    %dl,%ds:-0x6f70              # gen=3e 10 16  dis=3e 10 16 90 90
    adc    %bl,%ds:-0x6f70              # gen=3e 10 1e  dis=3e 10 1e 90 90
    adc    %ah,%ds:-0x6f70              # gen=3e 10 26  dis=3e 10 26 90 90
    adc    %ch,%ds:-0x6f70              # gen=3e 10 2e  dis=3e 10 2e 90 90
    adc    %dh,%ds:-0x6f70              # gen=3e 10 36  dis=3e 10 36 90 90
    adc    %bh,%ds:-0x6f70              # gen=3e 10 3e  dis=3e 10 3e 90 90
    ds adc %ax,%ax                      # gen=3e 11 c0  dis=3e 11 c0
    adc    %ax,%ds:-0x6f70              # gen=3e 11 06  dis=3e 11 06 90 90
    ds adc %cx,%ax                      # gen=3e 11 c8  dis=3e 11 c8
    ds adc %dx,%ax                      # gen=3e 11 d0  dis=3e 11 d0
    ds adc %bx,%ax                      # gen=3e 11 d8  dis=3e 11 d8
    ds adc %sp,%ax                      # gen=3e 11 e0  dis=3e 11 e0
    ds adc %bp,%ax                      # gen=3e 11 e8  dis=3e 11 e8
    ds adc %si,%ax                      # gen=3e 11 f0  dis=3e 11 f0
    ds adc %di,%ax                      # gen=3e 11 f8  dis=3e 11 f8
    adc    %cx,%ds:-0x6f70              # gen=3e 11 0e  dis=3e 11 0e 90 90
    adc    %dx,%ds:-0x6f70              # gen=3e 11 16  dis=3e 11 16 90 90
    adc    %bx,%ds:-0x6f70              # gen=3e 11 1e  dis=3e 11 1e 90 90
    adc    %sp,%ds:-0x6f70              # gen=3e 11 26  dis=3e 11 26 90 90
    adc    %bp,%ds:-0x6f70              # gen=3e 11 2e  dis=3e 11 2e 90 90
    adc    %si,%ds:-0x6f70              # gen=3e 11 36  dis=3e 11 36 90 90
    adc    %di,%ds:-0x6f70              # gen=3e 11 3e  dis=3e 11 3e 90 90
    ds adc %al,%al                      # gen=3e 12 c0  dis=3e 12 c0
    adc    %ds:-0x6f70,%al              # gen=3e 12 06  dis=3e 12 06 90 90
    ds adc %al,%cl                      # gen=3e 12 c8  dis=3e 12 c8
    ds adc %al,%dl                      # gen=3e 12 d0  dis=3e 12 d0
    ds adc %al,%bl                      # gen=3e 12 d8  dis=3e 12 d8
    ds adc %al,%ah                      # gen=3e 12 e0  dis=3e 12 e0
    ds adc %al,%ch                      # gen=3e 12 e8  dis=3e 12 e8
    ds adc %al,%dh                      # gen=3e 12 f0  dis=3e 12 f0
    ds adc %al,%bh                      # gen=3e 12 f8  dis=3e 12 f8
    adc    %ds:-0x6f70,%cl              # gen=3e 12 0e  dis=3e 12 0e 90 90
    adc    %ds:-0x6f70,%dl              # gen=3e 12 16  dis=3e 12 16 90 90
    adc    %ds:-0x6f70,%bl              # gen=3e 12 1e  dis=3e 12 1e 90 90
    adc    %ds:-0x6f70,%ah              # gen=3e 12 26  dis=3e 12 26 90 90
    adc    %ds:-0x6f70,%ch              # gen=3e 12 2e  dis=3e 12 2e 90 90
    adc    %ds:-0x6f70,%dh              # gen=3e 12 36  dis=3e 12 36 90 90
    adc    %ds:-0x6f70,%bh              # gen=3e 12 3e  dis=3e 12 3e 90 90
    ds adc %ax,%ax                      # gen=3e 13 c0  dis=3e 13 c0
    adc    %ds:-0x6f70,%ax              # gen=3e 13 06  dis=3e 13 06 90 90
    ds adc %ax,%cx                      # gen=3e 13 c8  dis=3e 13 c8
    ds adc %ax,%dx                      # gen=3e 13 d0  dis=3e 13 d0
    ds adc %ax,%bx                      # gen=3e 13 d8  dis=3e 13 d8
    ds adc %ax,%sp                      # gen=3e 13 e0  dis=3e 13 e0
    ds adc %ax,%bp                      # gen=3e 13 e8  dis=3e 13 e8
    ds adc %ax,%si                      # gen=3e 13 f0  dis=3e 13 f0
    ds adc %ax,%di                      # gen=3e 13 f8  dis=3e 13 f8
    adc    %ds:-0x6f70,%cx              # gen=3e 13 0e  dis=3e 13 0e 90 90
    adc    %ds:-0x6f70,%dx              # gen=3e 13 16  dis=3e 13 16 90 90
    adc    %ds:-0x6f70,%bx              # gen=3e 13 1e  dis=3e 13 1e 90 90
    adc    %ds:-0x6f70,%sp              # gen=3e 13 26  dis=3e 13 26 90 90
    adc    %ds:-0x6f70,%bp              # gen=3e 13 2e  dis=3e 13 2e 90 90
    adc    %ds:-0x6f70,%si              # gen=3e 13 36  dis=3e 13 36 90 90
    adc    %ds:-0x6f70,%di              # gen=3e 13 3e  dis=3e 13 3e 90 90
    ds adc $0xc0,%al                    # gen=3e 14 c0  dis=3e 14 c0
    ds adc $0x6,%al                     # gen=3e 14 06  dis=3e 14 06
    ds adc $0x90c0,%ax                  # gen=3e 15 c0  dis=3e 15 c0 90
    ds adc $0x9006,%ax                  # gen=3e 15 06  dis=3e 15 06 90
    ds push %ss                         # gen=3e 16 c0  dis=3e 16
    ds push %ss                         # gen=3e 16 06  dis=3e 16
    ds pop %ss                          # gen=3e 17 c0  dis=3e 17
    ds pop %ss                          # gen=3e 17 06  dis=3e 17
    ds sbb %al,%al                      # gen=3e 18 c0  dis=3e 18 c0
    sbb    %al,%ds:-0x6f70              # gen=3e 18 06  dis=3e 18 06 90 90
    ds sbb %cl,%al                      # gen=3e 18 c8  dis=3e 18 c8
    ds sbb %dl,%al                      # gen=3e 18 d0  dis=3e 18 d0
    ds sbb %bl,%al                      # gen=3e 18 d8  dis=3e 18 d8
    ds sbb %ah,%al                      # gen=3e 18 e0  dis=3e 18 e0
    ds sbb %ch,%al                      # gen=3e 18 e8  dis=3e 18 e8
    ds sbb %dh,%al                      # gen=3e 18 f0  dis=3e 18 f0
    ds sbb %bh,%al                      # gen=3e 18 f8  dis=3e 18 f8
    sbb    %cl,%ds:-0x6f70              # gen=3e 18 0e  dis=3e 18 0e 90 90
    sbb    %dl,%ds:-0x6f70              # gen=3e 18 16  dis=3e 18 16 90 90
    sbb    %bl,%ds:-0x6f70              # gen=3e 18 1e  dis=3e 18 1e 90 90
    sbb    %ah,%ds:-0x6f70              # gen=3e 18 26  dis=3e 18 26 90 90
    sbb    %ch,%ds:-0x6f70              # gen=3e 18 2e  dis=3e 18 2e 90 90
    sbb    %dh,%ds:-0x6f70              # gen=3e 18 36  dis=3e 18 36 90 90
    sbb    %bh,%ds:-0x6f70              # gen=3e 18 3e  dis=3e 18 3e 90 90
    ds sbb %ax,%ax                      # gen=3e 19 c0  dis=3e 19 c0
    sbb    %ax,%ds:-0x6f70              # gen=3e 19 06  dis=3e 19 06 90 90
    ds sbb %cx,%ax                      # gen=3e 19 c8  dis=3e 19 c8
    ds sbb %dx,%ax                      # gen=3e 19 d0  dis=3e 19 d0
    ds sbb %bx,%ax                      # gen=3e 19 d8  dis=3e 19 d8
    ds sbb %sp,%ax                      # gen=3e 19 e0  dis=3e 19 e0
    ds sbb %bp,%ax                      # gen=3e 19 e8  dis=3e 19 e8
    ds sbb %si,%ax                      # gen=3e 19 f0  dis=3e 19 f0
    ds sbb %di,%ax                      # gen=3e 19 f8  dis=3e 19 f8
    sbb    %cx,%ds:-0x6f70              # gen=3e 19 0e  dis=3e 19 0e 90 90
    sbb    %dx,%ds:-0x6f70              # gen=3e 19 16  dis=3e 19 16 90 90
    sbb    %bx,%ds:-0x6f70              # gen=3e 19 1e  dis=3e 19 1e 90 90
    sbb    %sp,%ds:-0x6f70              # gen=3e 19 26  dis=3e 19 26 90 90
    sbb    %bp,%ds:-0x6f70              # gen=3e 19 2e  dis=3e 19 2e 90 90
    sbb    %si,%ds:-0x6f70              # gen=3e 19 36  dis=3e 19 36 90 90
    sbb    %di,%ds:-0x6f70              # gen=3e 19 3e  dis=3e 19 3e 90 90
    ds sbb %al,%al                      # gen=3e 1a c0  dis=3e 1a c0
    sbb    %ds:-0x6f70,%al              # gen=3e 1a 06  dis=3e 1a 06 90 90
    ds sbb %al,%cl                      # gen=3e 1a c8  dis=3e 1a c8
    ds sbb %al,%dl                      # gen=3e 1a d0  dis=3e 1a d0
    ds sbb %al,%bl                      # gen=3e 1a d8  dis=3e 1a d8
    ds sbb %al,%ah                      # gen=3e 1a e0  dis=3e 1a e0
    ds sbb %al,%ch                      # gen=3e 1a e8  dis=3e 1a e8
    ds sbb %al,%dh                      # gen=3e 1a f0  dis=3e 1a f0
    ds sbb %al,%bh                      # gen=3e 1a f8  dis=3e 1a f8
    sbb    %ds:-0x6f70,%cl              # gen=3e 1a 0e  dis=3e 1a 0e 90 90
    sbb    %ds:-0x6f70,%dl              # gen=3e 1a 16  dis=3e 1a 16 90 90
    sbb    %ds:-0x6f70,%bl              # gen=3e 1a 1e  dis=3e 1a 1e 90 90
    sbb    %ds:-0x6f70,%ah              # gen=3e 1a 26  dis=3e 1a 26 90 90
    sbb    %ds:-0x6f70,%ch              # gen=3e 1a 2e  dis=3e 1a 2e 90 90
    sbb    %ds:-0x6f70,%dh              # gen=3e 1a 36  dis=3e 1a 36 90 90
    sbb    %ds:-0x6f70,%bh              # gen=3e 1a 3e  dis=3e 1a 3e 90 90
    ds sbb %ax,%ax                      # gen=3e 1b c0  dis=3e 1b c0
    sbb    %ds:-0x6f70,%ax              # gen=3e 1b 06  dis=3e 1b 06 90 90
    ds sbb %ax,%cx                      # gen=3e 1b c8  dis=3e 1b c8
    ds sbb %ax,%dx                      # gen=3e 1b d0  dis=3e 1b d0
    ds sbb %ax,%bx                      # gen=3e 1b d8  dis=3e 1b d8
    ds sbb %ax,%sp                      # gen=3e 1b e0  dis=3e 1b e0
    ds sbb %ax,%bp                      # gen=3e 1b e8  dis=3e 1b e8
    ds sbb %ax,%si                      # gen=3e 1b f0  dis=3e 1b f0
    ds sbb %ax,%di                      # gen=3e 1b f8  dis=3e 1b f8
    sbb    %ds:-0x6f70,%cx              # gen=3e 1b 0e  dis=3e 1b 0e 90 90
    sbb    %ds:-0x6f70,%dx              # gen=3e 1b 16  dis=3e 1b 16 90 90
    sbb    %ds:-0x6f70,%bx              # gen=3e 1b 1e  dis=3e 1b 1e 90 90
    sbb    %ds:-0x6f70,%sp              # gen=3e 1b 26  dis=3e 1b 26 90 90
    sbb    %ds:-0x6f70,%bp              # gen=3e 1b 2e  dis=3e 1b 2e 90 90
    sbb    %ds:-0x6f70,%si              # gen=3e 1b 36  dis=3e 1b 36 90 90
    sbb    %ds:-0x6f70,%di              # gen=3e 1b 3e  dis=3e 1b 3e 90 90
    ds sbb $0xc0,%al                    # gen=3e 1c c0  dis=3e 1c c0
    ds sbb $0x6,%al                     # gen=3e 1c 06  dis=3e 1c 06
    ds sbb $0x90c0,%ax                  # gen=3e 1d c0  dis=3e 1d c0 90
    ds sbb $0x9006,%ax                  # gen=3e 1d 06  dis=3e 1d 06 90
    ds push %ds                         # gen=3e 1e c0  dis=3e 1e
    ds push %ds                         # gen=3e 1e 06  dis=3e 1e
    ds pop %ds                          # gen=3e 1f c0  dis=3e 1f
    ds pop %ds                          # gen=3e 1f 06  dis=3e 1f
    ds and %al,%al                      # gen=3e 20 c0  dis=3e 20 c0
    and    %al,%ds:-0x6f70              # gen=3e 20 06  dis=3e 20 06 90 90
    ds and %cl,%al                      # gen=3e 20 c8  dis=3e 20 c8
    ds and %dl,%al                      # gen=3e 20 d0  dis=3e 20 d0
    ds and %bl,%al                      # gen=3e 20 d8  dis=3e 20 d8
    ds and %ah,%al                      # gen=3e 20 e0  dis=3e 20 e0
    ds and %ch,%al                      # gen=3e 20 e8  dis=3e 20 e8
    ds and %dh,%al                      # gen=3e 20 f0  dis=3e 20 f0
    ds and %bh,%al                      # gen=3e 20 f8  dis=3e 20 f8
    and    %cl,%ds:-0x6f70              # gen=3e 20 0e  dis=3e 20 0e 90 90
    and    %dl,%ds:-0x6f70              # gen=3e 20 16  dis=3e 20 16 90 90
    and    %bl,%ds:-0x6f70              # gen=3e 20 1e  dis=3e 20 1e 90 90
    and    %ah,%ds:-0x6f70              # gen=3e 20 26  dis=3e 20 26 90 90
    and    %ch,%ds:-0x6f70              # gen=3e 20 2e  dis=3e 20 2e 90 90
    and    %dh,%ds:-0x6f70              # gen=3e 20 36  dis=3e 20 36 90 90
    and    %bh,%ds:-0x6f70              # gen=3e 20 3e  dis=3e 20 3e 90 90
    ds and %ax,%ax                      # gen=3e 21 c0  dis=3e 21 c0
    and    %ax,%ds:-0x6f70              # gen=3e 21 06  dis=3e 21 06 90 90
    ds and %cx,%ax                      # gen=3e 21 c8  dis=3e 21 c8
    ds and %dx,%ax                      # gen=3e 21 d0  dis=3e 21 d0
    ds and %bx,%ax                      # gen=3e 21 d8  dis=3e 21 d8
    ds and %sp,%ax                      # gen=3e 21 e0  dis=3e 21 e0
    ds and %bp,%ax                      # gen=3e 21 e8  dis=3e 21 e8
    ds and %si,%ax                      # gen=3e 21 f0  dis=3e 21 f0
    ds and %di,%ax                      # gen=3e 21 f8  dis=3e 21 f8
    and    %cx,%ds:-0x6f70              # gen=3e 21 0e  dis=3e 21 0e 90 90
    and    %dx,%ds:-0x6f70              # gen=3e 21 16  dis=3e 21 16 90 90
    and    %bx,%ds:-0x6f70              # gen=3e 21 1e  dis=3e 21 1e 90 90
    and    %sp,%ds:-0x6f70              # gen=3e 21 26  dis=3e 21 26 90 90
    and    %bp,%ds:-0x6f70              # gen=3e 21 2e  dis=3e 21 2e 90 90
    and    %si,%ds:-0x6f70              # gen=3e 21 36  dis=3e 21 36 90 90
    and    %di,%ds:-0x6f70              # gen=3e 21 3e  dis=3e 21 3e 90 90
    ds and %al,%al                      # gen=3e 22 c0  dis=3e 22 c0
    and    %ds:-0x6f70,%al              # gen=3e 22 06  dis=3e 22 06 90 90
    ds and %al,%cl                      # gen=3e 22 c8  dis=3e 22 c8
    ds and %al,%dl                      # gen=3e 22 d0  dis=3e 22 d0
    ds and %al,%bl                      # gen=3e 22 d8  dis=3e 22 d8
    ds and %al,%ah                      # gen=3e 22 e0  dis=3e 22 e0
    ds and %al,%ch                      # gen=3e 22 e8  dis=3e 22 e8
    ds and %al,%dh                      # gen=3e 22 f0  dis=3e 22 f0
    ds and %al,%bh                      # gen=3e 22 f8  dis=3e 22 f8
    and    %ds:-0x6f70,%cl              # gen=3e 22 0e  dis=3e 22 0e 90 90
    and    %ds:-0x6f70,%dl              # gen=3e 22 16  dis=3e 22 16 90 90
    and    %ds:-0x6f70,%bl              # gen=3e 22 1e  dis=3e 22 1e 90 90
    and    %ds:-0x6f70,%ah              # gen=3e 22 26  dis=3e 22 26 90 90
    and    %ds:-0x6f70,%ch              # gen=3e 22 2e  dis=3e 22 2e 90 90
    and    %ds:-0x6f70,%dh              # gen=3e 22 36  dis=3e 22 36 90 90
    and    %ds:-0x6f70,%bh              # gen=3e 22 3e  dis=3e 22 3e 90 90
    ds and %ax,%ax                      # gen=3e 23 c0  dis=3e 23 c0
    and    %ds:-0x6f70,%ax              # gen=3e 23 06  dis=3e 23 06 90 90
    ds and %ax,%cx                      # gen=3e 23 c8  dis=3e 23 c8
    ds and %ax,%dx                      # gen=3e 23 d0  dis=3e 23 d0
    ds and %ax,%bx                      # gen=3e 23 d8  dis=3e 23 d8
    ds and %ax,%sp                      # gen=3e 23 e0  dis=3e 23 e0
    ds and %ax,%bp                      # gen=3e 23 e8  dis=3e 23 e8
    ds and %ax,%si                      # gen=3e 23 f0  dis=3e 23 f0
    ds and %ax,%di                      # gen=3e 23 f8  dis=3e 23 f8
    and    %ds:-0x6f70,%cx              # gen=3e 23 0e  dis=3e 23 0e 90 90
    and    %ds:-0x6f70,%dx              # gen=3e 23 16  dis=3e 23 16 90 90
    and    %ds:-0x6f70,%bx              # gen=3e 23 1e  dis=3e 23 1e 90 90
    and    %ds:-0x6f70,%sp              # gen=3e 23 26  dis=3e 23 26 90 90
    and    %ds:-0x6f70,%bp              # gen=3e 23 2e  dis=3e 23 2e 90 90
    and    %ds:-0x6f70,%si              # gen=3e 23 36  dis=3e 23 36 90 90
    and    %ds:-0x6f70,%di              # gen=3e 23 3e  dis=3e 23 3e 90 90
    ds and $0xc0,%al                    # gen=3e 24 c0  dis=3e 24 c0
    ds and $0x6,%al                     # gen=3e 24 06  dis=3e 24 06
    ds and $0x90c0,%ax                  # gen=3e 25 c0  dis=3e 25 c0 90
    ds and $0x9006,%ax                  # gen=3e 25 06  dis=3e 25 06 90
    .byte 0x3e,0x26,0xc0            # fallback; gen=3e 26 c0
    .byte 0x3e,0x26,0x06            # fallback; gen=3e 26 06
    ds daa                              # gen=3e 27 c0  dis=3e 27
    ds daa                              # gen=3e 27 06  dis=3e 27
    ds sub %al,%al                      # gen=3e 28 c0  dis=3e 28 c0
    sub    %al,%ds:-0x6f70              # gen=3e 28 06  dis=3e 28 06 90 90
    ds sub %cl,%al                      # gen=3e 28 c8  dis=3e 28 c8
    ds sub %dl,%al                      # gen=3e 28 d0  dis=3e 28 d0
    ds sub %bl,%al                      # gen=3e 28 d8  dis=3e 28 d8
    ds sub %ah,%al                      # gen=3e 28 e0  dis=3e 28 e0
    ds sub %ch,%al                      # gen=3e 28 e8  dis=3e 28 e8
    ds sub %dh,%al                      # gen=3e 28 f0  dis=3e 28 f0
    ds sub %bh,%al                      # gen=3e 28 f8  dis=3e 28 f8
    sub    %cl,%ds:-0x6f70              # gen=3e 28 0e  dis=3e 28 0e 90 90
    sub    %dl,%ds:-0x6f70              # gen=3e 28 16  dis=3e 28 16 90 90
    sub    %bl,%ds:-0x6f70              # gen=3e 28 1e  dis=3e 28 1e 90 90
    sub    %ah,%ds:-0x6f70              # gen=3e 28 26  dis=3e 28 26 90 90
    sub    %ch,%ds:-0x6f70              # gen=3e 28 2e  dis=3e 28 2e 90 90
    sub    %dh,%ds:-0x6f70              # gen=3e 28 36  dis=3e 28 36 90 90
    sub    %bh,%ds:-0x6f70              # gen=3e 28 3e  dis=3e 28 3e 90 90
    ds sub %ax,%ax                      # gen=3e 29 c0  dis=3e 29 c0
    sub    %ax,%ds:-0x6f70              # gen=3e 29 06  dis=3e 29 06 90 90
    ds sub %cx,%ax                      # gen=3e 29 c8  dis=3e 29 c8
    ds sub %dx,%ax                      # gen=3e 29 d0  dis=3e 29 d0
    ds sub %bx,%ax                      # gen=3e 29 d8  dis=3e 29 d8
    ds sub %sp,%ax                      # gen=3e 29 e0  dis=3e 29 e0
    ds sub %bp,%ax                      # gen=3e 29 e8  dis=3e 29 e8
    ds sub %si,%ax                      # gen=3e 29 f0  dis=3e 29 f0
    ds sub %di,%ax                      # gen=3e 29 f8  dis=3e 29 f8
    sub    %cx,%ds:-0x6f70              # gen=3e 29 0e  dis=3e 29 0e 90 90
    sub    %dx,%ds:-0x6f70              # gen=3e 29 16  dis=3e 29 16 90 90
    sub    %bx,%ds:-0x6f70              # gen=3e 29 1e  dis=3e 29 1e 90 90
    sub    %sp,%ds:-0x6f70              # gen=3e 29 26  dis=3e 29 26 90 90
    sub    %bp,%ds:-0x6f70              # gen=3e 29 2e  dis=3e 29 2e 90 90
    sub    %si,%ds:-0x6f70              # gen=3e 29 36  dis=3e 29 36 90 90
    sub    %di,%ds:-0x6f70              # gen=3e 29 3e  dis=3e 29 3e 90 90
    ds sub %al,%al                      # gen=3e 2a c0  dis=3e 2a c0
    sub    %ds:-0x6f70,%al              # gen=3e 2a 06  dis=3e 2a 06 90 90
    ds sub %al,%cl                      # gen=3e 2a c8  dis=3e 2a c8
    ds sub %al,%dl                      # gen=3e 2a d0  dis=3e 2a d0
    ds sub %al,%bl                      # gen=3e 2a d8  dis=3e 2a d8
    ds sub %al,%ah                      # gen=3e 2a e0  dis=3e 2a e0
    ds sub %al,%ch                      # gen=3e 2a e8  dis=3e 2a e8
    ds sub %al,%dh                      # gen=3e 2a f0  dis=3e 2a f0
    ds sub %al,%bh                      # gen=3e 2a f8  dis=3e 2a f8
    sub    %ds:-0x6f70,%cl              # gen=3e 2a 0e  dis=3e 2a 0e 90 90
    sub    %ds:-0x6f70,%dl              # gen=3e 2a 16  dis=3e 2a 16 90 90
    sub    %ds:-0x6f70,%bl              # gen=3e 2a 1e  dis=3e 2a 1e 90 90
    sub    %ds:-0x6f70,%ah              # gen=3e 2a 26  dis=3e 2a 26 90 90
    sub    %ds:-0x6f70,%ch              # gen=3e 2a 2e  dis=3e 2a 2e 90 90
    sub    %ds:-0x6f70,%dh              # gen=3e 2a 36  dis=3e 2a 36 90 90
    sub    %ds:-0x6f70,%bh              # gen=3e 2a 3e  dis=3e 2a 3e 90 90
    ds sub %ax,%ax                      # gen=3e 2b c0  dis=3e 2b c0
    sub    %ds:-0x6f70,%ax              # gen=3e 2b 06  dis=3e 2b 06 90 90
    ds sub %ax,%cx                      # gen=3e 2b c8  dis=3e 2b c8
    ds sub %ax,%dx                      # gen=3e 2b d0  dis=3e 2b d0
    ds sub %ax,%bx                      # gen=3e 2b d8  dis=3e 2b d8
    ds sub %ax,%sp                      # gen=3e 2b e0  dis=3e 2b e0
    ds sub %ax,%bp                      # gen=3e 2b e8  dis=3e 2b e8
    ds sub %ax,%si                      # gen=3e 2b f0  dis=3e 2b f0
    ds sub %ax,%di                      # gen=3e 2b f8  dis=3e 2b f8
    sub    %ds:-0x6f70,%cx              # gen=3e 2b 0e  dis=3e 2b 0e 90 90
    sub    %ds:-0x6f70,%dx              # gen=3e 2b 16  dis=3e 2b 16 90 90
    sub    %ds:-0x6f70,%bx              # gen=3e 2b 1e  dis=3e 2b 1e 90 90
    sub    %ds:-0x6f70,%sp              # gen=3e 2b 26  dis=3e 2b 26 90 90
    sub    %ds:-0x6f70,%bp              # gen=3e 2b 2e  dis=3e 2b 2e 90 90
    sub    %ds:-0x6f70,%si              # gen=3e 2b 36  dis=3e 2b 36 90 90
    sub    %ds:-0x6f70,%di              # gen=3e 2b 3e  dis=3e 2b 3e 90 90
    ds sub $0xc0,%al                    # gen=3e 2c c0  dis=3e 2c c0
    ds sub $0x6,%al                     # gen=3e 2c 06  dis=3e 2c 06
    ds sub $0x90c0,%ax                  # gen=3e 2d c0  dis=3e 2d c0 90
    ds sub $0x9006,%ax                  # gen=3e 2d 06  dis=3e 2d 06 90
    .byte 0x3e,0x2e,0xc0            # fallback; gen=3e 2e c0
    .byte 0x3e,0x2e,0x06            # fallback; gen=3e 2e 06
    ds das                              # gen=3e 2f c0  dis=3e 2f
    ds das                              # gen=3e 2f 06  dis=3e 2f
    ds xor %al,%al                      # gen=3e 30 c0  dis=3e 30 c0
    xor    %al,%ds:-0x6f70              # gen=3e 30 06  dis=3e 30 06 90 90
    ds xor %cl,%al                      # gen=3e 30 c8  dis=3e 30 c8
    ds xor %dl,%al                      # gen=3e 30 d0  dis=3e 30 d0
    ds xor %bl,%al                      # gen=3e 30 d8  dis=3e 30 d8
    ds xor %ah,%al                      # gen=3e 30 e0  dis=3e 30 e0
    ds xor %ch,%al                      # gen=3e 30 e8  dis=3e 30 e8
    ds xor %dh,%al                      # gen=3e 30 f0  dis=3e 30 f0
    ds xor %bh,%al                      # gen=3e 30 f8  dis=3e 30 f8
    xor    %cl,%ds:-0x6f70              # gen=3e 30 0e  dis=3e 30 0e 90 90
    xor    %dl,%ds:-0x6f70              # gen=3e 30 16  dis=3e 30 16 90 90
    xor    %bl,%ds:-0x6f70              # gen=3e 30 1e  dis=3e 30 1e 90 90
    xor    %ah,%ds:-0x6f70              # gen=3e 30 26  dis=3e 30 26 90 90
    xor    %ch,%ds:-0x6f70              # gen=3e 30 2e  dis=3e 30 2e 90 90
    xor    %dh,%ds:-0x6f70              # gen=3e 30 36  dis=3e 30 36 90 90
    xor    %bh,%ds:-0x6f70              # gen=3e 30 3e  dis=3e 30 3e 90 90
    ds xor %ax,%ax                      # gen=3e 31 c0  dis=3e 31 c0
    xor    %ax,%ds:-0x6f70              # gen=3e 31 06  dis=3e 31 06 90 90
    ds xor %cx,%ax                      # gen=3e 31 c8  dis=3e 31 c8
    ds xor %dx,%ax                      # gen=3e 31 d0  dis=3e 31 d0
    ds xor %bx,%ax                      # gen=3e 31 d8  dis=3e 31 d8
    ds xor %sp,%ax                      # gen=3e 31 e0  dis=3e 31 e0
    ds xor %bp,%ax                      # gen=3e 31 e8  dis=3e 31 e8
    ds xor %si,%ax                      # gen=3e 31 f0  dis=3e 31 f0
    ds xor %di,%ax                      # gen=3e 31 f8  dis=3e 31 f8
    xor    %cx,%ds:-0x6f70              # gen=3e 31 0e  dis=3e 31 0e 90 90
    xor    %dx,%ds:-0x6f70              # gen=3e 31 16  dis=3e 31 16 90 90
    xor    %bx,%ds:-0x6f70              # gen=3e 31 1e  dis=3e 31 1e 90 90
    xor    %sp,%ds:-0x6f70              # gen=3e 31 26  dis=3e 31 26 90 90
    xor    %bp,%ds:-0x6f70              # gen=3e 31 2e  dis=3e 31 2e 90 90
    xor    %si,%ds:-0x6f70              # gen=3e 31 36  dis=3e 31 36 90 90
    xor    %di,%ds:-0x6f70              # gen=3e 31 3e  dis=3e 31 3e 90 90
    ds xor %al,%al                      # gen=3e 32 c0  dis=3e 32 c0
    xor    %ds:-0x6f70,%al              # gen=3e 32 06  dis=3e 32 06 90 90
    ds xor %al,%cl                      # gen=3e 32 c8  dis=3e 32 c8
    ds xor %al,%dl                      # gen=3e 32 d0  dis=3e 32 d0
    ds xor %al,%bl                      # gen=3e 32 d8  dis=3e 32 d8
    ds xor %al,%ah                      # gen=3e 32 e0  dis=3e 32 e0
    ds xor %al,%ch                      # gen=3e 32 e8  dis=3e 32 e8
    ds xor %al,%dh                      # gen=3e 32 f0  dis=3e 32 f0
    ds xor %al,%bh                      # gen=3e 32 f8  dis=3e 32 f8
    xor    %ds:-0x6f70,%cl              # gen=3e 32 0e  dis=3e 32 0e 90 90
    xor    %ds:-0x6f70,%dl              # gen=3e 32 16  dis=3e 32 16 90 90
    xor    %ds:-0x6f70,%bl              # gen=3e 32 1e  dis=3e 32 1e 90 90
    xor    %ds:-0x6f70,%ah              # gen=3e 32 26  dis=3e 32 26 90 90
    xor    %ds:-0x6f70,%ch              # gen=3e 32 2e  dis=3e 32 2e 90 90
    xor    %ds:-0x6f70,%dh              # gen=3e 32 36  dis=3e 32 36 90 90
    xor    %ds:-0x6f70,%bh              # gen=3e 32 3e  dis=3e 32 3e 90 90
    ds xor %ax,%ax                      # gen=3e 33 c0  dis=3e 33 c0
    xor    %ds:-0x6f70,%ax              # gen=3e 33 06  dis=3e 33 06 90 90
    ds xor %ax,%cx                      # gen=3e 33 c8  dis=3e 33 c8
    ds xor %ax,%dx                      # gen=3e 33 d0  dis=3e 33 d0
    ds xor %ax,%bx                      # gen=3e 33 d8  dis=3e 33 d8
    ds xor %ax,%sp                      # gen=3e 33 e0  dis=3e 33 e0
    ds xor %ax,%bp                      # gen=3e 33 e8  dis=3e 33 e8
    ds xor %ax,%si                      # gen=3e 33 f0  dis=3e 33 f0
    ds xor %ax,%di                      # gen=3e 33 f8  dis=3e 33 f8
    xor    %ds:-0x6f70,%cx              # gen=3e 33 0e  dis=3e 33 0e 90 90
    xor    %ds:-0x6f70,%dx              # gen=3e 33 16  dis=3e 33 16 90 90
    xor    %ds:-0x6f70,%bx              # gen=3e 33 1e  dis=3e 33 1e 90 90
    xor    %ds:-0x6f70,%sp              # gen=3e 33 26  dis=3e 33 26 90 90
    xor    %ds:-0x6f70,%bp              # gen=3e 33 2e  dis=3e 33 2e 90 90
    xor    %ds:-0x6f70,%si              # gen=3e 33 36  dis=3e 33 36 90 90
    xor    %ds:-0x6f70,%di              # gen=3e 33 3e  dis=3e 33 3e 90 90
    ds xor $0xc0,%al                    # gen=3e 34 c0  dis=3e 34 c0
    ds xor $0x6,%al                     # gen=3e 34 06  dis=3e 34 06
    ds xor $0x90c0,%ax                  # gen=3e 35 c0  dis=3e 35 c0 90
    ds xor $0x9006,%ax                  # gen=3e 35 06  dis=3e 35 06 90
    .byte 0x3e,0x36,0xc0            # fallback; gen=3e 36 c0
    .byte 0x3e,0x36,0x06            # fallback; gen=3e 36 06
    ds aaa                              # gen=3e 37 c0  dis=3e 37
    ds aaa                              # gen=3e 37 06  dis=3e 37
    ds cmp %al,%al                      # gen=3e 38 c0  dis=3e 38 c0
    cmp    %al,%ds:-0x6f70              # gen=3e 38 06  dis=3e 38 06 90 90
    ds cmp %cl,%al                      # gen=3e 38 c8  dis=3e 38 c8
    ds cmp %dl,%al                      # gen=3e 38 d0  dis=3e 38 d0
    ds cmp %bl,%al                      # gen=3e 38 d8  dis=3e 38 d8
    ds cmp %ah,%al                      # gen=3e 38 e0  dis=3e 38 e0
    ds cmp %ch,%al                      # gen=3e 38 e8  dis=3e 38 e8
    ds cmp %dh,%al                      # gen=3e 38 f0  dis=3e 38 f0
    ds cmp %bh,%al                      # gen=3e 38 f8  dis=3e 38 f8
    cmp    %cl,%ds:-0x6f70              # gen=3e 38 0e  dis=3e 38 0e 90 90
    cmp    %dl,%ds:-0x6f70              # gen=3e 38 16  dis=3e 38 16 90 90
    cmp    %bl,%ds:-0x6f70              # gen=3e 38 1e  dis=3e 38 1e 90 90
    cmp    %ah,%ds:-0x6f70              # gen=3e 38 26  dis=3e 38 26 90 90
    cmp    %ch,%ds:-0x6f70              # gen=3e 38 2e  dis=3e 38 2e 90 90
    cmp    %dh,%ds:-0x6f70              # gen=3e 38 36  dis=3e 38 36 90 90
    cmp    %bh,%ds:-0x6f70              # gen=3e 38 3e  dis=3e 38 3e 90 90
    ds cmp %ax,%ax                      # gen=3e 39 c0  dis=3e 39 c0
    cmp    %ax,%ds:-0x6f70              # gen=3e 39 06  dis=3e 39 06 90 90
    ds cmp %cx,%ax                      # gen=3e 39 c8  dis=3e 39 c8
    ds cmp %dx,%ax                      # gen=3e 39 d0  dis=3e 39 d0
    ds cmp %bx,%ax                      # gen=3e 39 d8  dis=3e 39 d8
    ds cmp %sp,%ax                      # gen=3e 39 e0  dis=3e 39 e0
    ds cmp %bp,%ax                      # gen=3e 39 e8  dis=3e 39 e8
    ds cmp %si,%ax                      # gen=3e 39 f0  dis=3e 39 f0
    ds cmp %di,%ax                      # gen=3e 39 f8  dis=3e 39 f8
    cmp    %cx,%ds:-0x6f70              # gen=3e 39 0e  dis=3e 39 0e 90 90
    cmp    %dx,%ds:-0x6f70              # gen=3e 39 16  dis=3e 39 16 90 90
    cmp    %bx,%ds:-0x6f70              # gen=3e 39 1e  dis=3e 39 1e 90 90
    cmp    %sp,%ds:-0x6f70              # gen=3e 39 26  dis=3e 39 26 90 90
    cmp    %bp,%ds:-0x6f70              # gen=3e 39 2e  dis=3e 39 2e 90 90
    cmp    %si,%ds:-0x6f70              # gen=3e 39 36  dis=3e 39 36 90 90
    cmp    %di,%ds:-0x6f70              # gen=3e 39 3e  dis=3e 39 3e 90 90
    ds cmp %al,%al                      # gen=3e 3a c0  dis=3e 3a c0
    cmp    %ds:-0x6f70,%al              # gen=3e 3a 06  dis=3e 3a 06 90 90
    ds cmp %al,%cl                      # gen=3e 3a c8  dis=3e 3a c8
    ds cmp %al,%dl                      # gen=3e 3a d0  dis=3e 3a d0
    ds cmp %al,%bl                      # gen=3e 3a d8  dis=3e 3a d8
    ds cmp %al,%ah                      # gen=3e 3a e0  dis=3e 3a e0
    ds cmp %al,%ch                      # gen=3e 3a e8  dis=3e 3a e8
    ds cmp %al,%dh                      # gen=3e 3a f0  dis=3e 3a f0
    ds cmp %al,%bh                      # gen=3e 3a f8  dis=3e 3a f8
    cmp    %ds:-0x6f70,%cl              # gen=3e 3a 0e  dis=3e 3a 0e 90 90
    cmp    %ds:-0x6f70,%dl              # gen=3e 3a 16  dis=3e 3a 16 90 90
    cmp    %ds:-0x6f70,%bl              # gen=3e 3a 1e  dis=3e 3a 1e 90 90
    cmp    %ds:-0x6f70,%ah              # gen=3e 3a 26  dis=3e 3a 26 90 90
    cmp    %ds:-0x6f70,%ch              # gen=3e 3a 2e  dis=3e 3a 2e 90 90
    cmp    %ds:-0x6f70,%dh              # gen=3e 3a 36  dis=3e 3a 36 90 90
    cmp    %ds:-0x6f70,%bh              # gen=3e 3a 3e  dis=3e 3a 3e 90 90
    ds cmp %ax,%ax                      # gen=3e 3b c0  dis=3e 3b c0
    cmp    %ds:-0x6f70,%ax              # gen=3e 3b 06  dis=3e 3b 06 90 90
    ds cmp %ax,%cx                      # gen=3e 3b c8  dis=3e 3b c8
    ds cmp %ax,%dx                      # gen=3e 3b d0  dis=3e 3b d0
    ds cmp %ax,%bx                      # gen=3e 3b d8  dis=3e 3b d8
    ds cmp %ax,%sp                      # gen=3e 3b e0  dis=3e 3b e0
    ds cmp %ax,%bp                      # gen=3e 3b e8  dis=3e 3b e8
    ds cmp %ax,%si                      # gen=3e 3b f0  dis=3e 3b f0
    ds cmp %ax,%di                      # gen=3e 3b f8  dis=3e 3b f8
    cmp    %ds:-0x6f70,%cx              # gen=3e 3b 0e  dis=3e 3b 0e 90 90
    cmp    %ds:-0x6f70,%dx              # gen=3e 3b 16  dis=3e 3b 16 90 90
    cmp    %ds:-0x6f70,%bx              # gen=3e 3b 1e  dis=3e 3b 1e 90 90
    cmp    %ds:-0x6f70,%sp              # gen=3e 3b 26  dis=3e 3b 26 90 90
    cmp    %ds:-0x6f70,%bp              # gen=3e 3b 2e  dis=3e 3b 2e 90 90
    cmp    %ds:-0x6f70,%si              # gen=3e 3b 36  dis=3e 3b 36 90 90
    cmp    %ds:-0x6f70,%di              # gen=3e 3b 3e  dis=3e 3b 3e 90 90
    ds cmp $0xc0,%al                    # gen=3e 3c c0  dis=3e 3c c0
    ds cmp $0x6,%al                     # gen=3e 3c 06  dis=3e 3c 06
    ds cmp $0x90c0,%ax                  # gen=3e 3d c0  dis=3e 3d c0 90
    ds cmp $0x9006,%ax                  # gen=3e 3d 06  dis=3e 3d 06 90
    .byte 0x3e,0x3e,0xc0            # fallback; gen=3e 3e c0
    .byte 0x3e,0x3e,0x06            # fallback; gen=3e 3e 06
    ds aas                              # gen=3e 3f c0  dis=3e 3f
    ds aas                              # gen=3e 3f 06  dis=3e 3f
    ds inc %ax                          # gen=3e 40 c0  dis=3e 40
    ds inc %ax                          # gen=3e 40 06  dis=3e 40
    ds inc %cx                          # gen=3e 41 c0  dis=3e 41
    ds inc %cx                          # gen=3e 41 06  dis=3e 41
    ds inc %dx                          # gen=3e 42 c0  dis=3e 42
    ds inc %dx                          # gen=3e 42 06  dis=3e 42
    ds inc %bx                          # gen=3e 43 c0  dis=3e 43
    ds inc %bx                          # gen=3e 43 06  dis=3e 43
    ds inc %sp                          # gen=3e 44 c0  dis=3e 44
    ds inc %sp                          # gen=3e 44 06  dis=3e 44
    ds inc %bp                          # gen=3e 45 c0  dis=3e 45
    ds inc %bp                          # gen=3e 45 06  dis=3e 45
    ds inc %si                          # gen=3e 46 c0  dis=3e 46
    ds inc %si                          # gen=3e 46 06  dis=3e 46
    ds inc %di                          # gen=3e 47 c0  dis=3e 47
    ds inc %di                          # gen=3e 47 06  dis=3e 47
    ds dec %ax                          # gen=3e 48 c0  dis=3e 48
    ds dec %ax                          # gen=3e 48 06  dis=3e 48
    ds dec %cx                          # gen=3e 49 c0  dis=3e 49
    ds dec %cx                          # gen=3e 49 06  dis=3e 49
    ds dec %dx                          # gen=3e 4a c0  dis=3e 4a
    ds dec %dx                          # gen=3e 4a 06  dis=3e 4a
    ds dec %bx                          # gen=3e 4b c0  dis=3e 4b
    ds dec %bx                          # gen=3e 4b 06  dis=3e 4b
    ds dec %sp                          # gen=3e 4c c0  dis=3e 4c
    ds dec %sp                          # gen=3e 4c 06  dis=3e 4c
    ds dec %bp                          # gen=3e 4d c0  dis=3e 4d
    ds dec %bp                          # gen=3e 4d 06  dis=3e 4d
    ds dec %si                          # gen=3e 4e c0  dis=3e 4e
    ds dec %si                          # gen=3e 4e 06  dis=3e 4e
    ds dec %di                          # gen=3e 4f c0  dis=3e 4f
    ds dec %di                          # gen=3e 4f 06  dis=3e 4f
    ds push %ax                         # gen=3e 50 c0  dis=3e 50
    ds push %ax                         # gen=3e 50 06  dis=3e 50
    ds push %cx                         # gen=3e 51 c0  dis=3e 51
    ds push %cx                         # gen=3e 51 06  dis=3e 51
    ds push %dx                         # gen=3e 52 c0  dis=3e 52
    ds push %dx                         # gen=3e 52 06  dis=3e 52
    ds push %bx                         # gen=3e 53 c0  dis=3e 53
    ds push %bx                         # gen=3e 53 06  dis=3e 53
    ds push %sp                         # gen=3e 54 c0  dis=3e 54
    ds push %sp                         # gen=3e 54 06  dis=3e 54
    ds push %bp                         # gen=3e 55 c0  dis=3e 55
    ds push %bp                         # gen=3e 55 06  dis=3e 55
    ds push %si                         # gen=3e 56 c0  dis=3e 56
    ds push %si                         # gen=3e 56 06  dis=3e 56
    ds push %di                         # gen=3e 57 c0  dis=3e 57
    ds push %di                         # gen=3e 57 06  dis=3e 57
    ds pop %ax                          # gen=3e 58 c0  dis=3e 58
    ds pop %ax                          # gen=3e 58 06  dis=3e 58
    ds pop %cx                          # gen=3e 59 c0  dis=3e 59
    ds pop %cx                          # gen=3e 59 06  dis=3e 59
    ds pop %dx                          # gen=3e 5a c0  dis=3e 5a
    ds pop %dx                          # gen=3e 5a 06  dis=3e 5a
    ds pop %bx                          # gen=3e 5b c0  dis=3e 5b
    ds pop %bx                          # gen=3e 5b 06  dis=3e 5b
    ds pop %sp                          # gen=3e 5c c0  dis=3e 5c
    ds pop %sp                          # gen=3e 5c 06  dis=3e 5c
    ds pop %bp                          # gen=3e 5d c0  dis=3e 5d
    ds pop %bp                          # gen=3e 5d 06  dis=3e 5d
    ds pop %si                          # gen=3e 5e c0  dis=3e 5e
    ds pop %si                          # gen=3e 5e 06  dis=3e 5e
    ds pop %di                          # gen=3e 5f c0  dis=3e 5f
    ds pop %di                          # gen=3e 5f 06  dis=3e 5f
    .byte 0x3e,0x60,0xc0            # fallback; gen=3e 60 c0
    .byte 0x3e,0x60,0x06            # fallback; gen=3e 60 06
    .byte 0x3e,0x61,0xc0            # fallback; gen=3e 61 c0
    .byte 0x3e,0x61,0x06            # fallback; gen=3e 61 06
    .byte 0x3e,0x62,0xc0            # fallback; gen=3e 62 c0
    .byte 0x3e,0x62,0x06            # fallback; gen=3e 62 06
    .byte 0x3e,0x62,0xc8            # fallback; gen=3e 62 c8
    .byte 0x3e,0x62,0xd0            # fallback; gen=3e 62 d0
    .byte 0x3e,0x62,0xd8            # fallback; gen=3e 62 d8
    .byte 0x3e,0x62,0xe0            # fallback; gen=3e 62 e0
    .byte 0x3e,0x62,0xe8            # fallback; gen=3e 62 e8
    .byte 0x3e,0x62,0xf0            # fallback; gen=3e 62 f0
    .byte 0x3e,0x62,0xf8            # fallback; gen=3e 62 f8
    .byte 0x3e,0x62,0x0e            # fallback; gen=3e 62 0e
    .byte 0x3e,0x62,0x16            # fallback; gen=3e 62 16
    .byte 0x3e,0x62,0x1e            # fallback; gen=3e 62 1e
    .byte 0x3e,0x62,0x26            # fallback; gen=3e 62 26
    .byte 0x3e,0x62,0x2e            # fallback; gen=3e 62 2e
    .byte 0x3e,0x62,0x36            # fallback; gen=3e 62 36
    .byte 0x3e,0x62,0x3e            # fallback; gen=3e 62 3e
    .byte 0x3e,0x63,0xc0            # fallback; gen=3e 63 c0
    .byte 0x3e,0x63,0x06            # fallback; gen=3e 63 06
    .byte 0x3e,0x63,0xc8            # fallback; gen=3e 63 c8
    .byte 0x3e,0x63,0xd0            # fallback; gen=3e 63 d0
    .byte 0x3e,0x63,0xd8            # fallback; gen=3e 63 d8
    .byte 0x3e,0x63,0xe0            # fallback; gen=3e 63 e0
    .byte 0x3e,0x63,0xe8            # fallback; gen=3e 63 e8
    .byte 0x3e,0x63,0xf0            # fallback; gen=3e 63 f0
    .byte 0x3e,0x63,0xf8            # fallback; gen=3e 63 f8
    .byte 0x3e,0x63,0x0e            # fallback; gen=3e 63 0e
    .byte 0x3e,0x63,0x16            # fallback; gen=3e 63 16
    .byte 0x3e,0x63,0x1e            # fallback; gen=3e 63 1e
    .byte 0x3e,0x63,0x26            # fallback; gen=3e 63 26
    .byte 0x3e,0x63,0x2e            # fallback; gen=3e 63 2e
    .byte 0x3e,0x63,0x36            # fallback; gen=3e 63 36
    .byte 0x3e,0x63,0x3e            # fallback; gen=3e 63 3e
    .byte 0x3e,0x64,0xc0            # fallback; gen=3e 64 c0
    .byte 0x3e,0x64,0x06            # fallback; gen=3e 64 06
    .byte 0x3e,0x65,0xc0            # fallback; gen=3e 65 c0
    .byte 0x3e,0x65,0x06            # fallback; gen=3e 65 06
    .byte 0x3e,0x66,0xc0            # fallback; gen=3e 66 c0
    .byte 0x3e,0x66,0x06            # fallback; gen=3e 66 06
    .byte 0x3e,0x66,0xc8            # fallback; gen=3e 66 c8
    .byte 0x3e,0x66,0xd0            # fallback; gen=3e 66 d0
    .byte 0x3e,0x66,0xd8            # fallback; gen=3e 66 d8
    .byte 0x3e,0x66,0xe0            # fallback; gen=3e 66 e0
    .byte 0x3e,0x66,0xe8            # fallback; gen=3e 66 e8
    .byte 0x3e,0x66,0xf0            # fallback; gen=3e 66 f0
    .byte 0x3e,0x66,0xf8            # fallback; gen=3e 66 f8
    .byte 0x3e,0x66,0x0e            # fallback; gen=3e 66 0e
    .byte 0x3e,0x66,0x16            # fallback; gen=3e 66 16
    .byte 0x3e,0x66,0x1e            # fallback; gen=3e 66 1e
    .byte 0x3e,0x66,0x26            # fallback; gen=3e 66 26
    .byte 0x3e,0x66,0x2e            # fallback; gen=3e 66 2e
    .byte 0x3e,0x66,0x36            # fallback; gen=3e 66 36
    .byte 0x3e,0x66,0x3e            # fallback; gen=3e 66 3e
    .byte 0x3e,0x67,0xc0            # fallback; gen=3e 67 c0
    .byte 0x3e,0x67,0x06            # fallback; gen=3e 67 06
    .byte 0x3e,0x67,0xc8            # fallback; gen=3e 67 c8
    .byte 0x3e,0x67,0xd0            # fallback; gen=3e 67 d0
    .byte 0x3e,0x67,0xd8            # fallback; gen=3e 67 d8
    .byte 0x3e,0x67,0xe0            # fallback; gen=3e 67 e0
    .byte 0x3e,0x67,0xe8            # fallback; gen=3e 67 e8
    .byte 0x3e,0x67,0xf0            # fallback; gen=3e 67 f0
    .byte 0x3e,0x67,0xf8            # fallback; gen=3e 67 f8
    .byte 0x3e,0x67,0x0e            # fallback; gen=3e 67 0e
    .byte 0x3e,0x67,0x16            # fallback; gen=3e 67 16
    .byte 0x3e,0x67,0x1e            # fallback; gen=3e 67 1e
    .byte 0x3e,0x67,0x26            # fallback; gen=3e 67 26
    .byte 0x3e,0x67,0x2e            # fallback; gen=3e 67 2e
    .byte 0x3e,0x67,0x36            # fallback; gen=3e 67 36
    .byte 0x3e,0x67,0x3e            # fallback; gen=3e 67 3e
    .byte 0x3e,0x68,0xc0            # fallback; gen=3e 68 c0
    .byte 0x3e,0x68,0x06            # fallback; gen=3e 68 06
    .byte 0x3e,0x69,0xc0            # fallback; gen=3e 69 c0
    .byte 0x3e,0x69,0x06            # fallback; gen=3e 69 06
    .byte 0x3e,0x69,0xc8            # fallback; gen=3e 69 c8
    .byte 0x3e,0x69,0xd0            # fallback; gen=3e 69 d0
    .byte 0x3e,0x69,0xd8            # fallback; gen=3e 69 d8
    .byte 0x3e,0x69,0xe0            # fallback; gen=3e 69 e0
    .byte 0x3e,0x69,0xe8            # fallback; gen=3e 69 e8
    .byte 0x3e,0x69,0xf0            # fallback; gen=3e 69 f0
    .byte 0x3e,0x69,0xf8            # fallback; gen=3e 69 f8
    .byte 0x3e,0x69,0x0e            # fallback; gen=3e 69 0e
    .byte 0x3e,0x69,0x16            # fallback; gen=3e 69 16
    .byte 0x3e,0x69,0x1e            # fallback; gen=3e 69 1e
    .byte 0x3e,0x69,0x26            # fallback; gen=3e 69 26
    .byte 0x3e,0x69,0x2e            # fallback; gen=3e 69 2e
    .byte 0x3e,0x69,0x36            # fallback; gen=3e 69 36
    .byte 0x3e,0x69,0x3e            # fallback; gen=3e 69 3e
    .byte 0x3e,0x6a,0xc0            # fallback; gen=3e 6a c0
    .byte 0x3e,0x6a,0x06            # fallback; gen=3e 6a 06
    .byte 0x3e,0x6b,0xc0            # fallback; gen=3e 6b c0
    .byte 0x3e,0x6b,0x06            # fallback; gen=3e 6b 06
    .byte 0x3e,0x6b,0xc8            # fallback; gen=3e 6b c8
    .byte 0x3e,0x6b,0xd0            # fallback; gen=3e 6b d0
    .byte 0x3e,0x6b,0xd8            # fallback; gen=3e 6b d8
    .byte 0x3e,0x6b,0xe0            # fallback; gen=3e 6b e0
    .byte 0x3e,0x6b,0xe8            # fallback; gen=3e 6b e8
    .byte 0x3e,0x6b,0xf0            # fallback; gen=3e 6b f0
    .byte 0x3e,0x6b,0xf8            # fallback; gen=3e 6b f8
    .byte 0x3e,0x6b,0x0e            # fallback; gen=3e 6b 0e
    .byte 0x3e,0x6b,0x16            # fallback; gen=3e 6b 16
    .byte 0x3e,0x6b,0x1e            # fallback; gen=3e 6b 1e
    .byte 0x3e,0x6b,0x26            # fallback; gen=3e 6b 26
    .byte 0x3e,0x6b,0x2e            # fallback; gen=3e 6b 2e
    .byte 0x3e,0x6b,0x36            # fallback; gen=3e 6b 36
    .byte 0x3e,0x6b,0x3e            # fallback; gen=3e 6b 3e
    .byte 0x3e,0x6c,0xc0            # fallback; gen=3e 6c c0
    .byte 0x3e,0x6c,0x06            # fallback; gen=3e 6c 06
    .byte 0x3e,0x6d,0xc0            # fallback; gen=3e 6d c0
    .byte 0x3e,0x6d,0x06            # fallback; gen=3e 6d 06
    .byte 0x3e,0x6e,0xc0            # fallback; gen=3e 6e c0
    .byte 0x3e,0x6e,0x06            # fallback; gen=3e 6e 06
    .byte 0x3e,0x6f,0xc0            # fallback; gen=3e 6f c0
    .byte 0x3e,0x6f,0x06            # fallback; gen=3e 6f 06
    .byte 0x3e,0x70,0xc0            # fallback; gen=3e 70 c0
    .byte 0x3e,0x70,0x06            # fallback; gen=3e 70 06
    .byte 0x3e,0x71,0xc0            # fallback; gen=3e 71 c0
    .byte 0x3e,0x71,0x06            # fallback; gen=3e 71 06
    .byte 0x3e,0x72,0xc0            # fallback; gen=3e 72 c0
    .byte 0x3e,0x72,0x06            # fallback; gen=3e 72 06
    .byte 0x3e,0x73,0xc0            # fallback; gen=3e 73 c0
    .byte 0x3e,0x73,0x06            # fallback; gen=3e 73 06
    .byte 0x3e,0x74,0xc0            # fallback; gen=3e 74 c0
    .byte 0x3e,0x74,0x06            # fallback; gen=3e 74 06
    .byte 0x3e,0x75,0xc0            # fallback; gen=3e 75 c0
    .byte 0x3e,0x75,0x06            # fallback; gen=3e 75 06
    .byte 0x3e,0x76,0xc0            # fallback; gen=3e 76 c0
    .byte 0x3e,0x76,0x06            # fallback; gen=3e 76 06
    .byte 0x3e,0x77,0xc0            # fallback; gen=3e 77 c0
    .byte 0x3e,0x77,0x06            # fallback; gen=3e 77 06
    .byte 0x3e,0x78,0xc0            # fallback; gen=3e 78 c0
    .byte 0x3e,0x78,0x06            # fallback; gen=3e 78 06
    .byte 0x3e,0x79,0xc0            # fallback; gen=3e 79 c0
    .byte 0x3e,0x79,0x06            # fallback; gen=3e 79 06
    .byte 0x3e,0x7a,0xc0            # fallback; gen=3e 7a c0
    .byte 0x3e,0x7a,0x06            # fallback; gen=3e 7a 06
    .byte 0x3e,0x7b,0xc0            # fallback; gen=3e 7b c0
    .byte 0x3e,0x7b,0x06            # fallback; gen=3e 7b 06
    .byte 0x3e,0x7c,0xc0            # fallback; gen=3e 7c c0
    .byte 0x3e,0x7c,0x06            # fallback; gen=3e 7c 06
    .byte 0x3e,0x7d,0xc0            # fallback; gen=3e 7d c0
    .byte 0x3e,0x7d,0x06            # fallback; gen=3e 7d 06
    .byte 0x3e,0x7e,0xc0            # fallback; gen=3e 7e c0
    .byte 0x3e,0x7e,0x06            # fallback; gen=3e 7e 06
    .byte 0x3e,0x7f,0xc0            # fallback; gen=3e 7f c0
    .byte 0x3e,0x7f,0x06            # fallback; gen=3e 7f 06
    ds add $0x90,%al                    # gen=3e 80 c0  dis=3e 80 c0 90
    addb   $0x90,%ds:-0x6f70            # gen=3e 80 06  dis=3e 80 06 90 90 90
    ds or  $0x90,%al                    # gen=3e 80 c8  dis=3e 80 c8 90
    ds adc $0x90,%al                    # gen=3e 80 d0  dis=3e 80 d0 90
    ds sbb $0x90,%al                    # gen=3e 80 d8  dis=3e 80 d8 90
    ds and $0x90,%al                    # gen=3e 80 e0  dis=3e 80 e0 90
    ds sub $0x90,%al                    # gen=3e 80 e8  dis=3e 80 e8 90
    ds xor $0x90,%al                    # gen=3e 80 f0  dis=3e 80 f0 90
    ds cmp $0x90,%al                    # gen=3e 80 f8  dis=3e 80 f8 90
    orb    $0x90,%ds:-0x6f70            # gen=3e 80 0e  dis=3e 80 0e 90 90 90
    adcb   $0x90,%ds:-0x6f70            # gen=3e 80 16  dis=3e 80 16 90 90 90
    sbbb   $0x90,%ds:-0x6f70            # gen=3e 80 1e  dis=3e 80 1e 90 90 90
    andb   $0x90,%ds:-0x6f70            # gen=3e 80 26  dis=3e 80 26 90 90 90
    subb   $0x90,%ds:-0x6f70            # gen=3e 80 2e  dis=3e 80 2e 90 90 90
    xorb   $0x90,%ds:-0x6f70            # gen=3e 80 36  dis=3e 80 36 90 90 90
    cmpb   $0x90,%ds:-0x6f70            # gen=3e 80 3e  dis=3e 80 3e 90 90 90
    ds add $0x9090,%ax                  # gen=3e 81 c0  dis=3e 81 c0 90 90
    addw   $0x9090,%ds:-0x6f70          # gen=3e 81 06  dis=3e 81 06 90 90 90 90
    ds or  $0x9090,%ax                  # gen=3e 81 c8  dis=3e 81 c8 90 90
    ds adc $0x9090,%ax                  # gen=3e 81 d0  dis=3e 81 d0 90 90
    ds sbb $0x9090,%ax                  # gen=3e 81 d8  dis=3e 81 d8 90 90
    ds and $0x9090,%ax                  # gen=3e 81 e0  dis=3e 81 e0 90 90
    ds sub $0x9090,%ax                  # gen=3e 81 e8  dis=3e 81 e8 90 90
    ds xor $0x9090,%ax                  # gen=3e 81 f0  dis=3e 81 f0 90 90
    ds cmp $0x9090,%ax                  # gen=3e 81 f8  dis=3e 81 f8 90 90
    orw    $0x9090,%ds:-0x6f70          # gen=3e 81 0e  dis=3e 81 0e 90 90 90 90
    adcw   $0x9090,%ds:-0x6f70          # gen=3e 81 16  dis=3e 81 16 90 90 90 90
    sbbw   $0x9090,%ds:-0x6f70          # gen=3e 81 1e  dis=3e 81 1e 90 90 90 90
    andw   $0x9090,%ds:-0x6f70          # gen=3e 81 26  dis=3e 81 26 90 90 90 90
    subw   $0x9090,%ds:-0x6f70          # gen=3e 81 2e  dis=3e 81 2e 90 90 90 90
    xorw   $0x9090,%ds:-0x6f70          # gen=3e 81 36  dis=3e 81 36 90 90 90 90
    cmpw   $0x9090,%ds:-0x6f70          # gen=3e 81 3e  dis=3e 81 3e 90 90 90 90
    ds add $0x90,%al                    # gen=3e 82 c0  dis=3e 82 c0 90
    addb   $0x90,%ds:-0x6f70            # gen=3e 82 06  dis=3e 82 06 90 90 90
    ds or  $0x90,%al                    # gen=3e 82 c8  dis=3e 82 c8 90
    ds adc $0x90,%al                    # gen=3e 82 d0  dis=3e 82 d0 90
    ds sbb $0x90,%al                    # gen=3e 82 d8  dis=3e 82 d8 90
    ds and $0x90,%al                    # gen=3e 82 e0  dis=3e 82 e0 90
    ds sub $0x90,%al                    # gen=3e 82 e8  dis=3e 82 e8 90
    ds xor $0x90,%al                    # gen=3e 82 f0  dis=3e 82 f0 90
    ds cmp $0x90,%al                    # gen=3e 82 f8  dis=3e 82 f8 90
    orb    $0x90,%ds:-0x6f70            # gen=3e 82 0e  dis=3e 82 0e 90 90 90
    adcb   $0x90,%ds:-0x6f70            # gen=3e 82 16  dis=3e 82 16 90 90 90
    sbbb   $0x90,%ds:-0x6f70            # gen=3e 82 1e  dis=3e 82 1e 90 90 90
    andb   $0x90,%ds:-0x6f70            # gen=3e 82 26  dis=3e 82 26 90 90 90
    subb   $0x90,%ds:-0x6f70            # gen=3e 82 2e  dis=3e 82 2e 90 90 90
    xorb   $0x90,%ds:-0x6f70            # gen=3e 82 36  dis=3e 82 36 90 90 90
    cmpb   $0x90,%ds:-0x6f70            # gen=3e 82 3e  dis=3e 82 3e 90 90 90
    ds add $0xff90,%ax                  # gen=3e 83 c0  dis=3e 83 c0 90
    addw   $0xff90,%ds:-0x6f70          # gen=3e 83 06  dis=3e 83 06 90 90 90
    ds or  $0xff90,%ax                  # gen=3e 83 c8  dis=3e 83 c8 90
    ds adc $0xff90,%ax                  # gen=3e 83 d0  dis=3e 83 d0 90
    ds sbb $0xff90,%ax                  # gen=3e 83 d8  dis=3e 83 d8 90
    ds and $0xff90,%ax                  # gen=3e 83 e0  dis=3e 83 e0 90
    ds sub $0xff90,%ax                  # gen=3e 83 e8  dis=3e 83 e8 90
    ds xor $0xff90,%ax                  # gen=3e 83 f0  dis=3e 83 f0 90
    ds cmp $0xff90,%ax                  # gen=3e 83 f8  dis=3e 83 f8 90
    orw    $0xff90,%ds:-0x6f70          # gen=3e 83 0e  dis=3e 83 0e 90 90 90
    adcw   $0xff90,%ds:-0x6f70          # gen=3e 83 16  dis=3e 83 16 90 90 90
    sbbw   $0xff90,%ds:-0x6f70          # gen=3e 83 1e  dis=3e 83 1e 90 90 90
    andw   $0xff90,%ds:-0x6f70          # gen=3e 83 26  dis=3e 83 26 90 90 90
    subw   $0xff90,%ds:-0x6f70          # gen=3e 83 2e  dis=3e 83 2e 90 90 90
    xorw   $0xff90,%ds:-0x6f70          # gen=3e 83 36  dis=3e 83 36 90 90 90
    cmpw   $0xff90,%ds:-0x6f70          # gen=3e 83 3e  dis=3e 83 3e 90 90 90
    ds test %al,%al                     # gen=3e 84 c0  dis=3e 84 c0
    test   %al,%ds:-0x6f70              # gen=3e 84 06  dis=3e 84 06 90 90
    ds test %cl,%al                     # gen=3e 84 c8  dis=3e 84 c8
    ds test %dl,%al                     # gen=3e 84 d0  dis=3e 84 d0
    ds test %bl,%al                     # gen=3e 84 d8  dis=3e 84 d8
    ds test %ah,%al                     # gen=3e 84 e0  dis=3e 84 e0
    ds test %ch,%al                     # gen=3e 84 e8  dis=3e 84 e8
    ds test %dh,%al                     # gen=3e 84 f0  dis=3e 84 f0
    ds test %bh,%al                     # gen=3e 84 f8  dis=3e 84 f8
    test   %cl,%ds:-0x6f70              # gen=3e 84 0e  dis=3e 84 0e 90 90
    test   %dl,%ds:-0x6f70              # gen=3e 84 16  dis=3e 84 16 90 90
    test   %bl,%ds:-0x6f70              # gen=3e 84 1e  dis=3e 84 1e 90 90
    test   %ah,%ds:-0x6f70              # gen=3e 84 26  dis=3e 84 26 90 90
    test   %ch,%ds:-0x6f70              # gen=3e 84 2e  dis=3e 84 2e 90 90
    test   %dh,%ds:-0x6f70              # gen=3e 84 36  dis=3e 84 36 90 90
    test   %bh,%ds:-0x6f70              # gen=3e 84 3e  dis=3e 84 3e 90 90
    ds test %ax,%ax                     # gen=3e 85 c0  dis=3e 85 c0
    test   %ax,%ds:-0x6f70              # gen=3e 85 06  dis=3e 85 06 90 90
    ds test %cx,%ax                     # gen=3e 85 c8  dis=3e 85 c8
    ds test %dx,%ax                     # gen=3e 85 d0  dis=3e 85 d0
    ds test %bx,%ax                     # gen=3e 85 d8  dis=3e 85 d8
    ds test %sp,%ax                     # gen=3e 85 e0  dis=3e 85 e0
    ds test %bp,%ax                     # gen=3e 85 e8  dis=3e 85 e8
    ds test %si,%ax                     # gen=3e 85 f0  dis=3e 85 f0
    ds test %di,%ax                     # gen=3e 85 f8  dis=3e 85 f8
    test   %cx,%ds:-0x6f70              # gen=3e 85 0e  dis=3e 85 0e 90 90
    test   %dx,%ds:-0x6f70              # gen=3e 85 16  dis=3e 85 16 90 90
    test   %bx,%ds:-0x6f70              # gen=3e 85 1e  dis=3e 85 1e 90 90
    test   %sp,%ds:-0x6f70              # gen=3e 85 26  dis=3e 85 26 90 90
    test   %bp,%ds:-0x6f70              # gen=3e 85 2e  dis=3e 85 2e 90 90
    test   %si,%ds:-0x6f70              # gen=3e 85 36  dis=3e 85 36 90 90
    test   %di,%ds:-0x6f70              # gen=3e 85 3e  dis=3e 85 3e 90 90
    ds xchg %al,%al                     # gen=3e 86 c0  dis=3e 86 c0
    xchg   %al,%ds:-0x6f70              # gen=3e 86 06  dis=3e 86 06 90 90
    ds xchg %cl,%al                     # gen=3e 86 c8  dis=3e 86 c8
    ds xchg %dl,%al                     # gen=3e 86 d0  dis=3e 86 d0
    ds xchg %bl,%al                     # gen=3e 86 d8  dis=3e 86 d8
    ds xchg %ah,%al                     # gen=3e 86 e0  dis=3e 86 e0
    ds xchg %ch,%al                     # gen=3e 86 e8  dis=3e 86 e8
    ds xchg %dh,%al                     # gen=3e 86 f0  dis=3e 86 f0
    ds xchg %bh,%al                     # gen=3e 86 f8  dis=3e 86 f8
    xchg   %cl,%ds:-0x6f70              # gen=3e 86 0e  dis=3e 86 0e 90 90
    xchg   %dl,%ds:-0x6f70              # gen=3e 86 16  dis=3e 86 16 90 90
    xchg   %bl,%ds:-0x6f70              # gen=3e 86 1e  dis=3e 86 1e 90 90
    xchg   %ah,%ds:-0x6f70              # gen=3e 86 26  dis=3e 86 26 90 90
    xchg   %ch,%ds:-0x6f70              # gen=3e 86 2e  dis=3e 86 2e 90 90
    xchg   %dh,%ds:-0x6f70              # gen=3e 86 36  dis=3e 86 36 90 90
    xchg   %bh,%ds:-0x6f70              # gen=3e 86 3e  dis=3e 86 3e 90 90
    ds xchg %ax,%ax                     # gen=3e 87 c0  dis=3e 87 c0
    xchg   %ax,%ds:-0x6f70              # gen=3e 87 06  dis=3e 87 06 90 90
    ds xchg %cx,%ax                     # gen=3e 87 c8  dis=3e 87 c8
    ds xchg %dx,%ax                     # gen=3e 87 d0  dis=3e 87 d0
    ds xchg %bx,%ax                     # gen=3e 87 d8  dis=3e 87 d8
    ds xchg %sp,%ax                     # gen=3e 87 e0  dis=3e 87 e0
    ds xchg %bp,%ax                     # gen=3e 87 e8  dis=3e 87 e8
    ds xchg %si,%ax                     # gen=3e 87 f0  dis=3e 87 f0
    ds xchg %di,%ax                     # gen=3e 87 f8  dis=3e 87 f8
    xchg   %cx,%ds:-0x6f70              # gen=3e 87 0e  dis=3e 87 0e 90 90
    xchg   %dx,%ds:-0x6f70              # gen=3e 87 16  dis=3e 87 16 90 90
    xchg   %bx,%ds:-0x6f70              # gen=3e 87 1e  dis=3e 87 1e 90 90
    xchg   %sp,%ds:-0x6f70              # gen=3e 87 26  dis=3e 87 26 90 90
    xchg   %bp,%ds:-0x6f70              # gen=3e 87 2e  dis=3e 87 2e 90 90
    xchg   %si,%ds:-0x6f70              # gen=3e 87 36  dis=3e 87 36 90 90
    xchg   %di,%ds:-0x6f70              # gen=3e 87 3e  dis=3e 87 3e 90 90
    ds mov %al,%al                      # gen=3e 88 c0  dis=3e 88 c0
    mov    %al,%ds:-0x6f70              # gen=3e 88 06  dis=3e 88 06 90 90
    ds mov %cl,%al                      # gen=3e 88 c8  dis=3e 88 c8
    ds mov %dl,%al                      # gen=3e 88 d0  dis=3e 88 d0
    ds mov %bl,%al                      # gen=3e 88 d8  dis=3e 88 d8
    ds mov %ah,%al                      # gen=3e 88 e0  dis=3e 88 e0
    ds mov %ch,%al                      # gen=3e 88 e8  dis=3e 88 e8
    ds mov %dh,%al                      # gen=3e 88 f0  dis=3e 88 f0
    ds mov %bh,%al                      # gen=3e 88 f8  dis=3e 88 f8
    mov    %cl,%ds:-0x6f70              # gen=3e 88 0e  dis=3e 88 0e 90 90
    mov    %dl,%ds:-0x6f70              # gen=3e 88 16  dis=3e 88 16 90 90
    mov    %bl,%ds:-0x6f70              # gen=3e 88 1e  dis=3e 88 1e 90 90
    mov    %ah,%ds:-0x6f70              # gen=3e 88 26  dis=3e 88 26 90 90
    mov    %ch,%ds:-0x6f70              # gen=3e 88 2e  dis=3e 88 2e 90 90
    mov    %dh,%ds:-0x6f70              # gen=3e 88 36  dis=3e 88 36 90 90
    mov    %bh,%ds:-0x6f70              # gen=3e 88 3e  dis=3e 88 3e 90 90
    ds mov %ax,%ax                      # gen=3e 89 c0  dis=3e 89 c0
    mov    %ax,%ds:-0x6f70              # gen=3e 89 06  dis=3e 89 06 90 90
    ds mov %cx,%ax                      # gen=3e 89 c8  dis=3e 89 c8
    ds mov %dx,%ax                      # gen=3e 89 d0  dis=3e 89 d0
    ds mov %bx,%ax                      # gen=3e 89 d8  dis=3e 89 d8
    ds mov %sp,%ax                      # gen=3e 89 e0  dis=3e 89 e0
    ds mov %bp,%ax                      # gen=3e 89 e8  dis=3e 89 e8
    ds mov %si,%ax                      # gen=3e 89 f0  dis=3e 89 f0
    ds mov %di,%ax                      # gen=3e 89 f8  dis=3e 89 f8
    mov    %cx,%ds:-0x6f70              # gen=3e 89 0e  dis=3e 89 0e 90 90
    mov    %dx,%ds:-0x6f70              # gen=3e 89 16  dis=3e 89 16 90 90
    mov    %bx,%ds:-0x6f70              # gen=3e 89 1e  dis=3e 89 1e 90 90
    mov    %sp,%ds:-0x6f70              # gen=3e 89 26  dis=3e 89 26 90 90
    mov    %bp,%ds:-0x6f70              # gen=3e 89 2e  dis=3e 89 2e 90 90
    mov    %si,%ds:-0x6f70              # gen=3e 89 36  dis=3e 89 36 90 90
    mov    %di,%ds:-0x6f70              # gen=3e 89 3e  dis=3e 89 3e 90 90
    ds mov %al,%al                      # gen=3e 8a c0  dis=3e 8a c0
    mov    %ds:-0x6f70,%al              # gen=3e 8a 06  dis=3e 8a 06 90 90
    ds mov %al,%cl                      # gen=3e 8a c8  dis=3e 8a c8
    ds mov %al,%dl                      # gen=3e 8a d0  dis=3e 8a d0
    ds mov %al,%bl                      # gen=3e 8a d8  dis=3e 8a d8
    ds mov %al,%ah                      # gen=3e 8a e0  dis=3e 8a e0
    ds mov %al,%ch                      # gen=3e 8a e8  dis=3e 8a e8
    ds mov %al,%dh                      # gen=3e 8a f0  dis=3e 8a f0
    ds mov %al,%bh                      # gen=3e 8a f8  dis=3e 8a f8
    mov    %ds:-0x6f70,%cl              # gen=3e 8a 0e  dis=3e 8a 0e 90 90
    mov    %ds:-0x6f70,%dl              # gen=3e 8a 16  dis=3e 8a 16 90 90
    mov    %ds:-0x6f70,%bl              # gen=3e 8a 1e  dis=3e 8a 1e 90 90
    mov    %ds:-0x6f70,%ah              # gen=3e 8a 26  dis=3e 8a 26 90 90
    mov    %ds:-0x6f70,%ch              # gen=3e 8a 2e  dis=3e 8a 2e 90 90
    mov    %ds:-0x6f70,%dh              # gen=3e 8a 36  dis=3e 8a 36 90 90
    mov    %ds:-0x6f70,%bh              # gen=3e 8a 3e  dis=3e 8a 3e 90 90
    ds mov %ax,%ax                      # gen=3e 8b c0  dis=3e 8b c0
    mov    %ds:-0x6f70,%ax              # gen=3e 8b 06  dis=3e 8b 06 90 90
    ds mov %ax,%cx                      # gen=3e 8b c8  dis=3e 8b c8
    ds mov %ax,%dx                      # gen=3e 8b d0  dis=3e 8b d0
    ds mov %ax,%bx                      # gen=3e 8b d8  dis=3e 8b d8
    ds mov %ax,%sp                      # gen=3e 8b e0  dis=3e 8b e0
    ds mov %ax,%bp                      # gen=3e 8b e8  dis=3e 8b e8
    ds mov %ax,%si                      # gen=3e 8b f0  dis=3e 8b f0
    ds mov %ax,%di                      # gen=3e 8b f8  dis=3e 8b f8
    mov    %ds:-0x6f70,%cx              # gen=3e 8b 0e  dis=3e 8b 0e 90 90
    mov    %ds:-0x6f70,%dx              # gen=3e 8b 16  dis=3e 8b 16 90 90
    mov    %ds:-0x6f70,%bx              # gen=3e 8b 1e  dis=3e 8b 1e 90 90
    mov    %ds:-0x6f70,%sp              # gen=3e 8b 26  dis=3e 8b 26 90 90
    mov    %ds:-0x6f70,%bp              # gen=3e 8b 2e  dis=3e 8b 2e 90 90
    mov    %ds:-0x6f70,%si              # gen=3e 8b 36  dis=3e 8b 36 90 90
    mov    %ds:-0x6f70,%di              # gen=3e 8b 3e  dis=3e 8b 3e 90 90
    ds mov %es,%ax                      # gen=3e 8c c0  dis=3e 8c c0
    mov    %es,%ds:-0x6f70              # gen=3e 8c 06  dis=3e 8c 06 90 90
    ds mov %cs,%ax                      # gen=3e 8c c8  dis=3e 8c c8
    ds mov %ss,%ax                      # gen=3e 8c d0  dis=3e 8c d0
    ds mov %ds,%ax                      # gen=3e 8c d8  dis=3e 8c d8
    .byte 0x3e,0x8c,0xe0            # fallback; gen=3e 8c e0
    .byte 0x3e,0x8c,0xe8            # fallback; gen=3e 8c e8
    .byte 0x3e,0x8c,0xf0            # fallback; gen=3e 8c f0
    .byte 0x3e,0x8c,0xf8            # fallback; gen=3e 8c f8
    mov    %cs,%ds:-0x6f70              # gen=3e 8c 0e  dis=3e 8c 0e 90 90
    mov    %ss,%ds:-0x6f70              # gen=3e 8c 16  dis=3e 8c 16 90 90
    mov    %ds,%ds:-0x6f70              # gen=3e 8c 1e  dis=3e 8c 1e 90 90
    .byte 0x3e,0x8c,0x26            # fallback; gen=3e 8c 26
    .byte 0x3e,0x8c,0x2e            # fallback; gen=3e 8c 2e
    .byte 0x3e,0x8c,0x36            # fallback; gen=3e 8c 36
    .byte 0x3e,0x8c,0x3e            # fallback; gen=3e 8c 3e
    .byte 0x3e,0x8d,0xc0            # fallback; gen=3e 8d c0
    .byte 0x3e,0x8d,0x06            # fallback; gen=3e 8d 06
    .byte 0x3e,0x8d,0xc8            # fallback; gen=3e 8d c8
    .byte 0x3e,0x8d,0xd0            # fallback; gen=3e 8d d0
    .byte 0x3e,0x8d,0xd8            # fallback; gen=3e 8d d8
    .byte 0x3e,0x8d,0xe0            # fallback; gen=3e 8d e0
    .byte 0x3e,0x8d,0xe8            # fallback; gen=3e 8d e8
    .byte 0x3e,0x8d,0xf0            # fallback; gen=3e 8d f0
    .byte 0x3e,0x8d,0xf8            # fallback; gen=3e 8d f8
    .byte 0x3e,0x8d,0x0e            # fallback; gen=3e 8d 0e
    .byte 0x3e,0x8d,0x16            # fallback; gen=3e 8d 16
    .byte 0x3e,0x8d,0x1e            # fallback; gen=3e 8d 1e
    .byte 0x3e,0x8d,0x26            # fallback; gen=3e 8d 26
    .byte 0x3e,0x8d,0x2e            # fallback; gen=3e 8d 2e
    .byte 0x3e,0x8d,0x36            # fallback; gen=3e 8d 36
    .byte 0x3e,0x8d,0x3e            # fallback; gen=3e 8d 3e
    ds mov %ax,%es                      # gen=3e 8e c0  dis=3e 8e c0
    mov    %ds:-0x6f70,%es              # gen=3e 8e 06  dis=3e 8e 06 90 90
    ds mov %ax,%cs                      # gen=3e 8e c8  dis=3e 8e c8
    ds mov %ax,%ss                      # gen=3e 8e d0  dis=3e 8e d0
    ds mov %ax,%ds                      # gen=3e 8e d8  dis=3e 8e d8
    .byte 0x3e,0x8e,0xe0            # fallback; gen=3e 8e e0
    .byte 0x3e,0x8e,0xe8            # fallback; gen=3e 8e e8
    .byte 0x3e,0x8e,0xf0            # fallback; gen=3e 8e f0
    .byte 0x3e,0x8e,0xf8            # fallback; gen=3e 8e f8
    mov    %ds:-0x6f70,%cs              # gen=3e 8e 0e  dis=3e 8e 0e 90 90
    mov    %ds:-0x6f70,%ss              # gen=3e 8e 16  dis=3e 8e 16 90 90
    mov    %ds:-0x6f70,%ds              # gen=3e 8e 1e  dis=3e 8e 1e 90 90
    .byte 0x3e,0x8e,0x26            # fallback; gen=3e 8e 26
    .byte 0x3e,0x8e,0x2e            # fallback; gen=3e 8e 2e
    .byte 0x3e,0x8e,0x36            # fallback; gen=3e 8e 36
    .byte 0x3e,0x8e,0x3e            # fallback; gen=3e 8e 3e
    ds pop %ax                          # gen=3e 8f c0  dis=3e 8f c0
    pop    %ds:-0x6f70                  # gen=3e 8f 06  dis=3e 8f 06 90 90
    .byte 0x3e,0x8f,0xc8            # fallback; gen=3e 8f c8
    .byte 0x3e,0x8f,0xd0            # fallback; gen=3e 8f d0
    .byte 0x3e,0x8f,0xd8            # fallback; gen=3e 8f d8
    .byte 0x3e,0x8f,0xe0            # fallback; gen=3e 8f e0
    .byte 0x3e,0x8f,0xe8            # fallback; gen=3e 8f e8
    .byte 0x3e,0x8f,0xf0            # fallback; gen=3e 8f f0
    .byte 0x3e,0x8f,0xf8            # fallback; gen=3e 8f f8
    .byte 0x3e,0x8f,0x0e            # fallback; gen=3e 8f 0e
    .byte 0x3e,0x8f,0x16            # fallback; gen=3e 8f 16
    .byte 0x3e,0x8f,0x1e            # fallback; gen=3e 8f 1e
    .byte 0x3e,0x8f,0x26            # fallback; gen=3e 8f 26
    .byte 0x3e,0x8f,0x2e            # fallback; gen=3e 8f 2e
    .byte 0x3e,0x8f,0x36            # fallback; gen=3e 8f 36
    .byte 0x3e,0x8f,0x3e            # fallback; gen=3e 8f 3e
    ds nop                              # gen=3e 90 c0  dis=3e 90
    ds nop                              # gen=3e 90 06  dis=3e 90
    ds xchg %ax,%cx                     # gen=3e 91 c0  dis=3e 91
    ds xchg %ax,%cx                     # gen=3e 91 06  dis=3e 91
    ds xchg %ax,%dx                     # gen=3e 92 c0  dis=3e 92
    ds xchg %ax,%dx                     # gen=3e 92 06  dis=3e 92
    ds xchg %ax,%bx                     # gen=3e 93 c0  dis=3e 93
    ds xchg %ax,%bx                     # gen=3e 93 06  dis=3e 93
    ds xchg %ax,%sp                     # gen=3e 94 c0  dis=3e 94
    ds xchg %ax,%sp                     # gen=3e 94 06  dis=3e 94
    ds xchg %ax,%bp                     # gen=3e 95 c0  dis=3e 95
    ds xchg %ax,%bp                     # gen=3e 95 06  dis=3e 95
    ds xchg %ax,%si                     # gen=3e 96 c0  dis=3e 96
    ds xchg %ax,%si                     # gen=3e 96 06  dis=3e 96
    ds xchg %ax,%di                     # gen=3e 97 c0  dis=3e 97
    ds xchg %ax,%di                     # gen=3e 97 06  dis=3e 97
    ds cbtw                             # gen=3e 98 c0  dis=3e 98
    ds cbtw                             # gen=3e 98 06  dis=3e 98
    ds cwtd                             # gen=3e 99 c0  dis=3e 99
    ds cwtd                             # gen=3e 99 06  dis=3e 99
    .byte 0x3e,0x9a,0xc0            # fallback; gen=3e 9a c0
    .byte 0x3e,0x9a,0x06            # fallback; gen=3e 9a 06
    .byte 0x3e,0x9b,0xc0            # fallback; gen=3e 9b c0
    .byte 0x3e,0x9b,0x06            # fallback; gen=3e 9b 06
    ds pushf                            # gen=3e 9c c0  dis=3e 9c
    ds pushf                            # gen=3e 9c 06  dis=3e 9c
    ds popf                             # gen=3e 9d c0  dis=3e 9d
    ds popf                             # gen=3e 9d 06  dis=3e 9d
    ds sahf                             # gen=3e 9e c0  dis=3e 9e
    ds sahf                             # gen=3e 9e 06  dis=3e 9e
    ds lahf                             # gen=3e 9f c0  dis=3e 9f
    ds lahf                             # gen=3e 9f 06  dis=3e 9f
    mov    %ds:0x90c0,%al               # gen=3e a0 c0  dis=3e a0 c0 90
    mov    %ds:0x9006,%al               # gen=3e a0 06  dis=3e a0 06 90
    mov    %ds:0x90c0,%ax               # gen=3e a1 c0  dis=3e a1 c0 90
    mov    %ds:0x9006,%ax               # gen=3e a1 06  dis=3e a1 06 90
    mov    %al,%ds:0x90c0               # gen=3e a2 c0  dis=3e a2 c0 90
    mov    %al,%ds:0x9006               # gen=3e a2 06  dis=3e a2 06 90
    mov    %ax,%ds:0x90c0               # gen=3e a3 c0  dis=3e a3 c0 90
    mov    %ax,%ds:0x9006               # gen=3e a3 06  dis=3e a3 06 90
    movsb  %ds:(%si),%es:(%di)          # gen=3e a4 c0  dis=3e a4
    movsb  %ds:(%si),%es:(%di)          # gen=3e a4 06  dis=3e a4
    movsw  %ds:(%si),%es:(%di)          # gen=3e a5 c0  dis=3e a5
    movsw  %ds:(%si),%es:(%di)          # gen=3e a5 06  dis=3e a5
    cmpsb  %es:(%di),%ds:(%si)          # gen=3e a6 c0  dis=3e a6
    cmpsb  %es:(%di),%ds:(%si)          # gen=3e a6 06  dis=3e a6
    cmpsw  %es:(%di),%ds:(%si)          # gen=3e a7 c0  dis=3e a7
    cmpsw  %es:(%di),%ds:(%si)          # gen=3e a7 06  dis=3e a7
    ds test $0xc0,%al                   # gen=3e a8 c0  dis=3e a8 c0
    ds test $0x6,%al                    # gen=3e a8 06  dis=3e a8 06
    ds test $0x90c0,%ax                 # gen=3e a9 c0  dis=3e a9 c0 90
    ds test $0x9006,%ax                 # gen=3e a9 06  dis=3e a9 06 90
    ds stos %al,%es:(%di)               # gen=3e aa c0  dis=3e aa
    ds stos %al,%es:(%di)               # gen=3e aa 06  dis=3e aa
    ds stos %ax,%es:(%di)               # gen=3e ab c0  dis=3e ab
    ds stos %ax,%es:(%di)               # gen=3e ab 06  dis=3e ab
    lods   %ds:(%si),%al                # gen=3e ac c0  dis=3e ac
    lods   %ds:(%si),%al                # gen=3e ac 06  dis=3e ac
    lods   %ds:(%si),%ax                # gen=3e ad c0  dis=3e ad
    lods   %ds:(%si),%ax                # gen=3e ad 06  dis=3e ad
    ds scas %es:(%di),%al               # gen=3e ae c0  dis=3e ae
    ds scas %es:(%di),%al               # gen=3e ae 06  dis=3e ae
    ds scas %es:(%di),%ax               # gen=3e af c0  dis=3e af
    ds scas %es:(%di),%ax               # gen=3e af 06  dis=3e af
    ds mov $0xc0,%al                    # gen=3e b0 c0  dis=3e b0 c0
    ds mov $0x6,%al                     # gen=3e b0 06  dis=3e b0 06
    ds mov $0xc0,%cl                    # gen=3e b1 c0  dis=3e b1 c0
    ds mov $0x6,%cl                     # gen=3e b1 06  dis=3e b1 06
    ds mov $0xc0,%dl                    # gen=3e b2 c0  dis=3e b2 c0
    ds mov $0x6,%dl                     # gen=3e b2 06  dis=3e b2 06
    ds mov $0xc0,%bl                    # gen=3e b3 c0  dis=3e b3 c0
    ds mov $0x6,%bl                     # gen=3e b3 06  dis=3e b3 06
    ds mov $0xc0,%ah                    # gen=3e b4 c0  dis=3e b4 c0
    ds mov $0x6,%ah                     # gen=3e b4 06  dis=3e b4 06
    ds mov $0xc0,%ch                    # gen=3e b5 c0  dis=3e b5 c0
    ds mov $0x6,%ch                     # gen=3e b5 06  dis=3e b5 06
    ds mov $0xc0,%dh                    # gen=3e b6 c0  dis=3e b6 c0
    ds mov $0x6,%dh                     # gen=3e b6 06  dis=3e b6 06
    ds mov $0xc0,%bh                    # gen=3e b7 c0  dis=3e b7 c0
    ds mov $0x6,%bh                     # gen=3e b7 06  dis=3e b7 06
    ds mov $0x90c0,%ax                  # gen=3e b8 c0  dis=3e b8 c0 90
    ds mov $0x9006,%ax                  # gen=3e b8 06  dis=3e b8 06 90
    ds mov $0x90c0,%cx                  # gen=3e b9 c0  dis=3e b9 c0 90
    ds mov $0x9006,%cx                  # gen=3e b9 06  dis=3e b9 06 90
    ds mov $0x90c0,%dx                  # gen=3e ba c0  dis=3e ba c0 90
    ds mov $0x9006,%dx                  # gen=3e ba 06  dis=3e ba 06 90
    ds mov $0x90c0,%bx                  # gen=3e bb c0  dis=3e bb c0 90
    ds mov $0x9006,%bx                  # gen=3e bb 06  dis=3e bb 06 90
    ds mov $0x90c0,%sp                  # gen=3e bc c0  dis=3e bc c0 90
    ds mov $0x9006,%sp                  # gen=3e bc 06  dis=3e bc 06 90
    ds mov $0x90c0,%bp                  # gen=3e bd c0  dis=3e bd c0 90
    ds mov $0x9006,%bp                  # gen=3e bd 06  dis=3e bd 06 90
    ds mov $0x90c0,%si                  # gen=3e be c0  dis=3e be c0 90
    ds mov $0x9006,%si                  # gen=3e be 06  dis=3e be 06 90
    ds mov $0x90c0,%di                  # gen=3e bf c0  dis=3e bf c0 90
    ds mov $0x9006,%di                  # gen=3e bf 06  dis=3e bf 06 90
    .byte 0x3e,0xc0,0xc0            # fallback; gen=3e c0 c0
    .byte 0x3e,0xc0,0x06            # fallback; gen=3e c0 06
    .byte 0x3e,0xc0,0xc8            # fallback; gen=3e c0 c8
    .byte 0x3e,0xc0,0xd0            # fallback; gen=3e c0 d0
    .byte 0x3e,0xc0,0xd8            # fallback; gen=3e c0 d8
    .byte 0x3e,0xc0,0xe0            # fallback; gen=3e c0 e0
    .byte 0x3e,0xc0,0xe8            # fallback; gen=3e c0 e8
    .byte 0x3e,0xc0,0xf0            # fallback; gen=3e c0 f0
    .byte 0x3e,0xc0,0xf8            # fallback; gen=3e c0 f8
    .byte 0x3e,0xc0,0x0e            # fallback; gen=3e c0 0e
    .byte 0x3e,0xc0,0x16            # fallback; gen=3e c0 16
    .byte 0x3e,0xc0,0x1e            # fallback; gen=3e c0 1e
    .byte 0x3e,0xc0,0x26            # fallback; gen=3e c0 26
    .byte 0x3e,0xc0,0x2e            # fallback; gen=3e c0 2e
    .byte 0x3e,0xc0,0x36            # fallback; gen=3e c0 36
    .byte 0x3e,0xc0,0x3e            # fallback; gen=3e c0 3e
    .byte 0x3e,0xc1,0xc0            # fallback; gen=3e c1 c0
    .byte 0x3e,0xc1,0x06            # fallback; gen=3e c1 06
    .byte 0x3e,0xc1,0xc8            # fallback; gen=3e c1 c8
    .byte 0x3e,0xc1,0xd0            # fallback; gen=3e c1 d0
    .byte 0x3e,0xc1,0xd8            # fallback; gen=3e c1 d8
    .byte 0x3e,0xc1,0xe0            # fallback; gen=3e c1 e0
    .byte 0x3e,0xc1,0xe8            # fallback; gen=3e c1 e8
    .byte 0x3e,0xc1,0xf0            # fallback; gen=3e c1 f0
    .byte 0x3e,0xc1,0xf8            # fallback; gen=3e c1 f8
    .byte 0x3e,0xc1,0x0e            # fallback; gen=3e c1 0e
    .byte 0x3e,0xc1,0x16            # fallback; gen=3e c1 16
    .byte 0x3e,0xc1,0x1e            # fallback; gen=3e c1 1e
    .byte 0x3e,0xc1,0x26            # fallback; gen=3e c1 26
    .byte 0x3e,0xc1,0x2e            # fallback; gen=3e c1 2e
    .byte 0x3e,0xc1,0x36            # fallback; gen=3e c1 36
    .byte 0x3e,0xc1,0x3e            # fallback; gen=3e c1 3e
    ds ret $0x90c0                      # gen=3e c2 c0  dis=3e c2 c0 90
    ds ret $0x9006                      # gen=3e c2 06  dis=3e c2 06 90
    ds ret                              # gen=3e c3 c0  dis=3e c3
    ds ret                              # gen=3e c3 06  dis=3e c3
    .byte 0x3e,0xc4,0xc0            # fallback; gen=3e c4 c0
    les    %ds:-0x6f70,%ax              # gen=3e c4 06  dis=3e c4 06 90 90
    .byte 0x3e,0xc4,0xc8            # fallback; gen=3e c4 c8
    .byte 0x3e,0xc4,0xd0            # fallback; gen=3e c4 d0
    .byte 0x3e,0xc4,0xd8            # fallback; gen=3e c4 d8
    .byte 0x3e,0xc4,0xe0            # fallback; gen=3e c4 e0
    .byte 0x3e,0xc4,0xe8            # fallback; gen=3e c4 e8
    .byte 0x3e,0xc4,0xf0            # fallback; gen=3e c4 f0
    .byte 0x3e,0xc4,0xf8            # fallback; gen=3e c4 f8
    les    %ds:-0x6f70,%cx              # gen=3e c4 0e  dis=3e c4 0e 90 90
    les    %ds:-0x6f70,%dx              # gen=3e c4 16  dis=3e c4 16 90 90
    les    %ds:-0x6f70,%bx              # gen=3e c4 1e  dis=3e c4 1e 90 90
    les    %ds:-0x6f70,%sp              # gen=3e c4 26  dis=3e c4 26 90 90
    les    %ds:-0x6f70,%bp              # gen=3e c4 2e  dis=3e c4 2e 90 90
    les    %ds:-0x6f70,%si              # gen=3e c4 36  dis=3e c4 36 90 90
    les    %ds:-0x6f70,%di              # gen=3e c4 3e  dis=3e c4 3e 90 90
    .byte 0x3e,0xc5,0xc0            # fallback; gen=3e c5 c0
    lds    %ds:-0x6f70,%ax              # gen=3e c5 06  dis=3e c5 06 90 90
    .byte 0x3e,0xc5,0xc8            # fallback; gen=3e c5 c8
    .byte 0x3e,0xc5,0xd0            # fallback; gen=3e c5 d0
    .byte 0x3e,0xc5,0xd8            # fallback; gen=3e c5 d8
    .byte 0x3e,0xc5,0xe0            # fallback; gen=3e c5 e0
    .byte 0x3e,0xc5,0xe8            # fallback; gen=3e c5 e8
    .byte 0x3e,0xc5,0xf0            # fallback; gen=3e c5 f0
    .byte 0x3e,0xc5,0xf8            # fallback; gen=3e c5 f8
    lds    %ds:-0x6f70,%cx              # gen=3e c5 0e  dis=3e c5 0e 90 90
    lds    %ds:-0x6f70,%dx              # gen=3e c5 16  dis=3e c5 16 90 90
    lds    %ds:-0x6f70,%bx              # gen=3e c5 1e  dis=3e c5 1e 90 90
    lds    %ds:-0x6f70,%sp              # gen=3e c5 26  dis=3e c5 26 90 90
    lds    %ds:-0x6f70,%bp              # gen=3e c5 2e  dis=3e c5 2e 90 90
    lds    %ds:-0x6f70,%si              # gen=3e c5 36  dis=3e c5 36 90 90
    lds    %ds:-0x6f70,%di              # gen=3e c5 3e  dis=3e c5 3e 90 90
    ds mov $0x90,%al                    # gen=3e c6 c0  dis=3e c6 c0 90
    movb   $0x90,%ds:-0x6f70            # gen=3e c6 06  dis=3e c6 06 90 90 90
    .byte 0x3e,0xc6,0xc8            # fallback; gen=3e c6 c8
    .byte 0x3e,0xc6,0xd0            # fallback; gen=3e c6 d0
    .byte 0x3e,0xc6,0xd8            # fallback; gen=3e c6 d8
    .byte 0x3e,0xc6,0xe0            # fallback; gen=3e c6 e0
    .byte 0x3e,0xc6,0xe8            # fallback; gen=3e c6 e8
    .byte 0x3e,0xc6,0xf0            # fallback; gen=3e c6 f0
    .byte 0x3e,0xc6,0xf8            # fallback; gen=3e c6 f8
    .byte 0x3e,0xc6,0x0e            # fallback; gen=3e c6 0e
    .byte 0x3e,0xc6,0x16            # fallback; gen=3e c6 16
    .byte 0x3e,0xc6,0x1e            # fallback; gen=3e c6 1e
    .byte 0x3e,0xc6,0x26            # fallback; gen=3e c6 26
    .byte 0x3e,0xc6,0x2e            # fallback; gen=3e c6 2e
    .byte 0x3e,0xc6,0x36            # fallback; gen=3e c6 36
    .byte 0x3e,0xc6,0x3e            # fallback; gen=3e c6 3e
    ds mov $0x9090,%ax                  # gen=3e c7 c0  dis=3e c7 c0 90 90
    movw   $0x9090,%ds:-0x6f70          # gen=3e c7 06  dis=3e c7 06 90 90 90 90
    .byte 0x3e,0xc7,0xc8            # fallback; gen=3e c7 c8
    .byte 0x3e,0xc7,0xd0            # fallback; gen=3e c7 d0
    .byte 0x3e,0xc7,0xd8            # fallback; gen=3e c7 d8
    .byte 0x3e,0xc7,0xe0            # fallback; gen=3e c7 e0
    .byte 0x3e,0xc7,0xe8            # fallback; gen=3e c7 e8
    .byte 0x3e,0xc7,0xf0            # fallback; gen=3e c7 f0
    .byte 0x3e,0xc7,0xf8            # fallback; gen=3e c7 f8
    .byte 0x3e,0xc7,0x0e            # fallback; gen=3e c7 0e
    .byte 0x3e,0xc7,0x16            # fallback; gen=3e c7 16
    .byte 0x3e,0xc7,0x1e            # fallback; gen=3e c7 1e
    .byte 0x3e,0xc7,0x26            # fallback; gen=3e c7 26
    .byte 0x3e,0xc7,0x2e            # fallback; gen=3e c7 2e
    .byte 0x3e,0xc7,0x36            # fallback; gen=3e c7 36
    .byte 0x3e,0xc7,0x3e            # fallback; gen=3e c7 3e
    .byte 0x3e,0xc8,0xc0            # fallback; gen=3e c8 c0
    .byte 0x3e,0xc8,0x06            # fallback; gen=3e c8 06
    .byte 0x3e,0xc9,0xc0            # fallback; gen=3e c9 c0
    .byte 0x3e,0xc9,0x06            # fallback; gen=3e c9 06
    ds lret $0x90c0                     # gen=3e ca c0  dis=3e ca c0 90
    ds lret $0x9006                     # gen=3e ca 06  dis=3e ca 06 90
    ds lret                             # gen=3e cb c0  dis=3e cb
    ds lret                             # gen=3e cb 06  dis=3e cb
    ds int3                             # gen=3e cc c0  dis=3e cc
    ds int3                             # gen=3e cc 06  dis=3e cc
    ds int $0xc0                        # gen=3e cd c0  dis=3e cd c0
    ds int $0x6                         # gen=3e cd 06  dis=3e cd 06
    ds into                             # gen=3e ce c0  dis=3e ce
    ds into                             # gen=3e ce 06  dis=3e ce
    ds iret                             # gen=3e cf c0  dis=3e cf
    ds iret                             # gen=3e cf 06  dis=3e cf
    ds rol $1,%al                       # gen=3e d0 c0  dis=3e d0 c0
    rolb   $1,%ds:-0x6f70               # gen=3e d0 06  dis=3e d0 06 90 90
    ds ror $1,%al                       # gen=3e d0 c8  dis=3e d0 c8
    ds rcl $1,%al                       # gen=3e d0 d0  dis=3e d0 d0
    ds rcr $1,%al                       # gen=3e d0 d8  dis=3e d0 d8
    ds shl $1,%al                       # gen=3e d0 e0  dis=3e d0 e0
    ds shr $1,%al                       # gen=3e d0 e8  dis=3e d0 e8
    ds shl $1,%al                       # gen=3e d0 f0  dis=3e d0 f0
    ds sar $1,%al                       # gen=3e d0 f8  dis=3e d0 f8
    rorb   $1,%ds:-0x6f70               # gen=3e d0 0e  dis=3e d0 0e 90 90
    rclb   $1,%ds:-0x6f70               # gen=3e d0 16  dis=3e d0 16 90 90
    rcrb   $1,%ds:-0x6f70               # gen=3e d0 1e  dis=3e d0 1e 90 90
    shlb   $1,%ds:-0x6f70               # gen=3e d0 26  dis=3e d0 26 90 90
    shrb   $1,%ds:-0x6f70               # gen=3e d0 2e  dis=3e d0 2e 90 90
    shlb   $1,%ds:-0x6f70               # gen=3e d0 36  dis=3e d0 36 90 90
    sarb   $1,%ds:-0x6f70               # gen=3e d0 3e  dis=3e d0 3e 90 90
    ds rol $1,%ax                       # gen=3e d1 c0  dis=3e d1 c0
    rolw   $1,%ds:-0x6f70               # gen=3e d1 06  dis=3e d1 06 90 90
    ds ror $1,%ax                       # gen=3e d1 c8  dis=3e d1 c8
    ds rcl $1,%ax                       # gen=3e d1 d0  dis=3e d1 d0
    ds rcr $1,%ax                       # gen=3e d1 d8  dis=3e d1 d8
    ds shl $1,%ax                       # gen=3e d1 e0  dis=3e d1 e0
    ds shr $1,%ax                       # gen=3e d1 e8  dis=3e d1 e8
    ds shl $1,%ax                       # gen=3e d1 f0  dis=3e d1 f0
    ds sar $1,%ax                       # gen=3e d1 f8  dis=3e d1 f8
    rorw   $1,%ds:-0x6f70               # gen=3e d1 0e  dis=3e d1 0e 90 90
    rclw   $1,%ds:-0x6f70               # gen=3e d1 16  dis=3e d1 16 90 90
    rcrw   $1,%ds:-0x6f70               # gen=3e d1 1e  dis=3e d1 1e 90 90
    shlw   $1,%ds:-0x6f70               # gen=3e d1 26  dis=3e d1 26 90 90
    shrw   $1,%ds:-0x6f70               # gen=3e d1 2e  dis=3e d1 2e 90 90
    shlw   $1,%ds:-0x6f70               # gen=3e d1 36  dis=3e d1 36 90 90
    sarw   $1,%ds:-0x6f70               # gen=3e d1 3e  dis=3e d1 3e 90 90
    ds rol %cl,%al                      # gen=3e d2 c0  dis=3e d2 c0
    rolb   %cl,%ds:-0x6f70              # gen=3e d2 06  dis=3e d2 06 90 90
    ds ror %cl,%al                      # gen=3e d2 c8  dis=3e d2 c8
    ds rcl %cl,%al                      # gen=3e d2 d0  dis=3e d2 d0
    ds rcr %cl,%al                      # gen=3e d2 d8  dis=3e d2 d8
    ds shl %cl,%al                      # gen=3e d2 e0  dis=3e d2 e0
    ds shr %cl,%al                      # gen=3e d2 e8  dis=3e d2 e8
    ds shl %cl,%al                      # gen=3e d2 f0  dis=3e d2 f0
    ds sar %cl,%al                      # gen=3e d2 f8  dis=3e d2 f8
    rorb   %cl,%ds:-0x6f70              # gen=3e d2 0e  dis=3e d2 0e 90 90
    rclb   %cl,%ds:-0x6f70              # gen=3e d2 16  dis=3e d2 16 90 90
    rcrb   %cl,%ds:-0x6f70              # gen=3e d2 1e  dis=3e d2 1e 90 90
    shlb   %cl,%ds:-0x6f70              # gen=3e d2 26  dis=3e d2 26 90 90
    shrb   %cl,%ds:-0x6f70              # gen=3e d2 2e  dis=3e d2 2e 90 90
    shlb   %cl,%ds:-0x6f70              # gen=3e d2 36  dis=3e d2 36 90 90
    sarb   %cl,%ds:-0x6f70              # gen=3e d2 3e  dis=3e d2 3e 90 90
    ds rol %cl,%ax                      # gen=3e d3 c0  dis=3e d3 c0
    rolw   %cl,%ds:-0x6f70              # gen=3e d3 06  dis=3e d3 06 90 90
    ds ror %cl,%ax                      # gen=3e d3 c8  dis=3e d3 c8
    ds rcl %cl,%ax                      # gen=3e d3 d0  dis=3e d3 d0
    ds rcr %cl,%ax                      # gen=3e d3 d8  dis=3e d3 d8
    ds shl %cl,%ax                      # gen=3e d3 e0  dis=3e d3 e0
    ds shr %cl,%ax                      # gen=3e d3 e8  dis=3e d3 e8
    ds shl %cl,%ax                      # gen=3e d3 f0  dis=3e d3 f0
    ds sar %cl,%ax                      # gen=3e d3 f8  dis=3e d3 f8
    rorw   %cl,%ds:-0x6f70              # gen=3e d3 0e  dis=3e d3 0e 90 90
    rclw   %cl,%ds:-0x6f70              # gen=3e d3 16  dis=3e d3 16 90 90
    rcrw   %cl,%ds:-0x6f70              # gen=3e d3 1e  dis=3e d3 1e 90 90
    shlw   %cl,%ds:-0x6f70              # gen=3e d3 26  dis=3e d3 26 90 90
    shrw   %cl,%ds:-0x6f70              # gen=3e d3 2e  dis=3e d3 2e 90 90
    shlw   %cl,%ds:-0x6f70              # gen=3e d3 36  dis=3e d3 36 90 90
    sarw   %cl,%ds:-0x6f70              # gen=3e d3 3e  dis=3e d3 3e 90 90
    ds aam $0xc0                        # gen=3e d4 c0  dis=3e d4 c0
    ds aam $0x6                         # gen=3e d4 06  dis=3e d4 06
    ds aad $0xc0                        # gen=3e d5 c0  dis=3e d5 c0
    ds aad $0x6                         # gen=3e d5 06  dis=3e d5 06
    ds salc                             # gen=3e d6 c0  dis=3e d6
    ds salc                             # gen=3e d6 06  dis=3e d6
    xlat   %ds:(%bx)                    # gen=3e d7 c0  dis=3e d7
    xlat   %ds:(%bx)                    # gen=3e d7 06  dis=3e d7
    .byte 0x3e,0xd8,0xc0            # fallback; gen=3e d8 c0
    .byte 0x3e,0xd8,0x06            # fallback; gen=3e d8 06
    .byte 0x3e,0xd8,0xc8            # fallback; gen=3e d8 c8
    .byte 0x3e,0xd8,0xd0            # fallback; gen=3e d8 d0
    .byte 0x3e,0xd8,0xd8            # fallback; gen=3e d8 d8
    .byte 0x3e,0xd8,0xe0            # fallback; gen=3e d8 e0
    .byte 0x3e,0xd8,0xe8            # fallback; gen=3e d8 e8
    .byte 0x3e,0xd8,0xf0            # fallback; gen=3e d8 f0
    .byte 0x3e,0xd8,0xf8            # fallback; gen=3e d8 f8
    .byte 0x3e,0xd8,0x0e            # fallback; gen=3e d8 0e
    .byte 0x3e,0xd8,0x16            # fallback; gen=3e d8 16
    .byte 0x3e,0xd8,0x1e            # fallback; gen=3e d8 1e
    .byte 0x3e,0xd8,0x26            # fallback; gen=3e d8 26
    .byte 0x3e,0xd8,0x2e            # fallback; gen=3e d8 2e
    .byte 0x3e,0xd8,0x36            # fallback; gen=3e d8 36
    .byte 0x3e,0xd8,0x3e            # fallback; gen=3e d8 3e
    .byte 0x3e,0xd9,0xc0            # fallback; gen=3e d9 c0
    .byte 0x3e,0xd9,0x06            # fallback; gen=3e d9 06
    .byte 0x3e,0xd9,0xc8            # fallback; gen=3e d9 c8
    .byte 0x3e,0xd9,0xd0            # fallback; gen=3e d9 d0
    .byte 0x3e,0xd9,0xd8            # fallback; gen=3e d9 d8
    .byte 0x3e,0xd9,0xe0            # fallback; gen=3e d9 e0
    .byte 0x3e,0xd9,0xe8            # fallback; gen=3e d9 e8
    .byte 0x3e,0xd9,0xf0            # fallback; gen=3e d9 f0
    .byte 0x3e,0xd9,0xf8            # fallback; gen=3e d9 f8
    .byte 0x3e,0xd9,0x0e            # fallback; gen=3e d9 0e
    .byte 0x3e,0xd9,0x16            # fallback; gen=3e d9 16
    .byte 0x3e,0xd9,0x1e            # fallback; gen=3e d9 1e
    .byte 0x3e,0xd9,0x26            # fallback; gen=3e d9 26
    .byte 0x3e,0xd9,0x2e            # fallback; gen=3e d9 2e
    .byte 0x3e,0xd9,0x36            # fallback; gen=3e d9 36
    .byte 0x3e,0xd9,0x3e            # fallback; gen=3e d9 3e
    .byte 0x3e,0xda,0xc0            # fallback; gen=3e da c0
    .byte 0x3e,0xda,0x06            # fallback; gen=3e da 06
    .byte 0x3e,0xda,0xc8            # fallback; gen=3e da c8
    .byte 0x3e,0xda,0xd0            # fallback; gen=3e da d0
    .byte 0x3e,0xda,0xd8            # fallback; gen=3e da d8
    .byte 0x3e,0xda,0xe0            # fallback; gen=3e da e0
    .byte 0x3e,0xda,0xe8            # fallback; gen=3e da e8
    .byte 0x3e,0xda,0xf0            # fallback; gen=3e da f0
    .byte 0x3e,0xda,0xf8            # fallback; gen=3e da f8
    .byte 0x3e,0xda,0x0e            # fallback; gen=3e da 0e
    .byte 0x3e,0xda,0x16            # fallback; gen=3e da 16
    .byte 0x3e,0xda,0x1e            # fallback; gen=3e da 1e
    .byte 0x3e,0xda,0x26            # fallback; gen=3e da 26
    .byte 0x3e,0xda,0x2e            # fallback; gen=3e da 2e
    .byte 0x3e,0xda,0x36            # fallback; gen=3e da 36
    .byte 0x3e,0xda,0x3e            # fallback; gen=3e da 3e
    .byte 0x3e,0xdb,0xc0            # fallback; gen=3e db c0
    .byte 0x3e,0xdb,0x06            # fallback; gen=3e db 06
    .byte 0x3e,0xdb,0xc8            # fallback; gen=3e db c8
    .byte 0x3e,0xdb,0xd0            # fallback; gen=3e db d0
    .byte 0x3e,0xdb,0xd8            # fallback; gen=3e db d8
    .byte 0x3e,0xdb,0xe0            # fallback; gen=3e db e0
    .byte 0x3e,0xdb,0xe8            # fallback; gen=3e db e8
    .byte 0x3e,0xdb,0xf0            # fallback; gen=3e db f0
    .byte 0x3e,0xdb,0xf8            # fallback; gen=3e db f8
    .byte 0x3e,0xdb,0x0e            # fallback; gen=3e db 0e
    .byte 0x3e,0xdb,0x16            # fallback; gen=3e db 16
    .byte 0x3e,0xdb,0x1e            # fallback; gen=3e db 1e
    .byte 0x3e,0xdb,0x26            # fallback; gen=3e db 26
    .byte 0x3e,0xdb,0x2e            # fallback; gen=3e db 2e
    .byte 0x3e,0xdb,0x36            # fallback; gen=3e db 36
    .byte 0x3e,0xdb,0x3e            # fallback; gen=3e db 3e
    .byte 0x3e,0xdc,0xc0            # fallback; gen=3e dc c0
    .byte 0x3e,0xdc,0x06            # fallback; gen=3e dc 06
    .byte 0x3e,0xdc,0xc8            # fallback; gen=3e dc c8
    .byte 0x3e,0xdc,0xd0            # fallback; gen=3e dc d0
    .byte 0x3e,0xdc,0xd8            # fallback; gen=3e dc d8
    .byte 0x3e,0xdc,0xe0            # fallback; gen=3e dc e0
    .byte 0x3e,0xdc,0xe8            # fallback; gen=3e dc e8
    .byte 0x3e,0xdc,0xf0            # fallback; gen=3e dc f0
    .byte 0x3e,0xdc,0xf8            # fallback; gen=3e dc f8
    .byte 0x3e,0xdc,0x0e            # fallback; gen=3e dc 0e
    .byte 0x3e,0xdc,0x16            # fallback; gen=3e dc 16
    .byte 0x3e,0xdc,0x1e            # fallback; gen=3e dc 1e
    .byte 0x3e,0xdc,0x26            # fallback; gen=3e dc 26
    .byte 0x3e,0xdc,0x2e            # fallback; gen=3e dc 2e
    .byte 0x3e,0xdc,0x36            # fallback; gen=3e dc 36
    .byte 0x3e,0xdc,0x3e            # fallback; gen=3e dc 3e
    .byte 0x3e,0xdd,0xc0            # fallback; gen=3e dd c0
    .byte 0x3e,0xdd,0x06            # fallback; gen=3e dd 06
    .byte 0x3e,0xdd,0xc8            # fallback; gen=3e dd c8
    .byte 0x3e,0xdd,0xd0            # fallback; gen=3e dd d0
    .byte 0x3e,0xdd,0xd8            # fallback; gen=3e dd d8
    .byte 0x3e,0xdd,0xe0            # fallback; gen=3e dd e0
    .byte 0x3e,0xdd,0xe8            # fallback; gen=3e dd e8
    .byte 0x3e,0xdd,0xf0            # fallback; gen=3e dd f0
    .byte 0x3e,0xdd,0xf8            # fallback; gen=3e dd f8
    .byte 0x3e,0xdd,0x0e            # fallback; gen=3e dd 0e
    .byte 0x3e,0xdd,0x16            # fallback; gen=3e dd 16
    .byte 0x3e,0xdd,0x1e            # fallback; gen=3e dd 1e
    .byte 0x3e,0xdd,0x26            # fallback; gen=3e dd 26
    .byte 0x3e,0xdd,0x2e            # fallback; gen=3e dd 2e
    .byte 0x3e,0xdd,0x36            # fallback; gen=3e dd 36
    .byte 0x3e,0xdd,0x3e            # fallback; gen=3e dd 3e
    .byte 0x3e,0xde,0xc0            # fallback; gen=3e de c0
    .byte 0x3e,0xde,0x06            # fallback; gen=3e de 06
    .byte 0x3e,0xde,0xc8            # fallback; gen=3e de c8
    .byte 0x3e,0xde,0xd0            # fallback; gen=3e de d0
    .byte 0x3e,0xde,0xd8            # fallback; gen=3e de d8
    .byte 0x3e,0xde,0xe0            # fallback; gen=3e de e0
    .byte 0x3e,0xde,0xe8            # fallback; gen=3e de e8
    .byte 0x3e,0xde,0xf0            # fallback; gen=3e de f0
    .byte 0x3e,0xde,0xf8            # fallback; gen=3e de f8
    .byte 0x3e,0xde,0x0e            # fallback; gen=3e de 0e
    .byte 0x3e,0xde,0x16            # fallback; gen=3e de 16
    .byte 0x3e,0xde,0x1e            # fallback; gen=3e de 1e
    .byte 0x3e,0xde,0x26            # fallback; gen=3e de 26
    .byte 0x3e,0xde,0x2e            # fallback; gen=3e de 2e
    .byte 0x3e,0xde,0x36            # fallback; gen=3e de 36
    .byte 0x3e,0xde,0x3e            # fallback; gen=3e de 3e
    .byte 0x3e,0xdf,0xc0            # fallback; gen=3e df c0
    .byte 0x3e,0xdf,0x06            # fallback; gen=3e df 06
    .byte 0x3e,0xdf,0xc8            # fallback; gen=3e df c8
    .byte 0x3e,0xdf,0xd0            # fallback; gen=3e df d0
    .byte 0x3e,0xdf,0xd8            # fallback; gen=3e df d8
    .byte 0x3e,0xdf,0xe0            # fallback; gen=3e df e0
    .byte 0x3e,0xdf,0xe8            # fallback; gen=3e df e8
    .byte 0x3e,0xdf,0xf0            # fallback; gen=3e df f0
    .byte 0x3e,0xdf,0xf8            # fallback; gen=3e df f8
    .byte 0x3e,0xdf,0x0e            # fallback; gen=3e df 0e
    .byte 0x3e,0xdf,0x16            # fallback; gen=3e df 16
    .byte 0x3e,0xdf,0x1e            # fallback; gen=3e df 1e
    .byte 0x3e,0xdf,0x26            # fallback; gen=3e df 26
    .byte 0x3e,0xdf,0x2e            # fallback; gen=3e df 2e
    .byte 0x3e,0xdf,0x36            # fallback; gen=3e df 36
    .byte 0x3e,0xdf,0x3e            # fallback; gen=3e df 3e
    .byte 0x3e,0xe0,0xc0            # fallback; gen=3e e0 c0
    .byte 0x3e,0xe0,0x06            # fallback; gen=3e e0 06
    .byte 0x3e,0xe1,0xc0            # fallback; gen=3e e1 c0
    .byte 0x3e,0xe1,0x06            # fallback; gen=3e e1 06
    .byte 0x3e,0xe2,0xc0            # fallback; gen=3e e2 c0
    .byte 0x3e,0xe2,0x06            # fallback; gen=3e e2 06
    .byte 0x3e,0xe3,0xc0            # fallback; gen=3e e3 c0
    .byte 0x3e,0xe3,0x06            # fallback; gen=3e e3 06
    ds in  $0xc0,%al                    # gen=3e e4 c0  dis=3e e4 c0
    ds in  $0x6,%al                     # gen=3e e4 06  dis=3e e4 06
    ds in  $0xc0,%ax                    # gen=3e e5 c0  dis=3e e5 c0
    ds in  $0x6,%ax                     # gen=3e e5 06  dis=3e e5 06
    ds out %al,$0xc0                    # gen=3e e6 c0  dis=3e e6 c0
    ds out %al,$0x6                     # gen=3e e6 06  dis=3e e6 06
    ds out %ax,$0xc0                    # gen=3e e7 c0  dis=3e e7 c0
    ds out %ax,$0x6                     # gen=3e e7 06  dis=3e e7 06
    .byte 0x3e,0xe8,0xc0            # fallback; gen=3e e8 c0
    .byte 0x3e,0xe8,0x06            # fallback; gen=3e e8 06
    .byte 0x3e,0xe9,0xc0            # fallback; gen=3e e9 c0
    .byte 0x3e,0xe9,0x06            # fallback; gen=3e e9 06
    .byte 0x3e,0xea,0xc0            # fallback; gen=3e ea c0
    .byte 0x3e,0xea,0x06            # fallback; gen=3e ea 06
    .byte 0x3e,0xeb,0xc0            # fallback; gen=3e eb c0
    .byte 0x3e,0xeb,0x06            # fallback; gen=3e eb 06
    ds in  (%dx),%al                    # gen=3e ec c0  dis=3e ec
    ds in  (%dx),%al                    # gen=3e ec 06  dis=3e ec
    ds in  (%dx),%ax                    # gen=3e ed c0  dis=3e ed
    ds in  (%dx),%ax                    # gen=3e ed 06  dis=3e ed
    ds out %al,(%dx)                    # gen=3e ee c0  dis=3e ee
    ds out %al,(%dx)                    # gen=3e ee 06  dis=3e ee
    ds out %ax,(%dx)                    # gen=3e ef c0  dis=3e ef
    ds out %ax,(%dx)                    # gen=3e ef 06  dis=3e ef
    .byte 0x3e,0xf0,0xc0            # fallback; gen=3e f0 c0
    .byte 0x3e,0xf0,0x06            # fallback; gen=3e f0 06
    .byte 0x3e,0xf0,0xc8            # fallback; gen=3e f0 c8
    .byte 0x3e,0xf0,0xd0            # fallback; gen=3e f0 d0
    .byte 0x3e,0xf0,0xd8            # fallback; gen=3e f0 d8
    .byte 0x3e,0xf0,0xe0            # fallback; gen=3e f0 e0
    .byte 0x3e,0xf0,0xe8            # fallback; gen=3e f0 e8
    .byte 0x3e,0xf0,0xf0            # fallback; gen=3e f0 f0
    .byte 0x3e,0xf0,0xf8            # fallback; gen=3e f0 f8
    .byte 0x3e,0xf0,0x0e            # fallback; gen=3e f0 0e
    .byte 0x3e,0xf0,0x16            # fallback; gen=3e f0 16
    .byte 0x3e,0xf0,0x1e            # fallback; gen=3e f0 1e
    .byte 0x3e,0xf0,0x26            # fallback; gen=3e f0 26
    .byte 0x3e,0xf0,0x2e            # fallback; gen=3e f0 2e
    .byte 0x3e,0xf0,0x36            # fallback; gen=3e f0 36
    .byte 0x3e,0xf0,0x3e            # fallback; gen=3e f0 3e
    ds int1                             # gen=3e f1 c0  dis=3e f1
    ds int1                             # gen=3e f1 06  dis=3e f1
    .byte 0x3e,0xf2,0xc0            # fallback; gen=3e f2 c0
    .byte 0x3e,0xf2,0x06            # fallback; gen=3e f2 06
    .byte 0x3e,0xf2,0xc8            # fallback; gen=3e f2 c8
    .byte 0x3e,0xf2,0xd0            # fallback; gen=3e f2 d0
    .byte 0x3e,0xf2,0xd8            # fallback; gen=3e f2 d8
    .byte 0x3e,0xf2,0xe0            # fallback; gen=3e f2 e0
    .byte 0x3e,0xf2,0xe8            # fallback; gen=3e f2 e8
    .byte 0x3e,0xf2,0xf0            # fallback; gen=3e f2 f0
    .byte 0x3e,0xf2,0xf8            # fallback; gen=3e f2 f8
    .byte 0x3e,0xf2,0x0e            # fallback; gen=3e f2 0e
    .byte 0x3e,0xf2,0x16            # fallback; gen=3e f2 16
    .byte 0x3e,0xf2,0x1e            # fallback; gen=3e f2 1e
    .byte 0x3e,0xf2,0x26            # fallback; gen=3e f2 26
    .byte 0x3e,0xf2,0x2e            # fallback; gen=3e f2 2e
    .byte 0x3e,0xf2,0x36            # fallback; gen=3e f2 36
    .byte 0x3e,0xf2,0x3e            # fallback; gen=3e f2 3e
    .byte 0x3e,0xf3,0xc0            # fallback; gen=3e f3 c0
    .byte 0x3e,0xf3,0x06            # fallback; gen=3e f3 06
    .byte 0x3e,0xf3,0xc8            # fallback; gen=3e f3 c8
    .byte 0x3e,0xf3,0xd0            # fallback; gen=3e f3 d0
    .byte 0x3e,0xf3,0xd8            # fallback; gen=3e f3 d8
    .byte 0x3e,0xf3,0xe0            # fallback; gen=3e f3 e0
    .byte 0x3e,0xf3,0xe8            # fallback; gen=3e f3 e8
    .byte 0x3e,0xf3,0xf0            # fallback; gen=3e f3 f0
    .byte 0x3e,0xf3,0xf8            # fallback; gen=3e f3 f8
    .byte 0x3e,0xf3,0x0e            # fallback; gen=3e f3 0e
    .byte 0x3e,0xf3,0x16            # fallback; gen=3e f3 16
    .byte 0x3e,0xf3,0x1e            # fallback; gen=3e f3 1e
    .byte 0x3e,0xf3,0x26            # fallback; gen=3e f3 26
    .byte 0x3e,0xf3,0x2e            # fallback; gen=3e f3 2e
    .byte 0x3e,0xf3,0x36            # fallback; gen=3e f3 36
    .byte 0x3e,0xf3,0x3e            # fallback; gen=3e f3 3e
    ds hlt                              # gen=3e f4 c0  dis=3e f4
    ds hlt                              # gen=3e f4 06  dis=3e f4
    ds cmc                              # gen=3e f5 c0  dis=3e f5
    ds cmc                              # gen=3e f5 06  dis=3e f5
    ds test $0x90,%al                   # gen=3e f6 c0  dis=3e f6 c0 90
    testb  $0x90,%ds:-0x6f70            # gen=3e f6 06  dis=3e f6 06 90 90 90
    ds test $0x90,%al                   # gen=3e f6 c8  dis=3e f6 c8 90
    ds not %al                          # gen=3e f6 d0  dis=3e f6 d0
    ds neg %al                          # gen=3e f6 d8  dis=3e f6 d8
    ds mul %al                          # gen=3e f6 e0  dis=3e f6 e0
    ds imul %al                         # gen=3e f6 e8  dis=3e f6 e8
    ds div %al                          # gen=3e f6 f0  dis=3e f6 f0
    ds idiv %al                         # gen=3e f6 f8  dis=3e f6 f8
    testb  $0x90,%ds:-0x6f70            # gen=3e f6 0e  dis=3e f6 0e 90 90 90
    notb   %ds:-0x6f70                  # gen=3e f6 16  dis=3e f6 16 90 90
    negb   %ds:-0x6f70                  # gen=3e f6 1e  dis=3e f6 1e 90 90
    mulb   %ds:-0x6f70                  # gen=3e f6 26  dis=3e f6 26 90 90
    imulb  %ds:-0x6f70                  # gen=3e f6 2e  dis=3e f6 2e 90 90
    divb   %ds:-0x6f70                  # gen=3e f6 36  dis=3e f6 36 90 90
    idivb  %ds:-0x6f70                  # gen=3e f6 3e  dis=3e f6 3e 90 90
    ds test $0x9090,%ax                 # gen=3e f7 c0  dis=3e f7 c0 90 90
    testw  $0x9090,%ds:-0x6f70          # gen=3e f7 06  dis=3e f7 06 90 90 90 90
    ds test $0x9090,%ax                 # gen=3e f7 c8  dis=3e f7 c8 90 90
    ds not %ax                          # gen=3e f7 d0  dis=3e f7 d0
    ds neg %ax                          # gen=3e f7 d8  dis=3e f7 d8
    ds mul %ax                          # gen=3e f7 e0  dis=3e f7 e0
    ds imul %ax                         # gen=3e f7 e8  dis=3e f7 e8
    ds div %ax                          # gen=3e f7 f0  dis=3e f7 f0
    ds idiv %ax                         # gen=3e f7 f8  dis=3e f7 f8
    testw  $0x9090,%ds:-0x6f70          # gen=3e f7 0e  dis=3e f7 0e 90 90 90 90
    notw   %ds:-0x6f70                  # gen=3e f7 16  dis=3e f7 16 90 90
    negw   %ds:-0x6f70                  # gen=3e f7 1e  dis=3e f7 1e 90 90
    mulw   %ds:-0x6f70                  # gen=3e f7 26  dis=3e f7 26 90 90
    imulw  %ds:-0x6f70                  # gen=3e f7 2e  dis=3e f7 2e 90 90
    divw   %ds:-0x6f70                  # gen=3e f7 36  dis=3e f7 36 90 90
    idivw  %ds:-0x6f70                  # gen=3e f7 3e  dis=3e f7 3e 90 90
    ds clc                              # gen=3e f8 c0  dis=3e f8
    ds clc                              # gen=3e f8 06  dis=3e f8
    ds stc                              # gen=3e f9 c0  dis=3e f9
    ds stc                              # gen=3e f9 06  dis=3e f9
    ds cli                              # gen=3e fa c0  dis=3e fa
    ds cli                              # gen=3e fa 06  dis=3e fa
    ds sti                              # gen=3e fb c0  dis=3e fb
    ds sti                              # gen=3e fb 06  dis=3e fb
    ds cld                              # gen=3e fc c0  dis=3e fc
    ds cld                              # gen=3e fc 06  dis=3e fc
    ds std                              # gen=3e fd c0  dis=3e fd
    ds std                              # gen=3e fd 06  dis=3e fd
    ds inc %al                          # gen=3e fe c0  dis=3e fe c0
    incb   %ds:-0x6f70                  # gen=3e fe 06  dis=3e fe 06 90 90
    ds dec %al                          # gen=3e fe c8  dis=3e fe c8
    .byte 0x3e,0xfe,0xd0            # fallback; gen=3e fe d0
    .byte 0x3e,0xfe,0xd8            # fallback; gen=3e fe d8
    .byte 0x3e,0xfe,0xe0            # fallback; gen=3e fe e0
    .byte 0x3e,0xfe,0xe8            # fallback; gen=3e fe e8
    .byte 0x3e,0xfe,0xf0            # fallback; gen=3e fe f0
    .byte 0x3e,0xfe,0xf8            # fallback; gen=3e fe f8
    decb   %ds:-0x6f70                  # gen=3e fe 0e  dis=3e fe 0e 90 90
    .byte 0x3e,0xfe,0x16            # fallback; gen=3e fe 16
    .byte 0x3e,0xfe,0x1e            # fallback; gen=3e fe 1e
    .byte 0x3e,0xfe,0x26            # fallback; gen=3e fe 26
    .byte 0x3e,0xfe,0x2e            # fallback; gen=3e fe 2e
    .byte 0x3e,0xfe,0x36            # fallback; gen=3e fe 36
    .byte 0x3e,0xfe,0x3e            # fallback; gen=3e fe 3e
    ds inc %ax                          # gen=3e ff c0  dis=3e ff c0
    incw   %ds:-0x6f70                  # gen=3e ff 06  dis=3e ff 06 90 90
    ds dec %ax                          # gen=3e ff c8  dis=3e ff c8
    .byte 0x3e,0xff,0xd0            # fallback; gen=3e ff d0
    .byte 0x3e,0xff,0xd8            # fallback; gen=3e ff d8
    .byte 0x3e,0xff,0xe0            # fallback; gen=3e ff e0
    .byte 0x3e,0xff,0xe8            # fallback; gen=3e ff e8
    ds push %ax                         # gen=3e ff f0  dis=3e ff f0
    .byte 0x3e,0xff,0xf8            # fallback; gen=3e ff f8
    decw   %ds:-0x6f70                  # gen=3e ff 0e  dis=3e ff 0e 90 90
    .byte 0x3e,0xff,0x16            # fallback; gen=3e ff 16
    lcall  *%ds:-0x6f70                 # gen=3e ff 1e  dis=3e ff 1e 90 90
    .byte 0x3e,0xff,0x26            # fallback; gen=3e ff 26
    ljmp   *%ds:-0x6f70                 # gen=3e ff 2e  dis=3e ff 2e 90 90
    push   %ds:-0x6f70                  # gen=3e ff 36  dis=3e ff 36 90 90
    .byte 0x3e,0xff,0x3e            # fallback; gen=3e ff 3e
