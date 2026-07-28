/*
 * netdb_r.c — reentrant network-database lookups.
 *
 * The reentrant _r forms of the gethost, getserv, getproto and getnet family.
 * Each calls the corresponding non-reentrant lookup (which returns a pointer
 * into static storage) and then deep-copies the result — every string, the
 * NULL-terminated alias vector, and (for hosts) the address vector — into the
 * caller-supplied buffer, so the returned struct is wholly owned by the caller.
 * If the buffer is too small the call reports ERANGE and *result is NULL.
 *
 * A small spinlock serializes the window between the underlying call and the
 * copy-out, so two threads cannot clobber each other's static result mid-copy.
 */

#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <string.h>

#include <sys/socket.h>

static volatile int netdb_lock;
static void lock(void)   { while (__sync_lock_test_and_set(&netdb_lock, 1)) { } }
static void unlock(void) { __sync_lock_release(&netdb_lock); }

/* ---- packing a deep copy into the caller buffer ---- */
struct packer {
    char *p;
    char *end;
    int   ok;
};

static char *
pk_str(struct packer *k, const char *s)
{
    size_t n;
    char  *r;
    if (s == NULL)
        return NULL;
    n = strlen(s) + 1;
    if (k->p + n > k->end) { k->ok = 0; return NULL; }
    r = k->p;
    memcpy(r, s, n);
    k->p += n;
    return r;
}

static void
pk_align(struct packer *k)
{
    while (((uintptr_t)k->p) & (sizeof(void *) - 1)) {
        if (k->p >= k->end) { k->ok = 0; return; }
        k->p++;
    }
}

static char *
pk_blob(struct packer *k, const void *b, size_t n)
{
    char *r;
    if (k->p + n > k->end) { k->ok = 0; return NULL; }
    r = k->p;
    memcpy(r, b, n);
    k->p += n;
    return r;
}

/* Copy a NULL-terminated array of C strings. */
static char **
pk_strvec(struct packer *k, char **vec)
{
    size_t n = 0, i;
    char **out;
    if (vec)
        while (vec[n]) n++;
    pk_align(k);
    out = (char **)k->p;
    if (k->p + (n + 1) * sizeof(char *) > k->end) { k->ok = 0; return NULL; }
    k->p += (n + 1) * sizeof(char *);
    for (i = 0; i < n; i++)
        out[i] = pk_str(k, vec[i]);
    out[n] = NULL;
    return out;
}

/* Copy a NULL-terminated array of fixed-size address blobs. */
static char **
pk_addrvec(struct packer *k, char **vec, int alen)
{
    size_t n = 0, i;
    char **out;
    if (vec)
        while (vec[n]) n++;
    pk_align(k);
    out = (char **)k->p;
    if (k->p + (n + 1) * sizeof(char *) > k->end) { k->ok = 0; return NULL; }
    k->p += (n + 1) * sizeof(char *);
    for (i = 0; i < n; i++)
        out[i] = pk_blob(k, vec[i], (size_t)alen);
    out[n] = NULL;
    return out;
}

/* ---- hosts ---- */
static int
copy_hostent(struct hostent *src, struct hostent *ret, char *buf, size_t buflen,
             struct hostent **result, int *h_errnop)
{
    struct packer k = { buf, buf + buflen, 1 };
    if (src == NULL) {
        *result = NULL;
        if (h_errnop) *h_errnop = h_errno;
        return 0;                       /* not found, not an error */
    }
    ret->h_addrtype = src->h_addrtype;
    ret->h_length   = src->h_length;
    ret->h_name     = pk_str(&k, src->h_name);
    ret->h_aliases  = pk_strvec(&k, src->h_aliases);
    ret->h_addr_list = pk_addrvec(&k, src->h_addr_list, src->h_length);
    if (!k.ok) {
        *result = NULL;
        if (h_errnop) *h_errnop = NETDB_INTERNAL;
        return ERANGE;
    }
    *result = ret;
    if (h_errnop) *h_errnop = NETDB_SUCCESS;
    return 0;
}

int
gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen,
                struct hostent **result, int *h_errnop)
{
    int rc;
    lock();
    rc = copy_hostent(gethostbyname(name), ret, buf, buflen, result, h_errnop);
    unlock();
    return rc;
}

int
gethostbyname2_r(const char *name, int af, struct hostent *ret, char *buf,
                 size_t buflen, struct hostent **result, int *h_errnop)
{
    int rc;
    struct hostent *src;
    lock();
    /* substrate's resolver is IPv4-only; for AF_INET defer to gethostbyname,
     * otherwise report "not found" via the standard h_errno path. */
    if (af == AF_INET) {
        src = gethostbyname(name);
    } else {
        src = NULL;
        h_errno = HOST_NOT_FOUND;
    }
    rc = copy_hostent(src, ret, buf, buflen, result, h_errnop);
    unlock();
    return rc;
}

