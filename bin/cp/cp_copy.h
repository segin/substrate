#ifndef CP_COPY_H
#define CP_COPY_H

#include "cp_hardlink.h"
#include "cp_opts.h"

#include <signal.h>

struct cp_context {
    const char *progname;
    const struct cp_options *opts;
    volatile sig_atomic_t *stop_requested;
    struct cp_hardlink_map hardlinks;
    struct cp_devino_set visited_dirs;
    int had_error;
};

int cp_context_init(struct cp_context *ctx,
                    const struct cp_options *opts,
                    const char *progname,
                    volatile sig_atomic_t *stop_requested);

void cp_context_destroy(struct cp_context *ctx);

int cp_execute(struct cp_context *ctx, int argc, char **argv);

#endif
