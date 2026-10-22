/*
 * GNU attributes may open a declarator, before its `*`s.  Linux's
 * <linux/efi.h> declares
 *     efi_call_acpi_prm_handler(efi_status_t (__attribute__((regparm(0)))
 *                               *handler_addr)(u64, void *), ...)
 * and cc stopped at the `*`: "expected ')' in declarator".
 */
#include <stdio.h>

typedef unsigned long st_t;
typedef unsigned long long u64;

static st_t handler(u64 a, void *p) { return (st_t)a + (p == NULL); }

static st_t call(st_t (__attribute__((unused)) *h)(u64, void *), u64 a) { return h(a, NULL); }

int main(void) {
    int (__attribute__((unused)) *fp)(const char *, ...) = printf;
    char *__attribute__((unused)) *pp = NULL;

    fp("%lu %d\n", call(handler, 41), pp == NULL);
    return 0;
}
