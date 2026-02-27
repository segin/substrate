#ifndef CP_PRESERVE_H
#define CP_PRESERVE_H

#include "cp_opts.h"

#include <sys/stat.h>

typedef void (*cp_preserve_warn_cb)(void *userdata,
                                    const char *src_path,
                                    const char *dst_path,
                                    const char *reason,
                                    int errnum);

int cp_preserve_metadata(const struct cp_options *opts,
                         const char *src_path,
                         const struct stat *src_st,
                         const char *dst_path,
                         int dst_fd,
                         int is_symlink,
                         cp_preserve_warn_cb warn_cb,
                         void *warn_userdata);

#endif
