#ifndef _EXVI_H
#define _EXVI_H

typedef enum {
    EXVI_FRONTEND_EX = 0,
    EXVI_FRONTEND_VI = 1
} exvi_frontend_t;

int exvi_main(int argc, char **argv, exvi_frontend_t frontend);

#endif
