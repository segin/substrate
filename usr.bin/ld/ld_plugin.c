/*
 * ld_plugin.c -- LTO plugins that are programs.
 */

#include "ld.h"

static int run_cmd_first_line(char *const argv[], char *out, size_t out_sz) {
    int pipefd[2];
    pid_t pid;
    int status;
    int rc = -1;
    ssize_t nread;
    char *nl;

    if (argv == NULL || argv[0] == NULL || out == NULL || out_sz == 0) {
        return -1;
    }
    out[0] = '\0';

    if (pipe(pipefd) == -1) {
        return -1;
    }

    pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (pipefd[1] != STDOUT_FILENO) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
        }

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDERR_FILENO);
            if (devnull != STDERR_FILENO) {
                close(devnull);
            }
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);

    do {
        nread = read(pipefd[0], out, out_sz - 1);
    } while (nread == -1 && errno == EINTR);

    if (nread > 0) {
        out[nread] = '\0';
        nl = strchr(out, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        rc = out[0] != '\0' ? 0 : 1;
    } else {
        out[0] = '\0';
        rc = 1;
    }

    close(pipefd[0]);
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            out[0] = '\0';
            return -1;
        }
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        out[0] = '\0';
        return -1;
    }

    return rc;
}

static int discover_default_plugin(ld_ctx_t *ctx) {
    static char discovered[PATH_MAX];
    const char *envp;
    int rc;

    if (ctx == NULL || (ctx->plugin_path != NULL && ctx->plugin_path[0] != '\0')) {
        return 0;
    }
    envp = getenv("SUBSTRATE_LD_PLUGIN");
    if (envp != NULL && envp[0] != '\0' && access(envp, R_OK | X_OK) == 0) {
        ctx->plugin_path = envp;
        return 0;
    }
    envp = getenv("LD_PLUGIN");
    if (envp != NULL && envp[0] != '\0' && access(envp, R_OK | X_OK) == 0) {
        ctx->plugin_path = envp;
        return 0;
    }

    {
        char *gcc_args[] = {"gcc", "-print-file-name=liblto_plugin.so", NULL};
        rc = run_cmd_first_line(gcc_args, discovered, sizeof(discovered));
        if (rc == 0 && discovered[0] == '/' && access(discovered, R_OK | X_OK) == 0) {
            ctx->plugin_path = discovered;
            return 0;
        }
    }

    {
        char *clang_args[] = {"clang", "-print-file-name=LLVMgold.so", NULL};
        rc = run_cmd_first_line(clang_args, discovered, sizeof(discovered));
        if (rc == 0 && discovered[0] == '/' && access(discovered, R_OK | X_OK) == 0) {
            ctx->plugin_path = discovered;
            return 0;
        }
    }

    return 0;
}

int plugin_discover_and_handshake(ld_ctx_t *ctx) {
    int rc;
    pid_t pid;
    int status;

    if (ctx == NULL || ctx->plugin_checked) {
        return 0;
    }
    /*
     * A compiler driver names its LTO plugin on every link, and the
     * plugin GCC has is a shared object for a linker to load, which this
     * one does not: its plugins are programs it runs.  Such a plugin is
     * set aside.  It is only wanted if an input turns out to hold
     * bytecode in place of code, and that input is refused when met.
     */
    if (ctx->plugin_path != NULL && strstr(ctx->plugin_path, ".so") != NULL) {
        ctx->plugin_path = NULL;
        ctx->plugin_unusable = 1;
        ctx->plugin_checked = 1;
        return 0;
    }
    if (ctx->plugin_path == NULL || ctx->plugin_path[0] == '\0') {
        if (ctx->plugin_opt_count != 0) {
            if (discover_default_plugin(ctx) != 0) {
                return -1;
            }
            if (ctx->plugin_path == NULL || ctx->plugin_path[0] == '\0') {
                fprintf(stderr, "ld: -plugin-opt requires -plugin or a discoverable plugin\n");
                return -1;
            }
        } else {
            return 0;
        }
    }
    if (access(ctx->plugin_path, R_OK | X_OK) != 0) {
        fprintf(stderr, "ld: plugin not executable: %s\n", ctx->plugin_path);
        return -1;
    }

    pid = fork();
    if (pid == -1) {
        return -1;
    }

    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        char *args[] = {(char *)ctx->plugin_path, "--version", NULL};
        execv(ctx->plugin_path, args);
        _exit(127);
    }

    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            fprintf(stderr, "ld: waitpid failed for plugin handshake\n");
            return -1;
        }
    }
    rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (rc != 0) {
        fprintf(stderr, "ld: plugin handshake failed for %s\n", ctx->plugin_path);
        return -1;
    }

    ctx->plugin_checked = 1;
    return 0;
}

int plugin_materialize_object(const ld_ctx_t *ctx, const char *in_path, char *out_path, size_t out_path_sz) {
    char *argv[3 + 32 + 1];
    char *plugin_opt_args[32];
    size_t i, argc;
    int pipefd[2];
    pid_t pid;
    int status;
    ssize_t nread;
    char *nl;

    if (out_path == NULL || out_path_sz == 0) {
        return -1;
    }
    out_path[0] = '\0';
    if (ctx == NULL || ctx->plugin_path == NULL || ctx->plugin_path[0] == '\0' || in_path == NULL) {
        return 0;
    }

    memset(plugin_opt_args, 0, sizeof(plugin_opt_args));
    argc = 0;
    argv[argc++] = (char *)ctx->plugin_path;
    argv[argc++] = "--materialize";
    argv[argc++] = (char *)in_path;

    for (i = 0; i < ctx->plugin_opt_count; ++i) {
        if (argc + 1 >= sizeof(argv) / sizeof(argv[0])) {
            goto fail;
        }
        size_t len = strlen("--plugin-opt=") + strlen(ctx->plugin_opts[i]) + 1;
        plugin_opt_args[i] = (char *)malloc(len);
        if (plugin_opt_args[i] == NULL) {
            goto fail;
        }
        snprintf(plugin_opt_args[i], len, "--plugin-opt=%s", ctx->plugin_opts[i]);
        argv[argc++] = plugin_opt_args[i];
    }
    argv[argc] = NULL;

    if (pipe(pipefd) == -1) {
        goto fail;
    }

    pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        goto fail;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (pipefd[1] != STDOUT_FILENO) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
        }

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDERR_FILENO);
            if (devnull != STDERR_FILENO) {
                close(devnull);
            }
        }

        execv(ctx->plugin_path, argv);
        _exit(127);
    }

    close(pipefd[1]);

    do {
        nread = read(pipefd[0], out_path, out_path_sz - 1);
    } while (nread == -1 && errno == EINTR);

    if (nread > 0) {
        out_path[nread] = '\0';
        nl = strchr(out_path, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
    } else {
        out_path[0] = '\0';
    }

    close(pipefd[0]);
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            out_path[0] = '\0';
            goto fail;
        }
    }

    for (i = 0; i < ctx->plugin_opt_count; ++i) {
        free(plugin_opt_args[i]);
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        out_path[0] = '\0';
        return -1;
    }

    return out_path[0] != '\0' ? 1 : 0;

fail:
    for (i = 0; i < ctx->plugin_opt_count; ++i) {
        free(plugin_opt_args[i]);
    }
    out_path[0] = '\0';
    return -1;
}
