#include "proc.h"
#include "../mm/pmm.h"

#include "../lib/string.h"
#include "memlayout.h"

struct proc proc_table[MAX_PROC];
static int next_pid = 1;


void proc_init(void){
    memset(proc_table, 0, sizeof(proc_table));
}

struct proc *proc_create(const char * name, void (*entry)(void)){
    struct proc *p = NULL;
    for(int i = 0; i < MAX_PROC; i++){
        if(proc_table[i].state == UNUSED) {
            p = &proc_table[i];
            break;
        }
    }
    if(!p) return NULL;

    p->kstack = (unsigned long) pmm_alloc();
    if (!p->kstack) return NULL;

    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (unsigned long) entry;
    p->context.sp = p->kstack + PAGE_SIZE;

    p->pid = next_pid++;
    p->state = READY;
    strncpy(p->name, name, sizeof(p->name));
    return p;
}