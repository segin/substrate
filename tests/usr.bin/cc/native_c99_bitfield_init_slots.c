typedef struct {
    unsigned int context : 4;
    unsigned int halt : 1;
    unsigned int mode : 3;
} state;

int main(void) {
    state s = {0, 1, 5};
    if (s.context != 0) return 1;
    if (s.halt != 1) return 2;
    if (s.mode != 5) return 3;
    return 0;
}
