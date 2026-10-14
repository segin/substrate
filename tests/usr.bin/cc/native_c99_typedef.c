typedef unsigned int u32;
typedef int *iptr;
typedef iptr *ipptr;

static int bump(u32 x) {
    return (int)(x + 1u);
}

int main(void) {
    typedef int local_i;
    local_i x = 7;
    iptr p = &x;
    ipptr pp = &p;

    if ((int)sizeof(local_i) != 4) {
        return 1;
    }
    if ((int)sizeof(u32) != 4) {
        return 2;
    }
    if ((int)sizeof(iptr) != (int)sizeof(void *)) {
        return 3;
    }

    **pp = bump((u32)**pp);
    if (**pp != 8) {
        return 4;
    }
    return 0;
}
