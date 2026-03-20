#include "trap.h"
#include "../lib/printf.h"

void s_trap_handler(unsigned long scause, struct trap_context *ctx){
    unsigned long sepc = ctx->sepc;
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
                printf("Unknown interrupt: %d\n", cause);
                panic("unhandled interrupt");
        }
    } else {
        switch (cause) {
            case 5:  // 加载访问错误
            case 7:  // 存储访问错误
            case 12: // 指令缺页异常
            case 13: // 加载缺页异常
            case 15: // 存储缺页异常
                page_fault_handler(scause, sepc, ctx);
                break;
            case 8:  // 系统调用（U-mode ecall）
                ctx->sepc += 4;
                syscall_handler(ctx);
                break;
            default:
                printf("Unknown exception: %d\n", cause);
                panic("unhandled exception");
        }
    }
}