#ifndef _EXVI_H
#define _EXVI_H

typedef enum {
    EXVI_FRONTEND_EX = 0,
    EXVI_FRONTEND_VI = 1
} exvi_frontend_t;

#define EXVI_EXIT_VISUAL_HANDOFF 2

int exvi_main(int argc, char **argv, exvi_frontend_t frontend);
const char *exvi_handoff_file(void);
int exvi_readonly_mode(void);

#endif
