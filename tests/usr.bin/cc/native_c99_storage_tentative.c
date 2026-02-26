int shared;
int shared;
extern int shared;
int shared = 7;
extern int shared;

static int sarr[4];

int main(void) {
    if (shared != 7) {
        return 1;
    }
    if (sarr[0] != 0 || sarr[3] != 0) {
        return 2;
    }
    return 0;
}
