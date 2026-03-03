struct packet {
    int tag;
    struct {
        int x;
        int y;
    };
    union {
        unsigned flags;
        struct {
            unsigned lo:4;
            unsigned hi:4;
        };
    };
};

int main(void) {
    struct packet p;
    p.tag = 1;
    p.x = 4;
    p.y = 5;
    p.flags = 0;
    p.lo = 0xA;
    p.hi = 0x3;
    if (p.tag + p.x + p.y != 10) {
        return 1;
    }
    if ((p.flags & 0xF) != 0xA) {
        return 2;
    }
    if (((p.flags >> 4) & 0xF) != 0x3) {
        return 3;
    }
    return 0;
}
