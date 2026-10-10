#!/bin/sh
# A reference to a label in a mergeable section keeps the label
# (AS-T-215).
#
# A compiler puts string constants in a section flagged "aMS", which the
# linker merges: it moves each string where it will and drops duplicates.
# A reference to a .L label is ordinarily turned into the label's section
# and an offset; into such a section that says "this far in", and the
# linker takes the offset for a place inside whichever string it falls
# in.  A PC-relative field carries -4 besides, so the first string's
# reference fell before the section: ld warned of an access beyond the end
# of a merged section, and the program printed "" and "" where it should
# have printed alpha and beta.
#
# Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

cat > t.s <<'EOF'
	.section .rodata.str1.1,"aMS",@progbits,1
.LC0:	.string "alpha"
.LC1:	.string "beta"
.LC2:	.string "gamma"
	.text
	.globl f
f:	leaq .LC0(%rip), %rdi
	leaq .LC1(%rip), %rsi
	leaq .LC2(%rip), %rdx
	ret
EOF
if ! "$AS" --64 -o t.o t.s 2> err; then
    echo "FAIL: $(head -1 err)"; fail=1
else
    rel=$(readelf -rW t.o | awk '/ R_/ { print $3, $5, $6, $7 }' | tr '\n' ';')
    want='R_X86_64_PC32 .LC0 - 4;R_X86_64_PC32 .LC1 - 4;R_X86_64_PC32 .LC2 - 4;'
    [ "$rel" = "$want" ] || { echo "FAIL: relocations '$rel', not '$want'"; fail=1; }
    # And each label is in the symbol table, local, at its string.
    for pair in .LC0:0 .LC1:6 .LC2:b; do
        name=${pair%%:*}; value=${pair##*:}
        readelf -sW t.o | awk -v n="$name" '$8 == n { print $2, $5 }' | sed 's/^0*\([0-9a-f]\)/\1/' |
            grep -qx "$value LOCAL" || { echo "FAIL: $name is not a local symbol at $value"; fail=1; }
    done
fi

# A label in a section that is not merged is still its section and an
# offset, and is not in the symbol table.
cat > t.s <<'EOF'
	.data
	.long 0
.Ld:	.long 1
	.text
	leaq .Ld(%rip), %rax
EOF
"$AS" --64 -o t.o t.s 2> err || { echo "FAIL plain section: $(head -1 err)"; fail=1; }
rel=$(readelf -rW t.o | awk '/ R_/ { print $3, $5, $6, $7 }' | tr '\n' ';')
[ "$rel" = 'R_X86_64_PC32 .data + 0;' ] || { echo "FAIL plain section: '$rel'"; fail=1; }
if readelf -sW t.o | grep -q ' \.Ld$'; then echo "FAIL plain section: .Ld is in the symbol table"; fail=1; fi

# Linked and run, where the host can: three strings, each the right one.
if command -v gcc > /dev/null; then
    cat > main.c <<'EOF'
#include <stdio.h>
void f(void);
static const char *a, *b, *c;
void take(const char *x, const char *y, const char *z) { a = x; b = y; c = z; }
EOF
    cat > t.s <<'EOF'
	.section .rodata.str1.1,"aMS",@progbits,1
.LC0:	.string "alpha"
.LC1:	.string "beta"
.LC2:	.string "gamma"
	.text
	.globl f
f:	leaq .LC0(%rip), %rdi
	leaq .LC1(%rip), %rsi
	leaq .LC2(%rip), %rdx
	jmp take
EOF
    cat >> main.c <<'EOF'
int main(void) { f(); printf("%s|%s|%s\n", a, b, c); return 0; }
EOF
    if "$AS" --64 -o t.o t.s 2> err && gcc -o prog main.c t.o 2> lerr; then
        out=$(./prog 2>&1)
        [ "$out" = 'alpha|beta|gamma' ] || { echo "FAIL linked: printed '$out'"; fail=1; }
        if grep -q 'merged section' lerr; then echo "FAIL linked: $(head -1 lerr)"; fail=1; fi
    fi
fi

[ "$fail" -eq 0 ] && echo "ok: references into merged sections"
exit "$fail"
