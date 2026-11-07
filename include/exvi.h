#ifndef _EXVI_H
#define _EXVI_H

/*
 * libexvi's interface: the whole of what libexvi.so.0 exports
 * (usr.lib/exvi/exvi.map).
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EXVI_FRONTEND_EX = 0,
    EXVI_FRONTEND_VI = 1
} exvi_frontend_t;

#define EXVI_EXIT_VISUAL_HANDOFF 2
#define EXVI_EXIT_EX_HANDOFF 3

int exvi_main(int argc, char **argv, exvi_frontend_t frontend);
const char *exvi_handoff_file(void);
int exvi_readonly_mode(void);

#ifdef __cplusplus
}
#endif

#endif
