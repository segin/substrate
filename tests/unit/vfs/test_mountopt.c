/*
 * Unit tests for the generic mount option parser.
 *
 * Covers: empty input, bare keys, key=value pairs, last-wins for
 * duplicate keys, integer/bool/string getters, conflict detection in
 * mountopt_apply_generic, and malformed input rejection.
 */
#include <sys/mountopt.h>
#include <sys/mount.h>
#include <sys/errno.h>
#include <stdbool.h>
#include <string.h>

#define ASSERT(cond) do { if (!(cond)) return false; } while (0)

bool test_mountopt_empty(void) {
    int err = -1;
    mountopt_t *h = mountopt_parse(NULL, &err);
    ASSERT(h == NULL && err == 0);
    h = mountopt_parse("", &err);
    ASSERT(h == NULL && err == 0);
    return true;
}

bool test_mountopt_basic(void) {
    int err = -1;
    mountopt_t *h = mountopt_parse("ro,nosuid,size=1024,name=foo", &err);
    ASSERT(h != NULL && err == 0);

    ASSERT(mountopt_has(h, "ro"));
    ASSERT(mountopt_has(h, "nosuid"));
    ASSERT(mountopt_has(h, "size"));
    ASSERT(mountopt_has(h, "name"));
    ASSERT(!mountopt_has(h, "missing"));

    long sz;
    ASSERT(mountopt_get_int(h, "size", &sz) == 0 && sz == 1024);

    const char *name = NULL;
    ASSERT(mountopt_get_string(h, "name", &name) == 0);
    ASSERT(strcmp(name, "foo") == 0);

    int b;
    ASSERT(mountopt_get_bool(h, "ro", &b) == 0 && b == 1);

    mountopt_free(h);
    return true;
}

bool test_mountopt_last_wins(void) {
    int err;
    mountopt_t *h = mountopt_parse("ro,rw,ro", &err);
    ASSERT(h != NULL);
    /* Last occurrence wins for the simple lookup helpers. */
    int b;
    ASSERT(mountopt_get_bool(h, "ro", &b) == 0 && b == 1);
    mountopt_free(h);
    return true;
}

bool test_mountopt_apply_generic(void) {
    int err;
    uint32_t flags;

    /* ro alone */
    mountopt_t *h = mountopt_parse("ro", &err);
    flags = 0;
    ASSERT(mountopt_apply_generic(h, &flags) == 0);
    ASSERT(flags & MNT_RDONLY);
    mountopt_free(h);

    /* nosuid + nodev + noexec */
    h = mountopt_parse("nosuid,nodev,noexec", &err);
    flags = 0;
    ASSERT(mountopt_apply_generic(h, &flags) == 0);
    ASSERT(flags & MNT_NOSUID);
    ASSERT(flags & MNT_NODEV);
    ASSERT(flags & MNT_NOEXEC);
    mountopt_free(h);

    /*
     * "ro,rw" and "sync,async" are not conflicts -- options apply in list
     * order and the last occurrence wins ([VFS-30]).  Rejecting the pair
     * contradicted mountopt_lookup(), which documents the same last-wins
     * rule, and would have made option shorthands unusable: the moment
     * `defaults` expands to "rw,suid,dev,exec,async", `-o defaults,ro`
     * becomes "rw,...,ro" and was refused outright.  These two asserted
     * -EINVAL.
     */
    h = mountopt_parse("ro,rw", &err);
    flags = 0;
    ASSERT(mountopt_apply_generic(h, &flags) == 0);
    ASSERT(!(flags & MNT_RDONLY));          /* rw came last */
    mountopt_free(h);

    h = mountopt_parse("rw,ro", &err);
    flags = 0;
    ASSERT(mountopt_apply_generic(h, &flags) == 0);
    ASSERT(flags & MNT_RDONLY);             /* ro came last */
    mountopt_free(h);

    h = mountopt_parse("sync,async", &err);
    flags = 0;
    ASSERT(mountopt_apply_generic(h, &flags) == 0);
    ASSERT(!(flags & MNT_SYNCHRONOUS));     /* async came last */
    mountopt_free(h);

    return true;
}

bool test_mountopt_int_overflow(void) {
    int err;
    mountopt_t *h = mountopt_parse("n=99999999999999999999", &err);
    ASSERT(h != NULL);
    long v;
    ASSERT(mountopt_get_int(h, "n", &v) == -EINVAL); /* overflow rejected */
    mountopt_free(h);

    h = mountopt_parse("n=abc", &err);
    ASSERT(mountopt_get_int(h, "n", &v) == -EINVAL); /* non-numeric */
    mountopt_free(h);

    h = mountopt_parse("n=42", &err);
    ASSERT(mountopt_get_int(h, "n", &v) == 0 && v == 42);
    mountopt_free(h);

    h = mountopt_parse("n=-7", &err);
    ASSERT(mountopt_get_int(h, "n", &v) == 0 && v == -7);
    mountopt_free(h);

    return true;
}

bool test_mountopt_bool_forms(void) {
    int err;
    mountopt_t *h = mountopt_parse("a=true,b=0,c=yes,d=off,e", &err);
    int v;
    ASSERT(mountopt_get_bool(h, "a", &v) == 0 && v == 1);
    ASSERT(mountopt_get_bool(h, "b", &v) == 0 && v == 0);
    ASSERT(mountopt_get_bool(h, "c", &v) == 0 && v == 1);
    ASSERT(mountopt_get_bool(h, "d", &v) == 0 && v == 0);
    ASSERT(mountopt_get_bool(h, "e", &v) == 0 && v == 1); /* bare */
    mountopt_free(h);
    return true;
}

bool test_mountopt_malformed(void) {
    int err = 0;
    /* leading comma is fine (treated as empty option) */
    mountopt_t *h = mountopt_parse(",ro", &err);
    ASSERT(h != NULL && err == 0);
    mountopt_free(h);

    /* equals with no key */
    err = 0;
    h = mountopt_parse("=value", &err);
    ASSERT(h == NULL && err == -EINVAL);

    return true;
}
