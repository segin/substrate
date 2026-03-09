#include <exec/formats/elks_aout.h>
#include <stdio.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *buf, uint16_t off) {
    return (uint16_t)(buf[off] | ((uint16_t)buf[off + 1] << 8));
}

int main(void) {
    struct elks_load_plan plan;
    uint8_t segment[0x8000];
    uint16_t sp = 0;
    uint16_t argc_off;
    uint16_t argv0_off;
    uint16_t argv1_off;
    uint16_t env0_off;
    char *argv[] = { "prog", "-x", NULL };
    char *envp[] = { "TERM=ansi", "HOME=/", NULL };

    memset(&plan, 0, sizeof(plan));
    memset(segment, 0, sizeof(segment));
    plan.data_limit = sizeof(segment);
    plan.stack_top = sizeof(segment);

    if (!elks_build_stack_image(segment, &plan, argv, envp, &sp)) {
        fprintf(stderr, "FAIL: ELKS stack image build rejected\n");
        return 1;
    }
    if ((sp & 1U) != 0) {
        fprintf(stderr, "FAIL: ELKS initial stack pointer not even\n");
        return 1;
    }

    argc_off = sp;
    argv0_off = read_u16(segment, (uint16_t)(sp + 2));
    argv1_off = read_u16(segment, (uint16_t)(sp + 4));
    env0_off = read_u16(segment, (uint16_t)(sp + 8));

    if (read_u16(segment, argc_off) != 2) {
        fprintf(stderr, "FAIL: ELKS argc wrong\n");
        return 1;
    }
    if (strcmp((char *)(segment + argv0_off), "prog") != 0 ||
        strcmp((char *)(segment + argv1_off), "-x") != 0) {
        fprintf(stderr, "FAIL: ELKS argv strings wrong\n");
        return 1;
    }
    if (read_u16(segment, (uint16_t)(sp + 6)) != 0) {
        fprintf(stderr, "FAIL: ELKS argv terminator missing\n");
        return 1;
    }
    if (strcmp((char *)(segment + env0_off), "TERM=ansi") != 0) {
        fprintf(stderr, "FAIL: ELKS envp string wrong\n");
        return 1;
    }
    if (read_u16(segment, (uint16_t)(sp + 12)) != 0) {
        fprintf(stderr, "FAIL: ELKS envp terminator missing\n");
        return 1;
    }

    puts("host_test_elks_stack_image: ok");
    return 0;
}
