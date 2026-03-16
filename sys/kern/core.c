#include <sys/core.h>
#include <sys/proc.h>
#include <sys/ldt.h>
#include <arch/i386/idt.h>
#include <exec/perso/personality.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>

static struct core_record last_core_record;

static void core_reset_record(struct core_record *rec) {
    if (!rec) {
        return;
    }
    memset(rec, 0, sizeof(*rec));
}

static void core_fill_process_metadata(struct core_record *rec, process_t *p) {
    if (!rec || !p) {
        return;
    }

    rec->valid = 1;
    rec->pid = p->pid;
    rec->perso_id = p->perso_id;
    rec->bitness = p->bitness;
    rec->trap_addr = current_thread && current_thread->proc == p ? current_thread->trap_addr : 0;
    rec->trap_code = current_thread && current_thread->proc == p ? current_thread->trap_code : 0;

    strncpy(rec->comm, p->comm, sizeof(rec->comm) - 1U);
    rec->comm[sizeof(rec->comm) - 1U] = '\0';
    strncpy(rec->exec_path, p->exec_path, sizeof(rec->exec_path) - 1U);
    rec->exec_path[sizeof(rec->exec_path) - 1U] = '\0';
}

static void core_fill_elks_segments(struct core_record *rec, process_t *p) {
    const gdt_entry_t *ldt;
    int count;
    int i;

    if (!rec || !p || p->perso_id != PERS_ELKS || !p->ldt || p->ldt_entry_count <= 0) {
        return;
    }

    ldt = (const gdt_entry_t *)p->ldt;
    count = p->ldt_entry_count;
    if (count > CORE_SEGMENT_MAX) {
        count = CORE_SEGMENT_MAX;
    }

    for (i = 0; i < count; i++) {
        rec->segments[i].selector = (uint16_t)((i << 3) | 4U | 3U);
        rec->segments[i].base = ldt_entry_base(&ldt[i]);
        rec->segments[i].limit = ldt_entry_limit(&ldt[i]);
        rec->segments[i].access = ldt[i].access;
        rec->segments[i].granularity = ldt[i].granularity;
    }
    rec->segment_count = count;
}

void core_prepare_dump(process_t *p, int sig) {
    if (!p) {
        return;
    }

    if (!last_core_record.valid || last_core_record.pid != p->pid) {
        core_reset_record(&last_core_record);
        core_fill_process_metadata(&last_core_record, p);
    }
    last_core_record.signal = sig;
}

void core_capture_trapframe(process_t *p, const registers_t *regs) {
    if (!p || !regs) {
        return;
    }

    if (!last_core_record.valid || last_core_record.pid != p->pid) {
        core_reset_record(&last_core_record);
        core_fill_process_metadata(&last_core_record, p);
    }

    last_core_record.has_regs = 1;
    last_core_record.regs.eax = regs->eax;
    last_core_record.regs.ebx = regs->ebx;
    last_core_record.regs.ecx = regs->ecx;
    last_core_record.regs.edx = regs->edx;
    last_core_record.regs.esi = regs->esi;
    last_core_record.regs.edi = regs->edi;
    last_core_record.regs.ebp = regs->ebp;
    last_core_record.regs.esp = regs->esp;
    last_core_record.regs.ds = regs->ds;
    last_core_record.regs.es = regs->es;
    last_core_record.regs.fs = regs->fs;
    last_core_record.regs.gs = regs->gs;
    last_core_record.regs.eip = regs->eip;
    last_core_record.regs.cs = regs->cs;
    last_core_record.regs.eflags = regs->eflags;
    last_core_record.regs.useresp = regs->useresp;
    last_core_record.regs.ss = regs->ss;
}

const struct core_record *core_last_record(void) {
    return &last_core_record;
}

int coredump(process_t *p) {
    char msg[256];

    if (!p) {
        return -1;
    }

    if (!last_core_record.valid || last_core_record.pid != p->pid) {
        core_reset_record(&last_core_record);
    }

    core_fill_process_metadata(&last_core_record, p);
    core_fill_elks_segments(&last_core_record, p);

    sprintf(msg, "CORE: captured crash state for pid=%d perso=%d signal=%d\n",
            p->pid, p->perso_id, last_core_record.signal);
    kprint(msg);

    sprintf(msg, "CORE: exec='%s' comm='%s' trap_addr=0x%08x trap_code=0x%08x\n",
            last_core_record.exec_path,
            last_core_record.comm,
            last_core_record.trap_addr,
            last_core_record.trap_code);
    kprint(msg);

    if (last_core_record.has_regs) {
        sprintf(msg, "CORE: regs eip=0x%08x cs=0x%04x esp=0x%08x ss=0x%04x eflags=0x%08x\n",
                last_core_record.regs.eip,
                (unsigned int)(last_core_record.regs.cs & 0xFFFFU),
                last_core_record.regs.useresp,
                (unsigned int)(last_core_record.regs.ss & 0xFFFFU),
                last_core_record.regs.eflags);
        kprint(msg);
    }

    if (last_core_record.segment_count > 0) {
        int i;
        for (i = 0; i < last_core_record.segment_count; i++) {
            sprintf(msg,
                    "CORE: seg[%d] sel=0x%04x base=0x%08x limit=0x%08x access=0x%02x gran=0x%02x\n",
                    i,
                    last_core_record.segments[i].selector,
                    last_core_record.segments[i].base,
                    last_core_record.segments[i].limit,
                    last_core_record.segments[i].access,
                    last_core_record.segments[i].granularity);
            kprint(msg);
        }
    }

    return 0;
}
