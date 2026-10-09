#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int cc_ld_wrapper_exec(const char *ld_path, char *const *argv, int *exit_code_out) {
    pid_t pid;
    int st;

    if (ld_path == NULL || argv == NULL) {
        errno = EINVAL;
        return(-1);
    }

    pid = fork();
    if (pid < 0) {
        return(-1);
    }
    if (pid == 0) {
        execvp(ld_path, argv);
        _exit(127);
    }

    if (waitpid(pid, &st, 0) < 0) {
        return(-1);
    }

    if (exit_code_out != NULL) {
        if (WIFEXITED(st)) {
            *exit_code_out = WEXITSTATUS(st);
        } else if (WIFSIGNALED(st)) {
            *exit_code_out = 128 + WTERMSIG(st);
        } else {
            *exit_code_out = 1;
        }
    }
    return(0);
}
