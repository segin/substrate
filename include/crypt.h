#ifndef _CRYPT_H
#define _CRYPT_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * crypt(3) — password hashing.
 *
 * `setting` selects the algorithm by leading prefix and supplies
 * the salt:
 *
 *   "$5$<salt>"             SHA-256-crypt (Ulrich Drepper, glibc)
 *   "$5$rounds=<n>$<salt>"  SHA-256-crypt with explicit round count
 *   "$1$<salt>"             MD5-crypt (Poul-Henning Kamp, FreeBSD)
 *   anything else (2-char)  Traditional DES crypt (legacy)
 *
 * Salts run up to "$" or 8/16 chars depending on scheme.
 *
 * Returns a pointer to a static buffer; subsequent calls overwrite
 * it.  Returns NULL on invalid setting.
 *
 * Use crypt_r for reentrancy.
 *
 * IMPORTANT: $1$ MD5 and traditional DES are kept for compatibility
 * with imported hashes (e.g. someone pastes an old /etc/shadow row
 * over). Don't generate new MD5 or DES hashes — use $5$ for new
 * passwords.  passwd(1) defaults to $5$.
 */
char *crypt(const char *key, const char *setting);

/*
 * Reentrant variant.  `data` is a caller-owned scratch buffer at
 * least 128 bytes; the returned pointer is into that buffer.
 */
struct crypt_data {
    char buf[256];
    char internal[1024];
};
char *crypt_r(const char *key, const char *setting, struct crypt_data *data);

#ifdef __cplusplus
}
#endif

#endif
