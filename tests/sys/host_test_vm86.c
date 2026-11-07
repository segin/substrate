#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/proc.h>
#include <sys/vm86.h>
#include <arch/i386/idt.h>
#include <arch/i386/pmap.h>

process_t *current_process;
thread_t *current_thread;

static uint8_t lowmem[1 << 20];
static struct vm86_struct entered_vm86;
static int vm86_enter_called;

static int copyin_calls;

int copyin(const void *src, void *dst, size_t size) {
    copyin_calls++;
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

void kprint(const char *msg) {
    (void)msg;
}

int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) {
    (void)pmap;
    (void)va;
    (void)pa;
    (void)prot;
    (void)flags;
    return 0;
}

pmap_t pmap_kernel(void) {
    return (pmap_t)(uintptr_t)0xCAFEBABE;
}

void vm86_enter(struct vm86_struct *info) {
    entered_vm86 = *info;
    vm86_enter_called = 1;
}

void vm86_bios_ret_point(void) {
}

void preempt_disable(void) {
}

void preempt_enable_noresched(void) {
}

static int trapped_sig;
static int trapped_code;

void trapsignal(process_t *p, int sig, int code) {
    (void)p;
    trapped_sig = sig;
    trapped_code = code;
}

#define HOST_TEST 1
#include "../../sys/arch/i386/vm86.c"

#define IF  0x00000200U     /* the CPU's interrupt flag */
#define VIF 0x00080000U     /* the guest's, virtual */

static void reset_state(void) {
    memset(lowmem, 0, sizeof(lowmem));
    memset(&entered_vm86, 0, sizeof(entered_vm86));
    vm86_enter_called = 0;
    vm86_host_set_memory(lowmem, sizeof(lowmem));
    current_vm86_monitor = NULL;    /* a user guest unless a test says not */
    current_process = NULL;
    trapped_sig = 0;
    trapped_code = 0;
    copyin_calls = 0;
}

static void test_monitor_basics(void) {
    struct vm86_monitor mon;

    reset_state();
    vm86_monitor_init(&mon);
    assert(vm86_monitor_get() == &mon);
    assert(mon.pending_int == (uint32_t)-1);
    assert(mon.in_vm86 == 0);
    vm86_monitor_signal_fault(0x1234, 0xAB);
    assert(mon.signal_pending == 1);
    assert(mon.fault_eip == 0x1234);
    assert(mon.fault_opcode == 0xAB);
}

static void test_sys_vm86_copies_input(void) {
    struct vm86_struct info;

    reset_state();
    memset(&info, 0, sizeof(info));
    info.regs.eip = 0x1122;
    info.regs.cs = 0x3344;
    info.regs.eflags = 0x55AA;

    assert(sys_vm86(&info) == 0);
    assert(vm86_enter_called == 1);
    assert(entered_vm86.regs.eip == info.regs.eip);
    assert(entered_vm86.regs.cs == info.regs.cs);
    /* 0x55AA has NT and an IOPL bit set: the guest gets neither. */
    assert(entered_vm86.regs.eflags == ((info.regs.eflags & ~0x7000U) | 0x20200U));
}

static void test_vm86_init_bsd_copies_args(void) {
    struct i386_vm86_args args;
    struct vm86_struct info;

    reset_state();
    memset(&args, 0, sizeof(args));
    memset(&info, 0, sizeof(info));
    args.sub_op = VM86_INIT;
    args.sub_args = &info;
    info.regs.eip = 0x7788;
    info.regs.cs = 0x1234;

    assert(vm86_init_bsd(&args) == 0);
    assert(vm86_enter_called == 1);
    assert(entered_vm86.regs.eip == 0x7788);
    assert(entered_vm86.regs.cs == 0x1234);
    assert((entered_vm86.regs.eflags & 0x20200U) == 0x20200U);
}

