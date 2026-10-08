#include <stddef.h>
#include <string.h>

#include <exec/perso/personality.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/stat.h>

static struct personality *personalities[PERS_MAX] = {
    [PERS_NATIVE]  = &personality_native,
    [PERS_LINUX]   = &personality_linux,
    [PERS_SVR4]    = &personality_svr4,
    [PERS_SVR3]    = &personality_svr3,
    [PERS_SOLARIS] = NULL,
    [PERS_FREEBSD] = &personality_freebsd,
    [PERS_NETBSD]  = &personality_netbsd,
    [PERS_OPENBSD] = &personality_openbsd,
    [PERS_SUNOS]   = &personality_sunos,
    [PERS_ELKS]    = &personality_elks,
    [PERS_XENIX]   = &personality_xenix,
};

struct personality *perso_lookup(int id) {
    if (id < 0 || id >= PERS_MAX) return NULL;
    if (id == PERS_ELKS) {
        elks_personality_init();
    }
    return personalities[id];
}

const char *perso_name(int id) {
    struct personality *p = perso_lookup(id);
    return p ? p->name : "unknown";
}

/*
 * Where a personality that works in its own tree (struct personality.
 * works_in_tree) means by the absolute `path`: the name under the tree, if
 * the file is there or the directory it would be made in is; otherwise the
 * name as given, which is substrate's.
 *
 * Substrate looks a file that exists up under the tree first, and that is
 * all it does on its own.  A name that does not exist yet falls through to
 * substrate's root -- so a program could read /export/x, where /export is
 * the tree's alone, and not create /export/y beside it; could create
 * /tmp/x and then not find it in the /tmp it lists -- and mkdir, rmdir,
 * unlink, link, rename, chmod, chown and chdir never looked under the
 * tree at all.  A personality that passes every path it is given through
 * here means the same file by the same name in all of them.
 *
 * /dev is left alone where the personality's devices are the kernel's, and
 * so is a name that is under the tree already.  Returns 1 with the name to
 * use in `out`, 0 if `path` is to be used as it is.
 */
int perso_tree_path(const char *path, char *out, size_t size) {
    struct personality *p = current_process
        ? perso_lookup(current_process->perso_id) : NULL;
    struct stat st;
    size_t plen, len;
    char *slash;
    int in_tree;

    if (!p || !p->works_in_tree || !p->path_prefix || !p->path_prefix[0] ||
        !path || path[0] != '/') {
        return 0;
    }
    plen = strlen(p->path_prefix);
    len = strlen(path);
    if (plen + len + 1U > size) {
        return 0;
    }
    if (strncmp(path, p->path_prefix, plen) == 0 &&
        (path[plen] == '/' || path[plen] == '\0')) {
        return 0;
    }
    if (p->native_dev && strncmp(path, "/dev", 4) == 0 &&
        (path[4] == '/' || path[4] == '\0')) {
        return 0;
    }
    memcpy(out, p->path_prefix, plen);
    memcpy(out + plen, path, len + 1U);

    in_tree = kern_lstat(out, &st) == 0;
    if (!in_tree) {
        /* Not there: is the directory it would go in? */
        slash = strrchr(out + plen, '/');
        if (slash && slash > out + plen) {
            *slash = '\0';
            in_tree = kern_stat(out, &st) == 0 && S_ISDIR(st.st_mode);
            *slash = '/';
        } else {
            in_tree = 1;                /* directly under the tree's root */
        }
    }
    return in_tree;
}
