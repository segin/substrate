typedef unsigned char u8;

struct item {
    u8 a;
    u8 b;
};

struct packet {
    u8 tag;
    struct item items[];
};

struct packet pkt = {
    9,
    {1, 2, 3}
};

int main(void) {
    if (pkt.tag != 9)
        return 1;
    if (pkt.items[0].a != 1)
        return 2;
    if (pkt.items[0].b != 2)
        return 3;
    if (pkt.items[1].a != 3)
        return 4;
    if (pkt.items[1].b != 0)
        return 5;
    return 0;
}
