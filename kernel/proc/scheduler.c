#include "scheduler.h"
#include "proc.h"
#include "../lib/types.h"
#include "../lib/riscv.h"

static struct context scheduler_context;

struct proc *current_proc = NULL;

void scheduler(void) {
    struct proc *p;

    while (1) {
        for (int i = 0; i < MAX_PROC; i++) {
            p = &proc_table[i];
            if (p->state != READY) continue;

            p->state = RUNNING;
            current_proc = p;

            csrs(sstatus, SSTATUS_SIE);
            switch_to(&scheduler_context, &p->context);
            csrc(sstatus, SSTATUS_SIE);

            current_proc = NULL;
        }
    }
}

void yield(void) {
    current_proc->state = READY;
    switch_to(&current_proc->context, &scheduler_context);
}