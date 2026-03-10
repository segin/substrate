typedef unsigned short u16;

static const char default_path[] = "/elks-cat.txt";
static char io_buf[128];

static int elks_syscall3(int nr, u16 a1, u16 a2, u16 a3) {
    int ret;

    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(nr), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

static void elks_exit(int status) {
    (void)elks_syscall3(1, (u16)status, 0, 0);
    for (;;) {
    }
}

static int elks_open(const char *path, int flags, int mode) {
    return elks_syscall3(5, (u16)path, (u16)flags, (u16)mode);
}

static int elks_close(int fd) {
    return elks_syscall3(6, (u16)fd, 0, 0);
}

static int elks_read(int fd, void *buf, int len) {
    return elks_syscall3(3, (u16)fd, (u16)buf, (u16)len);
}

static int elks_write(int fd, const void *buf, int len) {
    return elks_syscall3(4, (u16)fd, (u16)buf, (u16)len);
}

int cat_entry(u16 *stack_words) {
    u16 argc = stack_words[0];
    char **argv = (char **)&stack_words[1];
    const char *path = default_path;
    int fd;
    int rc;

    if (argc > 1 && argv[1]) {
        path = argv[1];
    }

    fd = elks_open(path, 0, 0);
    if (fd < 0) {
        return 1;
    }

    for (;;) {
        rc = elks_read(fd, io_buf, sizeof(io_buf));
        if (rc < 0) {
            elks_close(fd);
            return 1;
        }
        if (rc == 0) {
            break;
        }
        if (elks_write(1, io_buf, rc) != rc) {
            elks_close(fd);
            return 1;
        }
    }

    if (elks_close(fd) < 0) {
        return 1;
    }
    return 0;
}

__asm__(
    ".code16\n"
    ".global _start\n"
    "_start:\n"
    "    mov %sp, %ax\n"
    "    push %ax\n"
    "    call cat_entry\n"
    "    add $2, %sp\n"
    "    mov %ax, %bx\n"
    "    mov $1, %ax\n"
    "    int $0x80\n"
);
