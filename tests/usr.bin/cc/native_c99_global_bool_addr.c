typedef struct {
    int x;
} node_t;

static node_t s;
_Bool e = &s;

int main(void) {
    return !e;
}
