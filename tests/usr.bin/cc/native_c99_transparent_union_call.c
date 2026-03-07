typedef union __attribute__((transparent_union)) sockarg_u {
    void *vp;
    char *cp;
    int *ip;
} sockarg_u;

static int marker = 7;

static int accept_u(sockarg_u u) {
    return u.ip == &marker;
}

int main(void) {
    return accept_u(&marker) ? 0 : 1;
}
