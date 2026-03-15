#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include <arch/i386/percpu.h>
#include <arch/i386/idt.h>
#include <arch/i386/pmap.h>
#include <sys/proc.h>
#include <sys/exec.h>
#include <exec/perso/personality.h>

static char outbuf[8192];
static size_t outlen;
static struct percpu_data cpu0;
process_t *current_process;
thread_t *current_thread;
static int vm86_dispatch_count;
static const char *panic_msg;
uint32_t idt_host_cr2;

static void append_output(const char *s) {
    size_t len = strlen(s);
    if (outlen + len >= sizeof(outbuf)) {
        len = sizeof(outbuf) - outlen - 1;
    }
    memcpy(outbuf + outlen, s, len);
    outlen += len;
    outbuf[outlen] = '\0';
}

void kprint(const char *msg) {
    append_output(msg ? msg : "");
}

void panic(const char *msg) {
    panic_msg = msg;
}

struct percpu_data *percpu_get(void) {
    return &cpu0;
}

struct percpu_data *percpu_get_cpu(int cpu_id) {
    return cpu_id == 0 ? &cpu0 : NULL;
}

int percpu_get_cpu_id(void) {
    return 0;
}

uint64_t i386_cpu_cycle_counter(void) {
    return 0;
}

void random_harvest_fast(const void *data, size_t len) {
    (void)data;
    (void)len;
}

void exec_maybe_unpin_current_thread(int from_user) {
    (void)from_user;
}

void timer_tick_context(int is_usermode) {
    (void)is_usermode;
}

void rusage_add_tick(process_t *p, int is_usermode) {
    (void)p;
    (void)is_usermode;
}

void sched_yield(void) {
}

void signal_handle_pending(registers_t *regs) {
    (void)regs;
}

void keyboard_handler(registers_t *regs) { (void)regs; }
void mouse_handler(registers_t *regs) { (void)regs; }
void fpu_handler(registers_t *regs) { (void)regs; }
void uart_handler(registers_t *regs) { (void)regs; }
void ide_irq_handler(int irq) { (void)irq; }
void pmap_dump(pmap_t pmap) { (void)pmap; }
int pmap_fault(uint32_t err_code, uint32_t cr2) { (void)err_code; (void)cr2; return 0; }
void debug_dump_processes(void) {}
int cmdline_debug_enabled(const char *channel) { (void)channel; return 0; }
int cmdline_has(const char *key) { (void)key; return 0; }
struct personality *perso_lookup(int id) { (void)id; return NULL; }
void trapsignal(process_t *p, int sig, int code) { (void)p; (void)sig; (void)code; }
int i386_trap_to_signal(const registers_t *regs, uint32_t cr2, int *sig, int *code, uintptr_t *addr) {
    (void)regs; (void)cr2; (void)sig; (void)code; (void)addr; return 0;
}

void vm86_gpf_handler(registers_t *regs) {
    vm86_dispatch_count++;
    regs->eip += 1;
}

#define DECL_ISR(n) void isr##n(void) {}
DECL_ISR(0) DECL_ISR(1) DECL_ISR(2) DECL_ISR(3) DECL_ISR(4) DECL_ISR(5) DECL_ISR(6) DECL_ISR(7)
DECL_ISR(8) DECL_ISR(9) DECL_ISR(10) DECL_ISR(11) DECL_ISR(12) DECL_ISR(13) DECL_ISR(14) DECL_ISR(15)
DECL_ISR(16) DECL_ISR(17) DECL_ISR(18) DECL_ISR(19) DECL_ISR(20) DECL_ISR(21) DECL_ISR(22) DECL_ISR(23)
DECL_ISR(24) DECL_ISR(25) DECL_ISR(26) DECL_ISR(27) DECL_ISR(28) DECL_ISR(29) DECL_ISR(30) DECL_ISR(31)
DECL_ISR(32) DECL_ISR(33) DECL_ISR(34) DECL_ISR(35) DECL_ISR(36) DECL_ISR(37) DECL_ISR(38) DECL_ISR(39)
DECL_ISR(40) DECL_ISR(41) DECL_ISR(42) DECL_ISR(43) DECL_ISR(44) DECL_ISR(45) DECL_ISR(46) DECL_ISR(47)
void isr128(void) {}
void idt_flush(uint32_t ptr) { (void)ptr; }

#include "../../sys/arch/i386/idt.c"

static void reset_state(void) {
    memset(outbuf, 0, sizeof(outbuf));
    outlen = 0;
    memset(&cpu0, 0, sizeof(cpu0));
    current_process = NULL;
    current_thread = NULL;
    vm86_dispatch_count = 0;
    panic_msg = NULL;
}

static uint8_t *map_low_page(void) {
    void *addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(addr != MAP_FAILED);
    assert((uintptr_t)addr <= 0xFFFFFFFFu);
    return (uint8_t *)addr;
}

static void test_vm86_dispatch(void) {
    registers_t regs;
    thread_t thread;
    process_t proc;

    reset_state();
    memset(&regs, 0, sizeof(regs));
    memset(&thread, 0, sizeof(thread));
    memset(&proc, 0, sizeof(proc));
    cpu0.current = &thread;
    thread.proc = &proc;
    regs.int_no = 13;
    regs.eflags = 0x20000;
    regs.cs = 0x0003;
    regs.eip = 0x1000;

    isr_handler(&regs);
    assert(vm86_dispatch_count == 1);
    assert(panic_msg == NULL);
}

static void test_invalid_opcode_dump(void) {
    registers_t regs;
    uint8_t *code;

    reset_state();
    memset(&regs, 0, sizeof(regs));
    code = map_low_page();
    for (int i = 0; i < 16; i++) {
        code[i] = (uint8_t)(0x90 + i);
    }

    regs.int_no = 6;
    regs.cs = 0x0008;
    regs.eip = (uint32_t)(uintptr_t)code;
    regs.err_code = 0xBEEF;
    regs.eax = 0x11;
    regs.ebx = 0x22;
    regs.ecx = 0x33;
    regs.edx = 0x44;
    regs.esi = 0x55;
    regs.edi = 0x66;
    regs.ebp = 0x77;
    regs.esp = 0x88;

    isr_handler(&regs);
    assert(strstr(outbuf, "EXCEPTION: Invalid Opcode (in kernel)") != NULL);
    assert(strstr(outbuf, "EAX: 0x00000011  EBX: 0x00000022  ECX: 0x00000033  EDX: 0x00000044") != NULL);
    assert(strstr(outbuf, "Instruction bytes at EIP: 90 91 92 93 94 95 96 97 98 99 9A 9B 9C 9D 9E 9F") != NULL);
    assert(panic_msg != NULL);
    munmap(code, 4096);
}

int main(void) {
    test_vm86_dispatch();
    test_invalid_opcode_dump();
    puts("host_test_idt_diag: PASS");
    return 0;
}
