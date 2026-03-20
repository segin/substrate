/*
 * exec/formats/script.c - Shebang (#!) Script Handler
 *
 * Handles execution of scripts with #! interpreter lines.
 * When a file starts with "#!", the kernel extracts the interpreter path
 * (and optional argument), then re-dispatches execution as:
 *   interpreter [arg] script_path [original_argv[1:]]
 *
 * Recursion depth is limited to prevent infinite loops.
 */

#include <exec/formats/script.h>
#include <sys/exec.h>
#include <sys/errno.h>
#include <sys/kern_syscalls.h>
#include <string.h>

#define SCRIPT_MAX_RECURSION 4
#define SCRIPT_MAX_ARGV      256
#define SCRIPT_LINE_MAX      256

static int script_recursion_depth = 0;

static int script_check(const char *path, const char *header_buf, size_t len) {
    (void)path;
    if (len >= 2 && header_buf[0] == '#' && header_buf[1] == '!')
        return 0;
    return -1;
}

static int script_load(int fd, const char *path, char *const argv[],
                        char *const envp[]) {
    char line_buf[SCRIPT_LINE_MAX];
    char *interp;
    char *interp_arg = NULL;
    char *new_argv[SCRIPT_MAX_ARGV];
    int argc, i, ret;

    if (script_recursion_depth >= SCRIPT_MAX_RECURSION) {
        kern_close(fd);
        return -ELOOP;
    }

    /*
     * The header_buf was read by exec_dispatch, but we received the fd.
     * Re-read the first line from the file since we need to parse it.
     */
    kern_lseek(fd, 0, 0 /* SEEK_SET */);
    int len = kern_read(fd, line_buf, sizeof(line_buf) - 1);
    kern_close(fd);

    if (len < 3)
        return -ENOEXEC;

    line_buf[len] = '\0';

    /* Terminate at newline */
    char *nl = strchr(line_buf, '\n');
    if (nl)
        *nl = '\0';

    /* Also terminate at \r for DOS-style line endings */
    char *cr = strchr(line_buf, '\r');
    if (cr)
        *cr = '\0';

    /* Skip "#!" */
    char *p = line_buf + 2;

    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t')
        p++;

    if (*p == '\0')
        return -ENOEXEC;

    interp = p;

    /* Find end of interpreter path */
    while (*p && *p != ' ' && *p != '\t')
        p++;

    if (*p) {
        *p++ = '\0';

        /* Skip whitespace before optional argument */
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p)
            interp_arg = p;
    }

    /* Build new argv: interpreter [arg] script_path [original_argv[1:]] */
    argc = 0;
    new_argv[argc++] = interp;

    if (interp_arg && argc < SCRIPT_MAX_ARGV - 2)
        new_argv[argc++] = interp_arg;

    /* Script path as argv for the interpreter */
    new_argv[argc++] = (char *)path;

    /* Copy remaining original argv (skip argv[0]) */
    if (argv) {
        for (i = 1; argv[i] && argc < SCRIPT_MAX_ARGV - 1; i++)
            new_argv[argc++] = argv[i];
    }
    new_argv[argc] = NULL;

    script_recursion_depth++;
    ret = exec_dispatch(interp, new_argv, envp);
    script_recursion_depth--;

    return ret;
}

static struct exec_binary_handler script_handler = {
    .name  = "script",
    .check = script_check,
    .load  = script_load,
    .next  = NULL,
};

void script_init_handler(void) {
    exec_register_handler(&script_handler);
}
