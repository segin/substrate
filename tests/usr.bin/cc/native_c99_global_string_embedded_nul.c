#include <string.h>

static const char msg[] = "Success" "\0" "No match" "\0";
static const char msg_braced[] = { "Alpha" "\0" "Beta" "\0" };

int main(void) {
    if (sizeof(msg) != 18) return 1;
    if (sizeof(msg_braced) != 12) return 2;
    if (msg[8] != 'N') return 3;
    if (strcmp(msg + 8, "No match") != 0) return 4;
    if (msg_braced[6] != 'B') return 5;
    if (strcmp(msg_braced + 6, "Beta") != 0) return 6;
    return 0;
}
