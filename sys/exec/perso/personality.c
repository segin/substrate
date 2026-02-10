#include <exec/perso/personality.h>
#include <stddef.h>

extern struct personality personality_native;
extern struct personality personality_linux;
extern struct personality personality_freebsd;
extern struct personality personality_netbsd;
extern struct personality personality_openbsd;
extern struct personality personality_svr3;
extern struct personality personality_svr4;
// extern struct personality personality_solaris;
extern struct personality personality_sunos;

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
};

struct personality *perso_lookup(int id) {
    if (id < 0 || id >= PERS_MAX) return NULL;
    return personalities[id];
}

const char *perso_name(int id) {
    struct personality *p = perso_lookup(id);
    return p ? p->name : "unknown";
}
