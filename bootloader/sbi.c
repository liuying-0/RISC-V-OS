#include "../kernel/driver/uart.h"
#include "trap.h"

void sbi_handler(struct trap_context *ctx){
    unsigned long eid = ctx->a7;
    unsigned long arg0 = ctx->a0;

    switch (eid){
        case 0x00: { // 设置定时器
            unsigned long stime = ctx->a0;
            *(volatile unsigned long *)(0x2000000UL + 0x4000) = stime; // 写 mtimecmp
            asm volatile("csrc mip, %0" : : "r"(1UL << 5)); // 清除 STIP
            break;
        }
        case 0x01: // 输出字符
            uart_putc((char)arg0);
            break;
        case 0x02: // 读取字符
            ctx->a0 = uart_getc();
            break;
        case 0x08: // 关机
            *(volatile unsigned int *)0x100000 = 0x5555;
            break;
    }

    ctx->mepc += 4;
}