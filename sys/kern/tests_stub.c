/*
 * tests_stub.c - Empty stub for run_kernel_tests() when KERNEL_TESTS=0
 *
 * When the kernel is built without the test suite (the default),
 * this stub satisfies the reference from main.c.
 */

void run_kernel_tests(void)
{
    /* Tests not compiled in; do nothing */
}
