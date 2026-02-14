#!/bin/sh
set -eu

PR=${1:-../pr}

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

check_contains() {
    file=$1
    needle=$2
    if ! grep -F "$needle" "$file" >/dev/null 2>&1; then
        fail "expected '$needle' in $file"
    fi
}

check_equal() {
    got=$1
    exp=$2
    [ "$got" = "$exp" ] || fail "expected '$exp', got '$got'"
}

echo "1) pagination and header"
cat > page.tmp <<'TXT'
line1
line2
line3
line4
line5
line6
line7
line8
TXT
"$PR" -l 10 -h Report page.tmp > page.out
check_contains page.out "Report"
check_contains page.out "Page 1"
check_contains page.out "line1"
check_contains page.out "line5"

echo "2) column layout by-column"
cat > cols.tmp <<'TXT'
A1
A2
A3
A4
TXT
"$PR" -t -2 -w 20 cols.tmp > cols.out
check_contains cols.out "A1"
check_contains cols.out "A3"


echo "3) across mode"
"$PR" -t -2 -a -w 20 cols.tmp > across.out
check_contains across.out "A1"
check_contains across.out "A2"


echo "4) merge mode"
cat > m1.tmp <<'TXT'
L1
L2
TXT
cat > m2.tmp <<'TXT'
R1
R2
TXT
"$PR" -t -m -w 20 m1.tmp m2.tmp > merge.out
check_contains merge.out "L1"
check_contains merge.out "R1"


echo "5) line numbering"
"$PR" -t -n.3 page.tmp > num.out
first=$(head -n 1 num.out)
check_equal "$first" "  1.line1"


echo "6) UTF-8 handling"
cat > utf.tmp <<'TXT'
表
x
TXT
LC_ALL=C.UTF-8 "$PR" -t -2 -w 8 utf.tmp > utf.out || LC_ALL=en_US.UTF-8 "$PR" -t -2 -w 8 utf.tmp > utf.out
check_contains utf.out "表"
check_contains utf.out "x"

echo "7) stdin input"
printf 'stdin-line\n' | "$PR" -t - > stdin.out
check_contains stdin.out "stdin-line"

echo "All pr tests passed"
