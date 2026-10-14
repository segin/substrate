#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

"$CC_BIN" -E -std=c23 -P pp_s4_elif.c -o /tmp/cc_pp_s4_elif_c23.i
grep -q '^int a = 1;$' /tmp/cc_pp_s4_elif_c23.i
grep -q '^int b = 3;$' /tmp/cc_pp_s4_elif_c23.i
! "$CC_BIN" -E -std=c11 -P pp_s4_elif.c -o /tmp/cc_pp_s4_elif_c11.i

"$CC_BIN" -E -std=c23 pp_s4_warning.c -o /tmp/cc_pp_s4_warning_c23.i 2>/tmp/cc_pp_s4_warning_c23.log
grep -q "warning: c23-warning-text" /tmp/cc_pp_s4_warning_c23.log
! "$CC_BIN" -E -std=c11 pp_s4_warning.c -o /tmp/cc_pp_s4_warning_c11.i

"$CC_BIN" -E -std=c23 -I. -P pp_s4_has.c -o /tmp/cc_pp_s4_has_c23.i
grep -q '^int hi = 1;$' /tmp/cc_pp_s4_has_c23.i
grep -q '^int he = 1;$' /tmp/cc_pp_s4_has_c23.i

"$CC_BIN" -E -std=c11 -I. -P pp_s4_has.c -o /tmp/cc_pp_s4_has_c11.i
grep -q '^int hi = 0;$' /tmp/cc_pp_s4_has_c11.i
grep -q '^int he = 0;$' /tmp/cc_pp_s4_has_c11.i

"$CC_BIN" -E -std=c23 -P pp_s4_vaopt.c -o /tmp/cc_pp_s4_vaopt_c23.i
grep -q '^int v = 1 + (2);$' /tmp/cc_pp_s4_vaopt_c23.i
! "$CC_BIN" -E -std=c11 -P pp_s4_vaopt.c -o /tmp/cc_pp_s4_vaopt_c11.i

# standard macro version/alignment matrix
for std in c99 c11 c17 c23 gnu11 gnu17 gnu23; do
	"$CC_BIN" -E -std="$std" -P pp_s4_stdver.c -o "/tmp/cc_pp_s4_stdver_${std}.i"
done

grep -q '^int sv = 199901L;$' /tmp/cc_pp_s4_stdver_c99.i
grep -q '^int sv = 201112L;$' /tmp/cc_pp_s4_stdver_c11.i
grep -q '^int sv = 201710L;$' /tmp/cc_pp_s4_stdver_c17.i
grep -q '^int sv = 202311L;$' /tmp/cc_pp_s4_stdver_c23.i
grep -q '^int sv = 201112L;$' /tmp/cc_pp_s4_stdver_gnu11.i
grep -q '^int sv = 201710L;$' /tmp/cc_pp_s4_stdver_gnu17.i
grep -q '^int sv = 202311L;$' /tmp/cc_pp_s4_stdver_gnu23.i

grep -q '^int u16 = 0;$' /tmp/cc_pp_s4_stdver_c99.i
grep -q '^int u32 = 0;$' /tmp/cc_pp_s4_stdver_c99.i
grep -q '^int u16 = 1;$' /tmp/cc_pp_s4_stdver_c11.i
grep -q '^int u32 = 1;$' /tmp/cc_pp_s4_stdver_c11.i
grep -q '^int ef = -1;$' /tmp/cc_pp_s4_stdver_c11.i
grep -q '^int ef = 1;$' /tmp/cc_pp_s4_stdver_c23.i

# c23 removed trigraph replacement, older modes still enable it
"$CC_BIN" -E -std=c11 -P pp_s4_trigraph.c -o /tmp/cc_pp_s4_trigraph_c11.i
"$CC_BIN" -E -std=c23 -P pp_s4_trigraph.c -o /tmp/cc_pp_s4_trigraph_c23.i
grep -q '^int tri = 9;$' /tmp/cc_pp_s4_trigraph_c11.i
grep -q '^int tri = TRI;$' /tmp/cc_pp_s4_trigraph_c23.i

printf 'AB' > /tmp/cc_pp_s4_embed.bin
cat > /tmp/cc_pp_s4_embed.c <<'EOF'
unsigned char embed_data[] = {
#embed "/tmp/cc_pp_s4_embed.bin"
};
EOF
"$CC_BIN" -E -std=c23 -P /tmp/cc_pp_s4_embed.c -o /tmp/cc_pp_s4_embed.i
grep -q '0x41, 0x42' /tmp/cc_pp_s4_embed.i
! "$CC_BIN" -E -std=c11 -P /tmp/cc_pp_s4_embed.c -o /tmp/cc_pp_s4_embed_c11.i

for host in gcc clang; do
	if ! command -v "$host" >/dev/null 2>&1; then
		continue
	fi
	std='-std=c23'
	if ! echo 'int x;' | "$host" -x c -E "$std" - >/dev/null 2>&1; then
		std='-std=c2x'
	fi
	if ! "$host" "$std" -P -E -I. pp_s4_elif.c -o /tmp/cc_pp_s4_host_${host}.i >/dev/null 2>&1; then
		continue
	fi
	grep -E '^int [ab] = [13];$' /tmp/cc_pp_s4_host_${host}.i > /tmp/cc_pp_s4_host_${host}.ab
	grep -E '^int [ab] = [13];$' /tmp/cc_pp_s4_elif_c23.i > /tmp/cc_pp_s4_ours.ab
	diff -u /tmp/cc_pp_s4_ours.ab /tmp/cc_pp_s4_host_${host}.ab
done