static void test_int_emulation(void) {
    registers_t regs;
    uint16_t *ivt = (uint16_t *)&lowmem[0];
    uint16_t *stack;

    reset_state();
    memset(&regs, 0, sizeof(regs));
    regs.cs = 0x1000;
    regs.eip = 0;
    regs.ss = 0x2000;
    regs.useresp = 0x0100;
    regs.eflags = 0x0202 | VIF;     /* the guest's IF on */
    lowmem[0x10000] = 0xCD;
    lowmem[0x10001] = 0x21;
    ivt[0x21 * 2] = 0x4321;
    ivt[0x21 * 2 + 1] = 0x5678;

    vm86_gpf_handler(&regs);
    assert(regs.eip == 0x4321);
    assert(regs.cs == 0x5678);
    assert(regs.useresp == 0x00FA);
    stack = (uint16_t *)&lowmem[0x20000 + 0x00FA];
    assert(stack[0] == 0x0002);
    assert(stack[1] == 0x1000);
    assert(stack[2] == 0x0202);     /* the image's IF is the guest's */
    /* INT clears the guest's IF, never the CPU's */
    assert((regs.eflags & VIF) == 0);
    assert((regs.eflags & IF) != 0);
}

static void test_flag_and_stack_opcodes(void) {
    registers_t regs;
    uint16_t *stack;

    reset_state();
    memset(&regs, 0, sizeof(regs));
    regs.cs = 0x1000;
    regs.ss = 0x2000;
    regs.useresp = 0x0200;
    regs.eip = 0;
    regs.eflags = IF | VIF;

    /* CLI and STI change the guest's IF, VIF; the CPU's stays set */
    lowmem[0x10000] = 0xFA;
    vm86_gpf_handler(&regs);
    assert((regs.eflags & VIF) == 0);
    assert((regs.eflags & IF) != 0);
    assert(regs.eip == 1);

    regs.eip = 0;
    lowmem[0x10000] = 0xFB;
    vm86_gpf_handler(&regs);
    assert((regs.eflags & VIF) != 0);
    assert((regs.eflags & IF) != 0);
    assert(regs.eip == 1);

    /* PUSHF pushes the guest's view: IF is VIF */
    regs.eip = 0;
    regs.eflags = 0x1234 | VIF;     /* 0x1234 has IF; so has the guest */
    lowmem[0x10000] = 0x9C;
    vm86_gpf_handler(&regs);
    assert(regs.useresp == 0x01FE);
    stack = (uint16_t *)&lowmem[0x20000 + 0x01FE];
    assert(stack[0] == 0x1234);
    regs.eflags = 0x1234;           /* the CPU's IF, but not the guest's */
    regs.useresp = 0x0200;
    regs.eip = 0;
    vm86_gpf_handler(&regs);
    assert(stack[0] == 0x1034);
    regs.eflags = 0x1234 | VIF;

    /* POPF of an image with IF clear: VIF clear, the CPU's IF set */
    regs.eip = 0;
    lowmem[0x10000] = 0x9D;
    stack[0] = 0x5478;
    vm86_gpf_handler(&regs);
    /* POPF takes the flags image but not its IOPL/NT bits (0x7000). */
    assert((regs.eflags & 0xFFFFU & ~IF) == (((0x1234U & 0x7000U) | (0x5478U & ~0x7000U)) & ~IF));
    assert((regs.eflags & IF) != 0);
    assert((regs.eflags & VIF) == 0);
    assert(regs.useresp == 0x0200);

    regs.eip = 0;
    lowmem[0x10000] = 0xCF;
    stack = (uint16_t *)&lowmem[0x20000 + 0x0200];
    stack[0] = 0xAAAA;
    stack[1] = 0xBBBB;
    stack[2] = 0xCCCC;
    uint32_t before = regs.eflags;
    vm86_gpf_handler(&regs);
    assert(regs.eip == 0xAAAA);
    assert(regs.cs == 0xBBBB);
    /* Nor does IRET's (0xCCCC sets IOPL 0 and NT 1 over before's), and its
     * IF -- clear in 0xCCCC -- is the guest's, the CPU's staying set. */
    assert((regs.eflags & 0xFFFFU & ~IF) == (((before & 0x7000U) | (0xCCCCU & ~0x7000U)) & ~IF));
    assert((regs.eflags & IF) != 0);
    assert((regs.eflags & VIF) == 0);
    assert(regs.useresp == 0x0206);

    /* an IRET image with IF set gives the guest its IF back */
    regs.cs = 0x1000;
    regs.eip = 0;
    regs.useresp = 0x0200;
    stack[2] = 0x0202;
    vm86_gpf_handler(&regs);
    assert((regs.eflags & VIF) != 0);
    assert((regs.eflags & IF) != 0);
}

