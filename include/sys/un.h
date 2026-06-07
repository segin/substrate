/*
 * <sys/un.h> — Unix domain socket address (stub).
 */
#ifndef _SYS_UN_H
#define _SYS_UN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/socket.h>

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[108];
};

/* Actual length of a filled-in sockaddr_un (offset of sun_path + the path
 * string length).  Needs <string.h> for strlen at the use site. */
#define SUN_LEN(ptr) \
    ((size_t)(((struct sockaddr_un *)0)->sun_path) + strlen((ptr)->sun_path))

#ifdef __cplusplus
}
#endif
#endif
