/*
 * lib/c/src/crypt.c — password hashing dispatcher.
 *
 *   "$5$..."  → SHA-256 crypt (crypt_sha256.c)
 *   "$1$..."  → MD5 crypt     (crypt_md5.c)
 *   other     → traditional DES crypt (crypt_des.c)
 *
 * The static return buffer is in crypt_data.buf; callers that need
 * thread safety use crypt_r with their own struct.
 */

#include <crypt.h>
#include <stddef.h>
#include <string.h>

extern char *__crypt_sha256(const char *key, const char *setting,
                            char *out, size_t outsz);
extern char *__crypt_md5(const char *key, const char *setting,
                         char *out, size_t outsz);
extern char *__crypt_des(const char *key, const char *setting,
                         char *out, size_t outsz);

static struct crypt_data __crypt_static;

char *
crypt_r(const char *key, const char *setting, struct crypt_data *data)
{
    if (key == NULL || setting == NULL || data == NULL) {
        return NULL;
    }
    if (setting[0] == '$' && setting[1] == '5' && setting[2] == '$') {
        return __crypt_sha256(key, setting, data->buf, sizeof(data->buf));
    }
    if (setting[0] == '$' && setting[1] == '1' && setting[2] == '$') {
        return __crypt_md5(key, setting, data->buf, sizeof(data->buf));
    }
    /* Anything else (including 2-char salts) → traditional DES. */
    return __crypt_des(key, setting, data->buf, sizeof(data->buf));
}

char *
crypt(const char *key, const char *setting)
{
    return crypt_r(key, setting, &__crypt_static);
}