/* IN and OUT of AX and, with an operand-size prefix, EAX: a video BIOS
 * writes its index registers a word at a time (OUT DX, AX trapped as an
 * unhandled opcode, forever). */
static void test_wide_port_io(void) {
    registers_t regs;

    reset_state();
    memset(&regs, 0, sizeof(regs));
    regs.cs = 0x1000;
    regs.edx = 0x3CE;
    regs.eax = 0xAABB1234U;
    lowmem[0x10000] = 0xEF;         /* OUT DX, AX */
    assert(vm86_gpf_handler(&regs) == 0);
    assert(regs.eip == 1);
    regs.eip = 0;
    regs.eax = 0xFFFFFFFFU;
    lowmem[0x10000] = 0xED;         /* IN AX, DX */
    assert(vm86_gpf_handler(&regs) == 0);
    assert(regs.eax == 0xFFFF1234U);
    regs.eip = 0;
    regs.eax = 0x89ABCDEFU;
    lowmem[0x10000] = 0x66;         /* OUT DX, EAX */
    lowmem[0x10001] = 0xEF;
    assert(vm86_gpf_handler(&regs) == 0);
    assert(regs.eip == 2);
    regs.eip = 0;
    regs.eax = 0;
    lowmem[0x10001] = 0xED;         /* IN EAX, DX */
    assert(vm86_gpf_handler(&regs) == 0);
    assert(regs.eax == 0x89ABCDEFU);
    regs.eip = 0;
    regs.eax = 0;
    lowmem[0x10000] = 0xE5;         /* IN AX, imm8 */
    lowmem[0x10001] = 0x40;
    vm86_port_space[0x40] = 0x34;
    vm86_port_space[0x41] = 0x12;
    assert(vm86_gpf_handler(&regs) == 0);
    assert(regs.eax == 0x1234U);
    assert(regs.eip == 2);
}

/* In the kernel's own BIOS call, an instruction the monitor does not
 * emulate ends the call -- the caller sees a failed one -- instead of
 * trapping on it forever. */
static void test_kernel_bios_unhandled_exits(void) {
    registers_t regs;
    struct vm86_monitor mon;
    struct vm86_regs out;

    reset_state();
    memset(&regs, 0, sizeof(regs));
    memset(&out, 0, sizeof(out));
    vm86_monitor_init(&mon);
    mon.in_kernel_bios = 1;
    mon.out_regs = &out;
    regs.cs = 0x1000;
    regs.eax = 0x4F00;
    regs.eflags = 0x20200;
    lowmem[0x10000] = 0x0F;         /* not emulated */
    lowmem[0x10001] = 0x0B;
    vm86_gpf_handler(&regs);
    assert(mon.req_exit == 1);
    assert(regs.cs == 0x08);
    assert((regs.eflags & 0x20000U) == 0);
    assert(out.eax == 0x4F00);
}

/* The kernel's own BIOS call enters VM86 mode with its own copy of the
 * registers, not by copyin() -- which refuses a kernel address, so it
 * failed every time -- and with the CPU's IF set. */
static void test_bios_call_enters_vm86(void) {
    struct vm86_regs regs;

    reset_state();
    memset(&regs, 0, sizeof(regs));
    regs.eax = 0x4F00;
    assert(vm86_bios_call(0x10, &regs) == 0);
    assert(copyin_calls == 0);
    assert(vm86_enter_called == 1);
    assert(entered_vm86.regs.eip == 0x1000);
    assert(entered_vm86.regs.cs == 0);
    assert(entered_vm86.regs.eax == 0x4F00);
    assert((entered_vm86.regs.eflags & 0x20000U) != 0);   /* VM */
    assert((entered_vm86.regs.eflags & IF) != 0);
    assert(lowmem[0x1000] == 0xCD && lowmem[0x1001] == 0x10 && lowmem[0x1002] == 0xF4);
}

/* A user guest the monitor cannot carry on with is ended by a signal,
 * instead of re-trapping on the same instruction forever. */
