#include "scheduler.h"
#include "proc.h"
#include "../lib/types.h"
#include "../lib/riscv.h"
#include "../mm/vmm.h"

static struct context scheduler_context;

struct proc *current_proc = NULL;

void scheduler(void) {
    struct proc *p;

    while (1) {
        for (int i = 0; i < MAX_PROC; i++) {
            p = &proc_table[i];

            if (p->state == ZOMBIE) {
                proc_destroy(p);
                continue;
            }
            if (p->state != READY) continue;

            p->state = RUNNING;
            current_proc = p;

            if (p->pagetable) {
                unsigned long satp_val = (8UL << 60) |
                    ((unsigned long)p->pagetable >> 12);
                csrw(satp, satp_val);
                asm volatile("sfence.vma zero, zero");
            } else {
                csrs(sstatus, SSTATUS_SIE);
            }
            switch_to(&scheduler_context, &p->context);

            if (!p->pagetable) {
                csrc(sstatus, SSTATUS_SIE);
            }

            if (p->pagetable) {
                unsigned long satp_val = (8UL << 60) |
                    ((unsigned long)kernel_pagetable >> 12);
                csrw(satp, satp_val);
                asm volatile("sfence.vma zero, zero");
            }

            current_proc = NULL;
        }
    }
}

void yield(void) {
    current_proc->state = READY;
    switch_to(&current_proc->context, &scheduler_context);
}