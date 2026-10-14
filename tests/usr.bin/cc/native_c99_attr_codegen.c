int hot_value __attribute__((section(".hotdata"), aligned(32))) = 7;
static int tiny_value __attribute__((packed)) = 1;

void panic_loop(void) __attribute__((noreturn, section(".text.panic"), aligned(16)));
void panic_loop(void) {
    for (;;) {
    }
}

int main(void) {
    return hot_value == 7 && tiny_value == 1 ? 0 : 1;
}
