#include <limits.h>

struct arg_cursor {
    const char *f;
    int curr_arg;
    int curr_s_arg;
    int end_arg;
    int direc_arg;
};

__attribute__((pure)) static struct arg_cursor get_curr_arg(int pos, struct arg_cursor ac) {
    int arg = 0;
    const char *f = ac.f;

    if (pos < 3 && *f >= '0' && *f <= '9') {
        int a = *f++ - '0';
        int v = 0;
        for (; *f >= '0' && *f <= '9'; f++) {
            a = a * 10 + (*f - '0');
        }
        if (*f == '$') {
            arg = v ? INT_MAX : a;
        }
    }

    if (0 < arg) {
        arg--;
        ac.f = f + 1;
        if (pos == 0) {
            ac.direc_arg = arg;
        }
    } else {
        arg = (pos == 0 ? (ac.direc_arg = -1)
                        : pos < 3 || ac.direc_arg < 0 ? ++ac.curr_s_arg : ac.direc_arg);
    }

    if (0 <= arg) {
        ac.curr_arg = arg;
        if (ac.end_arg < arg) {
            ac.end_arg = arg;
        }
    }
    return ac;
}

static int test_sequential(void) {
    struct arg_cursor ac;
    ac.f = "d";
    ac.curr_arg = -1;
    ac.curr_s_arg = -1;
    ac.end_arg = -1;
    ac.direc_arg = -1;

    ac = get_curr_arg(0, ac);
    if (ac.f[0] != 'd' || ac.curr_arg != -1 || ac.curr_s_arg != -1 || ac.end_arg != -1 || ac.direc_arg != -1) {
        return 1;
    }

    ac = get_curr_arg(3, ac);
    if (ac.f[0] != 'd' || ac.curr_arg != 0 || ac.curr_s_arg != 0 || ac.end_arg != 0 || ac.direc_arg != -1) {
        return 2;
    }
    return 0;
}

static int test_positional(void) {
    struct arg_cursor ac;
    ac.f = "2$d";
    ac.curr_arg = -1;
    ac.curr_s_arg = -1;
    ac.end_arg = -1;
    ac.direc_arg = -1;

    ac = get_curr_arg(0, ac);
    if (ac.f[0] != 'd' || ac.curr_arg != 1 || ac.curr_s_arg != -1 || ac.end_arg != 1 || ac.direc_arg != 1) {
        return 3;
    }

    ac = get_curr_arg(3, ac);
    if (ac.f[0] != 'd' || ac.curr_arg != 1 || ac.curr_s_arg != -1 || ac.end_arg != 1 || ac.direc_arg != 1) {
        return 4;
    }
    return 0;
}

int main(void) {
    int rc;

    rc = test_sequential();
    if (rc != 0) {
        return rc;
    }
    rc = test_positional();
    if (rc != 0) {
        return rc;
    }
    return 0;
}
