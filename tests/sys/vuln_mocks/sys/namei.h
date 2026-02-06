#ifndef _SYS_NAMEI_H
#define _SYS_NAMEI_H

struct componentname {
    char *cn_nameptr;
};

// VDIR is usually defined in sys/vfs/vnode.h enum, but if we need the macro:
// sys/vfs/vnode.h defines enum vtype { VDIR, ... }.
// It does NOT define VDIR as a macro.
// udf.c checks: if (dvp->v_type != VDIR)
// So we don't need #define VDIR if we include vnode.h.
// But if sys/namei.h is included alone, it might not have VDIR.
// udf.c includes vnode.h before namei.h. So VDIR is available.

#endif
