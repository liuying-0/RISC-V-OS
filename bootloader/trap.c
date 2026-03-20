#include "../kernel/driver/uart.h"
#include "trap.h"
#include "sbi.h"

#define CLINT_BASE 0x2000000UL
#define CLINT_MTIME (*(volatile unsigned long *)(CLINT_BASE + 0xBFF8))
#define CLINT_MTIMECMP (*(volatile unsigned long *)(CLINT_BASE + 0x4000))

void m_trap_handler(unsigned long mcause, struct trap_context *ctx) {
    int is_interrupt = (mcause >> 63) & 1;
    unsigned long cause = mcause & 0x7FFFFFFFFFFFFFFFULL;

    if (is_interrupt) {
        switch (cause) {
            case 3:  // M-mode 软件中断
                uart_puts("M-mode software interrupt\n");
                break;
            case 7: { // M-mode 定时器中断
                CLINT_MTIMECMP = 0xFFFFFFFFFFFFFFFFULL;
                unsigned long mip;
                asm volatile("csrr %0, mip" : "=r"(mip));
                mip |= (1UL << 5); // 置位 STIP，通知 S-mode
                asm volatile("csrw mip, %0" : : "r"(mip));
                break;
            }
            case 11: // M-mode 外部中断
                uart_puts("M-mode external interrupt\n");
                break;
            default:
                uart_puts("Unknown M-mode interrupt\n");
                break;
        }
    } else {
        switch (cause) {
            case 9: // S-mode ecall（SBI 调用）
                sbi_handler(ctx);
                break;
            default:
                uart_puts("Unhandled M-mode exception\n");
                while (1) {}
        }
    }
}
