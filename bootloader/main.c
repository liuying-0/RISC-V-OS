#include "../kernel/driver/uart.h"

void switch_to_s_mode(void);
void delegate_traps(void);
void configure_pmp(void);

void main(void){
    uart_init();
    uart_puts("M-mode bootloader started.\n");

    delegate_traps();
    configure_pmp();
    switch_to_s_mode();

    uart_puts("Should not reach here!\n");
    while (1){}
}

void switch_to_s_mode(void){
    unsigned long mstatus;
    asm volatile("csrr %0, mstatus" : "=r"(mstatus));
    mstatus &= ~(3UL << 11); // 清除MPP位
    mstatus |= (1UL << 11); // 设置MPP位为S模式
    asm volatile("csrw mstatus, %0" : : "r"(mstatus));

    extern void kernel_main(void);
    asm volatile("csrw mepc, %0" : : "r"((unsigned long)kernel_main));

    asm volatile("csrw satp, zero");

    // 使能 M-mode 定时器中断，mtime >= mtimecmp 时才能触发 M-mode trap
    asm volatile("csrs mie, %0" : : "r"(1UL << 7));

    asm volatile("mret");
}
void configure_pmp(void){
    asm volatile("csrw pmpaddr0, %0" : : "r"(0x3FFFFFFFFFFFFFULL));
    asm volatile("csrw pmpcfg0, %0" : : "r"(0x0F));
}
void delegate_traps(void){
    unsigned long medeleg = 0;
    medeleg |= (1 << 0);  // 指令地址未对齐
    medeleg |= (1 << 1);  // 指令访问错误
    medeleg |= (1 << 2);  // 非法指令
    medeleg |= (1 << 3);  // 断点
    medeleg |= (1 << 5);  // 加载访问错误
    medeleg |= (1 << 7);  // 存储访问错误
    medeleg |= (1 << 8);  // U-mode 环境调用
    medeleg |= (1 << 12); // 指令缺页异常
    medeleg |= (1 << 13); // 加载缺页异常
    medeleg |= (1 << 15); // 存储缺页异常
    asm volatile("csrw medeleg, %0" : : "r"(medeleg));

    unsigned long mideleg = 0;
    mideleg |= (1 << 1);  // S-mode 软件中断
    mideleg |= (1 << 5);  // S-mode 定时器中断
    mideleg |= (1 << 9);  // S-mode 外部中断
    asm volatile("csrw mideleg, %0" : : "r"(mideleg));
}