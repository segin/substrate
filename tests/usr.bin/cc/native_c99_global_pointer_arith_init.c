struct zone {
    void *opaque;
    const char *name;
};

static struct zone zones[] = {
    { 0, "zero" },
    { 0, "one" },
    { 0, "two" }
};

struct entry {
    struct zone *z;
    long value;
};

static struct entry entries[] = {
    { zones + 0, 10 },
    { zones + 1, 20 },
    { zones + 2, 30 }
};

int main(void) {
    if (entries[0].z != &zones[0]) return 1;
    if (entries[1].z != &zones[1]) return 2;
    if (entries[2].z != &zones[2]) return 3;
    if (entries[1].value != 20) return 4;
    return 0;
}
