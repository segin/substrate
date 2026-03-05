#include "cc_backend.h"

#include <limits.h>
#include <stdio.h>

static void set_diag(cc_diag_t *diag, const char *msg) {
    if (diag == NULL || diag->message[0] != '\0') {
        return;
    }
    diag->path[0] = '\0';
    diag->line = 0;
    diag->col = 0;
    snprintf(diag->message, sizeof(diag->message), "%s", msg);
}

int cc_backend_checked_frame_add(int *raw_frame, int bytes, cc_diag_t *diag, const char *context) {
    if (raw_frame == NULL || bytes < 0) {
        set_diag(diag, "invalid frame accounting input");
        return(-1);
    }
    if (*raw_frame > INT_MAX - bytes) {
        if (context != NULL && context[0] != '\0' && diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "stack frame too large: %s", context);
        } else {
            set_diag(diag, "stack frame too large");
        }
        return(-1);
    }
    *raw_frame += bytes;
    return(0);
}

int cc_backend_align_frame_size(int raw_frame, int stack_align) {
    int a;

    if (raw_frame <= 0) {
        return(0);
    }
    if (stack_align <= 1) {
        return(raw_frame);
    }
    a = stack_align - 1;
    return((raw_frame + a) & ~a);
}
