/*
 * In a static initializer, an array member of a static object, used as a
 * value, is the address of its first element.  Linux's init_files has
 * `.open_fds = init_files.open_fds_init` beside `&init_files.fdtab`;
 * cc took the address form but refused the array ("unsupported scalar in
 * global initializer (expr kind 5 ...)").
 */
#include <stdio.h>

struct fdtable {
    unsigned long *close_on_exec;
    int **fd;
};

struct files_struct {
    struct fdtable *fdt;
    struct fdtable fdtab;
    int *fd_array[4];
    unsigned long close_on_exec_init[1];
    char name[8];
};

struct files_struct init_files = {
    .fdt = &init_files.fdtab,
    .fdtab = {.close_on_exec = init_files.close_on_exec_init, .fd = &init_files.fd_array[0]},
};

static char *np = init_files.name;
static char *np2 = init_files.name + 2;
static unsigned long *cp = (init_files.close_on_exec_init);

int main(void) {
    printf("%d %d %d\n", init_files.fdt == &init_files.fdtab,
           init_files.fdtab.close_on_exec == init_files.close_on_exec_init,
           init_files.fdtab.fd == &init_files.fd_array[0]);
    printf("%d %d %d\n", np == init_files.name, np2 == init_files.name + 2, cp == init_files.close_on_exec_init);
    return 0;
}
