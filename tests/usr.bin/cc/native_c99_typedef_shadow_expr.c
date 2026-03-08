typedef struct node node;

struct node {
    int value;
};

int main(void) {
    node root;

    root.value = 1;
    {
        int node = 7;
        if (node != 7)
            return 1;
    }
    if (root.value != 1)
        return 2;
    return 0;
}
