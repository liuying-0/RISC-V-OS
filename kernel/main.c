#include "lib/printf.h"
#include "lib/riscv.h"
#include "driver/uart.h"
#include "driver/plic.h"
#include "../include/sbi_call.h"
#include "./mm/page_table.h"
#include "./mm/vmm.h"
#include "./mm/pmm.h"
#include "./proc/proc.h"
#include "./proc/scheduler.h"
#include "./trap/trap.h"

// 用户程序 ELF 数据
extern char _binary_user_hello_start[];
extern char _binary_user_hello_end[];

static void delay(void) {
    for (volatile int i = 0; i < 500000000; i++);
}

void thread_a(void){
    int count = 0;
    while(1){
        printf("[A] count = %d\n",count++);
        delay();
    }
}

void thread_b(void){
    int count = 0;
    while(1) {
        printf("[B] count = %d\n",count++);
        delay();
    }
}



void kernel_main(void){

    extern void s_trap_entry(void);
    csrw(stvec, (unsigned long)s_trap_entry);
    csrw(sscratch, 0);  // 内核态 sscratch = 0，切换页表前设为 trapframe 地址

    uart_init();
    printf("UART initialized\n");
    plic_init();
    printf("PLIC initialized\n");
    plic_init_hart();
    printf("PLIC Hart initialized\n");

    csrw(sie, SIE_SSIE | SIE_STIE | SIE_SEIE);
    csrs(sstatus, SSTATUS_SIE);
    printf("S-mode interrupts enabled\n");

    pmm_init();
    printf("PMM initialized\n");
    vmm_init();
    printf("VMM initialized\n");

    proc_init();
    printf("Proc initialized\n");
    timer_init();
    printf("Timer initialized\n");

    printf("Hello from S-mode kernel!\n");

    proc_create("thread_a", thread_a);
    proc_create("thread_b", thread_b);
    printf("Starting scheduler...\n");

    // 创建用户进程
    unsigned long elf_size = (unsigned long)(_binary_user_hello_end - _binary_user_hello_start);
    user_proc_create("hello", (const unsigned char *)_binary_user_hello_start, elf_size);

    scheduler();


   // printf("[test] Setting timer (1s)...\n");
    //sbi_set_timer(rdtime() + 10000000);

    //printf("[test] Type some characters to test UART interrupt echo:\n");

    //printf("Testing exception...\n");
    //*(volatile int *)0xdeadbeef = 42;

    //printf("Shutting down...\n");
    //sbi_shutdown();


    while(1){}
}


