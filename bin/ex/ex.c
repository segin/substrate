#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "exvi.h"

static int
launch_vi(int argc, char **argv)
{
    const char *handoff_file = exvi_handoff_file();
    char sibling_path[1024];
    char **vi_argv;
    int outc = 1;
    int saw_file = 0;

    vi_argv = calloc((size_t)argc + 3, sizeof(*vi_argv));
    if (!vi_argv) {
        perror("calloc");
        return 1;
    }

    vi_argv[0] = "vi";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            continue;
        }
        vi_argv[outc++] = argv[i];
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            vi_argv[outc++] = argv[++i];
            continue;
        }
        if (argv[i][0] != '-') {
            saw_file = 1;
        }
    }

    if (!saw_file && handoff_file) {
        if (exvi_readonly_mode()) {
            int has_readonly = 0;

            for (int i = 1; i < outc; i++) {
                if (strcmp(vi_argv[i], "-R") == 0) {
                    has_readonly = 1;
                    break;
                }
            }
            if (!has_readonly) {
                vi_argv[outc++] = "-R";
            }
        }
        vi_argv[outc++] = (char *)handoff_file;
    }
    vi_argv[outc] = NULL;

    if (argv[0] && strchr(argv[0], '/')) {
        const char *slash = strrchr(argv[0], '/');
        size_t prefix_len = (size_t)(slash - argv[0]);

        if (prefix_len + strlen("/../vi/vi") < sizeof(sibling_path)) {
            strlcpy(sibling_path, argv[0], prefix_len + 1);
            strlcat(sibling_path, "/../vi/vi", sizeof(sibling_path));
            execv(sibling_path, vi_argv);
        }
    }

    execvp("vi", vi_argv);
    perror("vi");
    free(vi_argv);
    return 1;
}

int
main(int argc, char **argv)
{
    int ret = exvi_main(argc, argv, EXVI_FRONTEND_EX);

    if (ret == EXVI_EXIT_VISUAL_HANDOFF) {
        return launch_vi(argc, argv);
    }
    return ret;
}
