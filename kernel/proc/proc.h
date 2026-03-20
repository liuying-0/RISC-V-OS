#ifndef __PROC_H__
#define __PROC_H__
#define MAX_PROC 64

#include "../mm/vmm.h"

enum proc_state {
    UNUSED,
    READY,
    RUNNING,
    SLEEPING,
    ZOMBIE,
};

struct context {
    unsigned long ra;
    unsigned long sp;
    unsigned long s0;
    unsigned long s1;
    unsigned long s2;
    unsigned long s3;
    unsigned long s4;
    unsigned long s5;
    unsigned long s6;
    unsigned long s7;
    unsigned long s8;
    unsigned long s9;
    unsigned long s10;
    unsigned long s11;
};


struct proc {
    int pid;
    enum proc_state state;
    struct context context;
    unsigned long kstack;
    char name[16];

    pagetable_t pagetable;
    struct trap_context *trapfram;
    unsigned long sz;
};

extern struct proc proc_table[MAX_PROC];

extern struct proc *current_proc;

extern void switch_to(struct context *old, struct context *new);

void proc_init(void);
struct proc *proc_create(const char *name, void (*entry)(void));
void proc_exit(int status);
void proc_destroy(struct proc *p);

#endif