int
gethostbyaddr_r(const void *addr, socklen_t len, int type, struct hostent *ret,
                char *buf, size_t buflen, struct hostent **result, int *h_errnop)
{
    int rc;
    lock();
    rc = copy_hostent(gethostbyaddr(addr, len, type), ret, buf, buflen,
                      result, h_errnop);
    unlock();
    return rc;
}

int
gethostent_r(struct hostent *ret, char *buf, size_t buflen,
             struct hostent **result, int *h_errnop)
{
    int rc;
    lock();
    rc = copy_hostent(gethostent(), ret, buf, buflen, result, h_errnop);
    unlock();
    return rc;
}

/* ---- services ---- */
static int
copy_servent(struct servent *src, struct servent *ret, char *buf, size_t buflen,
             struct servent **result)
{
    struct packer k = { buf, buf + buflen, 1 };
    if (src == NULL) { *result = NULL; return 0; }
    ret->s_port    = src->s_port;
    ret->s_name    = pk_str(&k, src->s_name);
    ret->s_proto   = pk_str(&k, src->s_proto);
    ret->s_aliases = pk_strvec(&k, src->s_aliases);
    if (!k.ok) { *result = NULL; return ERANGE; }
    *result = ret;
    return 0;
}

int
getservbyname_r(const char *name, const char *proto, struct servent *ret,
                char *buf, size_t buflen, struct servent **result)
{
    int rc;
    lock();
    rc = copy_servent(getservbyname(name, proto), ret, buf, buflen, result);
    unlock();
    return rc;
}

int
getservbyport_r(int port, const char *proto, struct servent *ret,
                char *buf, size_t buflen, struct servent **result)
{
    int rc;
    lock();
    rc = copy_servent(getservbyport(port, proto), ret, buf, buflen, result);
    unlock();
    return rc;
}

int
getservent_r(struct servent *ret, char *buf, size_t buflen,
             struct servent **result)
{
    int rc;
    lock();
    rc = copy_servent(getservent(), ret, buf, buflen, result);
    unlock();
    return rc;
}

/* ---- protocols ---- */
static int
copy_protoent(struct protoent *src, struct protoent *ret, char *buf,
              size_t buflen, struct protoent **result)
{
    struct packer k = { buf, buf + buflen, 1 };
    if (src == NULL) { *result = NULL; return 0; }
    ret->p_proto   = src->p_proto;
    ret->p_name    = pk_str(&k, src->p_name);
    ret->p_aliases = pk_strvec(&k, src->p_aliases);
    if (!k.ok) { *result = NULL; return ERANGE; }
    *result = ret;
    return 0;
}

int
getprotobyname_r(const char *name, struct protoent *ret, char *buf,
                 size_t buflen, struct protoent **result)
{
    int rc;
    lock();
    rc = copy_protoent(getprotobyname(name), ret, buf, buflen, result);
    unlock();
    return rc;
}

int
getprotobynumber_r(int proto, struct protoent *ret, char *buf, size_t buflen,
                   struct protoent **result)
{
    int rc;
    lock();
    rc = copy_protoent(getprotobynumber(proto), ret, buf, buflen, result);
    unlock();
    return rc;
}

int
getprotoent_r(struct protoent *ret, char *buf, size_t buflen,
              struct protoent **result)
{
    int rc;
    lock();
    rc = copy_protoent(getprotoent(), ret, buf, buflen, result);
    unlock();
    return rc;
}

/* ---- networks ---- */
static int
copy_netent(struct netent *src, struct netent *ret, char *buf, size_t buflen,
            struct netent **result)
{
    struct packer k = { buf, buf + buflen, 1 };
    if (src == NULL) { *result = NULL; return 0; }
    ret->n_addrtype = src->n_addrtype;
    ret->n_net      = src->n_net;
    ret->n_name     = pk_str(&k, src->n_name);
    ret->n_aliases  = pk_strvec(&k, src->n_aliases);
    if (!k.ok) { *result = NULL; return ERANGE; }
    *result = ret;
    return 0;
}

int
getnetbyname_r(const char *name, struct netent *ret, char *buf, size_t buflen,
               struct netent **result)
{
    int rc;
    lock();
    rc = copy_netent(getnetbyname(name), ret, buf, buflen, result);
    unlock();
    return rc;
}

int
getnetbyaddr_r(uint32_t net, int type, struct netent *ret, char *buf,
               size_t buflen, struct netent **result)
{
    int rc;
    lock();
    rc = copy_netent(getnetbyaddr(net, type), ret, buf, buflen, result);
    unlock();
    return rc;
}

int
getnetent_r(struct netent *ret, char *buf, size_t buflen,
            struct netent **result)
{
    int rc;
    lock();
    rc = copy_netent(getnetent(), ret, buf, buflen, result);
    unlock();
    return rc;
}
