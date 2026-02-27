#ifndef CP_ATOMIC_H
#define CP_ATOMIC_H

#include <sys/types.h>

int cp_atomic_open_temp(const char *dest_path, mode_t mode,
                        char **tmp_path_out, int *fd_out);

int cp_atomic_commit(int fd, const char *tmp_path, const char *dest_path);

void cp_atomic_cleanup(int fd, const char *tmp_path);

#endif
