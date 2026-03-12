#include <sys/exec.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

process_t *current_process;
thread_t *current_thread;

static int fake_check(const char *path, const char *header_buf, size_t len) {
    (void)path;
    (void)header_buf;
    (void)len;
    return 0;
}

static int fake_load(int fd, const char *path, char *const argv[], char *const envp[]) {
    (void)fd;
    (void)path;
    (void)argv;
    (void)envp;
    return 0;
}

void elks_init_handler(void) {
    static struct exec_binary_handler fake_elks_handler = {
        .name = "ELKS a.out",
        .check = fake_check,
        .load = fake_load,
        .next = NULL,
    };

    exec_register_handler(&fake_elks_handler);
}

int elks_check_file(const char *path, const char *header, size_t len) {
    (void)path;
    (void)header;
    (void)len;
    return -ENOEXEC;
}

int elks_load(int fd, const char *path, char *const argv[], char *const envp[]) {
    (void)fd;
    (void)path;
    (void)argv;
    (void)envp;
    return -ENOEXEC;
}

int elf_execve(int fd, const char *path, char *const argv[], char *const envp[]) {
    (void)fd;
    (void)path;
    (void)argv;
    (void)envp;
    return -ENOEXEC;
}

int kern_open(const char *path, int flags, int mode) {
    (void)path;
    (void)flags;
    (void)mode;
    return -ENOENT;
}

int kern_close(int fd) {
    (void)fd;
    return 0;
}

int kern_read(int fd, char *buf, size_t count) {
    (void)fd;
    (void)buf;
    (void)count;
    return -ENOENT;
}

int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode) {
    (void)node;
    (void)uid;
    (void)gid;
    (void)mode;
    return 0;
}

int smp_get_cpu_id(void) {
    return 0;
}

void kprint(const char *msg) {
    (void)msg;
}

int main(void) {
    exec_init();

    if (!exec_handler_registered("ELKS a.out")) {
        fprintf(stderr, "FAIL: ELKS handler not registered by exec_init\n");
        return 1;
    }

    puts("host_test_exec_init: ok");
    return 0;
}
