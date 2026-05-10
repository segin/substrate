/*
 * ldd — list dynamic dependencies of an ELF binary.
 *
 * Substrate ldd is a thin wrapper: we set the
 * LD_TRACE_LOADED_OBJECTS environment variable and exec the
 * target.  Its PT_INTERP=/sbin/ld.so kicks in normally; ld.so
 * notices the env var, loads everything as usual, then prints
 * each loaded object in the GNU ldd format and exits without
 * handing control to the program.
 *
 *   $ ldd /sbin/init
 *           libc.so.0 => /lib/libc.so.0 (0x10000)
 *           libm.so.0 => /lib/libm.so.0 (0x2d000)
 *           ld.so (0x40000000)
 *
 * Static binaries (no PT_INTERP) just exec normally — ld.so
 * isn't involved at all, and we get an empty/no-op result.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [--help] FILE [FILE ...]\n"
        "\n"
        "List the dynamic shared objects required by each FILE\n"
        "by re-execing it under /sbin/ld.so with\n"
        "LD_TRACE_LOADED_OBJECTS=1 set.\n",
        prog);
}

static int trace_one(const char *path) {
    pid_t pid = fork();
    if (pid < 0) { perror("ldd: fork"); return -1; }
    if (pid == 0) {
        /* Child: turn on the trace flag, exec the binary.  setenv
         * is fine since we're about to call execv (which inherits
         * the modified environment). */
        setenv("LD_TRACE_LOADED_OBJECTS", "1", 1);
        char *const argv[] = { (char *)path, NULL };
        execv(path, argv);
        /* If we got here, exec failed.  Report and exit non-zero
         * so the parent's wait sees a failure. */
        perror(path);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("ldd: waitpid");
        return -1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) return -1;
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "ldd: %s: killed by signal %d\n",
                path, WTERMSIG(status));
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }
    if (argv[1][0] == '-') {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
        fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[1]);
        return 1;
    }
    int fails = 0;
    for (int i = 1; i < argc; i++) {
        if (argc > 2) printf("%s:\n", argv[i]);
        if (trace_one(argv[i]) < 0) fails++;
    }
    return fails ? 1 : 0;
}
