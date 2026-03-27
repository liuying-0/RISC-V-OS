#ifndef __KERNEL_TRAP_H__
#define __KERNEL_TRAP_H__

struct trap_context {
    unsigned long ra; // 0
    unsigned long sp; // 8
    unsigned long gp; // 16
    unsigned long tp; // 24
    unsigned long t0; // 32
    unsigned long t1; // 40
    unsigned long t2; // 48
    unsigned long s0; // 56
    unsigned long s1; // 64
    unsigned long a0; // 72
    unsigned long a1; // 80
    unsigned long a2; // 88
    unsigned long a3; // 96
    unsigned long a4; // 104
    unsigned long a5; // 112
    unsigned long a6; // 120
    unsigned long a7; // 128
    unsigned long s2; // 136
    unsigned long s3; // 144
    unsigned long s4; // 152
    unsigned long s5; // 160
    unsigned long s6; // 168
    unsigned long s7; // 176
    unsigned long s8; // 184
    unsigned long s9; // 192
    unsigned long s10; // 200
    unsigned long s11; // 208
    unsigned long t3; // 216
    unsigned long t4; // 224
    unsigned long t5; // 232
    unsigned long t6; // 240
    unsigned long sepc; // 248
    unsigned long kernel_sp; // 256
    unsigned long kernel_satp; // 264
    unsigned long trap_handler; // 272
};

void s_trap_handler(unsigned long scause, struct trap_context *ctx);
void page_fault_handler(unsigned long scause, unsigned long sepc, struct trap_context *ctx);
void external_irq_handler(void);
void syscall_handler(struct trap_context *ctx);
void timer_handler(void);
void timer_init(void);

void usertrap(unsigned long scause, struct trap_context *tp);
void usertrapret(void);

#endif /* __KERNEL_TRAP_H__*/