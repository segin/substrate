#!/bin/sh
# test-ext2-htree.sh — ext2 directory writes against a real indexed (htree)
# volume, verified by e2fsck.
#
# [EXT2-08] The driver used to refuse every mutation of a directory carrying
# EXT2_INDEX_FL, so `mkdir src/.deps` -- what config.status does in any
# autotools build -- returned EOPNOTSUPP on a filesystem whose directories
# mke2fs or `e2fsck -D` had indexed.
#
# Two volumes are built here and both matter:
#   /big    is indexed, and is what exercises index maintenance
#   /small  stays linear, and is the control: it runs the identical
#           workload through the pre-existing path, so a failure common to
#           both is not the index code's doing.
#
# The entry count is chosen to overflow the root's ~124 slots at 1 KiB
# blocks, so the run covers leaf splitting AND the tree gaining an
# indirect level, not just the easy in-place insert.
set -eu

TOP=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

for t in mke2fs e2fsck debugfs; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed"; exit 0; }
done

make -C "$TOP/tests/sys" host_test_ext2_htree >/dev/null
BIN="$TOP/tests/sys/host_test_ext2_htree"

# Seed enough names that `e2fsck -D` decides /big is worth indexing.
mkdir -p "$WORK/seed/big" "$WORK/seed/small"
i=1
while [ "$i" -le 600 ]; do echo x > "$WORK/seed/big/file_$i.txt"; i=$((i + 1)); done
echo x > "$WORK/seed/small/a"

build_img() {
    mke2fs -q -F -t ext2 -b 1024 -O dir_index -I 128 -N 40000 \
           -d "$WORK/seed" "$1" 128M 2>/dev/null
    e2fsck -fD -y "$1" >/dev/null 2>&1 || true
}

echo "== indexed directory =="
build_img "$WORK/idx.img"
# Confirm the premise before trusting the result: if /big is not actually
# indexed the test would silently prove nothing.
flags=$(debugfs -R "stat /big" "$WORK/idx.img" 2>/dev/null | sed -n 's/.*Flags: \(0x[0-9a-f]*\).*/\1/p')
case "$flags" in
    *1000) : ;;
    *) echo "FAILED: /big is not indexed (Flags=$flags); mke2fs/e2fsck built no htree"; exit 1 ;;
esac
HTREE_ADD=12000 "$BIN" "$WORK/idx.img" big
e2fsck -fn "$WORK/idx.img" || { echo "FAILED: e2fsck rejected the indexed volume"; exit 1; }

levels=$(debugfs -R "htree_dump /big" "$WORK/idx.img" 2>/dev/null |
         sed -n 's/.*Indirect levels: \([0-9]*\).*/\1/p')
[ "${levels:-0}" -ge 1 ] || {
    echo "FAILED: tree never gained an indirect level (levels=$levels) —"
    echo "        the split/grow paths went untested"; exit 1; }
echo "   indirect levels: $levels"

echo "== linear control =="
build_img "$WORK/lin.img"
"$BIN" "$WORK/lin.img" small --allow-linear
e2fsck -fn "$WORK/lin.img" || { echo "FAILED: e2fsck rejected the linear control"; exit 1; }

echo "PASS: ext2 htree directory writes verified by e2fsck"
