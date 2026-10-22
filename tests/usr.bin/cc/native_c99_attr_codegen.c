int hot_value __attribute__((section(".hotdata"), aligned(32))) = 7;
static int tiny_value __attribute__((packed)) = 1;
/*
 * An attribute after the `*` is the declaration's too, as gcc reads it:
 * Linux's __ADDRESSABLE() puts its pointer in .discard.addressable so, and
 * cc left it in .data.
 */
static int * __attribute__((section(".ptrdata"), used)) hot_ptr = &hot_value;
/* and a function's: Linux's `static char * __init unpack_to_rootfs(...)` was in .text */
static int * __attribute__((section(".text.ptrfn"))) ptr_fn(void) {
    return hot_ptr;
}

void panic_loop(void) __attribute__((noreturn, section(".text.panic"), aligned(16)));
void panic_loop(void) {
    for (;;) {
    }
}

int main(void) {
    return hot_value == 7 && tiny_value == 1 && *ptr_fn() == 7 ? 0 : 1;
}
