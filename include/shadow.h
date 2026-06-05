/*
 * <shadow.h> — shadow password database (/etc/shadow).
 *
 * Substrate libc provides the reentrant shadow lookups getspnam_r(3),
 * getspent_r(3), fgetspent_r(3) and sgetspent_r(3): each parses a shadow entry
 * into a caller-supplied `struct spwd` plus a caller-supplied scratch buffer.
 * setspent()/endspent() position the internal stream used by getspent_r().
 */

#ifndef _SHADOW_H
#define _SHADOW_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spwd {
    char         *sp_namp;     /* login name */
    char         *sp_pwdp;     /* encrypted password */
    long          sp_lstchg;   /* days since epoch of last change */
    long          sp_min;      /* days before password may be changed */
    long          sp_max;      /* days after which it must be changed */
    long          sp_warn;     /* days before expiry to warn */
    long          sp_inact;    /* days after expiry until account disabled */
    long          sp_expire;   /* days since epoch when account expires */
    unsigned long sp_flag;     /* reserved */
};

void setspent(void);
void endspent(void);

int getspnam_r(const char *name, struct spwd *result_buf, char *buffer,
               size_t buflen, struct spwd **result);
int getspent_r(struct spwd *result_buf, char *buffer, size_t buflen,
               struct spwd **result);
int fgetspent_r(FILE *stream, struct spwd *result_buf, char *buffer,
                size_t buflen, struct spwd **result);
int sgetspent_r(const char *string, struct spwd *result_buf, char *buffer,
                size_t buflen, struct spwd **result);

#ifdef __cplusplus
}
#endif

#endif /* _SHADOW_H */
