struct opt {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

int main(void) {
    struct opt lo[] = {
        {"file", 1, 0, 'f'},
        {0, 0, 0, 0}
    };

    if (lo[0].name[0] != 'f' || lo[0].has_arg != 1 || lo[0].flag != 0 || lo[0].val != 'f') {
        return 1;
    }
    if (lo[1].name != 0 || lo[1].has_arg != 0 || lo[1].flag != 0 || lo[1].val != 0) {
        return 2;
    }
    return 0;
}
