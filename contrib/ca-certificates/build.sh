#!/bin/sh
#
# contrib/ca-certificates/build.sh — stage the CA trust store for substrate.
#
# Nothing is compiled here; the deliverable is data plus the layout OpenSSL
# actually looks in.  substrate's OpenSSL is configured --openssldir=/etc/ssl
# (contrib/openssl/build.sh), so its two defaults are:
#
#   CAfile  $OPENSSLDIR/cert.pem    -> /etc/ssl/cert.pem
#   CApath  $OPENSSLDIR/certs       -> /etc/ssl/certs
#
# and curl has no bundle path compiled in, so it inherits exactly those.
# Before this port /etc/ssl/certs was an empty directory and there was no
# cert.pem at all, so every TLS verification failed.
#
# Installed layout:
#   /etc/ssl/cert.pem                     the full bundle (OpenSSL CAfile)
#   /etc/ssl/certs/ca-certificates.crt    same bundle, Debian's path — plenty
#                                         of software hardcodes it
#   /etc/ssl/certs/<hash>.<n>             one file per root (OpenSSL CApath)
#   /usr/share/ca-certificates/mozilla/   the split roots, kept so
#                                         update-ca-certificates can rebuild
#   /usr/sbin/update-ca-certificates      regenerate after adding a local root
#
# Env: DESTDIR.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="ca-certificates"
VERSION="2026-08-13"
PEM="${HERE}/build/cacert-${VERSION}.pem"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${LIB}}"

[ -f "${PEM}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
command -v openssl >/dev/null 2>&1 || {
    echo "build.sh: host openssl is required to compute subject hashes" >&2
    exit 1
}

rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}/etc/ssl/certs" \
         "${DESTDIR}/usr/share/ca-certificates/mozilla" \
         "${DESTDIR}/usr/sbin"

# The bundle, at both paths software looks for it.
cp "${PEM}" "${DESTDIR}/etc/ssl/cert.pem"
cp "${PEM}" "${DESTDIR}/etc/ssl/certs/ca-certificates.crt"

# Split into one file per root and lay out the CApath.
#
# The CApath lookup is by SUBJECT HASH, not filename: OpenSSL hashes the
# issuer name it is looking for and opens <hash>.0, <hash>.1, ... until it
# finds a match, so the names carry meaning and cannot be chosen freely.
# Two roots can share a hash, hence the counter.
#
# Computing the hashes with the HOST openssl is safe: X509_NAME_hash_ex has
# been stable since OpenSSL 1.0.0 (it is a truncated SHA-1 over the DER of the
# canonicalised subject), so host 3.6 and target 3.0 agree.  The build asserts
# the resulting store actually verifies below rather than assuming it.
python3 - "${PEM}" "${DESTDIR}" <<'PY'
import os, re, subprocess, sys

pem_path, destdir = sys.argv[1], sys.argv[2]
text = open(pem_path, encoding="utf-8", errors="replace").read()

# Each root in curl's bundle is "<name>\n====...\n-----BEGIN CERTIFICATE-----".
# The name group is [^\n]+ rather than .*? because re.S would otherwise let it
# swallow the whole file header up to the first separator, producing one
# "certificate" named after the entire preamble.
blocks = re.findall(
    r"^([^\n]+)\n=+\n(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----\n)",
    text, re.S | re.M)
if not blocks:
    sys.exit("no certificates parsed out of the bundle")

mozdir = os.path.join(destdir, "usr/share/ca-certificates/mozilla")
certdir = os.path.join(destdir, "etc/ssl/certs")
used = {}
for name, body in blocks:
    safe = re.sub(r"[^A-Za-z0-9_.-]", "_", name.strip()) + ".crt"
    with open(os.path.join(mozdir, safe), "w") as f:
        f.write(body)
    h = subprocess.run(["openssl", "x509", "-subject_hash", "-noout"],
                       input=body, capture_output=True, text=True, check=True
                       ).stdout.strip()
    n = used.get(h, 0)
    used[h] = n + 1
    with open(os.path.join(certdir, f"{h}.{n}"), "w") as f:
        f.write(body)

print(f"split {len(blocks)} roots, {len(used)} distinct subject hashes")
PY

cat > "${DESTDIR}/usr/sbin/update-ca-certificates" <<'EOS'
#!/bin/sh
# update-ca-certificates — rebuild /etc/ssl/cert.pem and the CApath hash links
# from the shipped Mozilla roots plus any local ones.
#
# Drop extra roots (PEM, one cert per file, .crt) into
# /usr/local/share/ca-certificates and run this.
set -eu
CERTDIR=/etc/ssl/certs
SRC="/usr/share/ca-certificates/mozilla /usr/local/share/ca-certificates"

# Clear only what we generate: the hash entries and the bundles.  Anything an
# admin put here by hand under some other name is left alone.
for f in "${CERTDIR}"/*.[0-9]; do [ -e "$f" ] && rm -f "$f"; done
rm -f "${CERTDIR}/ca-certificates.crt" /etc/ssl/cert.pem

n=0
for dir in ${SRC}; do
    [ -d "${dir}" ] || continue
    for cert in "${dir}"/*.crt "${dir}"/*.pem; do
        [ -f "${cert}" ] || continue
        h=$(openssl x509 -subject_hash -noout -in "${cert}" 2>/dev/null) || continue
        i=0
        while [ -e "${CERTDIR}/${h}.${i}" ]; do i=$((i + 1)); done
        cp "${cert}" "${CERTDIR}/${h}.${i}"
        cat "${cert}" >> "${CERTDIR}/ca-certificates.crt"
        n=$((n + 1))
    done
done
cp "${CERTDIR}/ca-certificates.crt" /etc/ssl/cert.pem
echo "update-ca-certificates: ${n} roots installed"
EOS
chmod 0755 "${DESTDIR}/usr/sbin/update-ca-certificates"

# Assert the store is actually usable rather than merely present: OpenSSL must
# be able to verify a root out of the CApath by hash, which fails loudly if the
# hash naming is wrong.
_probe=$(ls "${DESTDIR}"/etc/ssl/certs/*.0 2>/dev/null | head -1)
[ -n "${_probe}" ] || { echo "build.sh: no hashed roots produced" >&2; exit 1; }
if ! openssl verify -CApath "${DESTDIR}/etc/ssl/certs" \
                    -CAfile "${DESTDIR}/etc/ssl/cert.pem" \
                    "${_probe}" >/dev/null 2>&1; then
    echo "build.sh: CApath self-check failed on ${_probe}" >&2
    exit 1
fi

echo "==> ${LIB} ${VERSION} staged under ${DESTDIR}"
echo "    roots:  $(grep -c 'BEGIN CERTIFICATE' "${DESTDIR}/etc/ssl/cert.pem")"
echo "    CApath: $(ls "${DESTDIR}"/etc/ssl/certs/*.[0-9] 2>/dev/null | wc -l) hashed entries"
