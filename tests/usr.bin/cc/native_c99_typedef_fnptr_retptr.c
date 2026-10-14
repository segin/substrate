struct node {
    int v;
};

typedef struct node * (*readdir_type_t)(int);

static struct node g;

static struct node *pick(int x) {
    (void)x;
    return &g;
}

int main(void) {
    readdir_type_t fn = pick;
    (void)fn;
    return 0;
}
