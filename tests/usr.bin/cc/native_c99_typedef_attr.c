typedef int register_t __attribute__((__mode__(__word__)));

int main(void) {
    register_t x = 0;
    return (int)x;
}
