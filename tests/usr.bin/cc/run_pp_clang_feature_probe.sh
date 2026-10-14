#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

"$CC_BIN" -E -std=gnu11 -P -I. pp_s6_feature_probe.c -o /tmp/cc_pp_s6_probe_gnu11.i
grep -q '^int hf = 1;$' /tmp/cc_pp_s6_probe_gnu11.i
grep -q '^int he = 1;$' /tmp/cc_pp_s6_probe_gnu11.i
grep -q '^int hb = 1;$' /tmp/cc_pp_s6_probe_gnu11.i
grep -q '^int hi = 1;$' /tmp/cc_pp_s6_probe_gnu11.i
grep -q '^int ha = 1;$' /tmp/cc_pp_s6_probe_gnu11.i
grep -q '^int hca = 1;$' /tmp/cc_pp_s6_probe_gnu11.i
grep -q '^int hda = 1;$' /tmp/cc_pp_s6_probe_gnu11.i
grep -q '^int hw = 1;$' /tmp/cc_pp_s6_probe_gnu11.i
grep -q '^int iid_kw = 0;$' /tmp/cc_pp_s6_probe_gnu11.i
grep -q '^int iid_user = 1;$' /tmp/cc_pp_s6_probe_gnu11.i

"$CC_BIN" -E -std=c99 -P -I. pp_s6_feature_probe.c -o /tmp/cc_pp_s6_probe_c99.i
grep -q '^int hf = 1;$' /tmp/cc_pp_s6_probe_c99.i
grep -q '^int he = 0;$' /tmp/cc_pp_s6_probe_c99.i
grep -q '^int hb = 1;$' /tmp/cc_pp_s6_probe_c99.i
grep -q '^int hi = 0;$' /tmp/cc_pp_s6_probe_c99.i
grep -q '^int ha = 1;$' /tmp/cc_pp_s6_probe_c99.i
grep -q '^int hca = 1;$' /tmp/cc_pp_s6_probe_c99.i
grep -q '^int hda = 1;$' /tmp/cc_pp_s6_probe_c99.i
grep -q '^int hw = 1;$' /tmp/cc_pp_s6_probe_c99.i
grep -q '^int iid_kw = 0;$' /tmp/cc_pp_s6_probe_c99.i
grep -q '^int iid_user = 1;$' /tmp/cc_pp_s6_probe_c99.i
