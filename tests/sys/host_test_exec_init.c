#include <sys/exec.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <vfs/vfs.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

process_t *current_process;
thread_t *current_thread;
static fs_node_t fake_node;
static file_t fake_file;
static uint32_t last_perm_uid;
static uint32_t last_perm_gid;
static int last_perm_mode;
static int permission_result;

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

void script_init_handler(void) {
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
    return 0;
}

int kern_close(int fd) {
    (void)fd;
    return 0;
}

int kern_read(int fd, char *buf, size_t count) {
    (void)fd;
    if (count >= 4) {
        buf[0] = 0x7f;
        buf[1] = 'E';
        buf[2] = 'L';
        buf[3] = 'F';
        return 4;
    }
    return 0;
}

int vfs_check_permissions_groups(fs_node_t *node, uint32_t uid, uint32_t gid,
                                 const uint32_t *groups, int ngroups, int mode) {
    (void)node;
    (void)groups;
    (void)ngroups;
    last_perm_uid = uid;
    last_perm_gid = gid;
    last_perm_mode = mode;
    return permission_result;
}

int smp_get_cpu_id(void) {
    return 0;
}

void kprint(const char *msg) {
    (void)msg;
}

int main(void) {
    process_t proc;

    memset(&proc, 0, sizeof(proc));
    memset(&fake_node, 0, sizeof(fake_node));
    memset(&fake_file, 0, sizeof(fake_file));
    fake_file.f_data = &fake_node;
    proc.fds[0] = &fake_file;
    current_process = &proc;

    exec_init();

    if (!exec_handler_registered("ELKS a.out")) {
        fprintf(stderr, "FAIL: ELKS handler not registered by exec_init\n");
        return 1;
    }

    current_process->uid = 1000;
    current_process->gid = 1000;
    current_process->euid = 2000;
    current_process->egid = 3000;
    permission_result = 0;
    last_perm_uid = 0;
    last_perm_gid = 0;
    last_perm_mode = 0;

    if (exec_dispatch("/bin/test", NULL, NULL) != 0) {
        fprintf(stderr, "FAIL: exec_dispatch rejected executable unexpectedly\n");
        return 1;
    }
    if (last_perm_uid != current_process->euid || last_perm_gid != current_process->egid || last_perm_mode != X_OK) {
        fprintf(stderr, "FAIL: exec_dispatch used wrong credentials for X_OK check\n");
        return 1;
    }

    permission_result = -1;
    if (exec_dispatch("/bin/test", NULL, NULL) != -EACCES) {
        fprintf(stderr, "FAIL: exec_dispatch did not reject non-executable file\n");
        return 1;
    }

    current_process->uid = 0;
    current_process->gid = 0;
    current_process->euid = 0;
    current_process->egid = 0;
    permission_result = -1;
    last_perm_uid = 1234;
    last_perm_gid = 5678;
    last_perm_mode = 0;
    if (exec_dispatch("/bin/test", NULL, NULL) != -EACCES) {
        fprintf(stderr, "FAIL: exec_dispatch did not propagate root execute denial\n");
        return 1;
    }
    if (last_perm_uid != 0 || last_perm_gid != 0 || last_perm_mode != X_OK) {
        fprintf(stderr, "FAIL: exec_dispatch did not use root effective credentials for X_OK check\n");
        return 1;
    }

    puts("host_test_exec_init: ok");
    return 0;
}
