/*
 * mountopt.c — implementation of the generic mount-option parser.
 *
 * Operates on a heap-allocated linked list keyed by string.  Each
 * key/value pair is its own kmalloc'd buffer so the order is
 * preserved (filesystems sometimes care about precedence between
 * ro,user_xattr,nouser_xattr — last wins).
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <sys/errno.h>
#include <sys/mount.h>
#include <sys/mountopt.h>
#include <vm/vm_kmem.h>

static char *mo_strndup(const char *s, size_t n) {
    char *p = kmalloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

void mountopt_free(mountopt_t *head) {
    while (head) {
        mountopt_t *next = head->next;
        if (head->key)   kfree(head->key,   strlen(head->key) + 1);
        if (head->value) kfree(head->value, strlen(head->value) + 1);
        kfree(head, sizeof(*head));
        head = next;
    }
}

mountopt_t *mountopt_parse(const char *opts, int *err) {
    if (err) *err = 0;
    if (!opts || !*opts) return NULL;

    mountopt_t *head = NULL;
    mountopt_t *tail = NULL;

    const char *p = opts;
    while (*p) {
        /* Skip leading commas/whitespace between options. */
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;

        const char *kstart = p;
        while (*p && *p != ',' && *p != '=') p++;
        size_t klen = (size_t)(p - kstart);
        if (klen == 0) {
            if (err) *err = -EINVAL;
            mountopt_free(head);
            return NULL;
        }

        char *value = NULL;
        if (*p == '=') {
            p++;
            const char *vstart = p;
            while (*p && *p != ',') p++;
            size_t vlen = (size_t)(p - vstart);
            value = mo_strndup(vstart, vlen);
            if (!value) {
                if (err) *err = -ENOMEM;
                mountopt_free(head);
                return NULL;
            }
        }

        char *key = mo_strndup(kstart, klen);
        if (!key) {
            if (err) *err = -ENOMEM;
            if (value) kfree(value, strlen(value) + 1);
            mountopt_free(head);
            return NULL;
        }

        mountopt_t *node = kmalloc(sizeof(*node));
        if (!node) {
            if (err) *err = -ENOMEM;
            kfree(key, strlen(key) + 1);
            if (value) kfree(value, strlen(value) + 1);
            mountopt_free(head);
            return NULL;
        }
        node->key = key;
        node->value = value;
        node->next = NULL;

        if (tail) {
            tail->next = node;
        } else {
            head = node;
        }
        tail = node;
    }

    return head;
}

static mountopt_t *mountopt_lookup(mountopt_t *head, const char *key) {
    /* Last-occurrence wins so "ro,rw" yields rw. */
    mountopt_t *found = NULL;
    for (mountopt_t *m = head; m; m = m->next) {
        if (strcmp(m->key, key) == 0) found = m;
    }
    return found;
}

int mountopt_has(mountopt_t *head, const char *key) {
    return mountopt_lookup(head, key) != NULL;
}

int mountopt_get_string(mountopt_t *head, const char *key, const char **out) {
    mountopt_t *m = mountopt_lookup(head, key);
    if (!m) return -ENOENT;
    if (!m->value) return -EINVAL;
    if (out) *out = m->value;
    return 0;
}

int mountopt_get_int(mountopt_t *head, const char *key, long *out) {
    mountopt_t *m = mountopt_lookup(head, key);
    if (!m) return -ENOENT;
    if (!m->value) return -EINVAL;

    /* strtol with strict end-of-string check. */
    const char *s = m->value;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    if (!*s) return -EINVAL;
    long v = 0;
    while (*s >= '0' && *s <= '9') {
        long d = *s - '0';
        /* overflow guard */
        if (v > (LONG_MAX - d) / 10) return -EINVAL;
        v = v * 10 + d;
        s++;
    }
    if (*s != '\0') return -EINVAL;
    if (out) *out = neg ? -v : v;
    return 0;
}

int mountopt_get_uint(mountopt_t *head, const char *key, unsigned long *out) {
    long sv;
    int rc = mountopt_get_int(head, key, &sv);
    if (rc) return rc;
    if (sv < 0) return -EINVAL;
    if (out) *out = (unsigned long)sv;
    return 0;
}

int mountopt_get_bool(mountopt_t *head, const char *key, int *out) {
    mountopt_t *m = mountopt_lookup(head, key);
    if (!m) return -ENOENT;
    if (!m->value) {
        /* Bare key — implicit true. */
        if (out) *out = 1;
        return 0;
    }
    const char *v = m->value;
    if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 ||
        strcmp(v, "yes") == 0 || strcmp(v, "on") == 0) {
        if (out) *out = 1;
        return 0;
    }
    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0 ||
        strcmp(v, "no")  == 0 || strcmp(v, "off") == 0) {
        if (out) *out = 0;
        return 0;
    }
    return -EINVAL;
}

int mountopt_apply_generic(mountopt_t *head, uint32_t *flags) {
    if (!flags) return -EINVAL;

    /*
     * [VFS-30] Apply in list order, LAST OCCURRENCE WINS.
     *
     * This used to collect saw_ro/saw_rw/... booleans and then reject any
     * pair that both appeared:
     *
     *     if ((saw_ro && saw_rw) || ...) return -EINVAL;
     *
     * which directly contradicts mountopt_lookup(), whose own comment says
     * "Last-occurrence wins so \"ro,rw\" yields rw" -- the same option
     * string is accepted by the parser and rejected by the applier.  It is
     * also what every other Unix does, and it is what makes option
     * shorthands work: the moment `defaults` expands to
     * "rw,suid,dev,exec,async", `-o defaults,ro` becomes "rw,...,ro" and
     * would have been rejected outright.
     *
     * Applying each option as it is encountered gives last-wins for free
     * and needs no conflict table.
     */
    for (mountopt_t *m = head; m; m = m->next) {
        if      (strcmp(m->key, "ro")     == 0) *flags |=  MNT_RDONLY;
        else if (strcmp(m->key, "rw")     == 0) *flags &= ~MNT_RDONLY;
        else if (strcmp(m->key, "nosuid") == 0) *flags |=  MNT_NOSUID;
        else if (strcmp(m->key, "suid")   == 0) *flags &= ~MNT_NOSUID;
        else if (strcmp(m->key, "nodev")  == 0) *flags |=  MNT_NODEV;
        else if (strcmp(m->key, "dev")    == 0) *flags &= ~MNT_NODEV;
        else if (strcmp(m->key, "noexec") == 0) *flags |=  MNT_NOEXEC;
        else if (strcmp(m->key, "exec")   == 0) *flags &= ~MNT_NOEXEC;
        else if (strcmp(m->key, "sync")   == 0) {
            /* [VFS-29] each of the pair clears its opposite. */
            *flags |=  MNT_SYNCHRONOUS;
            *flags &= ~MNT_ASYNC;
        } else if (strcmp(m->key, "async") == 0) {
            *flags |=  MNT_ASYNC;
            *flags &= ~MNT_SYNCHRONOUS;
        }
    }

    return 0;
}
