struct opt {
    const char *name;
};

struct opt o_options[] = {
    {"a"},
    {"b"},
    {"c"},
};

int main(void) {
    char tflag[sizeof(o_options) / sizeof(o_options[0])];
    if (sizeof(tflag) == sizeof(o_options) / sizeof(o_options[0]))
        return 0;
    return 1;
}
