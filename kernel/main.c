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

    uart_init();
    plic_init();
    plic_init_hart();

    csrw(sie, SIE_SSIE | SIE_STIE | SIE_SEIE);
    csrs(sstatus, SSTATUS_SIE);

    pmm_init();
    vmm_init();

    proc_init();
    timer_init();

    printf("Hello from S-mode kernel!\n");

    proc_create("thread_a", thread_a);
    proc_create("thread_b", thread_b);
    printf("Starting scheduler...\n");

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


