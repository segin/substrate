# ca-certificates on Substrate

The system CA trust store: the Mozilla root bundle, laid out where OpenSSL
actually looks for it.

Before this port `/etc/ssl/certs` was an empty directory and there was no
`/etc/ssl/cert.pem` at all, so **every TLS verification failed** — `curl`
against any `https://` URL returned
`(60) SSL certificate problem: unable to get local issuer certificate`.

## What "port ca-certificates" means here

There is no ca-certificates tarball to compile.  Debian's package is a shell
script plus the trust store extracted from NSS; the store is the deliverable.
curl.se republishes exactly that store as one PEM bundle generated from NSS's
`certdata.txt`, and that is what this port installs.

`fetch.sh` pins the **dated** URL (`cacert-YYYY-MM-DD.pem`), not the floating
`cacert.pem`, because the latter changes whenever Mozilla updates the store —
pinning it would make the port unreproducible and break SHA verification on
every upstream refresh.  Upstream publishes a matching `.sha256`, so the hash
in `fetch.sh` is independently checkable.

To update: bump `VERSION`, drop in the new SHA256, re-run `./fetch.sh` and
`./build.sh`.

## Build

    ./fetch.sh && ./build.sh

Stages into `dist-overlay/dist-ca-certificates`.  Nothing is cross-compiled;
the host `openssl` is used to compute subject hashes.

## Installed layout

| path | what |
|---|---|
| `/etc/ssl/cert.pem` | the full bundle — OpenSSL's default **CAfile** |
| `/etc/ssl/certs/<hash>.<n>` | one file per root — OpenSSL's default **CApath** |
| `/etc/ssl/certs/ca-certificates.crt` | same bundle at Debian's path, which plenty of software hardcodes |
| `/usr/share/ca-certificates/mozilla/*.crt` | the split roots, kept so the store can be rebuilt |
| `/usr/sbin/update-ca-certificates` | rebuild after adding a local root |

The two default paths are not arbitrary: substrate's OpenSSL is configured
`--openssldir=/etc/ssl` (`contrib/openssl/build.sh`), which makes its defaults
`$OPENSSLDIR/cert.pem` and `$OPENSSLDIR/certs`.

**The CApath filenames carry meaning.**  Lookup is by *subject hash*, not by
name: OpenSSL hashes the issuer it wants and opens `<hash>.0`, `<hash>.1`, …
until one matches.  Two roots can collide on a hash, hence the counter.  The
hashes are computed with the host `openssl`, which is safe because
`X509_NAME_hash_ex` has been stable since OpenSSL 1.0.0 — but `build.sh`
asserts the result actually verifies rather than assuming it.

## Adding your own root

    cp myca.crt /usr/local/share/ca-certificates/
    update-ca-certificates

## curl needed a matching change

Installing the store is necessary but not sufficient.  curl's `configure`
probes the **build host** for a trust store and compiles in whatever it finds;
substrate's was empty at the time, so curl had no default baked in and failed
even with the store present.  `contrib/curl/build.sh` now passes
`--with-ca-bundle=/etc/ssl/cert.pem --with-ca-path=/etc/ssl/certs`
explicitly, so it cannot silently regress to "no default" again.

## Verified on target

With networking up, from the guest:

* `curl https://curl.se/ca/` — `http_code=200 verify=0`, no flags
* `curl https://www.google.com/` — `http_code=200 verify=0` (a different root)
* `curl https://expired.badssl.com/` — **rejected**, "certificate has expired"
* `openssl verify -CApath /etc/ssl/certs /etc/ssl/certs/002c0b4f.0` — `OK`

The expired-certificate case is the one that matters: it shows verification is
really happening and not quietly disabled.
