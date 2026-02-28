#!/bin/sh
set -eu

CP_BIN=${1:-./cp_host}
TMPBASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPBASE/cp_prop.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

iter=0
while [ "$iter" -lt 20 ]; do
    src="$WORK/src_$iter"
    dst="$WORK/dst_$iter"
    mkdir -p "$src"

    python3 - "$src" "$iter" <<'PY'
import hashlib
import os
import random
import sys

root = sys.argv[1]
seed = int(sys.argv[2])
r = random.Random(seed)

# directory skeleton
for i in range(8):
    d = os.path.join(root, f"d{i}")
    os.makedirs(d, exist_ok=True)
    for j in range(r.randint(1, 4)):
        p = os.path.join(d, f"f{j}")
        n = r.randint(0, 2048)
        data = os.urandom(n)
        with open(p, "wb") as f:
            f.write(data)

# sparse-like random zeros
sp = os.path.join(root, "sparse")
with open(sp, "wb") as f:
    f.seek(1024 * 128)
    f.write(b"X")

# hardlinks
hl_src = os.path.join(root, "d0", "f0")
hl_dst = os.path.join(root, "d1", "hard_to_f0")
if os.path.exists(hl_src):
    try:
        os.link(hl_src, hl_dst)
    except OSError:
        pass

# symlinks
try:
    os.symlink("d0/f0", os.path.join(root, "sym_rel"))
except OSError:
    pass
PY

    "$CP_BIN" -a "$src" "$dst"

    python3 - "$src" "$dst" <<'PY'
import hashlib
import os
import stat
import sys

src_root = sys.argv[1]
dst_root = sys.argv[2]


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(65536)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def walk_manifest(root):
    out = {}
    for base, dirs, files in os.walk(root, topdown=True, followlinks=False):
        rel_base = os.path.relpath(base, root)
        if rel_base == ".":
            rel_base = ""
        for d in dirs:
            p = os.path.join(base, d)
            rp = os.path.join(rel_base, d)
            st = os.lstat(p)
            out[rp] = ("dir", st.st_mode & 0o777, st.st_nlink)
        for f in files:
            p = os.path.join(base, f)
            rp = os.path.join(rel_base, f)
            st = os.lstat(p)
            if stat.S_ISLNK(st.st_mode):
                out[rp] = ("symlink", os.readlink(p))
            elif stat.S_ISREG(st.st_mode):
                out[rp] = ("file", digest(p), st.st_mode & 0o777, st.st_nlink, st.st_size)
            else:
                out[rp] = ("other", st.st_mode)
    return out

m1 = walk_manifest(src_root)
m2 = walk_manifest(dst_root)
if m1.keys() != m2.keys():
    print("manifest key mismatch", file=sys.stderr)
    sys.exit(1)

for k in m1:
    a = m1[k]
    b = m2[k]
    if a[0] != b[0]:
        print("type mismatch", k, a, b, file=sys.stderr)
        sys.exit(1)
    if a[0] == "file":
        if a[1] != b[1] or a[4] != b[4]:
            print("file mismatch", k, a, b, file=sys.stderr)
            sys.exit(1)
    if a[0] == "symlink" and a[1] != b[1]:
        print("symlink mismatch", k, a, b, file=sys.stderr)
        sys.exit(1)

sys.exit(0)
PY

    iter=$((iter + 1))
done

echo "property: PASS"
