#include "trap.h"
#include "../lib/printf.h"
#include "../lib/riscv.h"
#include "../proc/proc.h"
#include "../proc/scheduler.h"
#include "memlayout.h"

void s_trap_handler(unsigned long scause, struct trap_context *ctx){
    int is_interrupt = (scause >> 63) & 1;
    unsigned long cause = scause & 0x7FFFFFFFFFFFFFFFULL;

    if (is_interrupt) {
        switch (cause){
            case 1: // 软件中断
                asm volatile("csrc sip, %0" : : "r"(1 << 1));
                break;
            case 5: // 定时器中断
                timer_handler();
                break;
            case 9: // 外部中断
                external_irq_handler();
                break;
            default:
                printf("Unknown S-mode interrupt: %d\n", cause);
                panic("unhandled interrupt");
        }
    } else {
            printf("S-mode exception: cause = %d, sepc = %lx\n", cause, ctx->sepc);
            panic("unhandled S-mode exception");
        
    }
}

void usertrap(unsigned long scause, struct trap_context *tf){
    int is_interrupt = (scause >> 63) & 1;
    unsigned long cause = scause & 0x7FFFFFFFFFFFFFFFULL;

    if (is_interrupt) {
        switch (cause) {
            case 5:
                timer_handler();
                break;
            case 9:
                external_irq_handler();
                break;
            default:
                printf("usertrap: unknown interrupt: %d\n", cause);
        }
    } else {
        switch (cause){
            case 8:
                tf->sepc += 4;
                syscall_handler(tf);
                break;
            case 12:
            case 13:
            case 15:
                page_fault_handler(scause, tf->sepc, tf);
                break;
            default:
                printf("usertrap: unknown exception: %d at sepc = %lx\n",cause, tf->sepc );
                panic("unhandled user exception");
        }
    }
}

void usertrapret(void){
    struct proc *p = current_proc;

    extern void s_trap_entry(void);
    csrw(stvec, (unsigned long)s_trap_entry);

    p->trapframe->kernel_sp = p->kstack + PAGE_SIZE;

    extern void s_trap_return(unsigned long);
    // tail jump：s_trap_return 以 sret 结束，不返回到此处
    unsigned long user_satp = (8UL << 60) | ((unsigned long)p->pagetable >> 12);
    asm volatile("mv a0, %0; mv a1, %1; j s_trap_return"
                 : : "r"(p->trapframe), "r"(user_satp));

    panic("usertrapret returned!");
}