static void test_user_guest_failures(void) {
    static process_t proc;
    registers_t regs;

    reset_state();
    memset(&proc, 0, sizeof(proc));
    current_process = &proc;
    proc.sig_actions[SIGILL - 1].sa_handler = (sig_t)1;   /* a handler */

    memset(&regs, 0, sizeof(regs));
    regs.cs = 0x1000;
    regs.eip = 0;
    lowmem[0x10000] = 0x0F;                 /* not emulated */
    assert(vm86_gpf_handler(&regs) == 1);
    assert(trapped_sig == SIGILL && trapped_code == ILL_PRVOPC);
    assert(proc.sig_actions[SIGILL - 1].sa_handler == SIG_DFL);

    trapped_sig = 0;
    regs.eip = 0;
    lowmem[0x10000] = 0xF4;                 /* HLT outside a BIOS call */
    assert(vm86_gpf_handler(&regs) == 1);
    assert(trapped_sig == SIGILL);

    trapped_sig = 0;
    regs.eip = 0;
    regs.ss = 0xFFFF;                       /* stack past the guest's memory */
    regs.useresp = 0xFFFF;
    lowmem[0x10000] = 0x9C;                 /* PUSHF */
    assert(vm86_gpf_handler(&regs) == 1);
    assert(trapped_sig == SIGSEGV && trapped_code == SEGV_MAPERR);

    /* a trap other than #GP -- an invalid opcode, a divide error -- ends
     * the guest by a signal too, where the kernel took it for its own fault
     * and panicked */
    trapped_sig = 0;
    regs.int_no = 6;
    assert(vm86_trap(&regs) == 1);
    assert(trapped_sig == SIGILL && trapped_code == ILL_ILLOPC);
    trapped_sig = 0;
    regs.int_no = 0;
    assert(vm86_trap(&regs) == 1);
    assert(trapped_sig == SIGFPE && trapped_code == FPE_INTDIV);
}

static void test_port_io_opcodes(void) {
    registers_t regs;

    reset_state();
    memset(&regs, 0, sizeof(regs));
    regs.cs = 0x1000;
    regs.eip = 0;
    regs.eax = 0x0000005AU;
    regs.edx = 0x00000070U;

    lowmem[0x10000] = 0xE6;
    lowmem[0x10001] = 0x61;
    vm86_gpf_handler(&regs);
    assert(regs.eip == 2);

    regs.eip = 0;
    regs.eax = 0;
    lowmem[0x10000] = 0xE4;
    lowmem[0x10001] = 0x61;
    vm86_gpf_handler(&regs);
    assert((regs.eax & 0xFFU) == 0x5AU);

    regs.eip = 0;
    regs.eax = 0x000000A5U;
    lowmem[0x10000] = 0xEE;
    vm86_gpf_handler(&regs);
    assert(regs.eip == 1);

    regs.eip = 0;
    regs.eax = 0;
    lowmem[0x10000] = 0xEC;
    vm86_gpf_handler(&regs);
    assert((regs.eax & 0xFFU) == 0xA5U);
}

static void test_hlt_kernel_bios_exit(void) {
    registers_t regs;
    struct vm86_monitor mon;
    struct vm86_regs out;

    reset_state();
    memset(&regs, 0, sizeof(regs));
    memset(&out, 0, sizeof(out));
    vm86_monitor_init(&mon);
    mon.in_kernel_bios = 1;
    mon.out_regs = &out;

    regs.cs = 0x1000;
    regs.eip = 0;
    regs.ss = 0x2000;
    regs.useresp = 0x0100;
    regs.eax = 0x11;
    regs.ebx = 0x22;
    regs.ecx = 0x33;
    regs.edx = 0x44;
    regs.esi = 0x55;
    regs.edi = 0x66;
    regs.ebp = 0x77;
    regs.eflags = 0x20200;
    lowmem[0x10000] = 0xF4;

    vm86_gpf_handler(&regs);
    assert(mon.in_vm86 == 0);
    assert(mon.req_exit == 1);
    assert((regs.eflags & 0x20000U) == 0);
    assert(regs.cs == 0x08);
    assert(out.eax == 0x11);
    assert(out.ebx == 0x22);
    assert(out.ecx == 0x33);
    assert(out.edx == 0x44);
}

int main(void) {
    test_monitor_basics();
    test_sys_vm86_copies_input();
    test_vm86_init_bsd_copies_args();
    test_int_emulation();
    test_flag_and_stack_opcodes();
    test_port_io_opcodes();
    test_hlt_kernel_bios_exit();
    test_bios_call_enters_vm86();
    test_wide_port_io();
    test_kernel_bios_unhandled_exits();
    test_user_guest_failures();
    puts("host_test_vm86: PASS");
    return 0;
}
