#include <exvi.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
launch_vi(char **argv)
{
    const char *handoff_file = exvi_handoff_file();
    char sibling_path[1024];
    char *vi_argv[3];

    vi_argv[0] = "vi";
    vi_argv[1] = (char *)handoff_file;
    vi_argv[2] = NULL;

    if (argv[0] && strchr(argv[0], '/')) {
        const char *slash = strrchr(argv[0], '/');
        size_t prefix_len = (size_t)(slash - argv[0]);

        if (prefix_len + strlen("/../vi/vi") < sizeof(sibling_path)) {
            memcpy(sibling_path, argv[0], prefix_len);
            sibling_path[prefix_len] = '\0';
            strcat(sibling_path, "/../vi/vi");
            execv(sibling_path, vi_argv);
        }
    }

    execvp("vi", vi_argv);
    perror("vi");
    return 1;
}

int
main(int argc, char **argv)
{
    int ret = exvi_main(argc, argv, EXVI_FRONTEND_EX);

    if (ret == EXVI_EXIT_VISUAL_HANDOFF) {
        return launch_vi(argv);
    }
    return ret;
}
